// DSP adapted from Punch-In FX by Vincent Fillion. See LICENSE.
#include "PunchFx.h"
#include <algorithm>
#include <array>
#include <cmath>

extern "C" const int16_t AudioWaveformSine[257];

namespace punch
{
constexpr Column columns[kColumns] = {
    {Effect::Loop, "Loop", {"1/8", "1/16", "1/32", "1/64"}},
    {Effect::TripletLoop, "Triplet Loop", {"1/8T", "1/16T", "1/32T", "1/64T"}},
    {Effect::Unison, "Unison", {"20%", "47%", "73%", "100%"}},
    {Effect::TapeStop, "Tape Stop", {"1/4", "1/8", "1/16", "1/32"}},
    {Effect::PitchUp, "Pitch Up", {"+3 st", "+5 st", "+7 st", "+12 st"}},
    {Effect::PitchDown, "Pitch Down", {"-3 st", "-5 st", "-7 st", "-12 st"}},
    {Effect::Stutter, "Stutter", {"80%", "60%", "40%", "20%"}},
    {Effect::FilterSweep, "Filter Sweep", {"4kHz", "2kHz", "800Hz", "250Hz"}},
    {Effect::Scratch, "Scratch", {"1x/2", "2x/4", "3.5x/6", "5x/8"}},
    {Effect::Quantize, "6/8 Quantize", {"25%", "50%", "75%", "100%"}},
    {Effect::Retrigger, "Retrigger", {"4cuts", "5cuts", "6cuts*", "8cuts*"}},
    {Effect::Reverse, "Reverse", {"1x", "1.1x", "1.2x", "1.3x"}},
};

namespace
{
constexpr float kPcm = 1.0f / 32768.0f;
constexpr unsigned kGrain = 768;
constexpr unsigned kDelayMask = kDelayFrames - 1;

constexpr auto effectColumns = [] {
    std::array<uint8_t, static_cast<unsigned>(Effect::FilterSweep) + 1> result{};
    result.fill(kColumns);
    for (unsigned i = 0; i < kColumns; ++i)
        result[static_cast<unsigned>(columns[i].effect)] = i;
    return result;
}();

unsigned column(Effect effect)
{
    const auto id = static_cast<unsigned>(effect);
    return id < effectColumns.size() ? effectColumns[id] : kColumns;
}

bool captures(Effect effect)
{
    return effect == Effect::Loop || effect == Effect::TripletLoop || effect == Effect::Scratch ||
           effect == Effect::Retrigger || effect == Effect::Reverse || effect == Effect::TapeStop;
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

int16_t pcm(float sample)
{
    return static_cast<int16_t>(std::clamp(sample * 32768.0f, -32768.0f, 32767.0f));
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

void Rack::enqueue(Command command)
{
    // A preempting producer never spins on the interrupted producer.
    if (producer.test_and_set(std::memory_order_acquire))
    {
        reset();
        return;
    }
    const auto next = (head.load(std::memory_order_relaxed) + 1) % kQueue;
    if (next == tail.load(std::memory_order_acquire))
        reset();
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

void Rack::reset() { resetRequested.store(true, std::memory_order_release); }

int Rack::soundingLevel(unsigned index) const
{
    return index < kColumns ? static_cast<int>(sounding[index].load(std::memory_order_relaxed)) - 1 : -1;
}

void Rack::consume()
{
    const auto end = head.load(std::memory_order_acquire);
    if (resetRequested.exchange(false, std::memory_order_acq_rel))
    {
        tail.store(end, std::memory_order_release);
        for (auto& entry : held) entry = {};
        for (auto& order : columnOrder) order = 0;
        for (auto& lane : lanes) lane = {};
        hasHeld = false;
        serial = historyWrite = historyValid = 0;
        random = 1;
        return;
    }

    auto index = tail.load(std::memory_order_relaxed);
    while (index != end)
    {
        const auto command = commands[index];
        index = (index + 1) % kQueue;
        tail.store(index, std::memory_order_release);
        Held* free = nullptr;
        for (auto& entry : held)
        {
            if (entry.owner && (entry.owner == command.owner ||
                (command.operation == Operation::ReleaseSource && (entry.owner >> 8) == (command.owner >> 8))))
                entry = {};
            if (!entry.owner && !free) free = &entry;
        }
        if (command.operation == Operation::Press)
        {
            if (!free)
            {
                reset();
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
        if (lane.effect == Effect::Loop) requested = beatFrames / (2u << level);
        if (lane.effect == Effect::TripletLoop) requested = beatFrames / (3u << level);
        if (lane.effect == Effect::Scratch) requested = beatFrames;
        lane.length = std::min({std::max(64u, requested), historyValid, kHistoryFrames / 2});
        lane.segment = historyWrite + kHistoryFrames - lane.length;
        if (lane.segment >= kHistoryFrames) lane.segment -= kHistoryFrames;
    }
    switch (lane.effect)
    {
        case Effect::PitchUp:
        {
            constexpr float ratios[] = {1.1892071f, 1.3348399f, 1.4983071f, 2.0f};
            lane.step = ratios[level];
            lane.headB = kGrain / 2;
            break;
        }
        case Effect::PitchDown:
        {
            constexpr float ratios[] = {0.8408964f, 0.7491535f, 0.6674199f, 0.5f};
            lane.step = ratios[level];
            lane.headB = kGrain / 2;
            break;
        }
        case Effect::Scratch:
        {
            constexpr float speeds[] = {1, 2, 3.5f, 5};
            lane.step = speeds[level];
            lane.flipPeriod = std::max(256u, beatFrames / (2 * (level + 1)));
            break;
        }
        case Effect::Retrigger:
        {
            constexpr unsigned counts[] = {4, 5, 6, 8};
            lane.chunkCount = counts[level];
            lane.chunkFrames = lane.length / lane.chunkCount;
            for (unsigned i = 0; i < lane.chunkCount; ++i) lane.chunks[i] = i;
            shuffle(lane);
            break;
        }
        case Effect::Reverse:
            lane.position = lane.length - 1;
            lane.step = 1 + 0.1f * level;
            break;
        case Effect::FilterSweep:
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
        lane.attack = entry && entry->level == lane.level && lane.order == columnOrder[i];
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

void Rack::beginBlock(float tempo, float sampleRate, uint8_t percent, bool record)
{
    consume();
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
            case Effect::FilterSweep: lane.phaseStep = 0.25f / beatFrames; break;
            case Effect::Quantize: lane.phaseStep = 0.5f / beatFrames; break;
            default: break;
        }
    }
    capture = record || laneCount != 0;
}

bool Rack::wet() const { return laneCount && (mix > 0 || mixTarget > 0); }

void Rack::shuffle(Lane& lane)
{
    for (unsigned i = lane.chunkCount - 1; i > 0; --i)
    {
        random = random * 1103515245u + 12345u;
        std::swap(lane.chunks[i], lane.chunks[((random >> 16) & 0x7fff) % (i + 1)]);
    }
}

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
    else if (captures(lane.effect))
    {
        if (lane.effect == Effect::Retrigger)
            lane.position = lane.chunks[lane.chunk] * lane.chunkFrames + lane.chunkOffset;
        l = readHistory(lane, 0);
        r = readHistory(lane, 1);
        switch (lane.effect)
        {
            case Effect::Loop:
            case Effect::TripletLoop: l *= 1.2f; r *= 1.2f; lane.position += 1; break;
            case Effect::Scratch:
                lane.position += lane.step * lane.direction;
                if (++lane.timer >= lane.flipPeriod)
                {
                    lane.timer -= lane.flipPeriod;
                    lane.direction = -lane.direction;
                }
                break;
            case Effect::Retrigger:
                if (++lane.chunkOffset >= lane.chunkFrames)
                {
                    lane.chunkOffset = 0;
                    if (++lane.chunk >= lane.chunkCount)
                    {
                        lane.chunk = 0;
                        if (lane.level >= 2) shuffle(lane);
                    }
                }
                break;
            case Effect::Reverse: lane.position -= lane.step; break;
            default: break;
        }
        wrap(lane.position, lane.length);
    }
    else if (lane.effect == Effect::FilterSweep)
    {
        lane.phase = std::min(1.0f, lane.phase + lane.phaseStep);
        const float open = 1 - lane.phase;
        const float coefficient = blend(lane.depth, 1, open * open * open);
        l = lowpass(l, coefficient, lane.lowpass[0]);
        r = lowpass(r, coefficient, lane.lowpass[1]);
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
    else
    {
        memory.delay[index][lane.write][0] = pcm(l);
        memory.delay[index][lane.write][1] = pcm(r);
        lane.write = (lane.write + 1) & kDelayMask;
        lane.valid = std::min(lane.valid + 1, kDelayFrames);

        if (lane.effect == Effect::Unison)
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
        else if (lane.effect == Effect::Quantize)
        {
            lane.phase += lane.phaseStep;
            wrap(lane.phase, 1);
            const float speed = lane.phase < 2.0f / 3 ? 0.75f : 2.0f;
            lane.position += blend(1, speed, (lane.level + 1) * 0.25f);
            wrap(lane.position, kDelayFrames);
            l = readDelay(lane, index, lane.position, 0);
            r = readDelay(lane, index, lane.position, 1);
        }
        else
        {
            lane.position += lane.step;
            lane.headB += lane.step;
            if (++lane.phase >= kGrain)
            {
                lane.phase = 0;
                lane.position = static_cast<float>(lane.write) - kDelayFrames / 2;
            }
            if (lane.phase == kGrain / 2)
                lane.headB = static_cast<float>(lane.write) - kDelayFrames / 2;
            wrap(lane.position, kDelayFrames);
            wrap(lane.headB, kDelayFrames);
            const float window = 0.5f * (1 - sine(lane.phase / kGrain + 0.25f));
            l = blend(readDelay(lane, index, lane.headB, 0), readDelay(lane, index, lane.position, 0), window);
            r = blend(readDelay(lane, index, lane.headB, 1), readDelay(lane, index, lane.position, 1), window);
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
