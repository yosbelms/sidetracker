// DSP adapted from Punch-In FX by Vincent Fillion. See LICENSE.
#include "PerfFx.h"
#include <algorithm>
#include <array>
#include <cmath>

extern "C" const int16_t AudioWaveformSine[257];

namespace perffx
{
constexpr Column columns[kColumns] = {
    {Effect::BeatRepeat, "Beat Repeat", {"1/8", "1/8T", "1/16", "1/32"}},
    {Effect::Stutter, "Cutter", {"80%", "60%", "40%", "20%"}},
    {Effect::Scatter, "Scatter", {"Skip", "Shuffle", "Flip", "Chop"}},
    {Effect::DubEcho, "Dub Echo", {"1/8", "1/8D", "1/4", "1/4D"}},
    {Effect::Cassette, "Cassette", {"Warm", "Wow", "Worn", "Chewed"}},
    {Effect::Reverse, "Reverse", {"1x", "1.1x", "1.2x", "1.3x"}},
    {Effect::Scratch, "Scratch", {"Baby", "Swing", "Cut", "Scribble"}},
    {Effect::TapeStop, "Tape Stop", {"1/4", "1/8", "1/16", "1/32"}},
    {Effect::LowPass, "Low Pass", {"4kHz", "2kHz", "800Hz", "250Hz"}},
    {Effect::HighPass, "High Pass", {"160Hz", "400Hz", "900Hz", "1.8kHz"}},
    {Effect::Crush, "Crush", {"2x/12b", "4x/10b", "8x/8b", "16x/6b"}},
    {Effect::Unison, "Unison", {"20%", "47%", "73%", "100%"}},
};

namespace
{
constexpr float kPcm = 1.0f / 32768.0f;
constexpr unsigned kDelayMask = kDelayFrames - 1;
constexpr unsigned kScatterSteps = 8;
constexpr unsigned kScratchTicks = 16;
constexpr unsigned kScratchStrokes = 8;
struct ScratchStroke
{
    uint8_t ticks;
    int8_t travel; // Percent of the capture excursion. Negative pulls back, zero passes dry.
};
constexpr ScratchStroke scratchPatterns[kLevels][kScratchStrokes] = {
    {{6, 75}, {6, -75}, {4, 0}},
    {{3, 65}, {5, -65}, {3, 90}, {5, -90}},
    {{3, 80}, {2, -80}, {3, 0}, {2, 90}, {3, -90}, {3, 0}},
    {{2, 30}, {2, -30}, {1, 24}, {1, -24}, {1, 30}, {1, -30}, {3, 80}, {5, -80}},
};
static_assert([] {
    for (const auto& pattern : scratchPatterns)
    {
        unsigned ticks = 0;
        for (const auto& stroke : pattern) ticks += stroke.ticks;
        if (ticks != kScratchTicks) return false;
    }
    return true;
}());

struct CassettePreset
{
    float cutoff, drive, wowMs, flutterMs, hiss;
};
constexpr CassettePreset cassettePresets[kLevels] = {
    {4300, 0.36f, 0, 0, 0},
    {7500, 0.13f, 1.5f, 0.06f, 0},
    {4700, 0.18f, 2.5f, 0.28f, 0.002f},
    {3700, 0.22f, 2.8f, 0.36f, 0.0035f},
};

struct ScatterStep
{
    uint8_t slice;
    uint8_t openQuarters;
    bool reverse = false;
};
constexpr ScatterStep scatterPatterns[kLevels][kScatterSteps] = {
    {{0,4}, {1,4}, {2,4}, {2,4}, {4,4}, {5,4}, {6,4}, {6,3}},
    {{0,4}, {2,4}, {1,4}, {3,3}, {4,4}, {6,4}, {5,4}, {7,2}},
    {{0,4}, {3,3,true}, {2,4}, {1,2,true}, {4,4}, {7,3,true}, {6,4}, {5,2,true}},
    {{0,3}, {4,2,true}, {0,2}, {3,0}, {6,3,true}, {2,1}, {6,2,true}, {7,0}},
};

constexpr auto effectColumns = [] {
    std::array<uint8_t, static_cast<unsigned>(Effect::Cassette) + 1> result{};
    result.fill(kColumns);
    for (unsigned i = 0; i < kColumns; ++i)
        result[static_cast<unsigned>(columns[i].effect)] = i;
    // Preserve the command presets for the original split loop columns.
    for (auto effect : {Effect::Loop, Effect::TripletLoop})
        result[static_cast<unsigned>(effect)] = result[static_cast<unsigned>(Effect::BeatRepeat)];
    return result;
}();

unsigned column(Effect effect)
{
    const auto id = static_cast<unsigned>(effect);
    return id < effectColumns.size() ? effectColumns[id] : kColumns;
}

// Serialized pattern order. The Performance grid may be rearranged independently.
constexpr struct
{
    Effect effect;
    const char* mnemonic;
} patternEffects[kColumns] = {
    {Effect::BeatRepeat, "BR"}, {Effect::Stutter, "CT"}, {Effect::Scatter, "SC"},
    {Effect::DubEcho, "DE"}, {Effect::Cassette, "CA"}, {Effect::Reverse, "RV"},
    {Effect::Scratch, "SK"}, {Effect::TapeStop, "TS"}, {Effect::LowPass, "LP"},
    {Effect::HighPass, "HP"}, {Effect::Crush, "CR"}, {Effect::Unison, "UN"},
};

bool captures(Effect effect)
{
    return effect == Effect::BeatRepeat || effect == Effect::Loop || effect == Effect::TripletLoop ||
           effect == Effect::Scratch || effect == Effect::Scatter || effect == Effect::Reverse ||
           effect == Effect::TapeStop;
}

float sine(float phase)
{
    if (phase >= 1) phase -= 1;
    const float position = phase * 256;
    const unsigned index = static_cast<unsigned>(position) & 255u;
    const float fraction = position - index;
    return (AudioWaveformSine[index] +
            fraction * (AudioWaveformSine[index + 1] - AudioWaveformSine[index])) * kPcm;
}

float blend(float dry, float wet, float amount) { return dry + (wet - dry) * amount; }

float lowpass(float sample, float coefficient, float (&state)[2])
{
    state[0] = blend(state[0], sample, coefficient);
    state[1] = blend(state[1], state[0], coefficient);
    return state[1];
}

float highpass(float sample, float coefficient, float (&state)[2])
{
    for (auto& pole : state)
    {
        const float step = (sample - pole) * coefficient;
        const float low = pole + step;
        pole = low + step;
        sample -= low;
    }
    return sample;
}

int16_t pcm(float sample)
{
    return static_cast<int16_t>(std::clamp(sample * 32768.0f, -32768.0f, 32767.0f));
}

float crush(float sample, uint16_t mask)
{
    const int value = pcm(sample);
    // Quantize toward zero on both sides to avoid a DC bias at low bit depths.
    const int magnitude = std::abs(value) & mask;
    return (value < 0 ? -magnitude : magnitude) * kPcm;
}

void wrap(float& position, unsigned frames)
{
    if (position >= frames) position -= frames;
    else if (position < 0)
    {
        position += frames;
        if (position >= frames) position = 0;
    }
}
}

PatternCommand decodePatternCommand(uint8_t value)
{
    using Action = PatternCommand::Action;
    if (value == kPatternOff) return {Action::AllOff, nullptr, nullptr, 0};
    if (value > kPatternMax) return {Action::Invalid, nullptr, nullptr, 0};
    const bool release = value >= kPatternFirstRelease;
    const unsigned preset = value - kPatternFirstPreset;
    const auto& effect = patternEffects[release ? value - kPatternFirstRelease : preset / kLevels];
    return {release ? Action::Release : Action::Press, &columns[column(effect.effect)],
            effect.mnemonic, static_cast<uint8_t>(release ? 0 : preset % kLevels)};
}

uint8_t patternValueForPad(uint8_t pad)
{
    if (pad < kColumns * kLevels)
        for (unsigned i = 0; i < kColumns; ++i)
            if (patternEffects[i].effect == columns[pad % kColumns].effect)
                return kPatternFirstPreset + i * kLevels + pad / kColumns;
    return kPatternMax + 1;
}

uint8_t patternPadForValue(uint8_t value)
{
    const auto command = decodePatternCommand(value);
    return command.action == PatternCommand::Action::Press
        ? column(command.column->effect) + command.level * kColumns : kColumns * kLevels;
}

void Rack::enqueue(Command command)
{
    // A preempting producer never spins on the interrupted producer.
    if (producer.test_and_set(std::memory_order_acquire))
    {
        resetRequested.store(true, std::memory_order_release);
        return;
    }
    const auto next = (head.load(std::memory_order_relaxed) + 1) % kQueue;
    if (next == tail.load(std::memory_order_acquire))
        resetRequested.store(true, std::memory_order_release);
    else
    {
        commands[head.load(std::memory_order_relaxed)] = command;
        head.store(next, std::memory_order_release);
    }
    producer.clear(std::memory_order_release);
}

void Rack::press(Owner token, Effect effect, uint8_t level)
{
    if (token && column(effect) < kColumns && level < kLevels)
        enqueue({token, effect, level, Operation::Press});
}

void Rack::release(Owner token)
{
    if (token) enqueue({token, Effect::None, 0, Operation::Release});
}

void Rack::releaseSource(Source source)
{
    enqueue({owner(source, 0), Effect::None, 0, Operation::ReleaseSource});
}

void Rack::sequence(uint8_t value)
{
    const auto command = decodePatternCommand(value);
    if (command.action == PatternCommand::Action::AllOff)
        releaseSource(Source::Sequencer);
    else if (command.column)
    {
        const auto effect = command.column->effect;
        const auto token = owner(Source::Sequencer, static_cast<uint8_t>(effect));
        if (command.action == PatternCommand::Action::Release)
            release(token);
        else
            press(token, effect, command.level);
    }
}

void Rack::reset() { enqueue({0, Effect::None, 0, Operation::Reset}); }

int Rack::soundingLevel(unsigned index) const
{
    return index < kColumns ? static_cast<int>(sounding[index].load(std::memory_order_relaxed)) - 1 : -1;
}

void Rack::clear()
{
    for (auto& entry : held) entry = {};
    for (auto& order : columnOrder) order = 0;
    for (auto& lane : lanes) lane = {};
    hasHeld = false;
    serial = historyWrite = historyValid = 0;
}

void Rack::consume(bool sequencedOnly)
{
    const auto end = head.load(std::memory_order_acquire);
    if (resetRequested.exchange(false, std::memory_order_acq_rel))
    {
        tail.store(end, std::memory_order_release);
        clear();
        return;
    }

    auto index = tail.load(std::memory_order_relaxed);
    while (index != end)
    {
        const auto command = commands[index];
        index = (index + 1) % kQueue;
        tail.store(index, std::memory_order_release);
        if (command.operation == Operation::Reset)
        {
            clear();
            continue;
        }
        const auto source = static_cast<Source>(command.owner >> 8);
        if (sequencedOnly && source != Source::Sequencer) continue;

        Held* free = nullptr;
        bool unchanged = false;
        for (auto& entry : held)
        {
            if (command.operation == Operation::Press && source == Source::Sequencer &&
                entry.owner == command.owner && entry.effect == command.effect && entry.level == command.level)
            {
                unchanged = true;
                break;
            }
            if (entry.owner && (entry.owner == command.owner ||
                (command.operation == Operation::ReleaseSource && (entry.owner >> 8) == (command.owner >> 8))))
                entry = {};
            if (!entry.owner && !free) free = &entry;
        }
        if (unchanged) continue;
        if (command.operation == Operation::Press)
        {
            if (!free)
            {
                resetRequested.store(true, std::memory_order_release);
                return;
            }
            *free = {command.owner, command.effect, command.level, ++serial};
            auto& order = columnOrder[column(command.effect)];
            if (!order) order = serial;
        }

        bool occupied[kColumns] = {};
        hasHeld = false;
        for (const auto& entry : held)
        {
            if (!entry.owner) continue;
            occupied[column(entry.effect)] = true;
            hasHeld = true;
        }
        for (unsigned i = 0; i < kColumns; ++i)
            if (!occupied[i]) columnOrder[i] = 0;
    }
}

void Rack::activate(Lane& lane, const Held& entry, uint32_t order)
{
    lane = {};
    lane.effect = entry.effect;
    lane.level = entry.level;
    lane.order = order;
    lane.attack = true;
    const auto level = lane.level;
    if (lane.effect == Effect::TapeStop)
    {
        const auto frames = std::clamp(beatFrames >> level, 64u, kHistoryFrames / 2);
        // Start just behind the live write head, with both interpolation taps valid.
        // Playback only falls further behind as its speed drops below 1x.
        lane.segment = historyWrite < 2 ? historyWrite + kHistoryFrames - 2 : historyWrite - 2;
        lane.length = frames + 2;
        lane.step = 1.0f / frames;
    }
    else if (captures(lane.effect))
    {
        unsigned requested = 2 * beatFrames;
        if (lane.effect == Effect::BeatRepeat)
        {
            constexpr unsigned divisors[] = {2, 3, 4, 8};
            requested = beatFrames / divisors[level];
        }
        if (lane.effect == Effect::Loop) requested = beatFrames / (2u << level);
        if (lane.effect == Effect::TripletLoop) requested = beatFrames / (3u << level);
        if (lane.effect == Effect::Scratch) requested = beatFrames;
        lane.length = std::min({std::max(64u, requested), historyValid, kHistoryFrames / 2});
        lane.segment = historyWrite + kHistoryFrames - lane.length;
        if (lane.segment >= kHistoryFrames) lane.segment -= kHistoryFrames;
    }
    switch (lane.effect)
    {
        case Effect::Scratch:
            lane.beatLength = beatFrames;
            lane.depth = 0.55f * (lane.length - 1);
            setScratchStroke(lane);
            break;
        case Effect::Cassette:
        {
            const auto& preset = cassettePresets[level];
            lane.depth = preset.cutoff / (preset.cutoff + 0.15915494f * rate);
            lane.phaseStep = 0.55f / rate;
            lane.step = 7.3f / rate;
            lane.edgeScale = preset.wowMs * (0.001f * rate);
            lane.flutterDepth = preset.flutterMs * (0.001f * rate);
            lane.echoFrames = 2 + static_cast<unsigned>(lane.edgeScale + lane.flutterDepth);
            break;
        }
        case Effect::Scatter:
            lane.chunkFrames = lane.length / kScatterSteps;
            break;
        case Effect::DubEcho:
        {
            constexpr unsigned quarterBeats[] = {2, 3, 4, 6};
            constexpr float amounts[] = {0.25f, 0.38f, 0.5f, 0.65f};
            lane.echoFrames = std::clamp(beatFrames * quarterBeats[level] / 4, 1u, kHistoryFrames / 2);
            lane.depth = amounts[level];
            break;
        }
        case Effect::HighPass:
        {
            constexpr float cutoffs[] = {160, 400, 900, 1800};
            const float phase = cutoffs[level] / (2 * rate);
            // Bilinear one-pole sections. Compensate for cascading two poles
            // so the combined response is -3 dB at the labeled cutoff.
            const float warped = 0.64359425f * sine(phase) / sine(phase + 0.25f);
            lane.depth = warped / (1 + warped);
            break;
        }
        case Effect::Crush:
        {
            lane.crushPeriod = 2u << level;
            lane.crushMask = static_cast<uint16_t>(0xffffu << (4 + 2 * level));
            break;
        }
        case Effect::Reverse:
            lane.position = lane.length - 1;
            lane.step = 1 + 0.1f * level;
            break;
        case Effect::LowPass:
        {
            constexpr float cutoffs[] = {4000, 2000, 800, 250};
            const float halfAngleSine = sine(cutoffs[level] / (2 * rate));
            // Solve for the combined -3 dB cutoff of two cascaded one-pole filters.
            const float d = 4 * halfAngleSine * halfAngleSine / 0.41421356f;
            lane.depth = 2 * d / (std::sqrt(d * (d + 4)) + d);
            break;
        }
        default: break;
    }
}

void Rack::setScratchStroke(Lane& lane)
{
    const auto& stroke = scratchPatterns[lane.level][lane.chunk];
    lane.chunkFrames = lane.beatLength * stroke.ticks;
    lane.phaseStep = 1.0f / lane.chunkFrames;
    lane.step = 0.01f * std::abs(stroke.travel) * lane.depth;
}

void Rack::admit()
{
    const Held* latest[kColumns] = {};
    for (const auto& entry : held)
    {
        if (!entry.owner) continue;
        auto& current = latest[column(entry.effect)];
        if (!current || entry.order > current->order) current = &entry;
    }

    bool assigned[kColumns] = {};
    for (auto& lane : lanes)
    {
        if (lane.effect == Effect::None) continue;
        const auto i = column(lane.effect);
        const auto* entry = latest[i];
        lane.attack = entry && entry->effect == lane.effect && entry->level == lane.level &&
                      lane.order == columnOrder[i];
        if (!lane.attack && lane.gate <= 0) lane = {};
        else assigned[i] = true;
    }

    for (auto& lane : lanes)
    {
        if (lane.effect != Effect::None) continue;
        unsigned next = kColumns;
        for (unsigned i = 0; i < kColumns; ++i)
        {
            if (latest[i] && !assigned[i] && (!captures(latest[i]->effect) || historyValid >= 64) &&
                (next == kColumns || columnOrder[i] < columnOrder[next]))
                next = i;
        }
        if (next == kColumns) break;
        activate(lane, *latest[next], columnOrder[next]);
        assigned[next] = true;
    }

    uint8_t rows[kColumns] = {};
    laneCount = 0;
    for (unsigned i = 0; i < kLanes; ++i)
    {
        const auto& lane = lanes[i];
        if (lane.effect == Effect::None) continue;
        unsigned position = laneCount++;
        while (position && lanes[renderOrder[position - 1]].order > lane.order)
        {
            renderOrder[position] = renderOrder[position - 1];
            --position;
        }
        renderOrder[position] = i;
        rows[column(lane.effect)] = lane.level + 1;
    }
    for (unsigned i = 0; i < kColumns; ++i)
        sounding[i].store(rows[i], std::memory_order_relaxed);
}

void Rack::beginBlock(float tempo, float sampleRate, uint8_t percent, bool record,
                     bool sequencedOnly)
{
    consume(sequencedOnly);
    rate = sampleRate;
    beatFrames = static_cast<unsigned>(rate * 60 / std::clamp(tempo >= 5 ? tempo : 5.0f, 5.0f, 800.0f));
    attackStep = 100 / rate;
    releaseStep = (1000.0f / 30) / rate;
    const float target = std::min<unsigned>(percent, 100) * 0.01f;
    if (target != mixTarget)
    {
        mixTarget = target;
        mixRemaining = static_cast<unsigned>(rate * 0.01f);
        mixStep = (mixTarget - mix) / mixRemaining;
    }
    if (hasHeld || laneCount) admit();
    for (unsigned i = 0; i < laneCount; ++i)
    {
        auto& lane = lanes[renderOrder[i]];
        switch (lane.effect)
        {
            case Effect::Unison:
                lane.phaseStep = 1.5f / rate;
                lane.depth = 0.008f * rate;
                lane.edgeScale = lane.depth * (0.2f + lane.level * (0.8f / 3));
                break;
            case Effect::Stutter:
            {
                const float period = std::max(64u, beatFrames / 2);
                lane.phaseStep = 1 / period;
                lane.duty = 0.8f - 0.2f * lane.level;
                lane.edgeScale = 1 / std::min(0.002f * rate * lane.phaseStep, lane.duty * 0.5f);
                break;
            }
            case Effect::LowPass: lane.phaseStep = 0.25f / beatFrames; break;
            case Effect::Cassette:
                if (lane.level == 3)
                {
                    lane.duty = 1.0f / beatFrames;
                    lane.sagDepth = std::min(0.006f * rate, 0.025f * beatFrames);
                    if (lane.beatLength != beatFrames)
                    {
                        if (lane.beatLength)
                            lane.chunkOffset = lane.chunkOffset * (static_cast<float>(beatFrames) / lane.beatLength);
                        lane.beatLength = beatFrames;
                    }
                }
                break;
            default: break;
        }
    }
    capture = record || laneCount != 0;
}

bool Rack::wet() const { return laneCount && (mix > 0 || mixTarget > 0); }

float Rack::readHistory(const Lane& lane, unsigned channel) const
{
    const unsigned offset = static_cast<unsigned>(lane.position);
    unsigned a = lane.segment + offset;
    if (a >= kHistoryFrames) a -= kHistoryFrames;
    unsigned b = offset + 1 == lane.length ? lane.segment : a + 1;
    if (b == kHistoryFrames) b = 0;
    return blend(memory.history[a][channel], memory.history[b][channel], lane.position - offset) * kPcm;
}

float Rack::readDelay(const Lane& lane, unsigned index, float position, unsigned channel) const
{
    wrap(position, kDelayFrames);
    const auto a = static_cast<unsigned>(position);
    const auto b = (a + 1) & kDelayMask;
    const auto valid = [&](unsigned frame)
    {
        return ((lane.write - 1 - frame) & kDelayMask) < lane.valid;
    };
    const int16_t x = valid(a) ? memory.delay[index][a][channel] : 0;
    const int16_t y = valid(b) ? memory.delay[index][b][channel] : 0;
    return blend(x, y, position - a) * kPcm;
}

void Rack::processLane(Lane& lane, unsigned index, float& left, float& right)
{
    if (lane.attack) lane.gate = std::min(1.0f, lane.gate + attackStep);
    else lane.gate = std::max(0.0f, lane.gate - releaseStep);
    if (lane.gate == 0) return;

    float l = left, r = right;
    if (lane.effect == Effect::TapeStop)
    {
        const float speed = 1 - lane.timer * lane.step;
        if (speed > 0)
        {
            const float gain = std::min(1.0f, 8 * speed);
            l = readHistory(lane, 0) * gain;
            r = readHistory(lane, 1) * gain;
            lane.position += speed;
            ++lane.timer;
        }
        else l = r = 0;
    }
    else if (lane.effect == Effect::Scatter)
    {
        const auto& step = scatterPatterns[lane.level][lane.chunk];
        const unsigned open = lane.chunkFrames * step.openQuarters / 4;
        l = r = 0;
        if (lane.chunkOffset < open)
        {
            const unsigned offset = step.reverse ? lane.chunkFrames - 1 - lane.chunkOffset : lane.chunkOffset;
            unsigned frame = lane.segment + step.slice * lane.chunkFrames + offset;
            if (frame >= kHistoryFrames) frame -= kHistoryFrames;
            const unsigned edge = std::min(lane.chunkOffset, open - 1 - lane.chunkOffset);
            const unsigned fade = std::max(1u, std::min(32u, (open - 1) / 2));
            const float gain = kPcm * (edge < fade ? float(edge) / fade : 1.0f);
            l = memory.history[frame][0] * gain;
            r = memory.history[frame][1] * gain;
        }
        if (++lane.chunkOffset == lane.chunkFrames)
        {
            lane.chunkOffset = 0;
            if (++lane.chunk == kScatterSteps) lane.chunk = 0;
        }
    }
    else if (lane.effect == Effect::Scratch)
    {
        const auto travel = scratchPatterns[lane.level][lane.chunk].travel;
        if (travel)
        {
            // The push peaks early. Read the curve backwards for a softer pull.
            constexpr float peak = 0.375f;
            float phase = lane.chunkOffset * lane.phaseStep;
            if (travel < 0) phase = 1 - phase;
            const bool beforePeak = phase < peak;
            const float distance = beforePeak ? phase : 1 - phase;
            const float speed = distance * (beforePeak ? 1 / peak : 1 / (1 - peak));
            const float progress = distance * speed;
            lane.position = lane.step * (beforePeak ? progress : 1 - progress);
            const float gain = std::min(1.0f, 8 * speed);
            l = blend(l, readHistory(lane, 0), gain);
            r = blend(r, readHistory(lane, 1), gain);
        }
        // Carry the fraction into the next stroke so each phrase stays one beat.
        lane.chunkOffset += kScratchTicks;
        if (lane.chunkOffset >= lane.chunkFrames)
        {
            lane.chunkOffset -= lane.chunkFrames;
            if (++lane.chunk == kScratchStrokes || !scratchPatterns[lane.level][lane.chunk].ticks)
                lane.chunk = 0;
            setScratchStroke(lane);
        }
    }
    else if (captures(lane.effect))
    {
        l = readHistory(lane, 0);
        r = readHistory(lane, 1);
        switch (lane.effect)
        {
            case Effect::BeatRepeat:
            case Effect::Loop:
            case Effect::TripletLoop: l *= 1.2f; r *= 1.2f; lane.position += 1; break;
            case Effect::Reverse: lane.position -= lane.step; break;
            default: break;
        }
        wrap(lane.position, lane.length);
    }
    else if (lane.effect == Effect::LowPass)
    {
        lane.phase = std::min(1.0f, lane.phase + lane.phaseStep);
        const float open = 1 - lane.phase;
        const float coefficient = blend(lane.depth, 1, open * open * open);
        l = lowpass(l, coefficient, lane.filterState[0]);
        r = lowpass(r, coefficient, lane.filterState[1]);
    }
    else if (lane.effect == Effect::Stutter)
    {
        lane.phase += lane.phaseStep;
        wrap(lane.phase, 1);
        float gain = std::clamp(std::min(lane.phase, lane.duty - lane.phase) * lane.edgeScale, 0.0f, 1.0f);
        if (gain > 0 && gain < 1) gain = 0.5f * (1 - sine(0.25f + 0.5f * gain));
        l *= gain;
        r *= gain;
    }
    else if (lane.effect == Effect::HighPass)
    {
        l = highpass(l, lane.depth, lane.filterState[0]);
        r = highpass(r, lane.depth, lane.filterState[1]);
    }
    else if (lane.effect == Effect::Crush)
    {
        if (!lane.crushRemaining)
        {
            lane.heldSamples[0] = crush(l, lane.crushMask);
            lane.heldSamples[1] = crush(r, lane.crushMask);
            lane.crushRemaining = lane.crushPeriod;
        }
        --lane.crushRemaining;
        l = lane.heldSamples[0];
        r = lane.heldSamples[1];
    }
    else if (lane.effect == Effect::DubEcho)
    {
        // Read the pre-rack mix so taps are bounded and need no feedback buffer.
        float echoL = 0, echoR = 0;
        unsigned distance = lane.echoFrames;
        float gain = lane.depth * kPcm;
        for (unsigned tap = 0; tap < 3 && distance <= historyValid; ++tap)
        {
            const unsigned frame = (historyWrite >= distance ? historyWrite : historyWrite + kHistoryFrames) - distance;
            echoL += memory.history[frame][0] * gain;
            echoR += memory.history[frame][1] * gain;
            distance += lane.echoFrames;
            gain *= 0.5f;
        }
        l += lowpass(echoL, 0.22f, lane.filterState[0]);
        r += lowpass(echoR, 0.22f, lane.filterState[1]);
    }
    else
    {
        if (lane.effect != Effect::Cassette || lane.level)
        {
            memory.delay[index][lane.write][0] = pcm(l);
            memory.delay[index][lane.write][1] = pcm(r);
            lane.write = (lane.write + 1) & kDelayMask;
            lane.valid = std::min(lane.valid + 1, kDelayFrames);
        }

        if (lane.effect == Effect::Cassette)
        {
            float dip = 0;
            if (lane.level)
            {
                lane.phase += lane.phaseStep;
                lane.position += lane.step;
                wrap(lane.phase, 1);
                wrap(lane.position, 1);
                float delay = lane.echoFrames + lane.edgeScale * sine(lane.phase) +
                              lane.flutterDepth * sine(lane.position);
                if (lane.level == 3)
                {
                    // Briefly stretch the tape, then return to speed over the next eighth of the beat.
                    const float phase = lane.chunkOffset * lane.duty;
                    const float bend = std::clamp(std::min(16 * phase, 1.5f - 8 * phase), 0.0f, 1.0f);
                    dip = bend * bend * (3 - 2 * bend);
                    delay += lane.sagDepth * dip;
                    if (++lane.chunkOffset >= lane.beatLength) lane.chunkOffset = 0;
                }
                const float tap = lane.write - 1.0f - delay;
                const float wet = std::clamp((lane.valid - delay - 1) * (1.0f / 64), 0.0f, 1.0f);
                l = blend(l, readDelay(lane, index, tap, 0), wet);
                r = blend(r, readDelay(lane, index, tap, 1), wet);
            }
            const auto& preset = cassettePresets[lane.level];
            float hiss = 0;
            if (preset.hiss)
            {
                lane.noise = lane.noise * 1664525u + 1013904223u;
                hiss = (static_cast<int>(lane.noise >> 16) - 32768) * kPcm * preset.hiss;
            }
            const auto color = [&](float sample, unsigned channel) {
                auto& tone = lane.filterState[channel][0];
                tone = blend(tone, std::clamp(sample, -1.0f, 1.0f), lane.depth);
                // Warm rounds off medium-level peaks as well as the loudest ones.
                const float shape = lane.level ? tone * tone : std::abs(tone);
                return tone * (1 - preset.drive * shape) * (1 - 0.38f * dip) + hiss;
            };
            l = color(l, 0);
            r = color(r, 1);
        }
        else
        {
            lane.phase += lane.phaseStep;
            wrap(lane.phase, 1);
            const float modulation = sine(lane.phase) * lane.edgeScale;
            const float center = lane.write - lane.depth - 2;
            l = (l + readDelay(lane, index, center - modulation, 0) +
                 readDelay(lane, index, center + modulation, 0)) * 0.667f;
            r = (r + readDelay(lane, index, center - modulation, 1) +
                 readDelay(lane, index, center + modulation, 1)) * 0.667f;
        }
    }
    left = blend(left, l, lane.gate);
    right = blend(right, r, lane.gate);
}

void Rack::process(const int16_t* left, const int16_t* right,
                   int16_t* outLeft, int16_t* outRight, unsigned frames)
{
    if (!laneCount && !outLeft && !outRight)
    {
        const unsigned ramp = std::min(mixRemaining, frames);
        mixRemaining -= ramp;
        mix = mixRemaining ? mix + mixStep * ramp : mixTarget;
        if (!capture) return;
        for (unsigned i = 0; i < frames; ++i)
        {
            memory.history[historyWrite][0] = left ? left[i] : 0;
            memory.history[historyWrite][1] = right ? right[i] : 0;
            if (++historyWrite == kHistoryFrames) historyWrite = 0;
        }
        historyValid = std::min(historyValid + frames, kHistoryFrames);
        return;
    }
    for (unsigned i = 0; i < frames; ++i)
    {
        const int16_t dryL = left ? left[i] : 0, dryR = right ? right[i] : 0;
        float l = dryL * kPcm, r = dryR * kPcm;
        for (unsigned j = 0; j < laneCount; ++j)
        {
            const auto index = renderOrder[j];
            processLane(lanes[index], index, l, r);
        }
        if (capture)
        {
            memory.history[historyWrite][0] = dryL;
            memory.history[historyWrite][1] = dryR;
            if (++historyWrite == kHistoryFrames) historyWrite = 0;
        }
        if (mixRemaining)
        {
            mix += mixStep;
            if (!--mixRemaining) mix = mixTarget;
        }
        if (outLeft) outLeft[i] = mix > 0 ? pcm(blend(dryL * kPcm, l, mix)) : dryL;
        if (outRight) outRight[i] = mix > 0 ? pcm(blend(dryR * kPcm, r, mix)) : dryR;
    }
    if (capture) historyValid = std::min(historyValid + frames, kHistoryFrames);
}
}
