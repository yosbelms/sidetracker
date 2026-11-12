#include "AnalogVoice.h"

#include <algorithm>
#include <cmath>

namespace analogsynth
{
namespace
{
constexpr float kPi = 3.14159265f;
constexpr float kMaximumIncrement = 0.45f;

float saturate(float input)
{
    const float x = std::clamp(input, -3.0f, 3.0f);
    const float square = x * x;
    return x * (27.0f + square) / (27.0f + 9.0f * square);
}

float sine(float phase)
{
    float x = phase * 4.0f;
    if (x > 3.0f) x -= 4.0f;
    else if (x > 1.0f) x = 2.0f - x;
    const float angle = x * (kPi / 2.0f);
    const float square = angle * angle;
    return angle * (1.0f + square * (-1.0f / 6.0f + square * (1.0f / 120.0f - square / 5040.0f)));
}

float waveValue(Wave wave, float phase, float width)
{
    const float saw = 2.0f * phase - 1.0f;
    const float pulse = phase < width ? 1.0f : -1.0f;
    switch (wave)
    {
        case Wave::Saw: return saw;
        case Wave::Pulse: return pulse;
        case Wave::SawPulse: return (saw + pulse) * 0.5f;
        case Wave::Triangle: return 1.0f - 4.0f * std::abs(phase - 0.5f);
        case Wave::Sine: return sine(phase);
        case Wave::Shark: return 0.5f * (saw + 1.0f - 4.0f * std::abs(phase - 0.5f));
        default: return 0.0f;
    }
}

float triangleCorrection(float phase, float increment)
{
    const float distance = std::min(phase, 1.0f - phase);
    if (distance >= increment) return 0.0f;
    const float remaining = 1.0f - distance / increment;
    return (4.0f / 3.0f) * increment * remaining * remaining * remaining;
}

float lfoValue(LfoWave wave, float phase)
{
    switch (wave)
    {
        case LfoWave::Saw: return 2.0f * phase - 1.0f;
        case LfoWave::Ramp: return 1.0f - 2.0f * phase;
        case LfoWave::Square: return phase < 0.5f ? 1.0f : -1.0f;
        case LfoWave::Sine: return sine(phase);
        default: return 1.0f - 4.0f * std::abs(phase - 0.5f);
    }
}

float bipolarNoise(uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return float(state >> 8) * (1.0f / 8388608.0f) - 1.0f;
}

float filterCoefficient(float hz)
{
    return std::tan(kPi * std::clamp(hz, 20.0f, 17000.0f) / kSampleRate);
}
}

Settings defaults(Model model)
{
    Settings settings;
    settings.oscillators[0] = {Wave::Saw, 0.0f, 0.0f, 0.75f};
    settings.oscillators[1] = {Wave::Pulse, 0.0f, 0.07f, 0.30f};
    if (model == Model::Mini)
    {
        settings.oscillators[2] = {Wave::Triangle, -1.0f, 0.0f, 0.22f};
        settings.cutoff = 1150.0f;
        settings.contour.release = settings.contour.decay;
    }
    else
    {
        settings.filter = Filter::Low12;
    }
    return settings;
}

Voice::Voice(Model model, unsigned voiceIndex)
    : model(model), params(defaults(model)), noiseState(0x85ebca6bu + 0x9e3779b9u * voiceIndex),
      lfoRandomState(noiseState ^ 0xb5297a4du)
{
    setSettings(params);
    lfoPrevious = bipolarNoise(lfoRandomState);
    lfoNext = bipolarNoise(lfoRandomState);
}

float Voice::Oscillator::tick(Wave wave, float increment, float width, float resetAt)
{
    if (wave == Wave::WidePulse || wave == Wave::NarrowPulse)
    {
        width = wave == Wave::WidePulse ? 0.65f : 0.25f;
        wave = Wave::Pulse;
    }
    const bool saw = wave == Wave::Saw || wave == Wave::SawPulse || wave == Wave::Shark;
    const bool pulse = wave == Wave::Pulse || wave == Wave::SawPulse;
    const float gain = wave == Wave::SawPulse || wave == Wave::Shark ? 0.5f : 1.0f;
    float output = pending;
    float correction = 0.0f;
    wrapAt = -1.0f;
    // Distribute a discontinuity over the samples on either side of the edge.
    const auto edge = [&](float time, float jump) {
        const float before = 1.0f - time;
        output += 0.5f * jump * before * before;
        correction -= 0.5f * jump * time * time;
    };
    const auto advance = [&](float start, float end) {
        const float next = phase + (end - start) * increment;
        if (next >= 1.0f)
        {
            wrapAt = start + (1.0f - phase) / increment;
            if (pulse && phase < width) edge(start + (width - phase) / increment, -2.0f * gain);
            if (saw || pulse) edge(wrapAt, 2.0f * (float(pulse) - float(saw)) * gain);
            phase = next - 1.0f;
            if (pulse && phase >= width) edge(wrapAt + width / increment, -2.0f * gain);
        }
        else
        {
            if (pulse && phase < width && next >= width)
                edge(start + (width - phase) / increment, -2.0f * gain);
            phase = next;
        }
    };
    if (resetAt >= 0.0f)
    {
        advance(0.0f, resetAt);
        if (saw || pulse) edge(resetAt, waveValue(wave, 0.0f, width) - waveValue(wave, phase, width));
        phase = 0.0f;
        advance(resetAt, 1.0f);
    }
    else
    {
        advance(0.0f, 1.0f);
    }
    pending = waveValue(wave, phase, width) + correction;
    if (wave == Wave::Triangle || wave == Wave::Shark)
    {
        const float opposite = phase < 0.5f ? phase + 0.5f : phase - 0.5f;
        pending += gain * (triangleCorrection(phase, increment) - triangleCorrection(opposite, increment));
    }
    previous = output;
    return output;
}

void Voice::Envelope::configure(const EnvelopeSettings& settings)
{
    sustain = settings.sustain;
    const float seconds[] = {settings.attack, settings.decay, settings.release};
    for (unsigned i = 0; i < speed.size(); ++i)
    {
        speed[i] = 1.0f / std::max(1.0f, seconds[i] * kSampleRate);
        coefficient[i] = -std::expm1(-5.0f * speed[i]);
    }
}

void Voice::Envelope::trigger()
{
    stage = Stage::Attack;
    progress = 0.0f;
}

void Voice::Envelope::release()
{
    if (!active()) return;
    stage = Stage::Release;
    progress = 0.0f;
}

float Voice::Envelope::tick()
{
    unsigned index;
    float target;
    Stage next;
    switch (stage)
    {
        case Stage::Attack: index = 0; target = 1.0f; next = Stage::Decay; break;
        case Stage::Decay: index = 1; target = sustain; next = Stage::Sustain; break;
        case Stage::Release: index = 2; target = 0.0f; next = Stage::Idle; break;
        case Stage::Sustain:
            level += (sustain - level) * 0.004f;
            return level;
        default: return 0.0f;
    }
    level += (target - level) * coefficient[index];
    progress += speed[index];
    if (progress >= 1.0f)
    {
        level = target;
        stage = next;
        progress = 0.0f;
    }
    return level;
}

void Voice::setSettings(const Settings& settings)
{
    if (params.amp != settings.amp || amp.speed[0] == 0.0f)
        amp.configure(settings.amp);
    if (params.contour != settings.contour || contour.speed[0] == 0.0f)
        contour.configure(settings.contour);
    if (params.glide != settings.glide)
    {
        const float frames = settings.glide * kSampleRate;
        if (frames < 1.0f) pitch = targetPitch;
        glideStep = frames >= 1.0f ? std::abs(targetPitch - pitch) / frames : 0.0f;
    }
    for (unsigned i = 0; i < oscillatorRatios.size(); ++i)
        if (oscillatorRatios[i] == 0.0f || settings.oscillators[i].detune != params.oscillators[i].detune ||
            settings.oscillators[i].range != params.oscillators[i].range)
            oscillatorRatios[i] = std::exp2(settings.oscillators[i].range + settings.oscillators[i].detune / 12.0f);
    if (cutoffTarget == 0.0f || settings.cutoff != params.cutoff)
        cutoffTarget = std::log2(settings.cutoff);
    params = settings;
}

void Voice::setPitchBend(float semitones)
{
    bend = semitones;
}

int Voice::selectedIndex() const
{
    if (!heldCount) return -1;
    if (model == Model::Ober) return static_cast<int>(heldCount) - 1;
    return static_cast<int>(std::min_element(held.begin(), held.begin() + heldCount,
      [](const Note& a, const Note& b) { return a.pitch < b.pitch; }) - held.begin());
}

int Voice::currentNote() const
{
    const int selected = selectedIndex();
    return selected >= 0 ? held[selected].pitch : -1;
}

float Voice::velocity() const
{
    const int selected = selectedIndex();
    return selected >= 0 ? held[selected].velocity : 0.0f;
}

bool Voice::handlesNote(int note) const
{
    return std::any_of(held.begin(), held.begin() + heldCount,
                       [note](const Note& key) { return key.pitch == note; });
}

bool Voice::active() const { return amp.active(); }

void Voice::clearPreviousNotes()
{
    const int selected = selectedIndex();
    if (selected >= 0)
    {
        held[0] = held[selected];
        heldCount = 1;
    }
}

void Voice::noteOn(int note, float velocity)
{
    const int previous = currentNote();
    if (model == Model::Ober && !params.legato) heldCount = 0;
    const auto end = held.begin() + heldCount;
    const auto duplicate = std::find_if(held.begin(), end, [note](const Note& key) { return key.pitch == note; });
    if (duplicate != end || heldCount == held.size())
    {
        std::move(duplicate != end ? duplicate + 1 : held.begin() + 1,
                  end, duplicate != end ? duplicate : held.begin());
        --heldCount;
    }
    held[heldCount++] = {note, velocity};
    const Note& selected = held[selectedIndex()];
    if (selected.pitch == note || selected.pitch != previous) trigger(selected, previous >= 0);
}

void Voice::noteOff(int note)
{
    const int previous = currentNote();
    const auto end = held.begin() + heldCount;
    const auto key = std::find_if(held.begin(), end, [note](const Note& n) { return n.pitch == note; });
    if (key == end) return;
    std::move(key + 1, end, key);
    --heldCount;
    if (!heldCount)
    {
        amp.release();
        contour.release();
    }
    else if (note == previous)
    {
        trigger(held[selectedIndex()], true);
    }
}

void Voice::trigger(const Note& note, bool overlap)
{
    const bool wasActive = active();
    const bool legato = model == Model::Ober && params.legato && overlap;
    targetPitch = static_cast<float>(note.pitch);
    const float glideFrames = params.glide * kSampleRate;
    if (!wasActive || glideFrames < 1.0f)
        pitch = targetPitch;
    glideStep = glideFrames >= 1.0f ? std::abs(targetPitch - pitch) / glideFrames : 0.0f;
    previousNote = note.pitch;
    noteVelocity = std::clamp(note.velocity, 0.0f, 1.0f);
    if (!wasActive)
    {
        svf = {};
        ladder = {};
        feedbackLag = 0.0f;
        smoothedCutoff = cutoffTarget;
        smoothedResonance = params.resonance;
    }
    if (!legato || !wasActive)
    {
        amp.trigger();
        contour.trigger();
        if (params.lfoRetrigger) lfoPhase = 0.25f;
        if (model == Model::Mini) ladder[0] += 0.00012f;
    }
    framesUntilUpdate = 0;
}

void Voice::updateControls()
{
    constexpr float beats[] = {
        0.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 1.0f / 3.0f, 0.75f,
        8.0f, 16.0f, 1.0f / 6.0f, 0.125f
    };
    const auto division = static_cast<unsigned>(params.lfoSync);
    lfoIncrement = (division ? lastTempo / (60.0f * beats[division]) : params.lfoRate) / kSampleRate;
    float lfo;
    if (params.lfoWave == LfoWave::SampleHold) lfo = lfoNext;
    else if (params.lfoWave == LfoWave::SmoothRandom) lfo = lfoPrevious + (lfoNext - lfoPrevious) * lfoPhase;
    else lfo = lfoValue(params.lfoWave, lfoPhase);
    const float note = pitch + bend;
    const float frequency = (440.0f / kSampleRate) * std::exp2((note - 69.0f) / 12.0f);
    const unsigned count = model == Model::Mini ? 3 : 2;
    for (unsigned i = 0; i < count; ++i)
    {
        const float pitchLfo = params.pitchTarget == 2 || params.pitchTarget == i ? lfo * params.lfoPitch : 0.0f;
        const float contourPitch = i == 1 ? contour.level * params.osc2Contour : 0.0f;
        const float modulation = pitchLfo + contourPitch;
        const float ratio = modulation != 0.0f ? std::exp2(modulation / 12.0f) : 1.0f;
        increments[i] = std::min(frequency * oscillatorRatios[i] * ratio, kMaximumIncrement);
    }
    const float modulation = 0.4f * params.pwm * lfo;
    width[0] = std::clamp(params.pulseWidth + (params.pwmTarget != 1 ? modulation : 0.0f), 0.06f, 0.94f);
    width[1] = std::clamp(params.pulseWidth2 + (params.pwmTarget != 0 ? modulation : 0.0f), 0.06f, 0.94f);
    tremolo = 1.0f - params.lfoAmp * (0.5f - 0.5f * lfo);
    const float smoothing = model == Model::Mini ? 0.114f : 0.059f;
    smoothedCutoff += (cutoffTarget - smoothedCutoff) * smoothing;
    smoothedResonance += (params.resonance - smoothedResonance) * smoothing;
    const float cutoff = std::exp2(smoothedCutoff + (pitch - 60.0f) * (params.keyTrack / 12.0f) +
                                   contour.level * params.contourAmount + lfo * params.lfoFilter);
    if (model == Model::Mini || params.filter == Filter::Low24)
    {
        const bool mini = model == Model::Mini;
        const float pole = filterCoefficient(cutoff * (mini ? 1.33f : 1.0f));
        filterG = pole / (1.0f + pole);
        if (mini)
        {
            const float lagPole = filterCoefficient(cutoff * 1.05f);
            lagG = lagPole / (1.0f + lagPole);
        }
        feedback = smoothedResonance * (mini ? 4.1f : 4.12f);
        const float g2 = filterG * filterG;
        inverseLoopGain = 1.0f / (1.0f + feedback * g2 * g2 * (mini ? lagG : 1.0f));
    }
    else
    {
        svfG = filterCoefficient(cutoff);
        svfDamping = 2.0f - 1.92f * smoothedResonance;
        svfDenominator = 1.0f / (1.0f + svfG * (svfG + svfDamping));
    }
    framesUntilUpdate = model == Model::Mini ? 32 : 16;
}

float Voice::filterSample(float input)
{
    if (model == Model::Mini || params.filter == Filter::Low24)
    {
        const bool mini = model == Model::Mini;
        float predicted = 0.0f;
        for (float state : ladder) predicted = predicted * filterG + state;
        predicted *= 1.0f - filterG;
        const float loop = mini ? feedbackLag + lagG * (predicted - feedbackLag) : predicted;
        float output = (input - feedback * saturate(loop)) * inverseLoopGain;
        for (float& state : ladder)
        {
            const float delta = filterG * (output - state);
            output = state + delta;
            state = std::clamp(output + delta, -3.0f, 3.0f);
        }
        if (mini) feedbackLag += 2.0f * lagG * (output - feedbackLag);
        return output;
    }

    const float band = (svf[0] + svfG * (input - svf[1])) * svfDenominator;
    const float low = svf[1] + svfG * band;
    const float high = input - low - svfDamping * band;
    svf[0] = std::clamp(2.0f * band - svf[0], -4.0f, 4.0f);
    svf[1] = std::clamp(2.0f * low - svf[1], -4.0f, 4.0f);
    switch (params.filter)
    {
        case Filter::High12: return high;
        case Filter::Band12: return band;
        case Filter::Notch: return low + high;
        case Filter::SEM:
            return params.morph < 0.5f ? low + 2.0f * params.morph * high
                                       : high + 2.0f * (1.0f - params.morph) * low;
        default: return low;
    }
}

float Voice::noiseSample()
{
    const float white = bipolarNoise(noiseState);
    if (!params.pink) return white;
    // Sum three one-pole bands with evenly spaced corners on a log scale.
    pinkState[0] += 0.004f * (white - pinkState[0]);
    pinkState[1] += 0.036f * (white - pinkState[1]);
    pinkState[2] += 0.265f * (white - pinkState[2]);
    return pinkState[0] + 0.4f * pinkState[1] + 0.16f * pinkState[2];
}

void Voice::render(std::span<float> output, float tempo)
{
    if (tempo > 0.0f && tempo <= 800.0f) lastTempo = tempo;
    if (!active())
    {
        std::fill(output.begin(), output.end(), 0.0f);
        if (!params.lfoRetrigger)
        {
            lfoPhase += lfoIncrement * output.size();
            lfoPhase -= std::floor(lfoPhase);
        }
        return;
    }
    const bool mini = model == Model::Mini;
    const float mixGain = 0.8f + params.drive * 2.5f;
    const float outputGain = params.level * (0.24f + 0.76f * noteVelocity) * (mini ? 0.79f : 0.60f);
    for (float& sample : output)
    {
        if (glideStep > 0.0f)
        {
            const float difference = targetPitch - pitch;
            if (std::abs(difference) <= glideStep)
            {
                pitch = targetPitch;
                glideStep = 0.0f;
            }
            else
                pitch += std::copysign(glideStep, difference);
        }
        if (!framesUntilUpdate) updateControls();
        --framesUntilUpdate;

        const float mod = mini ? 0.0f : params.crossMod * oscillators[1].previous;
        const float increment = std::clamp(increments[0] * (1.0f + mod), 0.00001f, kMaximumIncrement);
        float signal = oscillators[0].tick(params.oscillators[0].wave, increment, width[0]) * params.oscillators[0].level;
        const float reset = !mini && params.hardSync ? oscillators[0].wrapAt : -1.0f;
        signal += oscillators[1].tick(params.oscillators[1].wave, increments[1], mini ? width[0] : width[1], reset) *
                  params.oscillators[1].level;
        if (mini)
            signal += oscillators[2].tick(params.oscillators[2].wave, increments[2], width[0]) * params.oscillators[2].level;
        if (params.noise > 0.0f) signal += noiseSample() * params.noise;
        contour.tick();
        sample = saturate(filterSample(saturate(signal * mixGain))) * amp.tick() * outputGain * tremolo;
        lfoPhase += lfoIncrement;
        if (lfoPhase >= 1.0f)
        {
            lfoPhase -= 1.0f;
            lfoPrevious = lfoNext;
            lfoNext = bipolarNoise(lfoRandomState);
        }
    }
}
}
