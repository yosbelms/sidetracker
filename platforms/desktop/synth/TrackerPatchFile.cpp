#include "TrackerPatchFile.h"

#include <bit>
#include <cmath>

namespace polysynth
{
namespace
{
using Bytes = std::vector<uint8_t>;
using BytesView = std::span<const uint8_t>;
using Macro = Patch::Metadata::Macro;
constexpr size_t kStoredModifiers = 6;

enum Wire { Varint = 0, Fixed64 = 1, Message = 2, Fixed32 = 5 };

struct Field
{
    uint32_t tag = 0;
    Wire wire = Varint;
    uint64_t number = 0;
    BytesView bytes;
};

bool readVarint(BytesView& input, uint64_t& value)
{
    value = 0;
    for (unsigned shift = 0; shift < 64 && !input.empty(); shift += 7)
    {
        const uint8_t byte = input.front();
        input = input.subspan(1);
        if (shift == 63 && byte > 1) return false;
        value |= uint64_t(byte & 127) << shift;
        if (byte < 128) return true;
    }
    return false;
}

bool readField(BytesView& input, Field& field)
{
    uint64_t key;
    if (!readVarint(input, key) || key < 8 || key > UINT32_MAX) return false;
    field = {};
    field.tag = key >> 3;
    field.wire = static_cast<Wire>(key & 7);
    switch (field.wire)
    {
        case Varint: return readVarint(input, field.number);
        case Fixed32: field.number = 4; break;
        case Fixed64: field.number = 8; break;
        case Message: if (!readVarint(input, field.number)) return false; break;
        default: return false;
    }
    if (field.number > input.size()) return false;
    field.bytes = input.first(field.number);
    input = input.subspan(field.number);
    return true;
}

float readFloat(BytesView bytes)
{
    const uint32_t bits = uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
                          (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
    return std::bit_cast<float>(bits);
}

bool readParam(BytesView input, Patch::optional_paramvalue_t& value)
{
    value = std::nullopt;
    Field field;
    while (!input.empty())
    {
        if (!readField(input, field)) return false;
        if (field.tag == 1 && field.wire == Fixed32)
        {
            const float decoded = readFloat(field.bytes);
            if (!std::isfinite(decoded)) return false;
            value = decoded;
        }
    }
    return true;
}

bool readModifier(BytesView input, Macro::ParamModifier& modifier, size_t paramCount)
{
    uint64_t id = 0;
    float amount = 0;
    Field field;
    while (!input.empty())
    {
        if (!readField(input, field)) return false;
        if (field.tag == 1 && field.wire == Varint) id = field.number;
        if (field.tag == 2 && field.wire == Fixed32) amount = readFloat(field.bytes);
    }
    if (!std::isfinite(amount) || std::abs(amount) > Macro::ParamModifier::kMaxAmount) return false;
    modifier = {id < paramCount ? tiny_optional<int>{static_cast<int>(id)} : std::nullopt, amount};
    return true;
}

bool readMacro(BytesView input, Macro& macro, size_t paramCount)
{
    macro.bipolar = false;
    std::vector<Macro::ParamModifier> modifiers;
    size_t count = 0;
    Field field;
    while (!input.empty())
    {
        if (!readField(input, field)) return false;
        if (field.tag == 1 && field.wire == Message)
        {
            if (field.bytes.size() > Macro::kMaxDisplayNameLen) return false;
            macro.displayName.assign(field.bytes.begin(), field.bytes.end());
        }
        if (field.tag == 2 && field.wire == Varint) macro.bipolar = field.number != 0;
        if (field.tag == 3 && field.wire == Varint)
        {
            if (field.number > kStoredModifiers) return false;
            count = field.number;
        }
        if (field.tag == 4 && field.wire == Message)
        {
            if (modifiers.size() == kStoredModifiers ||
                !readModifier(field.bytes, modifiers.emplace_back(), paramCount)) return false;
        }
    }
    if (count > modifiers.size()) return false;
    modifiers.resize(count);
    macro.modifiers = std::move(modifiers);
    return true;
}

void varint(Bytes& output, uint64_t value)
{
    while (value >= 128)
    {
        output.push_back((value & 127) | 128);
        value >>= 7;
    }
    output.push_back(value);
}

void integer(Bytes& output, uint32_t tag, uint64_t value)
{
    varint(output, (tag << 3) | Varint);
    varint(output, value);
}

void message(Bytes& output, uint32_t tag, BytesView payload = {})
{
    varint(output, (tag << 3) | Message);
    varint(output, payload.size());
    output.insert(output.end(), payload.begin(), payload.end());
}

void fixedFloat(Bytes& output, uint32_t tag, float value)
{
    varint(output, (tag << 3) | Fixed32);
    const uint32_t bits = std::bit_cast<uint32_t>(value);
    for (unsigned shift = 0; shift < 32; shift += 8)
        output.push_back(static_cast<uint8_t>(bits >> shift));
}

const Bytes& defaultSequencer()
{
    // Match the disabled arpeggiator and empty sequence in factory patch files.
    static const auto defaults = [] {
        Bytes arp, config, sequencer, chord, step, sequence;
        integer(arp, 1, 3);
        integer(arp, 2, 1);
        message(config, 2, arp);
        message(config, 3);
        message(sequencer, 1, config);
        message(chord, 2, std::array<uint8_t, 16>{});
        message(step, 2, chord);
        for (int i = 0; i < kMaxSequenceLength; ++i)
            sequence.insert(sequence.end(), step.begin(), step.end());
        message(sequencer, 2, sequence);
        return sequencer;
    }();
    return defaults;
}
}

std::vector<uint8_t> encodeTrackerPatch(const DesktopPatch& patch)
{
    Bytes output;
    const auto& metadata = patch.getMetadata();
    integer(output, 1, metadata.instrumentTypeId);
    for (size_t i = 0; i < metadata.params.size(); ++i)
    {
        Bytes param;
        if (const auto value = patch.getParamValue(i)) fixedFloat(param, 1, *value);
        message(output, 6, param);
    }
    const auto extraFields = patch.getExtraFields();
    if (extraFields.empty())
        for (int slot = 0; slot < 4; ++slot) message(output, 7);
    for (const auto& macro : metadata.macros)
    {
        Bytes payload;
        message(payload, 1, {reinterpret_cast<const uint8_t*>(macro.displayName.data()), macro.displayName.size()});
        if (macro.bipolar) integer(payload, 2, 1);
        const size_t count = std::min(macro.modifiers.size(), kStoredModifiers);
        if (count) integer(payload, 3, count);
        for (size_t i = 0; i < kStoredModifiers; ++i)
        {
            Bytes modifier;
            if (i < count)
            {
                const auto& mod = macro.modifiers[i];
                if (!mod.paramId) integer(modifier, 1, UINT64_MAX);
                else if (*mod.paramId) integer(modifier, 1, *mod.paramId);
                if (mod.amount != 0) fixedFloat(modifier, 2, mod.amount);
            }
            message(payload, 4, modifier);
        }
        message(output, 8, payload);
    }
    if (extraFields.empty()) message(output, 9, defaultSequencer());
    else output.insert(output.end(), extraFields.begin(), extraFields.end());
    return output;
}

bool decodeTrackerPatch(std::span<const uint8_t> data, DesktopPatch& patch)
{
    const auto& metadata = patch.getMetadata();
    std::vector<Patch::optional_paramvalue_t> values;
    values.reserve(metadata.params.size());
    for (const auto& param : metadata.params) values.push_back(param.defaultValue);
    auto macros = metadata.macros;
    for (auto& macro : macros)
    {
        macro.bipolar = false;
        macro.modifiers.clear();
    }
    if (metadata.initDefaultMacros) metadata.initDefaultMacros(macros);

    size_t paramIndex = 0, macroIndex = 0;
    bool typeMatches = false;
    Bytes extraFields;
    Field field;
    while (!data.empty())
    {
        const auto before = data;
        if (!readField(data, field)) return false;
        if (field.tag == 1 && field.wire == Varint)
            typeMatches = field.number == static_cast<uint64_t>(metadata.instrumentTypeId);
        if (field.tag == 6 && field.wire == Message && paramIndex < values.size())
        {
            if (!readParam(field.bytes, values[paramIndex++])) return false;
        }
        if (field.tag == 8 && field.wire == Message && macroIndex < macros.size())
        {
            if (!readMacro(field.bytes, macros[macroIndex++], values.size())) return false;
        }
        if (field.tag != 1 && field.tag != 6 && field.tag != 8)
        {
            const auto raw = before.first(before.size() - data.size());
            extraFields.insert(extraFields.end(), raw.begin(), raw.end());
        }
    }
    if (!typeMatches || paramIndex == 0) return false;
    patch.restore(std::move(values), std::move(macros), std::move(extraFields));
    return true;
}
}
