#include "DesktopPatch.h"
#include "TrackerPatchFile.h"

#include "PolySynth/FileSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>

namespace polysynth
{
namespace
{
template<class Range>
bool validIndex(const Range& range, int index)
{
    return index >= 0 && static_cast<size_t>(index) < range.size();
}

float limited(float value, const Patch::Metadata::Param& param)
{
    value = std::clamp(value, float(param.minValue), float(param.maxValue));
    return param.isEnum ? std::round(value) : value;
}

bool exponential(const Patch::Metadata::Param& param)
{
    using Curve = Patch::Metadata::Param::Curve;
    return param.minValue > 0 && (param.editCurve == Curve::kExponential ||
                                 param.editCurve == Curve::kExponentialSteep);
}
}

DesktopPatch::DesktopPatch(std::shared_ptr<const InstrumentType> type) : type(std::move(type))
{
    this->type->createMetadata(*this, metadata);
    for (const auto& param : metadata.params)
        params.push_back(param.defaultValue);
    metadata.macros[kUserMacroCount].displayName = "Velocity";
    metadata.macros[kUserMacroCount + 1].displayName = "Pressure";
    for (size_t i = kUserMacroCount; i < kMacroCount; ++i)
    {
        metadata.macros[i].perVoice = true;
        metadata.macros[i].system = true;
    }
    metadata.initDefaultMacros(metadata.macros);
}

Patch::optional_paramvalue_t DesktopPatch::getParamValue(int id) const
{
    return validIndex(params, id) ? params[id] : std::nullopt;
}

Patch::optional_paramvalue_t DesktopPatch::getParamValueEditMapped(int id) const
{
    const auto value = getParamValue(id);
    if (!value) return std::nullopt;
    const auto& param = metadata.params[id];
    const float low = param.minValue, high = param.maxValue;
    if (low >= high) return 0.0f;
    if (exponential(param)) return std::log(*value / low) / std::log(high / low);
    const float t = std::clamp((*value - low) / (high - low), 0.0f, 1.0f);
    return param.editCurve == Metadata::Param::Curve::kPower ? std::sqrt(t) : t;
}

void DesktopPatch::setParamValueEditMapped(int id, optional_paramvalue_t value, ParamChangeSource source)
{
    if (!validIndex(params, id)) return;
    if (!value)
    {
        setParamValue(id, value, source);
        return;
    }
    const auto& param = metadata.params[id];
    const float t = std::clamp(*value, 0.0f, 1.0f);
    const float low = param.minValue, high = param.maxValue;
    const float result = exponential(param) ? low * std::pow(high / low, t)
        : std::lerp(low, high, param.editCurve == Metadata::Param::Curve::kPower ? t * t : t);
    setParamValue(id, result, source);
}

void DesktopPatch::setParamValue(int id, optional_paramvalue_t value, ParamChangeSource)
{
    if (!validIndex(params, id) || (value && !std::isfinite(*value))) return;
    if (value) value = limited(*value, metadata.params[id]);
    if (params[id] == value) return;
    params[id] = value;
    if (metadata.params[id].invalidatesOtherMetadata) type->createMetadata(*this, metadata);
    ++version;
}

Patch::optional_paramvalue_t DesktopPatch::getParamFinalValue(int id) const
{
    return voiceValue(id, 1.0f, 0.0f);
}

Patch::optional_paramvalue_t DesktopPatch::voiceValue(int id, float velocity, float pressure) const
{
    const auto value = getParamValue(id);
    if (!value) return std::nullopt;
    const auto& param = metadata.params[id];
    float result = *value;
    for (size_t i = 0; i < metadata.macros.size(); ++i)
    {
        const auto& macro = metadata.macros[i];
        const float neutral = macro.bipolar ? 50.0f : 0.0f;
        const float modulation = i < macros.size()
            ? (macros[i].value_or(neutral) - neutral) * (macro.bipolar ? 0.02f : 0.01f)
            : i == kUserMacroCount ? velocity : pressure;
        for (const auto& entry : macro.modifiers)
        {
            if (!entry.paramId || *entry.paramId != id) continue;
            if (param.macroOperation == Metadata::Param::MacroOperation::kMult)
            {
                const float offset = i == kUserMacroCount ? modulation - 1.0f : modulation;
                result *= std::max(0.0f, 1.0f + offset * entry.amount);
            }
            else
                result += (float(param.maxValue) - float(param.minValue)) * modulation * entry.amount;
        }
    }
    return limited(result, param);
}

Patch::optional_paramvalue_t DesktopPatch::getMacroValue(int id) const
{
    return validIndex(macros, id) ? macros[id] : std::nullopt;
}

void DesktopPatch::setMacroValue(int id, optional_paramvalue_t value, tiny_optional<int>)
{
    if (!validIndex(macros, id) || (value && !std::isfinite(*value))) return;
    if (value) value = std::clamp(*value, 0.0f, kMaxMacroValue);
    if (macros[id] != value)
    {
        macros[id] = value;
        ++version;
    }
}

Patch::Metadata::Macro::ParamModifier* DesktopPatch::modifier(int id, int index)
{
    if (!validIndex(metadata.macros, id)) return nullptr;
    auto& mods = metadata.macros[id].modifiers;
    return validIndex(mods, index) ? &mods[index] : nullptr;
}

void DesktopPatch::setMacroModifier(int id, int index, tiny_optional<int> param)
{
    if (!validIndex(metadata.macros, id) || index < 0 || index >= 16 ||
        (param && !validIndex(params, *param))) return;
    auto& mods = metadata.macros[id].modifiers;
    if (param)
    {
        if (mods.size() <= size_t(index)) mods.resize(index + 1);
        mods[index].paramId = param;
    }
    else if (validIndex(mods, index))
        mods[index] = {};
    ++version;
}

void DesktopPatch::setMacroModifierAmount(int id, int index, float amount)
{
    auto* entry = modifier(id, index);
    if (!entry || !std::isfinite(amount)) return;
    constexpr float limit = Metadata::Macro::ParamModifier::kMaxAmount;
    entry->amount = std::clamp(amount, -limit, limit);
    ++version;
}

tiny_optional<float> DesktopPatch::getMacroModifierAmount(int id, int index)
{
    const auto* entry = modifier(id, index);
    return entry && entry->paramId ? tiny_optional<float>{entry->amount} : std::nullopt;
}

void DesktopPatch::clearMacroModifiers()
{
    for (auto& macro : metadata.macros) macro.modifiers.clear();
    ++version;
}

void DesktopPatch::changeMacroModifierAmount(int id, int index, float delta)
{
    if (const auto* entry = modifier(id, index))
        setMacroModifierAmount(id, index, entry->amount + delta);
}

void DesktopPatch::setMacroBipolar(int id, bool bipolar)
{
    if (!validIndex(macros, id)) return;
    metadata.macros[id].bipolar = bipolar;
    macros[id] = std::nullopt;
    ++version;
}

void DesktopPatch::setMacroDisplayName(int id, std::string name)
{
    if (validIndex(macros, id))
        metadata.macros[id].displayName = name.substr(0, Metadata::Macro::kMaxDisplayNameLen);
}

void DesktopPatch::setSequence(const Sequencer::Sequence& sequence, int index)
{
    if (validIndex(sequencer.sequences, index)) sequencer.sequences[index] = sequence;
}

void DesktopPatch::restore(std::vector<optional_paramvalue_t> values, std::vector<Metadata::Macro> newMacros,
                           std::vector<uint8_t> newExtraFields)
{
    for (size_t i = 0; i < values.size(); ++i)
        if (values[i]) values[i] = limited(*values[i], metadata.params[i]);
    params = std::move(values);
    metadata.macros = std::move(newMacros);
    extraFields = std::move(newExtraFields);
    macros = {};
    type->createMetadata(*this, metadata);
    ++version;
}

namespace
{
constexpr char kPatchFormat[] = "SideTrackerDesktopPatch";

class DesktopPatchStore final : public PatchStore
{
  public:
    DesktopPatchStore(const InstrumentTypeRegistry& registry, FileSystem& fs, bool typeFolders)
        : registry(registry), fs(fs), typeFolders(typeFolders) {}

    std::unique_ptr<Patch> createPatch(int typeId) override
    {
        auto type = registry.getInstrumentType(typeId);
        return type ? std::make_unique<DesktopPatch>(std::move(type)) : nullptr;
    }

    std::vector<std::string_view> listPatchesByType(int typeId) override
    {
        names.clear();
        if (typeFolders && registry.getInstrumentType(typeId)) appendNames(typeId);
        return {names.begin(), names.end()};
    }

    std::vector<std::pair<std::string_view, int>> listPatchesFilter(
        std::function<bool(InstrumentTypeId)> filter) override
    {
        names.clear();
        std::vector<int> ids;
        if (typeFolders)
            for (const auto& [id, type] : registry.getInstrumentTypes())
            {
                if (filter && !filter(static_cast<InstrumentTypeId>(id))) continue;
                appendNames(id);
                ids.resize(names.size(), id);
            }
        std::vector<std::pair<std::string_view, int>> result;
        for (size_t i = 0; i < names.size(); ++i) result.emplace_back(names[i], ids[i]);
        return result;
    }

    bool savePatch(const std::string& name, const Patch& patch) override
    {
        if (!validName(name)) return false;
        const auto& metadata = patch.getMetadata();
        const auto data = encodeTrackerPatch(static_cast<const DesktopPatch&>(patch));
        if (typeFolders) fs.createFolder(folder(metadata.instrumentTypeId));
        auto file = fs.open(path(metadata.instrumentTypeId, name), FileSystem::OpenOptions::kWrite);
        if (!file) return false;
        const bool written = fs.write(*file, data);
        fs.close(*file);
        return written;
    }

    bool loadPatch(const std::string& name, Patch& patch, bool) override
    {
        if (!validName(name)) return false;
        const auto& metadata = patch.getMetadata();
        auto file = openPatch(metadata.instrumentTypeId, name);
        if (!file) return false;
        std::array<char, 16385> data{};
        const int count = fs.read(*file, {reinterpret_cast<uint8_t*>(data.data()), data.size()});
        fs.close(*file);
        if (count <= 0 || size_t(count) >= data.size()) return false;

        if (!std::string_view(data.data(), count).starts_with(kPatchFormat))
            return decodeTrackerPatch({reinterpret_cast<const uint8_t*>(data.data()), static_cast<size_t>(count)},
                                      static_cast<DesktopPatch&>(patch));

        std::istringstream input(std::string(data.data(), count));
        input.imbue(std::locale::classic());
        std::string magic;
        unsigned version, type, params;
        if (!(input >> magic >> version >> type >> params) || magic != kPatchFormat || version != 1 ||
            type != unsigned(metadata.instrumentTypeId) || params != metadata.params.size()) return false;
        std::vector<Patch::optional_paramvalue_t> values(params);
        for (size_t i = 0; i < params; ++i)
        {
            bool present;
            float value;
            if (!(input >> present >> value) || !std::isfinite(value)) return false;
            if (present) values[i] = value;
        }
        auto macros = metadata.macros;
        for (auto& macro : macros)
        {
            unsigned mods;
            if (!(input >> std::quoted(macro.displayName) >> macro.bipolar >> mods) ||
                macro.displayName.size() > Patch::Metadata::Macro::kMaxDisplayNameLen || mods > 16) return false;
            macro.modifiers.resize(mods);
            for (auto& mod : macro.modifiers)
            {
                int id;
                float amount;
                if (!(input >> id >> amount) || id < -1 || id >= int(params) || !std::isfinite(amount) ||
                    std::abs(amount) > Patch::Metadata::Macro::ParamModifier::kMaxAmount) return false;
                mod = {id >= 0 ? tiny_optional<int>{id} : std::nullopt, amount};
            }
        }
        static_cast<DesktopPatch&>(patch).restore(std::move(values), std::move(macros));
        return true;
    }

    std::optional<InstrumentTypeId> loadPatchInstrumentType(const std::string& name) override
    {
        if (!typeFolders || !validName(name)) return {};
        for (const auto& [id, type] : registry.getInstrumentTypes())
            if (auto file = openPatch(id, name))
            {
                fs.close(*file);
                return static_cast<InstrumentTypeId>(id);
            }
        return {};
    }

  private:
    static bool validName(std::string_view name)
    {
        return !name.empty() && name.size() <= 80 && name != "." && name != ".." &&
               name.find_first_of("/\\") == std::string_view::npos;
    }

    std::string folder(int id) const
    {
        return registry.getInstrumentType(id)->name;
    }

    std::string path(int id, const std::string& name) const
    {
        return (typeFolders ? folder(id) + "/" : "") + name + kFileExtension;
    }

    std::optional<FileSystem::FileHandle> openPatch(int id, const std::string& name)
    {
        auto file = fs.open(path(id, name), FileSystem::OpenOptions::kNone);
        if (!file && typeFolders)
            file = fs.open(std::to_string(id) + "/" + name + kFileExtension, FileSystem::OpenOptions::kNone);
        return file;
    }

    void appendNames(int id)
    {
        const auto append = [&](const std::string& directory) {
            for (auto file : fs.listFiles(directory, [](const std::string& name) {
                     return name.ends_with(kFileExtension) && name.size() > kFileExtension.size();
                 }, std::nullopt))
                names.emplace_back(file.substr(0, file.size() - kFileExtension.size()));
        };
        const auto start = names.size();
        append(folder(id));
        append(std::to_string(id));
        auto typeNames = std::ranges::subrange(names.begin() + start, names.end());
        std::ranges::sort(typeNames);
        const auto duplicates = std::ranges::unique(typeNames);
        names.erase(duplicates.begin(), duplicates.end());
    }

    const InstrumentTypeRegistry& registry;
    FileSystem& fs;
    bool typeFolders;
    std::vector<std::string> names;
};
}

std::unique_ptr<PatchStore> createDesktopPatchStore(const InstrumentTypeRegistry& registry,
                                                    FileSystem& fs, bool typeFolders)
{
    return std::make_unique<DesktopPatchStore>(registry, fs, typeFolders);
}
}
