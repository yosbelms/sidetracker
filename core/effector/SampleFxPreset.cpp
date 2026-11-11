#include "SampleFxPreset.h"

#include <cstring>

namespace samplefx
{
constexpr char kPresetMagic[] = "PTPR";
constexpr uint8_t kPresetVersion = 1;

static void put32(uint8_t* p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
static uint32_t get32(const uint8_t* p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
bool encodePreset(const ChainDefinition& r, uint8_t* bytes, size_t size)
{
    if (size != kPresetFileBytes || !validChain(r)) return false;
    memset(bytes, 0, size);
    memcpy(bytes, kPresetMagic, sizeof(kPresetMagic) - 1);
    bytes[4] = kPresetVersion;
    bytes[5] = r.count;
    bytes[6] = r.qualityResolved;
    bytes[7] = r.mix;
    put32(bytes + 8, r.generatorFrames);
    for (unsigned i = 0; i <= kMaxStages; ++i)
    {
        const Node& n = i ? r.stages[i - 1] : r.source;
        auto* p = bytes + 16 + i * 100;
        p[0] = n.effect; p[1] = n.count; p[2] = n.bypass;
        p[3] = uint8_t(n.flags); p[4] = uint8_t(n.flags >> 8);
        put32(p + 5, n.fingerprint);
        memcpy(p + 9, n.filename, 42);
        p[99] = n.quality;
        for (unsigned j = 0; j < kMaxParams; ++j)
        {
            uint32_t bits;
            memcpy(&bits, &n.params[j], 4);
            put32(p + 51 + j * 4, bits);
        }
    }
    put32(bytes + 12, fingerprint(bytes + 16, size - 16,
                                 fingerprint(bytes, 12)));
    return true;
}
bool decodePreset(const uint8_t* bytes, size_t size, ChainDefinition& r)
{
    if (size != kPresetFileBytes || memcmp(bytes, kPresetMagic, sizeof(kPresetMagic) - 1) ||
        bytes[4] != kPresetVersion || bytes[6] > 1 || bytes[7] > 100 ||
        get32(bytes + 12) != fingerprint(bytes + 16, size - 16, fingerprint(bytes, 12))) return false;
    ChainDefinition candidate;
    candidate.count = bytes[5];
    candidate.mix = bytes[7];
    candidate.qualityResolved = bytes[6];
    candidate.generatorFrames = get32(bytes + 8);
    for (unsigned i = 0; i <= kMaxStages; ++i)
    {
        Node& n = i ? candidate.stages[i - 1] : candidate.source;
        const auto* p = bytes + 16 + i * 100;
        n.effect = p[0]; n.count = p[1]; n.bypass = p[2] != 0;
        n.flags = p[3] | uint16_t(p[4]) << 8;
        n.fingerprint = get32(p + 5);
        memcpy(n.filename, p + 9, 42);
        n.quality = p[99];
        for (unsigned j = 0; j < kMaxParams; ++j)
        {
            uint32_t bits = get32(p + 51 + j * 4);
            memcpy(&n.params[j], &bits, 4);
        }
    }
    if (!validChain(candidate)) return false;
    r = candidate;
    return true;
}
}
