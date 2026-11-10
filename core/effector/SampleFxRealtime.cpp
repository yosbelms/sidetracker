#include "SampleFxRealtime.h"
#include "mtSampleEditorEngine.h"
#include "mtEffectAmplifier.h"
#include "mtEffectOverdrive.h"
#include "mtEffectChorus.h"
#include "mtEffectFlanger.h"
#include "mtEffectEQ.h"
#include "mtEffectCompresor.h"
#include "mtEffectLimiter.h"
#include "mtEffectBitcrusher.h"
#include "mtEffectFadeIn.h"
#include "mtEffectFadeOut.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace samplefx
{
namespace
{
// PTFX v1 has no context argument for this callback. Calls are serialized on the
// audio thread; init runs before the stream is published to that thread.
RealtimePlugin* activePlugin = nullptr;
void extraSamples(uint32_t before, uint32_t after)
{
    if (activePlugin) { activePlugin->prepend = before; activePlugin->append = after; }
}
PtfxHostApi liveApi;
int16_t saturate(int32_t value) { return std::clamp<int32_t>(value, -32768, 32767); }
}

void* RealtimeArena::allocate(size_t bytes)
{
    const size_t padding = (-reinterpret_cast<uintptr_t>(base)) & 31;
    if (!base || padding > remaining || bytes > remaining - padding) return nullptr;
    void* result = base + padding;
    base += padding + bytes;
    remaining -= padding + bytes;
    memset(result, 0, bytes);
    return result;
}
RealtimeBuiltin::RealtimeBuiltin() = default;
RealtimeBuiltin::~RealtimeBuiltin() = default;
void RealtimeBuiltin::reset() { fx.reset(); native.reset(); }

bool RealtimeBuiltin::supports(uint8_t id)
{
    if(palette::contains(id)) return true;
    switch (id)
    {
        case editorEffectAmplifier: case editorEffectOverdrive: case editorEffectDelay:
        case editorEffectBitcrusher: case editorEffectChorus: case editorEffectFlanger:
        case editorEffectEQ: case editorEffectCompresor: case editorEffectLimiter:
        case editorEffectFadeIn: case editorEffectFadeOut: return true;
        default: return false;
    }
}

bool RealtimeBuiltin::prepare(const Node& node, uint32_t frames, uint8_t ch, RealtimeArena& arena, unsigned stage, float tempo)
{
    fx.reset();
    native.reset(); nativeParametersValid=true;
    settings = node;
    channels = ch; inputFrames = frames; position = 0; heldFrames = 0;
    history[0] = history[1] = nullptr;
    if(palette::contains(node.effect))
        return native.prepare(node,ch,frames,{&arena,[](void* a,size_t n){return static_cast<RealtimeArena*>(a)->allocate(n);}},stage,tempo);
    switch (node.effect)
    {
        case editorEffectAmplifier: fx.reset(new mtEffectAmplifier{}); break;
        case editorEffectOverdrive: fx.reset(new mtEffectOverdrive{}); break;
        case editorEffectBitcrusher: fx.reset(new mtEffectBitcrusher{}); break;
        case editorEffectChorus: fx.reset(new mtEffectChorus{}); break;
        case editorEffectFlanger: fx.reset(new mtEffectFlanger{}); break;
        case editorEffectEQ: fx.reset(new mtEffectEQ{}); break;
        case editorEffectCompresor: fx.reset(new mtEffectCompresor{}); break;
        case editorEffectLimiter: fx.reset(new mtEffectLimiter{}); break;
        case editorEffectFadeIn: fx.reset(new mtEffectFadeIn{}); break;
        case editorEffectFadeOut: fx.reset(new mtEffectFadeOut{}); break;
        case editorEffectDelay:
            // History includes every audible tap, matching the finite offline
            // echo sum instead of introducing a different feedback algorithm.
            parameters(node);
            historyFrames = tail() + 1;
            for (unsigned c = 0; c < ch; ++c)
            {
                history[c] = static_cast<int16_t*>(arena.allocate(size_t(historyFrames) * 2));
                if (!history[c]) return false;
            }
            return true;
        default: return false;
    }
    if (!fx) return false;
    for (unsigned i = 0; i < node.count; ++i) { float value = node.params[i]; fx->setParameter(value, i); }
    int16_t dummy[128] = {};
    int16_t* ptrs[2] = {dummy, dummy};
    fx->bindOffline(ptrs, ptrs, frames);
    if (!fx->startProcess()) return false;
    if (node.effect == editorEffectChorus || node.effect == editorEffectFlanger)
    {
        for (unsigned c = 0; c < mtEffect::kChannels; ++c)
        {
            // Mono processing on stereo hardware uses private storage as well.
            auto* buffer = static_cast<int16_t*>(arena.allocate(4096 * 2));
            if (!buffer) return false;
            if (node.effect == editorEffectChorus)
                static_cast<mtEffectChorus*>(fx.get())->channelParams[c].l_delayline = buffer;
            else static_cast<mtEffectFlanger*>(fx.get())->channelParams[c].l_delayline = buffer;
        }
    }
    return true;
}

void RealtimeBuiltin::parameters(const Node& node, float tempo)
{
    settings = node;
    if(palette::contains(node.effect)) { nativeParametersValid=native.parameters(node,tempo); return; }
    if (node.effect == editorEffectDelay)
    {
        delayFrames = std::max<uint32_t>(1u, uint32_t(uint16_t(node.params[1]) * 44.1f));
        float gain = 1;
        unsigned voices = 0;
        while (gain > 0.01f && voices < 99) { gain *= node.params[0]; ++voices; }
        echoes = voices ? voices - 1 : 0;
        for (unsigned i = 0; i <= echoes; ++i) gains[i] = powf(node.params[0], i);
        return;
    }
    if (!fx) return;
    for (unsigned i = 0; i < node.count; ++i) { float value = node.params[i]; fx->setParameter(value, i); }
    switch (node.effect)
    {
        case editorEffectEQ:
        {
            auto& e = *static_cast<mtEffectEQ*>(fx.get());
            for (auto& ch : e.channelParams)
            {
                auto update = [&](auto& ctx, float freq, float bw, float level) {
                    float z1 = ctx.z1, z2 = ctx.z2;
                    e.initContext(&ctx, freq, bw, level); ctx.z1 = z1; ctx.z2 = z2;
                };
                update(ch.ctxLow, e.freqLow, e.bwLow, e.levelLow);
                update(ch.ctxMid, e.freqMid, e.bwMid, e.levelMid);
                update(ch.ctxHigh, e.freqHigh, e.bwHigh, e.levelHigh);
            }
            break;
        }
        case editorEffectCompresor:
        {
            auto& e = *static_cast<mtEffectCompresor*>(fx.get());
            e.compressorThreshold = e.interfaceParams.threshold;
            e.compressorSlope = 1 - 1 / e.interfaceParams.ratio;
            e.attackCoeff = 1 - expf(-2.2f * 1000 / (44100 * e.interfaceParams.attack));
            e.releaseCoeff = 1 - expf(-2.2f * 1000 / (44100 * e.interfaceParams.release));
            break;
        }
        case editorEffectChorus:
        {
            auto& e = *static_cast<mtEffectChorus*>(fx.get());
            for (auto& ch : e.channelParams) ch.l_circ_idx %= e.delay_length;
            break;
        }
        case editorEffectFlanger:
        {
            auto& e = *static_cast<mtEffectFlanger*>(fx.get());
            for (auto& ch : e.channelParams)
            {
                ch.l_circ_idx %= e.delay_length;
                ch.delay_offset_idx = std::clamp(e.offset_percent * e.delay_length / 100, 0, e.delay_length - 1);
            }
            break;
        }
        default: break;
    }
}

uint32_t RealtimeBuiltin::tail() const { return palette::contains(settings.effect) ? native.tail() : settings.effect == editorEffectDelay ? echoes * delayFrames : 0; }

bool RealtimeBuiltin::process(int16_t* const* input, int16_t* const* output, unsigned frames)
{
    if(palette::contains(settings.effect)) return nativeParametersValid && native.process(input,output,frames);
    if (settings.effect == editorEffectDelay)
    {
        // Parameter edits cannot grow the allocation while audio is running.
        if (tail() >= historyFrames) return false;
        for (unsigned c = 0; c < channels; ++c)
            for (unsigned i = 0; i < frames; ++i)
            {
                const uint32_t at = position + i;
                history[c][at % historyFrames] = at < inputFrames ? input[c][i] : 0;
                int32_t sum = 0;
                for (unsigned tap = 0; tap <= echoes && tap * delayFrames <= at; ++tap)
                    sum += history[c][(at - tap * delayFrames) % historyFrames] * gains[tap];
                output[c][i] = saturate(sum);
            }
        position += frames;
        return true;
    }
    if (settings.effect == editorEffectBitcrusher)
    {
        const unsigned shift = 16 - std::clamp(int(settings.params[0]), 1, 16);
        const unsigned step = std::clamp(int(44117.64706 / settings.params[1] + 0.5), 1, 64);
        for (unsigned i = 0; i < frames; ++i)
        {
            // Preserve the existing editor's 8192-frame quantizer boundaries.
            if ((position + i) % 8192 == 0) heldFrames = 0;
            if (!heldFrames)
            {
                for (unsigned c = 0; c < channels; ++c)
                    held[c] = (int32_t(input[c][i]) >> shift) * (1 << shift);
                heldFrames = step;
            }
            for (unsigned c = 0; c < channels; ++c) output[c][i] = held[c];
            --heldFrames;
        }
        position += frames;
        return true;
    }
    // Retarget only this block's pointers/counters. Filter, envelope, fade and
    // modulation history remain intact across audio callbacks.
    auto feed = [&](auto& e) {
        e.length = frames; e.processedSamples = 0; e.state = true;
        for (unsigned c = 0; c < mtEffect::kChannels; ++c)
        { e.channelParams[c].srcAddr = input[c < channels ? c : 0]; e.channelParams[c].dstAddr = output[c]; }
        return e.processBlock() == int32_t(frames);
    };
    switch (settings.effect)
    {
        case editorEffectAmplifier: return feed(*static_cast<mtEffectAmplifier*>(fx.get()));
        case editorEffectOverdrive: return feed(*static_cast<mtEffectOverdrive*>(fx.get()));
        case editorEffectChorus: return feed(*static_cast<mtEffectChorus*>(fx.get()));
        case editorEffectFlanger: return feed(*static_cast<mtEffectFlanger*>(fx.get()));
        case editorEffectEQ: return feed(*static_cast<mtEffectEQ*>(fx.get()));
        case editorEffectCompresor: return feed(*static_cast<mtEffectCompresor*>(fx.get()));
        case editorEffectLimiter: return feed(*static_cast<mtEffectLimiter*>(fx.get()));
        case editorEffectFadeIn: return feed(*static_cast<mtEffectFadeIn*>(fx.get()));
        case editorEffectFadeOut: return feed(*static_cast<mtEffectFadeOut*>(fx.get()));
        default: return false;
    }
}

bool RealtimeChain::prepare(const ChainDefinition& definition, const Audio& audio, void* memory, size_t bytes,
                            RealtimePluginLoader& loader, uint32_t maxFrames, float tempo)
{
    release();
    this->tempo=tempo;
    slowBlocks = 0;
    overrun.store(0, std::memory_order_relaxed);
    nativeUsed=false;
    failed = false; message = ""; position = 0; capacity = maxFrames; source = audio;
    if (!memory || !bytes || !capacity || definition.count > kMaxStages || definition.mix > 100 || audio.channels < 1 || audio.channels > mtEffect::kChannels)
        return fail("Invalid live preview source");
    if (definition.source.effect == kSample && (!audio.frames || !audio.channel[0] || (audio.channels == 2 && !audio.channel[1])))
        return fail("Choose a sample or generator");
    RealtimeArena arena; arena.reset(memory, bytes);
    liveApi = *loader.hostApi(); liveApi.set_extra_samples = extraSamples;
    pending = definition;
    mix.reset(definition.mix);
    dryLatency = 0;
    dryHistoryFrames = 0;
    dryHistory[0] = dryHistory[1] = nullptr;
    for (unsigned i = 0; i < definition.count; ++i)
        if (!definition.stages[i].bypass && palette::contains(definition.stages[i].effect))
            dryLatency += palette::latencyFrames(definition.stages[i]);
    count = definition.count + 1;
    uint32_t frames = definition.source.effect == kSample ? source.frames : definition.generatorFrames;
    uint8_t channels = source.channels;
    for (unsigned i = 0; i < count; ++i)
    {
        auto& stage = stages[i];
        stage.node = i ? definition.stages[i - 1] : definition.source;
        nativeUsed |= palette::contains(stage.node.effect) && !stage.node.bypass;
        stage.plugin = {};
        stage.inputFrames = frames;
        stage.channels = stage.outputChannels = channels;
        if (stage.node.count > kMaxParams) return fail("Invalid parameter count");
        if (!i && stage.node.effect == kSample) { stage.outputFrames = frames; continue; }
        if (stage.node.bypass) { stage.outputFrames = frames; continue; }
        for (unsigned p = 0; p < stage.node.count; ++p)
            if (!std::isfinite(stage.node.params[p])) return fail("Invalid effect parameter");
        if (stage.node.effect == kPlugin)
        {
            if (!loader.load(stage.node, stage.plugin, arena)) return fail("Plugin cannot prepare live preview");
            auto& plugin = stage.plugin;
            if (plugin.header.flags != stage.node.flags || plugin.header.num_params != stage.node.count ||
                !plugin.init || !plugin.parameter || !plugin.process) return fail("Plugin changed: add again");
            const bool generator = !i && (stage.node.flags & PTFX_FLAG_GENERATOR);
            if (!i && !generator) return fail("Source must be a generator");
            if (i && (stage.node.flags & PTFX_FLAG_GENERATOR)) return fail("Generator belongs in Source");
            if (generator) channels = (stage.node.flags & PTFX_FLAG_STEREO) ? mtEffect::kChannels : 1;
            stage.channels = channels;
            if (channels == 2 && !(stage.node.flags & PTFX_FLAG_STEREO)) return fail("Plugin requires a mono source");
            activePlugin = &plugin;
            plugin.init(plugin.scratch, plugin.header.scratch_size, &liveApi);
            for (unsigned p = 0; p < stage.node.count; ++p)
            {
                if (stage.node.params[p] < plugin.header.params[p].min_val || stage.node.params[p] > plugin.header.params[p].max_val)
                    return fail("Parameter outside supported range");
                plugin.parameter(p, stage.node.params[p]);
            }
            activePlugin = nullptr;
            // Prepending changes the upstream timeline and needs a prepared pass.
            if (plugin.prepend) return fail("Prepared preview: plugin inserts audio before source");
            stage.outputChannels = (stage.node.flags & PTFX_FLAG_CONVERT_TO_MONO) ? 1 : channels;
        }
        else if (!stage.builtin.prepare(stage.node, frames, channels, arena, i, tempo))
            return fail(stage.node.effect == editorEffectNormalizer ? "Prepared preview: normalization needs analysis" :
                        "Prepared preview: effect or live memory limit");
        const uint64_t end = uint64_t(frames) + (stage.node.effect == kPlugin ? (i ? stage.plugin.append : 0) : stage.builtin.tail());
        if (!end || end > capacity) return fail("Effect output too long");
        frames = stage.outputFrames = end;
        channels = stage.outputChannels;
    }
    totalFrames = frames; outputChannels = channels;
    dryChannels = definition.source.effect == kSample ? source.channels : stages[0].outputChannels;
    if (definition.source.effect != kSample)
    {
        dryHistoryFrames = dryLatency + kBlock + 1;
        for (unsigned c = 0; c < dryChannels; ++c)
        {
            dryHistory[c] = static_cast<int16_t*>(arena.allocate(dryHistoryFrames * sizeof(int16_t)));
            if (!dryHistory[c]) return fail("Generator dry history exceeds memory");
        }
    }
    parametersDirty = false;
    return true;
}

bool RealtimeChain::updateLengths()
{
    uint32_t frames = pending.source.effect == kSample ? source.frames : pending.generatorFrames;
    for (unsigned i = 0; i < count; ++i)
    {
        auto& s = stages[i]; s.inputFrames = frames;
        uint64_t end = frames;
        if (!s.node.bypass && (i || s.node.effect != kSample))
            end += s.node.effect == kPlugin ? (i ? s.plugin.append : 0) : s.builtin.tail();
        if (end > capacity || (s.node.effect == kPlugin && s.plugin.prepend)) return fail("Live length changed; restart preview");
        frames = s.outputFrames = end;
    }
    totalFrames = frames;
    return true;
}

void RealtimeChain::reportDuration(unsigned chainUs, unsigned callbackUs)
{
    if (chainUs <= kChainBudgetUs && callbackUs <= kCallbackBudgetUs)
    {
        if (slowBlocks == 2) overrun.store(0, std::memory_order_relaxed);
        slowBlocks = 0;
        return;
    }

    if (slowBlocks < 2) ++slowBlocks;
    if (slowBlocks == 2)
    {
        const uint32_t measured = std::min(chainUs, 0xffffu) | (std::min(callbackUs, 0xffffu) << 16);
        overrun.store(measured, std::memory_order_relaxed);
    }
}

RealtimeChain::BudgetOverrun RealtimeChain::budgetOverrun() const
{
    const uint32_t measured = overrun.load(std::memory_order_relaxed);
    return {measured & 0xffffu, measured >> 16};
}

bool RealtimeChain::applyParameters()
{
    if (!parametersDirty) return true;
    parametersDirty = false;
    if (pending.mix > 100) return fail("Invalid chain mix");
    mix.set(pending.mix);
    for (unsigned i = 0; i < count; ++i)
    {
        auto& s = stages[i]; const auto& n = i ? pending.stages[i - 1] : pending.source;
        if (s.node.bypass || (!i && n.effect == kSample)) continue;
        if (s.node.effect == kPlugin)
        {
            activePlugin = &s.plugin;
            for (unsigned p = 0; p < n.count; ++p)
                if (s.node.params[p] != n.params[p]) s.plugin.parameter(p, n.params[p]);
            activePlugin = nullptr;
        }
        else s.builtin.parameters(n,tempo);
        s.node = n;
    }
    return updateLengths();
}

unsigned RealtimeChain::process(int16_t* left, int16_t* right, unsigned requested)
{
    if (!left || !right || requested > kBlock || finished() || !applyParameters() || finished()) return 0;
    const unsigned frames = std::min<uint32_t>(requested, totalFrames - position);
    memset(work, 0, sizeof(work));
    unsigned current = 0;
    if (pending.source.effect == kSample && position < source.frames)
        for (unsigned c = 0; c < source.channels; ++c)
            memcpy(work[current][c], source.channel[c] + position, std::min<uint32_t>(frames, source.frames - position) * 2);
    for (unsigned i = 0; i < count; ++i)
    {
        auto& s = stages[i];
        if ((!i && s.node.effect == kSample) || s.node.bypass) continue;
        const unsigned next = 1 - current;
        memset(work[next], 0, sizeof(work[next]));
        if (position < s.outputFrames)
        {
            unsigned count = std::min<uint32_t>(frames, s.outputFrames - position);
            int16_t* in[2] = {work[current][0], work[current][s.channels == 2 ? 1 : 0]};
            int16_t* out[2] = {work[next][0], work[next][1]};
            if (s.node.effect == kPlugin)
            {
                unsigned done = 0;
                while (done < count)
                {
                    const bool body = i && position + done < s.inputFrames;
                    unsigned block = body ? std::min<uint32_t>(count - done, s.inputFrames - position - done) : count - done;
                    const int16_t* inputs[2] = {body ? in[0] + done : nullptr, body ? in[1] + done : nullptr};
                    int16_t* outputs[2] = {out[0] + done, out[1] + done};
                    activePlugin = &s.plugin;
                    const int32_t result = s.plugin.process(inputs, outputs, block, s.channels);
                    activePlugin = nullptr;
                    if (result <= 0 || unsigned(result) > block) { fail("Plugin returned invalid live audio"); return 0; }
                    done += result;
                }
            }
            else if (!s.builtin.process(in, out, count)) { fail("Live effect changed; restart preview"); return 0; }
        }
        current = next;
        if (!i && dryHistoryFrames)
            for (unsigned c = 0; c < dryChannels; ++c)
                for (unsigned frame = 0; frame < frames; ++frame)
                    dryHistory[c][(position + frame) % dryHistoryFrames] = work[current][c][frame];
    }
    if (mix.fullyWet())
    {
        memcpy(left, work[current][0], frames * 2);
        memcpy(right, work[current][outputChannels == 2 ? 1 : 0], frames * 2);
    }
    else for (unsigned frame = 0; frame < frames; ++frame)
    {
        const unsigned at = position + frame;
        int16_t dry[2] = {};
        if (at >= dryLatency)
        {
            const unsigned read = at - dryLatency;
            if (pending.source.effect == kSample && read < source.frames)
                for (unsigned c = 0; c < dryChannels; ++c) dry[c] = source.channel[c][read];
            else if (dryHistoryFrames && read < stages[0].outputFrames)
                for (unsigned c = 0; c < dryChannels; ++c) dry[c] = dryHistory[c][read % dryHistoryFrames];
        }
        if (outputChannels == 1 && dryChannels == 2) dry[0] = int16_t((int32_t(dry[0]) + dry[1]) / 2);
        const int32_t gain = mix.advance();
        left[frame] = ChainMix::blend(dry[0], work[current][0][frame], gain);
        right[frame] = outputChannels == 1 ? left[frame] : ChainMix::blend(dry[dryChannels == 2 ? 1 : 0], work[current][1][frame], gain);
    }
    position += frames;
    return frames;
}

void RealtimeChain::release()
{
    // Destruction/allocation is kept off the audio thread.
    for (auto& s : stages) s.builtin.reset();
    count = 0; totalFrames = 0; parametersDirty = false;
    activePlugin = nullptr;
}
}
