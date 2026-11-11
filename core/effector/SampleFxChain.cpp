#include "SampleFxChain.h"
#include "stpalette/STPaletteEffects.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace samplefx
{
bool ChainDefinition::add(const Node& node)
{
    if (count >= kMaxStages || node.effect == kSample || (node.flags & 1)) return false;
    stages[count++] = node;
    return true;
}
bool ChainDefinition::remove(unsigned index)
{
    if (index >= count) return false;
    for (unsigned i = index; i + 1 < count; ++i) stages[i] = stages[i + 1];
    stages[--count] = {};
    return true;
}
bool ChainDefinition::move(unsigned index, int direction)
{
    const int target = int(index) + direction;
    if (index >= count || target < 0 || target >= count) return false;
    std::swap(stages[index], stages[target]);
    return true;
}
bool ChainDefinition::duplicate(unsigned index)
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
bool validChain(const ChainDefinition& r)
{
    if (r.count > kMaxStages || !r.generatorFrames || r.mix > 100) return false;
    for (unsigned i = 0; i <= r.count; ++i)
    {
        const Node& n = i ? r.stages[i - 1] : r.source;
        if (n.count > kMaxParams || !memchr(n.filename, 0, sizeof(n.filename))) return false;
        if (n.effect == kPlugin)
        {
            if(n.quality) return false;
            if (!n.filename[0] || strchr(n.filename, '/') || strchr(n.filename, '\\')) return false;
            if (bool(n.flags & 1) != (i == 0)) return false;
        }
        else if (stpalette::contains(n.effect)) { if (!i || !stpalette::valid(n)) return false; }
        else if ((i == 0 ? n.effect != kSample : n.effect >= 17) || n.flags != 0 || n.quality) return false;
        for (unsigned p = 0; p < n.count; ++p) if (!std::isfinite(n.params[p])) return false;
    }
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
bool Runner::start(const ChainDefinition& r, const Audio& source, int16_t* work[2][2], uint32_t cap)
{
    invalidate();
    if (!validChain(r) || !cap || source.frames > cap ||
        source.channels < 1 || source.channels > 2 ||
        (r.source.effect == kSample && (!source.frames || !source.channel[0])))
    { fail("Invalid source or chain"); return false; }
    for (unsigned b = 0; b < 2; ++b)
        for (unsigned c = 0; c < 2; ++c) buffers[b][c] = work[b][c];
    if (!buffers[0][0] || !buffers[1][0] || buffers[0][0] == buffers[1][0] ||
        (source.channels == 2 && (!source.channel[1] || !buffers[0][1] || !buffers[1][1])))
    { fail("Invalid working buffers"); return false; }
    definition = r;
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
        if (next > definition.count) { status = State::Ready; return; }
        const Node& node = next ? definition.stages[next - 1] : definition.source;
        if (next && node.bypass) { ++next; return; }
        output = { { buffers[destination][0], buffers[destination][1] }, 0, input.channels };
        backend.setStageIndex(next);
        if (!backend.begin(node, input, output, capacity, definition.generatorFrames))
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
