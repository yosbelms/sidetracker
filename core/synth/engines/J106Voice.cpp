#if defined(SIDETRACKER_PSYN_J106) || (defined(TRACKER_DESKTOP) && defined(FEATURE_SYNTH))

#ifdef SIDETRACKER_PSYN_J106
#include "psyn/psyn_cpp_bridge.h"
#endif
#include "J106Voice.h"
#include "PolySynth/sdk/SynthVoiceSDK.h"

namespace polysynth
{
namespace
{
enum class Section { Osc, Filter, Env, Lfo, Voice };
enum class Kind { Octave, Switch, Percent, Pwm, Hertz, Octaves, Hpf, Seconds, Vca, Semitones, Chorus, Sync };
enum ParamId
{
#define J106_PARAM(id, ...) id,
#include "J106Params.def"
#undef J106_PARAM
    kParamCount,
};

struct Control
{
    Section section;
    const char* label;
    Kind kind;
    float low;
    float high;
    float initial;
    int midi;
};

constexpr std::array<Control, kParamCount> kControls{{
#define J106_PARAM(id, section, label, kind, low, high, initial, midi) \
    {Section::section, label, Kind::kind, float(low), float(high), float(initial), midi},
#include "J106Params.def"
#undef J106_PARAM
}};

constexpr std::array<const char*, 3> kRanges{"16'", "8'", "4'"};
constexpr std::array<const char*, 2> kSwitch{"Off", "On"};
constexpr std::array<const char*, 2> kPwm{"LFO", "Manual"};
constexpr std::array<const char*, 2> kVca{"Envelope", "Gate"};
constexpr std::array<const char*, 4> kHpf{"Bass Boost", "Flat", "236 Hz", "754 Hz"};
constexpr std::array<const char*, 4> kChorus{"Off", "I", "II", "I+II"};
constexpr std::array<const char*, 7> kSync{"Free", "2 Bars", "1 Bar", "1/2", "1/4", "1/8", "1/16"};
constexpr std::array<float, 7> kBeats{0, 8, 4, 2, 1, 0.5f, 0.25f};

std::span<const char* const> choices(Kind kind)
{
    switch (kind)
    {
        case Kind::Octave: return kRanges;
        case Kind::Switch: return kSwitch;
        case Kind::Pwm: return kPwm;
        case Kind::Hpf: return kHpf;
        case Kind::Vca: return kVca;
        case Kind::Chorus: return kChorus;
        case Kind::Sync: return kSync;
        default: return {};
    }
}

constexpr float kSampleRate = sdk::kSampleRate;
constexpr unsigned kControlPeriod = 16;
constexpr int kMaxVoices = 6;

float wrap(float phase)
{
    return phase >= 1.f ? phase - 1.f : phase;
}

// Smooth a discontinuity over the two samples that straddle it.
float blep(float phase, float step)
{
    if (phase < step)
    {
        const float t = phase / step - 1.f;
        return -t * t;
    }
    if (phase > 1.f - step)
    {
        const float t = (phase - 1.f) / step + 1.f;
        return t * t;
    }
    return 0.f;
}

float pulse(float phase, float step, float width)
{
    const float falling = wrap(phase + 1.f - width);
    const float bipolar = phase < width ? 1.f : -1.f;
    return bipolar + blep(phase, step) - blep(falling, step) - (2.f * width - 1.f);
}

float coefficient(float hz)
{
    const float phase = hz * (0.5f / kSampleRate);
    const float sine = sdk::fastSine(phase);
    return sine / (sine + sdk::fastSine(phase + 0.25f));
}

float timeStep(float seconds)
{
    return 1.f - sdk::fastPow2(-7.65f / (seconds * kSampleRate));
}

// The ladder uses trapezoidal integrators with feedback solved from the stage state.
class LowPass
{
  public:
    float tick(float input, float g, float resonance)
    {
        const float residual = state[3] + g * (state[2] + g * (state[1] + g * state[0]));
        const float g2 = g * g;
        const float feedback = resonance * residual * (1.f - g);
        const float limited = feedback / (1.f + 0.6f * std::abs(feedback));
        float y = (input - limited) / (1.f + resonance * g2 * g2);
        for (auto& s : state)
        {
            const float difference = (y - s) * g;
            y = s + difference;
            s = y + difference;
        }
        return y;
    }

    void start() { state[0] = 0.0001f; }

  private:
    std::array<float, 4> state{};
};

// The first voice owns the stereo effects applied after the voice mix.
class PartEffects
{
  public:
    void reset()
    {
        writeIndex = valid = 0;
        chorusPhase = hpfState = wetLeft = wetRight = mix = 0.f;
    }

    void process(AudioBufferStereoRef output, unsigned hpf, unsigned chorus)
    {
        constexpr std::array<float, 4> kHighPassHz{103, 103, 236, 754};
        const float hp = 2.f * coefficient(kHighPassHz[hpf]);
        const float targetMix = chorus == 0 ? 0.f : 0.46f;
        if (chorus != 0)
            lastMode = chorus;
        constexpr std::array<float, 4> kRate{0.f, 0.51f, 0.86f, 9.75f};
        const float step = kRate[lastMode] / kSampleRate;
        const float depth = lastMode == 3 ? 13.f : 100.f;
        const float stereo = lastMode == 3 ? 1.f : -1.f;

        for (unsigned i = 0; i < output[0].size(); ++i)
        {
            float dry = output[0][i];
            hpfState += hp * (dry - hpfState);
            if (hpf == 0)
                dry += 1.82f * hpfState;
            else if (hpf != 1)
                dry -= hpfState;

            mix += 0.012f * (targetMix - mix);
            if (mix > 0.0001f)
            {
                const float lfo = sdk::fastSine(chorusPhase);
                chorusPhase = wrap(chorusPhase + step);
                wetLeft += 0.32f * (read(363.f + depth * lfo) - wetLeft);
                wetRight += 0.32f * (read(363.f + stereo * depth * lfo) - wetRight);
                output[0][i] = dry + mix * (wetLeft - dry);
                output[1][i] = dry + mix * (wetRight - dry);
            }
            else
            {
                output[0][i] = output[1][i] = dry;
            }

            history[writeIndex] = dry;
            writeIndex = (writeIndex + 1) & kMask;
            valid = std::min(valid + 1, kSize);
        }
    }

  private:
    float read(float delay) const
    {
        const auto frames = static_cast<unsigned>(delay);
        if (frames + 1 > valid)
            return 0.f;
        const float first = history[(writeIndex - frames) & kMask];
        const float second = history[(writeIndex - frames - 1) & kMask];
        return first + (delay - frames) * (second - first);
    }

    static constexpr unsigned kSize = 512;
    static constexpr unsigned kMask = kSize - 1;
    std::array<float, kSize> history;
    unsigned writeIndex = 0;
    unsigned valid = 0;
    unsigned lastMode = 1;
    float chorusPhase = 0.f;
    float hpfState = 0.f;
    float wetLeft = 0.f;
    float wetRight = 0.f;
    float mix = 0.f;
};

class J106Voice final : public InstrumentVoice
{
  public:
    explicit J106Voice(int index)
      : effects(index == 0 ? new (std::nothrow) PartEffects : nullptr),
        noiseState(0xab94de43u + static_cast<uint32_t>(index) * 0x9e3779b9u),
        firstVoice(index == 0)
    {
        for (unsigned i = 0; i < kControls.size(); ++i)
            values[i] = kControls[i].initial;
    }

    bool isValid() const { return !firstVoice || effects != nullptr; }

    void onParamChange(int id, Patch::optional_paramvalue_t value) override
    {
        if (!value || !std::isfinite(*value) || id < 0 || id >= kParamCount)
            return;
        const auto& control = kControls[id];
        const float clamped = std::clamp(*value, control.low, control.high);
        const float next = choices(control.kind).empty() ? clamped : std::round(clamped);
        if (values[id] != next)
        {
            values[id] = next;
            dirty = true;
        }
    }

    void onMidiEvent(const InstrumentVoiceContext&, const MidiEvent& event) override
    {
        using Type = MidiEvent::Type;
        switch (event.type)
        {
            case Type::kNoteOn:
                if (event.data.note.velocity != 0)
                {
                    if (!isActive())
                    {
                        lfoPhase = 0.f;
                        ladder.start();
                    }
                    note = event.data.note.note;
                    velocity = float(event.data.note.velocity) / MidiEvent::Data::Note::kMaxVelocity;
                    stage = Stage::Attack;
                    gate = true;
                    lfoAge = 0.f;
                    bendDirty = true;
                    return;
                }
                [[fallthrough]];
            case Type::kNoteOff:
                if (gate && event.data.note.note == note)
                {
                    gate = false;
                    stage = Stage::Release;
                }
                break;
            case Type::kPitchBendChange:
                bend = std::clamp(event.data.pitchBendChange.value / 8192.f, -1.f, 1.f);
                bendDirty = true;
                break;
            default: break;
        }
    }

    bool isActive() override { return stage != Stage::Idle || vca > 0.0001f; }
    bool handlesNote(note_t candidate) override { return gate && candidate == note.value_or(-1); }
    bool handlesAnyNote() override { return gate; }
    tiny_optional<note_t> getLastNote() override { return note; }
    tiny_optional<float> getCurrentNoteVelocity() override
    {
        return gate ? tiny_optional<float>{velocity} : std::nullopt;
    }

    bool hasPostProcess() const override { return effects != nullptr; }

    void reset() override
    {
        gate = false;
        stage = Stage::Idle;
        note = std::nullopt;
        subPhase = lfoPhase = lfoAge = envelope = vca = bend = 0.f;
        filterGain = filterTarget = 0.f;
        bendDirty = true;
        ladder = {};
        controlFrames = 0;
        if (effects)
            effects->reset();
    }

    void renderVoiceAudio(const InstrumentVoiceContext& ctx,
                          std::optional<AudioBufferStereoRef>, AudioBufferStereoRef output,
                          std::span<AudioBufferRef>) override
    {
        if (dirty)
        {
            attackStep = 1.f / (values[Attack] * kSampleRate);
            decayStep = timeStep(values[Decay]);
            releaseStep = timeStep(values[Release]);
            cutoffOctaves = std::log2(values[Cutoff]);
            controlFrames = 0;
            bendDirty = true;
            dirty = false;
        }
        if (bendDirty)
        {
            const float semitones = note.value_or(60) - 69.f + (values[Range] - 1.f) * 12.f +
                                    bend * values[BendPitch];
            baseStep = 440.f / kSampleRate * sdk::fastPow2(semitones / 12.f);
            controlFrames = 0;
            bendDirty = false;
        }
        const auto sync = static_cast<unsigned>(values[LfoSync]);
        if (std::isfinite(ctx.tempo) && ctx.tempo >= 20.f && ctx.tempo <= 400.f)
            lastTempo = ctx.tempo;
        const float rate = sync ? lastTempo / (60.f * kBeats[sync]) : values[LfoRate];
        const float lfoStep = rate / kSampleRate;
        const float keyOctaves = (note.value_or(60) - 60.f) / 12.f;
        const float resonance = 4.3f * values[Resonance];
        const float level = 0.33f * values[Level] * (0.55f + 0.45f * velocity);

        for (auto& sample : output[0])
        {
            updateEnvelope();
            if (controlFrames == 0)
            {
                const float delay = values[LfoDelay];
                const float depth = delay > 0.f ? std::clamp((lfoAge - delay) * 4.f, 0.f, 1.f) : 1.f;
                const float lfo = depth * sdk::fastSine(lfoPhase);
                step = std::clamp(baseStep * sdk::fastPow2(lfo * values[PitchLfo] / 12.f),
                                  0.00001f, 0.43f);
                const float modulation = values[PwmMode] != 0.f ? values[Pwm] : lfo * values[Pwm];
                width = 0.5f + 0.43f * modulation;
                const float position = cutoffOctaves + envelope * values[FilterEnv] + lfo * values[FilterLfo] +
                                       keyOctaves * values[KeyTrack] +
                                       bend * values[BendFilter];
                filterTarget = coefficient(std::clamp(sdk::fastPow2(position), 20.f, 16000.f));
                controlFrames = kControlPeriod;
            }

            const float phase = wrap(2.f * subPhase);
            float source = 0.f;
            if (values[Saw] != 0.f)
                source += 0.48f * (2.f * phase - 1.f - blep(phase, step));
            if (values[Pulse] != 0.f)
                source += 0.38f * pulse(phase, step, width);
            if (values[Sub] != 0.f)
                source += 0.48f * values[SubLevel] * pulse(subPhase, 0.5f * step, 0.5f);
            if (values[Noise] != 0.f)
            {
                noiseState ^= noiseState << 13;
                noiseState ^= noiseState >> 17;
                noiseState ^= noiseState << 5;
                source += 0.55f * values[Noise] *
                          (static_cast<float>(noiseState >> 8) * (1.f / 8388608.f) - 1.f);
            }
            filterGain += 0.09f * (filterTarget - filterGain);
            const float amp = values[Vca] != 0.f ? float(gate) : envelope;
            vca += 0.025f * (amp - vca);
            sample = isActive() ? ladder.tick(source, filterGain, resonance) * vca * level : 0.f;

            subPhase = wrap(subPhase + 0.5f * step);
            lfoPhase = wrap(lfoPhase + lfoStep);
            lfoAge = std::min(5.f, lfoAge + 1.f / kSampleRate);
            --controlFrames;
        }
        std::ranges::copy(output[0], output[1].begin());
    }

    void renderPostProcessAudio(const InstrumentVoiceContext&, AudioBufferStereoRef output) override
    {
        if (effects)
            effects->process(output, static_cast<unsigned>(values[Hpf]), static_cast<unsigned>(values[Chorus]));
    }

    static void createMetadata(const Patch&, Patch::Metadata& metadata)
    {
        metadata.instrumentTypeId = static_cast<int>(InstrumentTypeId::kJ106Synth);
        metadata.outputChannels = Patch::Metadata::OutputChannels::kStereo;
        metadata.maxVoices = kMaxVoices;
        metadata.supportsIdleSleep = true;
        metadata.layoutFlat.sections = {
            { .name = "Oscillator", .shortName = "Osc" },
            { .name = "Filter", .shortName = "Filter" },
            { .name = "Envelope", .shortName = "Env" },
            { .name = "LFO", .shortName = "LFO" },
            { .name = "Voice", .shortName = "Voice" },
        };
        metadata.params.resize(kParamCount);
        for (unsigned i = 0; i < kControls.size(); ++i)
        {
            const auto& control = kControls[i];
            const auto list = choices(control.kind);
            auto& param = metadata.params[i];
            param.displayName = control.label;
            param.defaultValue = control.initial;
            param.minValue = static_cast<Patch::paramvalue_short_t>(control.low);
            param.maxValue = static_cast<Patch::paramvalue_short_t>(control.high);
            if (control.midi >= 0)
                param.midiCC = static_cast<uint8_t>(control.midi);
            param.isEnum = !list.empty();
            if (param.isEnum)
            {
                param.valueFormatter = [list](const Patch&, float v)
                {
                    const int index = std::clamp<int>(std::lround(v), 0, list.size() - 1);
                    return std::string(list[index]);
                };
            }
            else if (control.kind == Kind::Hertz)
            {
                param.editCurve = sdk::Param::Curve::kExponential;
                param.valueFormatter = sdk::createFrequencyFormatter();
            }
            else if (control.kind == Kind::Seconds)
            {
                param.editCurve = control.low > 0.f ? sdk::Param::Curve::kExponential : sdk::Param::Curve::kPower;
                param.valueFormatter = [](const Patch&, float v)
                {
                    if (v == 0.f) return std::string("Off");
                    if (v < 1.f) return fmt::format("{:.1f} ms", v * 1000.f);
                    return fmt::format("{:.2f} s", v);
                };
            }
            else if (control.kind == Kind::Octaves || control.kind == Kind::Semitones)
            {
                param.valueFormatter = [unit = control.kind == Kind::Octaves ? "oct" : "st"](const Patch&, float v)
                {
                    return fmt::format("{:.2f} {}", v, unit);
                };
            }
            else
            {
                param.valueFormatter = sdk::createPercentFormatter();
            }
            metadata.layoutFlat.sections[static_cast<unsigned>(control.section)].elements.push_back(
              {sdk::EditorElement::Type::kParam, static_cast<int>(i)});
        }
#ifdef TRACKER_DESKTOP
        // The .psyn host appends this parameter on hardware. Keep the same patch layout here.
        metadata.voiceSleepParamId = kParamCount;
        metadata.params.push_back({
          .displayName = "Voice Sleep", .defaultValue = 1.f, .minValue = 0.f, .maxValue = 1.f,
          .isEnum = true,
          .valueFormatter = [](const Patch&, float v) { return std::string(v > 0.5f ? "On" : "Off"); },
        });
        metadata.layoutFlat.sections[static_cast<unsigned>(Section::Voice)].elements.push_back(
          {sdk::EditorElement::Type::kParam, kParamCount});
#endif
        metadata.layout23 = metadata.layoutFlat;
        metadata.initDefaultMacros = [](auto& macros)
        {
            const auto bind = [&macros](int i, const char* name, ParamId id, float amount)
            {
                macros[i].displayName = name;
                macros[i].modifiers = {{id, amount}};
                macros[i].bipolar = true;
            };
#define J106_MACRO(index, label, id, amount) bind(index, label, id, amount);
#include "J106Macros.def"
#undef J106_MACRO
        };
    }

  private:
    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void updateEnvelope()
    {
        switch (stage)
        {
            case Stage::Idle: break;
            case Stage::Attack:
                envelope = std::min(1.f, envelope + attackStep);
                if (envelope >= 1.f) stage = Stage::Decay;
                break;
            case Stage::Decay:
                envelope += decayStep * (values[Sustain] - envelope);
                if (std::abs(envelope - values[Sustain]) < 0.001f)
                    stage = Stage::Sustain;
                break;
            case Stage::Sustain:
                envelope += decayStep * (values[Sustain] - envelope);
                break;
            case Stage::Release:
                envelope -= releaseStep * envelope;
                if (envelope < 0.0001f)
                {
                    envelope = 0.f;
                    stage = Stage::Idle;
                }
                break;
        }
    }

    std::array<float, kParamCount> values{};
    std::unique_ptr<PartEffects> effects;
    LowPass ladder;
    uint32_t noiseState;
    tiny_optional<note_t> note = std::nullopt;
    Stage stage = Stage::Idle;
    const bool firstVoice;
    bool gate = false;
    bool dirty = true;
    bool bendDirty = true;
    unsigned controlFrames = 0;
    float envelope = 0.f;
    float vca = 0.f;
    float velocity = 1.f;
    float bend = 0.f;
    float subPhase = 0.f;
    float lfoPhase = 0.f;
    float lfoAge = 0.f;
    float lastTempo = 120.f;
    float attackStep = 0.f;
    float decayStep = 0.f;
    float releaseStep = 0.f;
    float cutoffOctaves = 0.f;
    float baseStep = 0.01f;
    float step = 0.01f;
    float width = 0.5f;
    float filterGain = 0.f;
    float filterTarget = 0.f;
};
}

#ifdef TRACKER_DESKTOP
std::unique_ptr<InstrumentType> createJ106Synth()
{
    return std::make_unique<InstrumentType>(InstrumentType{
      .name = "J106",
      .createMetadata = &J106Voice::createMetadata,
      .createVoice = [](int index) { return std::make_unique<J106Voice>(index); },
    });
}
#endif
}

#ifdef SIDETRACKER_PSYN_J106
PSYN_REGISTER_CPP_VOICE(polysynth::J106Voice)
#endif
#endif
