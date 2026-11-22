#include "SampleFxPatch.h"

#include <cstring>

namespace samplefx
{
constexpr char kPatchMagic[] = "PTPR";
constexpr uint8_t kPatchVersion = 1;

static void put32(uint8_t* p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
static uint32_t get32(const uint8_t* p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
size_t patchFileSize(const ChainDefinition& chain)
{
    for (unsigned i = 0; i <= chain.count && i <= kMaxStages; ++i)
    {
        const Node& node = i ? chain.stages[i - 1] : chain.source;
        for (unsigned p = 0; p < node.count && p < kMaxParams; ++p)
            if (parameterString(node, p)[0]) return kPatchStringFileBytes;
    }
    return kPatchFileBytes;
}

bool encodePatch(const ChainDefinition& r, uint8_t* bytes, size_t size)
{
    if (size != patchFileSize(r) || !validChain(r)) return false;
    memset(bytes, 0, size);
    memcpy(bytes, kPatchMagic, sizeof(kPatchMagic) - 1);
    bytes[4] = size == kPatchFileBytes ? kPatchVersion : 2;
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
        if (size == kPatchStringFileBytes)
            memcpy(bytes + kPatchFileBytes + i * kParameterStringBytes,
                   n.parameterStrings, kParameterStringBytes);
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
static void readNode(const uint8_t* bytes, size_t size, unsigned index, Node& node)
{
    const auto* p = bytes + 16 + index * 100;
    node.effect = p[0]; node.count = p[1]; node.bypass = p[2] != 0;
    node.flags = p[3] | uint16_t(p[4]) << 8;
    node.fingerprint = get32(p + 5);
    memcpy(node.filename, p + 9, 42);
    node.quality = p[99];
    memset(node.parameterStrings, 0, sizeof(node.parameterStrings));
    if (size == kPatchStringFileBytes)
        memcpy(node.parameterStrings, bytes + kPatchFileBytes + index * kParameterStringBytes,
               kParameterStringBytes);
    for (unsigned j = 0; j < kMaxParams; ++j)
    {
        uint32_t bits = get32(p + 51 + j * 4);
        memcpy(&node.params[j], &bits, 4);
    }
}

bool decodePatch(const uint8_t* bytes, size_t size, ChainDefinition& chain)
{
    if ((size != kPatchFileBytes && size != kPatchStringFileBytes) ||
        memcmp(bytes, kPatchMagic, sizeof(kPatchMagic) - 1) ||
        bytes[4] != (size == kPatchFileBytes ? kPatchVersion : 2) ||
        bytes[5] > kMaxStages || bytes[6] > 1 || bytes[7] > 100 || !get32(bytes + 8) ||
        get32(bytes + 12) != fingerprint(bytes + 16, size - 16, fingerprint(bytes, 12)))
        return false;
    for (unsigned i = 0; i <= bytes[5]; ++i)
    {
        Node node;
        readNode(bytes, size, i, node);
        if (!validNode(node, i)) return false;
    }
    chain.count = bytes[5];
    chain.mix = bytes[7];
    chain.qualityResolved = bytes[6];
    chain.generatorFrames = get32(bytes + 8);
    for (unsigned i = 0; i <= kMaxStages; ++i)
        readNode(bytes, size, i, i ? chain.stages[i - 1] : chain.source);
    return true;
}
}
