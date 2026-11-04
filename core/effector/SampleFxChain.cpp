#include "SampleFxChain.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace samplefx
{
bool Recipe::add(const Node& node)
{
    if (count >= kMaxStages || node.effect == kSample || (node.flags & 1)) return false;
    stages[count++] = node;
    return true;
}
bool Recipe::remove(unsigned index)
{
    if (index >= count) return false;
    for (unsigned i = index; i + 1 < count; ++i) stages[i] = stages[i + 1];
    stages[--count] = {};
    return true;
}
bool Recipe::move(unsigned index, int direction)
{
    const int target = int(index) + direction;
    if (index >= count || target < 0 || target >= count) return false;
    std::swap(stages[index], stages[target]);
    return true;
}
bool Recipe::duplicate(unsigned index)
{
    if (index >= count || count >= kMaxStages) return false;
    for (unsigned i = count; i > index + 1; --i) stages[i] = stages[i - 1];
    stages[index + 1] = stages[index];
    ++count;
    return true;
}
uint32_t fingerprint(const void* bytes, size_t size, uint32_t hash)
{
    const auto* p = static_cast<const uint8_t*>(bytes);
    while (size--) { hash ^= *p++; hash *= 16777619u; }
    return hash;
}
static void put32(uint8_t* p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(v >> (8 * i));
}
static uint32_t get32(const uint8_t* p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
static bool valid(const Recipe& r)
{
    if (r.count > kMaxStages || !r.generatorFrames) return false;
    for (unsigned i = 0; i <= r.count; ++i)
    {
        const Node& n = i ? r.stages[i - 1] : r.source;
        if (n.count > kMaxParams || !memchr(n.filename, 0, sizeof(n.filename))) return false;
        if (n.effect == kPlugin)
        {
            if (!n.filename[0] || strchr(n.filename, '/') || strchr(n.filename, '\\')) return false;
            if (bool(n.flags & 1) != (i == 0)) return false;
        }
        else if ((i == 0 ? n.effect != kSample : n.effect >= 17) || n.flags != 0) return false;
        for (unsigned p = 0; p < n.count; ++p) if (!std::isfinite(n.params[p])) return false;
    }
    return true;
}
bool encode(const Recipe& r, uint8_t* bytes, size_t size)
{
    if (size != kRecipeBytes || !valid(r)) return false;
    memset(bytes, 0, size);
    memcpy(bytes, "PTCH", 4);
    bytes[4] = 1;
    bytes[5] = r.count;
    put32(bytes + 8, r.generatorFrames);
    for (unsigned i = 0; i <= kMaxStages; ++i)
    {
        const Node& n = i ? r.stages[i - 1] : r.source;
        auto* p = bytes + 16 + i * 100;
        p[0] = n.effect; p[1] = n.count; p[2] = n.bypass;
        p[3] = uint8_t(n.flags); p[4] = uint8_t(n.flags >> 8);
        put32(p + 5, n.fingerprint);
        memcpy(p + 9, n.filename, 42);
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
bool decode(const uint8_t* bytes, size_t size, Recipe& r)
{
    if (size != kRecipeBytes || memcmp(bytes, "PTCH", 4) || bytes[4] != 1 ||
        get32(bytes + 12) != fingerprint(bytes + 16, size - 16, fingerprint(bytes, 12))) return false;
    Recipe candidate;
    candidate.count = bytes[5];
    candidate.generatorFrames = get32(bytes + 8);
    for (unsigned i = 0; i <= kMaxStages; ++i)
    {
        Node& n = i ? candidate.stages[i - 1] : candidate.source;
        const auto* p = bytes + 16 + i * 100;
        n.effect = p[0]; n.count = p[1]; n.bypass = p[2] != 0;
        n.flags = p[3] | uint16_t(p[4]) << 8;
        n.fingerprint = get32(p + 5);
        memcpy(n.filename, p + 9, 42);
        for (unsigned j = 0; j < kMaxParams; ++j)
        {
            uint32_t bits = get32(p + 51 + j * 4);
            memcpy(&n.params[j], &bits, 4);
        }
    }
    if (!valid(candidate)) return false;
    r = candidate;
    return true;
}
void Runner::invalidate()
{
    status = State::Dirty;
    processing = false;
    input = {};
    failure = "";
}
void Runner::fail(const char* message)
{
    status = State::Failed;
    processing = false;
    input = {};
    failure = message;
}
bool Runner::start(const Recipe& r, const Audio& source, int16_t* work[2][2], uint32_t cap)
{
    invalidate();
    if (!valid(r) || !cap || source.frames > cap ||
        source.channels < 1 || source.channels > 2 ||
        (r.source.effect == kSample && (!source.frames || !source.channel[0])))
    { fail("Invalid source or recipe"); return false; }
    for (unsigned b = 0; b < 2; ++b)
        for (unsigned c = 0; c < 2; ++c) buffers[b][c] = work[b][c];
    if (!buffers[0][0] || !buffers[1][0] || buffers[0][0] == buffers[1][0] ||
        (source.channels == 2 && (!source.channel[1] || !buffers[0][1] || !buffers[1][1])))
    { fail("Invalid working buffers"); return false; }
    recipe = r;
    input = source;
    capacity = cap;
    next = r.source.effect == kSample ? 1 : 0;
    destination = input.channel[0] == buffers[1][0] ? 0 : 1;
    status = State::Preparing;
    return true;
}
void Runner::update()
{
    if (status != State::Preparing) return;
    if (!processing)
    {
        if (next > recipe.count) { status = State::Ready; return; }
        const Node& node = next ? recipe.stages[next - 1] : recipe.source;
        if (next && node.bypass) { ++next; return; }
        output = { { buffers[destination][0], buffers[destination][1] }, 0, input.channels };
        if (!backend.begin(node, input, output, capacity, recipe.generatorFrames))
        { fail(backend.error()); return; }
        if (!output.frames || output.frames > capacity || output.channels < 1 || output.channels > 2 ||
            (output.channels == 2 && (!buffers[0][1] || !buffers[1][1])))
        { fail("Effect output exceeds capacity"); return; }
        processing = true;
        return;
    }
    bool complete = false;
    if (!backend.step(complete)) { fail(backend.error()); return; }
    if (complete)
    {
        input = output;
        destination ^= 1;
        processing = false;
        ++next;
    }
}
}
