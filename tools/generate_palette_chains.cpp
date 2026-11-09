// Native encoder for generate_palette_chains.py. Uses the firmware's registry
// and serializer so presets follow the same validation and file format as CE.
#include "SampleFxChain.h"
#include "palette/Palette.h"
#include <array>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace samplefx;
namespace fs = std::filesystem;

struct SavedChain {
    unsigned slot;
    std::string name;
    std::array<uint8_t, kChainFileBytes> bytes;
};

int main(int argc, char** argv)
{
    try {
        if (argc != 2) throw std::runtime_error("Expected output directory");
        unsigned count;
        if (!(std::cin >> count) || !count || count > 32)
            throw std::runtime_error("Expected 1–32 chains");
        std::vector<SavedChain> chains;
        std::set<unsigned> slots;
        std::set<std::string> filenames;
        for (unsigned index = 0; index < count; ++index) {
            SavedChain saved{};
            unsigned stages, mix;
            if (!(std::cin >> saved.slot >> std::quoted(saved.name) >> stages >> mix) ||
                saved.slot < 1 || saved.slot > 32 || !slots.insert(saved.slot).second ||
                !stages || stages > kMaxStages || mix > 100)
                throw std::runtime_error("Invalid or duplicate chain slot/stage count");
            if (saved.name.empty() || saved.name.size() >= sizeof(ChainDefinition{}.name) ||
                saved.name.front() == '.' || saved.name.back() == '.' || saved.name.back() == ' ' ||
                saved.name.find_first_of("/\\<>:\"|?*") != std::string::npos ||
                std::any_of(saved.name.begin(), saved.name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
                throw std::runtime_error("Invalid chain filename: " + saved.name);
            std::string filename = saved.name;
            std::transform(filename.begin(), filename.end(), filename.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (!filenames.insert(filename).second)
                throw std::runtime_error("Duplicate chain filename: " + saved.name);
            ChainDefinition definition;
            definition.mix = mix;
            snprintf(definition.name, sizeof(definition.name), "%s", saved.name.c_str());
            for (unsigned stage = 0; stage < stages; ++stage) {
                std::string category, type, sync;
                float amount, macro, drift, tail;
                if (!(std::cin >> std::quoted(category) >> std::quoted(type) >>
                      amount >> macro >> drift >> std::quoted(sync) >> tail))
                    throw std::runtime_error("Invalid stage settings");
                unsigned id = palette::kFirst;
                while (id <= palette::kLast && category != palette::category(id)) ++id;
                if (id > palette::kLast) throw std::runtime_error("Unknown category: " + category);
                unsigned selectedType = 0, division = 0;
                while (selectedType < 6 && type != palette::typeName(id, selectedType)) ++selectedType;
                while (division < palette::syncCount() && sync != palette::syncName(division)) ++division;
                if (selectedType == 6 || division == palette::syncCount())
                    throw std::runtime_error("Unknown Type/Sync in " + saved.name);
                Node node = palette::defaults(id);
                node.params[palette::Type] = selectedType;
                node.params[palette::Amount] = amount;
                node.params[palette::Macro] = macro;
                node.params[palette::Drift] = drift;
                node.params[palette::Sync] = division;
                node.params[palette::Tail] = tail;
                if (!palette::valid(node) || !definition.add(node))
                    throw std::runtime_error("Invalid parameters in " + saved.name);
            }
            if (!encode(definition, saved.bytes.data(), saved.bytes.size()))
                throw std::runtime_error("Cannot encode " + saved.name);
            chains.push_back(saved);
        }
        std::cin >> std::ws;
        if (!std::cin.eof()) throw std::runtime_error("Unexpected trailing settings");
        // Validate the entire input before replacing any chain files.
        const fs::path directory = argv[1];
        fs::create_directories(directory);
        for (const auto& saved : chains) {
            const std::string filename = saved.name + ".ptchain";
            const fs::path destination = directory / filename;
            const fs::path temporary = directory / (filename + ".tmp");
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                file.write(reinterpret_cast<const char*>(saved.bytes.data()), saved.bytes.size());
                file.close();
                if (!file) throw std::runtime_error("Cannot write " + temporary.string());
            }
            fs::rename(temporary, destination);
            std::cout << filename << " — " << saved.name << '\n';
        }
    } catch (const std::exception& error) {
        std::cerr << "Chain generation failed: " << error.what() << '\n';
        return 1;
    }
}
