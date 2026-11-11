// Native encoder for generate_stpalette_presets.py. Uses the firmware's registry
// and serializer so presets follow the same validation and file format as CE.
#include "SampleFxPreset.h"
#include "stpalette/STPaletteEffects.h"
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

struct SavedPreset {
    unsigned slot;
    std::string name;
    std::array<uint8_t, kPresetFileBytes> bytes;
};

int main(int argc, char** argv)
{
    try {
        if (argc != 2) throw std::runtime_error("Expected output directory");
        unsigned count;
        if (!(std::cin >> count) || !count || count > 32)
            throw std::runtime_error("Expected 1–32 presets");
        std::vector<SavedPreset> presets;
        std::set<unsigned> slots;
        std::set<std::string> filenames;
        for (unsigned index = 0; index < count; ++index) {
            SavedPreset saved{};
            unsigned stages, mix;
            if (!(std::cin >> saved.slot >> std::quoted(saved.name) >> stages >> mix) ||
                saved.slot < 1 || saved.slot > 32 || !slots.insert(saved.slot).second ||
                !stages || stages > kMaxStages || mix > 100)
                throw std::runtime_error("Invalid or duplicate preset slot/stage count");
            if (saved.name.empty() || saved.name.size() > kMaxPresetNameBytes ||
                saved.name.front() == '.' || saved.name.back() == '.' || saved.name.back() == ' ' ||
                saved.name.find_first_of("/\\<>:\"|?*") != std::string::npos ||
                std::any_of(saved.name.begin(), saved.name.end(), [](unsigned char c) { return c < 32 || c == 127; }))
                throw std::runtime_error("Invalid preset filename: " + saved.name);
            std::string filename = saved.name;
            std::transform(filename.begin(), filename.end(), filename.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (!filenames.insert(filename).second)
                throw std::runtime_error("Duplicate preset filename: " + saved.name);
            ChainDefinition definition;
            definition.mix = mix;
            for (unsigned stage = 0; stage < stages; ++stage) {
                std::string category, type, sync;
                float amount, macro, drift, tail;
                if (!(std::cin >> std::quoted(category) >> std::quoted(type) >>
                      amount >> macro >> drift >> std::quoted(sync) >> tail))
                    throw std::runtime_error("Invalid stage settings");
                unsigned id = stpalette::kFirst;
                while (id <= stpalette::kLast && category != stpalette::category(id)) ++id;
                if (id > stpalette::kLast) throw std::runtime_error("Unknown category: " + category);
                unsigned selectedType = 0, division = 0;
                while (selectedType < 6 && type != stpalette::typeName(id, selectedType)) ++selectedType;
                while (division < stpalette::syncCount() && sync != stpalette::syncName(division)) ++division;
                if (selectedType == 6 || division == stpalette::syncCount())
                    throw std::runtime_error("Unknown Type/Sync in " + saved.name);
                Node node = stpalette::defaults(id);
                node.params[stpalette::Type] = selectedType;
                node.params[stpalette::Amount] = amount;
                node.params[stpalette::Macro] = macro;
                node.params[stpalette::Drift] = drift;
                node.params[stpalette::Sync] = division;
                node.params[stpalette::Tail] = tail;
                if (!stpalette::valid(node) || !definition.add(node))
                    throw std::runtime_error("Invalid parameters in " + saved.name);
            }
            if (!encodePreset(definition, saved.bytes.data(), saved.bytes.size()))
                throw std::runtime_error("Cannot encode " + saved.name);
            presets.push_back(saved);
        }
        std::cin >> std::ws;
        if (!std::cin.eof()) throw std::runtime_error("Unexpected trailing settings");
        // Validate the entire input before replacing any preset files.
        const fs::path directory = argv[1];
        fs::create_directories(directory);
        for (const auto& saved : presets) {
            const std::string filename = saved.name + ".ptpreset";
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
        std::cerr << "Preset generation failed: " << error.what() << '\n';
        return 1;
    }
}
