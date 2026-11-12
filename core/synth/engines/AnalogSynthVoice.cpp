#include "config.h"
#ifdef FEATURE_SYNTH

#include "AnalogSynthVoice.h"
#include "AnalogVoice.h"
#include "PolySynth/sdk/SynthVoiceSDK.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <span>

namespace polysynth
{
namespace
{
using namespace analogsynth;
using CC = sdk::MidiCCs;

enum class Section { Oscillator, Filter, Amplifier, Envelope, LFO, Voice };
enum class Kind { Percent, Hertz, Time, Semitones, Octaves, Wave, Range, Switch, Filter, LfoWave, Sync, Mode, Target };
enum class Param : uint8_t
{
    Wave1, Range1, Tune1, Level1, Wave2, Range2, Tune2, Level2, Wave3, Range3, Tune3, Level3,
    Noise, Pink, Pulse, PWM, HardSync, CrossMod, Filter, Cutoff, Resonance, KeyTrack, Drive, Morph,
    AmpAttack, AmpDecay, AmpSustain, AmpRelease, ContourAmount, FilterAttack, FilterDecay,
    FilterSustain, FilterRelease, LfoWave, Rate, Sync, LfoPitch, LfoFilter, Retrigger, Glide,
    Mode, Level, Pulse2, Osc2Contour, LfoAmp, PitchTarget, PwmTarget,
};

struct Descriptor
{
    Param param;
    Section section;
    const char* name;
    Kind kind;
    float minimum;
    float maximum;
    float initial;
    int midi = -1;
};

using P = Param;
using S = Section;
using K = Kind;

// The order of each table is its patch parameter ID order. Append new entries.
constexpr Descriptor kMiniParams[] = {
    {P::Wave1, S::Oscillator, "Osc 1 Wave", K::Wave, 0, 7, 1, 20},
    {P::Range1, S::Oscillator, "Osc 1 Range", K::Range, 0, 5, 3, 21},
    {P::Tune1, S::Oscillator, "Osc 1 Tune", K::Semitones, -12, 12, 0, 22},
    {P::Level1, S::Oscillator, "Osc 1 Level", K::Percent, 0, 1, 0.75f, 23},
    {P::Wave2, S::Oscillator, "Osc 2 Wave", K::Wave, 0, 7, 2, 24},
    {P::Range2, S::Oscillator, "Osc 2 Range", K::Range, 0, 5, 3, 25},
    {P::Tune2, S::Oscillator, "Osc 2 Tune", K::Semitones, -12, 12, 0.07f, 26},
    {P::Level2, S::Oscillator, "Osc 2 Level", K::Percent, 0, 1, 0.30f, 27},
    {P::Wave3, S::Oscillator, "Osc 3 Wave", K::Wave, 0, 7, 3, 28},
    {P::Range3, S::Oscillator, "Osc 3 Range", K::Range, 0, 5, 2, 29},
    {P::Tune3, S::Oscillator, "Osc 3 Tune", K::Semitones, -12, 12, 0, 30},
    {P::Level3, S::Oscillator, "Osc 3 Level", K::Percent, 0, 1, 0.22f, 31},
    {P::Noise, S::Oscillator, "Noise Level", K::Percent, 0, 1, 0},
    {P::Pink, S::Oscillator, "Noise Color", K::Switch, 0, 1, 0},
    {P::Pulse, S::Oscillator, "Pulse Width", K::Percent, 0.08f, 0.92f, 0.50f},
    {P::Cutoff, S::Filter, "Cutoff", K::Hertz, 25, 16000, 1150, *CC::kFilterCutoff},
    {P::Resonance, S::Filter, "Resonance", K::Percent, 0, 1, 0.18f, *CC::kFilterResonance},
    {P::KeyTrack, S::Filter, "Key Tracking", K::Percent, 0, 1, 0.33f},
    {P::Drive, S::Filter, "Mixer Drive", K::Percent, 0, 1, 0.18f},
    {P::AmpAttack, S::Amplifier, "Attack", K::Time, 0.0005f, 8, 0.008f, *CC::kAmpEnvAttack},
    {P::AmpDecay, S::Amplifier, "Decay", K::Time, 0.001f, 8, 0.22f, *CC::kAmpEnvDecay},
    {P::AmpSustain, S::Amplifier, "Sustain", K::Percent, 0, 1, 0.65f, *CC::kAmpEnvSustain},
    {P::AmpRelease, S::Amplifier, "Release", K::Time, 0.001f, 8, 0.20f, *CC::kAmpEnvRelease},
    {P::ContourAmount, S::Envelope, "Filter Amount", K::Octaves, 0, 5, 2, *CC::kFilterEnvAmount},
    {P::FilterAttack, S::Envelope, "Filter Attack", K::Time, 0.0005f, 8, 0.008f, *CC::kFilterEnvAttack},
    {P::FilterDecay, S::Envelope, "Filter Decay", K::Time, 0.001f, 8, 0.22f, *CC::kFilterEnvDecay},
    {P::FilterSustain, S::Envelope, "Filter Sustain", K::Percent, 0, 1, 0.65f, *CC::kFilterEnvSustain},
    {P::LfoWave, S::LFO, "Shape", K::LfoWave, 0, 6, 0},
    {P::Rate, S::LFO, "Rate", K::Hertz, 0.05f, 40, 4, *CC::kLfo1Frequency},
    {P::Sync, S::LFO, "Sync", K::Sync, 0, 11, 0},
    {P::LfoPitch, S::LFO, "Pitch Depth", K::Semitones, 0, 12, 0},
    {P::LfoFilter, S::LFO, "Filter Depth", K::Octaves, 0, 3, 0, *CC::kFilterLfoAmount},
    {P::Retrigger, S::LFO, "Restart on Note", K::Switch, 0, 1, 0},
    {P::Glide, S::Voice, "Glide", K::Time, 0, 2, 0},
    {P::Level, S::Voice, "Level", K::Percent, 0, 1, 0.70f},
    {P::LfoAmp, S::LFO, "Amp Depth", K::Percent, 0, 1, 0},
};

constexpr Descriptor kOberParams[] = {
    {P::Wave1, S::Oscillator, "Osc 1 Wave", K::Wave, 0, 3, 1, 20},
    {P::Range1, S::Oscillator, "Osc 1 Range", K::Range, 0, 5, 3, 21},
    {P::Tune1, S::Oscillator, "Osc 1 Tune", K::Semitones, -12, 12, 0, 22},
    {P::Level1, S::Oscillator, "Osc 1 Level", K::Percent, 0, 1, 0.75f, 23},
    {P::Wave2, S::Oscillator, "Osc 2 Wave", K::Wave, 0, 3, 2, 24},
    {P::Range2, S::Oscillator, "Osc 2 Range", K::Range, 0, 5, 3, 25},
    {P::Tune2, S::Oscillator, "Osc 2 Tune", K::Semitones, -24, 24, 0.07f, 26},
    {P::Level2, S::Oscillator, "Osc 2 Level", K::Percent, 0, 1, 0.30f, 27},
    {P::Noise, S::Oscillator, "Noise Level", K::Percent, 0, 1, 0},
    {P::Pulse, S::Oscillator, "Pulse Width", K::Percent, 0.08f, 0.92f, 0.50f, 28},
    {P::PWM, S::Oscillator, "PWM Depth", K::Percent, 0, 1, 0, 29},
    {P::HardSync, S::Oscillator, "Hard Sync", K::Switch, 0, 1, 0, 30},
    {P::CrossMod, S::Oscillator, "Cross Mod", K::Percent, 0, 1, 0, 31},
    {P::Filter, S::Filter, "Type", K::Filter, 0, 5, 0, *CC::kFilterType},
    {P::Cutoff, S::Filter, "Cutoff", K::Hertz, 25, 16000, 1800, *CC::kFilterCutoff},
    {P::Resonance, S::Filter, "Resonance", K::Percent, 0, 1, 0.18f, *CC::kFilterResonance},
    {P::KeyTrack, S::Filter, "Key Tracking", K::Percent, 0, 1, 0.33f},
    {P::Drive, S::Filter, "Drive", K::Percent, 0, 1, 0.18f},
    {P::Morph, S::Filter, "SEM Morph", K::Percent, 0, 1, 0.50f},
    {P::AmpAttack, S::Amplifier, "Attack", K::Time, 0.0005f, 8, 0.008f, *CC::kAmpEnvAttack},
    {P::AmpDecay, S::Amplifier, "Decay", K::Time, 0.001f, 8, 0.22f, *CC::kAmpEnvDecay},
    {P::AmpSustain, S::Amplifier, "Sustain", K::Percent, 0, 1, 0.65f, *CC::kAmpEnvSustain},
    {P::AmpRelease, S::Amplifier, "Release", K::Time, 0.001f, 8, 0.20f, *CC::kAmpEnvRelease},
    {P::ContourAmount, S::Envelope, "Filter Amount", K::Octaves, -5, 5, 2, *CC::kFilterEnvAmount},
    {P::FilterAttack, S::Envelope, "Filter Attack", K::Time, 0.0005f, 8, 0.008f, *CC::kFilterEnvAttack},
    {P::FilterDecay, S::Envelope, "Filter Decay", K::Time, 0.001f, 8, 0.22f, *CC::kFilterEnvDecay},
    {P::FilterSustain, S::Envelope, "Filter Sustain", K::Percent, 0, 1, 0.65f, *CC::kFilterEnvSustain},
    {P::FilterRelease, S::Envelope, "Filter Release", K::Time, 0.001f, 8, 0.20f, *CC::kFilterEnvRelease},
    {P::LfoWave, S::LFO, "Shape", K::LfoWave, 0, 6, 0},
    {P::Rate, S::LFO, "Rate", K::Hertz, 0.05f, 30, 4, *CC::kLfo1Frequency},
    {P::Sync, S::LFO, "Sync", K::Sync, 0, 11, 0},
    {P::LfoPitch, S::LFO, "Pitch Depth", K::Semitones, 0, 12, 0},
    {P::LfoFilter, S::LFO, "Filter Depth", K::Octaves, 0, 5, 0, *CC::kFilterLfoAmount},
    {P::Retrigger, S::LFO, "Restart on Note", K::Switch, 0, 1, 0},
    {P::Glide, S::Voice, "Glide", K::Time, 0, 2, 0},
    {P::Mode, S::Voice, "Play Mode", K::Mode, 0, 1, 0},
    {P::Level, S::Voice, "Level", K::Percent, 0, 1, 0.70f},
    {P::Pulse2, S::Oscillator, "Osc 2 Pulse Width", K::Percent, 0.08f, 0.92f, 0.50f},
    {P::Osc2Contour, S::Envelope, "Filter Env to Osc 2", K::Semitones, -24, 24, 0},
    {P::LfoAmp, S::LFO, "Amp Depth", K::Percent, 0, 1, 0},
    {P::PitchTarget, S::LFO, "Pitch Target", K::Target, 0, 2, 2},
    {P::PwmTarget, S::LFO, "PWM Target", K::Target, 0, 2, 2},
};

std::span<const Descriptor> descriptors(Model model)
{
    if (model == Model::Mini) return kMiniParams;
    return kOberParams;
}

int paramId(Model model, Param param)
{
    const auto table = descriptors(model);
    const auto it = std::find_if(table.begin(), table.end(), [param](const Descriptor& d) { return d.param == param; });
    return it == table.end() ? -1 : static_cast<int>(it - table.begin());
}

void updateSettings(Settings& settings, Model model, Param param, float value)
{
    const auto id = static_cast<unsigned>(param);
    if (id <= static_cast<unsigned>(P::Level3))
    {
        auto& osc = settings.oscillators[id / 4];
        switch (id % 4)
        {
            case 0:
            {
                constexpr Wave mini[] = {Wave::Off, Wave::Saw, Wave::Pulse, Wave::Triangle,
                                         Wave::Sine, Wave::Shark, Wave::WidePulse, Wave::NarrowPulse};
                constexpr Wave ober[] = {Wave::Off, Wave::Saw, Wave::Pulse, Wave::SawPulse};
                const auto choice = static_cast<unsigned>(value);
                osc.wave = model == Model::Mini ? mini[choice] : ober[choice];
                break;
            }
            case 1: osc.range = value == 0.0f ? -4.0f : value - 3.0f; break;
            case 2: osc.detune = value; break;
            case 3: osc.level = value; break;
        }
        return;
    }
    switch (param)
    {
        case P::Noise: settings.noise = value; break;
        case P::Pink: settings.pink = value != 0; break;
        case P::Pulse: settings.pulseWidth = value; break;
        case P::Pulse2: settings.pulseWidth2 = value; break;
        case P::PWM: settings.pwm = value; break;
        case P::HardSync: settings.hardSync = value != 0; break;
        case P::CrossMod: settings.crossMod = value; break;
        case P::Filter: settings.filter = static_cast<Filter>(static_cast<int>(value)); break;
        case P::Cutoff: settings.cutoff = value; break;
        case P::Resonance: settings.resonance = value; break;
        case P::KeyTrack: settings.keyTrack = value; break;
        case P::Drive: settings.drive = value; break;
        case P::Morph: settings.morph = value; break;
        case P::AmpAttack: settings.amp.attack = value; break;
        case P::AmpDecay: settings.amp.decay = value; break;
        case P::AmpSustain: settings.amp.sustain = value; break;
        case P::AmpRelease: settings.amp.release = value; break;
        case P::ContourAmount: settings.contourAmount = value; break;
        case P::FilterAttack: settings.contour.attack = value; break;
        case P::FilterDecay:
            settings.contour.decay = value;
            if (model == Model::Mini) settings.contour.release = value;
            break;
        case P::FilterSustain: settings.contour.sustain = value; break;
        case P::FilterRelease: settings.contour.release = value; break;
        case P::LfoWave: settings.lfoWave = static_cast<LfoWave>(static_cast<int>(value)); break;
        case P::Rate: settings.lfoRate = value; break;
        case P::Sync: settings.lfoSync = static_cast<LfoSync>(static_cast<int>(value)); break;
        case P::LfoPitch: settings.lfoPitch = value; break;
        case P::LfoFilter: settings.lfoFilter = value; break;
        case P::LfoAmp: settings.lfoAmp = value; break;
        case P::Osc2Contour: settings.osc2Contour = value; break;
        case P::PitchTarget: settings.pitchTarget = static_cast<unsigned>(value); break;
        case P::PwmTarget: settings.pwmTarget = static_cast<unsigned>(value); break;
        case P::Retrigger: settings.lfoRetrigger = value != 0; break;
        case P::Glide: settings.glide = value; break;
        case P::Mode: settings.legato = value != 0; break;
        case P::Level: settings.level = value; break;
        default: break;
    }
}

std::span<const char* const> choices(const Descriptor& d, Model model)
{
    static constexpr const char* miniWaves[] = {"Off", "Saw", "Pulse", "Triangle", "Sine", "Shark", "Wide Pulse", "Narrow Pulse"};
    static constexpr const char* oberWaves[] = {"Off", "Saw", "Pulse", "Saw + Pulse"};
    static constexpr const char* ranges[] = {"LO", "32'", "16'", "8'", "4'", "2'"};
    static constexpr const char* offOn[] = {"Off", "On"};
    static constexpr const char* colors[] = {"White", "Pink"};
    static constexpr const char* filters[] = {"LP 12", "HP 12", "BP 12", "Notch", "SEM", "LP 24"};
    static constexpr const char* lfo[] = {"Triangle", "Saw", "Square", "Sine", "Ramp", "Sample & Hold", "Smooth Random"};
    static constexpr const char* sync[] = {
        "Free", "1/1", "1/2", "1/4", "1/8", "1/16", "1/8T", "1/8D", "2 Bars", "4 Bars", "1/16T", "1/32"
    };
    static constexpr const char* modes[] = {"Poly", "Mono Legato"};
    static constexpr const char* targets[] = {"Osc 1", "Osc 2", "Both"};
    switch (d.kind)
    {
        case K::Wave:
            if (model == Model::Mini) return miniWaves;
            return oberWaves;
        case K::Range: return ranges;
        case K::Switch: return d.param == P::Pink ? colors : offOn;
        case K::Filter: return filters;
        case K::LfoWave: return lfo;
        case K::Sync: return sync;
        case K::Mode: return modes;
        case K::Target: return targets;
        default: return {};
    }
}

class AnalogSynthVoice : public InstrumentVoice
{
  public:
    AnalogSynthVoice(Model model, unsigned voiceIndex)
        : model(model), voice(model, voiceIndex), settings(defaults(model))
    {
        for (const auto& descriptor : descriptors(model))
            updateSettings(settings, model, descriptor.param, descriptor.initial);
    }

    void onParamChange(int id, Patch::optional_paramvalue_t value) override
    {
        const auto table = descriptors(model);
        if (!value || id < 0 || static_cast<unsigned>(id) >= table.size() || !std::isfinite(*value))
            return;
        const auto& descriptor = table[id];
        float clamped = std::clamp(*value, descriptor.minimum, descriptor.maximum);
        if (!choices(descriptor, model).empty()) clamped = std::round(clamped);
        updateSettings(settings, model, descriptor.param, clamped);
        settingsDirty = true;
    }

    void onMidiEvent(const InstrumentVoiceContext&, const MidiEvent& event) override
    {
        switch (event.type)
        {
            case MidiEvent::Type::kNoteOn:
                updateVoice();
                if (event.data.note.velocity)
                    voice.noteOn(event.data.note.note, event.data.note.velocity / 127.0f);
                else
                    voice.noteOff(event.data.note.note);
                break;
            case MidiEvent::Type::kNoteOff:
                voice.noteOff(event.data.note.note);
                break;
            case MidiEvent::Type::kPitchBendChange:
                voice.setPitchBend(event.data.pitchBendChange.value * (2.0f / 8192.0f));
                break;
            default: break;
        }
    }

    void renderVoiceAudio(const InstrumentVoiceContext& ctx, std::optional<AudioBufferStereoRef>,
                          AudioBufferStereoRef output, std::span<AudioBufferRef>) override
    {
        updateVoice();
        voice.render(output[0], ctx.tempo);
    }

    bool isActive() override { return voice.active(); }
    bool handlesNote(note_t note) override { return voice.handlesNote(note); }
    bool handlesAnyNote() override { return voice.currentNote() >= 0; }
    tiny_optional<note_t> getLastNote() override
    {
        return voice.lastNote() >= 0 ? tiny_optional<note_t>{static_cast<note_t>(voice.lastNote())} : std::nullopt;
    }
    tiny_optional<float> getCurrentNoteVelocity() override
    {
        return handlesAnyNote() ? tiny_optional<float>{voice.velocity()} : std::nullopt;
    }
    void clearPreviousNotes() override { voice.clearPreviousNotes(); }

  private:
    void updateVoice()
    {
        if (!settingsDirty) return;
        voice.setSettings(settings);
        settingsDirty = false;
    }

    Model model;
    Voice voice;
    Settings settings;
    bool settingsDirty = true;
};

template<Model model>
void createMetadata(const Patch& patch, Patch::Metadata& metadata)
{
    const auto table = descriptors(model);
    metadata.instrumentTypeId = static_cast<int>(model == Model::Mini
        ? InstrumentTypeId::kMiniSynth : InstrumentTypeId::kOberSynth);
    metadata.outputChannels = Patch::Metadata::OutputChannels::kMono;
    const auto mode = model == Model::Ober ? patch.getParamValue(paramId(model, P::Mode)).value_or(0.0f) : 0.0f;
    metadata.maxVoices = model == Model::Mini || mode >= 0.5f ? 1 : 4;
    metadata.layoutFlat.sections = {
        { .name = "Oscillator", .shortName = "Osc" },
        { .name = "Filter", .shortName = "Filter" },
        { .name = "Amplifier", .shortName = "Amp" },
        { .name = "Filter Envelope", .shortName = "Env" },
        { .name = "LFO", .shortName = "LFO" },
        { .name = "Voice", .shortName = "Voice" },
    };
    metadata.params.resize(table.size());
    for (unsigned i = 0; i < table.size(); ++i)
    {
        const auto& descriptor = table[i];
        const auto list = choices(descriptor, model);
        auto& param = metadata.params[i];
        param.displayName = descriptor.name;
        param.defaultValue = descriptor.initial;
        param.minValue = static_cast<Patch::paramvalue_short_t>(descriptor.minimum);
        param.maxValue = static_cast<Patch::paramvalue_short_t>(descriptor.maximum);
        param.isEnum = !list.empty();
        param.invalidatesOtherMetadata = descriptor.param == P::Mode;
        if (descriptor.midi >= 0) param.midiCC = static_cast<uint8_t>(descriptor.midi);
        if (descriptor.kind == K::Hertz)
        {
            param.editCurve = sdk::Param::Curve::kExponential;
            param.valueFormatter = sdk::createFrequencyFormatter();
        }
        else if (descriptor.kind == K::Time)
        {
            param.editCurve = descriptor.minimum > 0 ? sdk::Param::Curve::kExponential : sdk::Param::Curve::kPower;
            param.valueFormatter = [](const Patch&, float value) {
                if (value == 0.0f) return std::string("Off");
                if (value < 1.0f) return fmt::format("{:.1f} ms", value * 1000.0f);
                return fmt::format("{:.2f} s", value);
            };
        }
        else if (!list.empty())
        {
            param.valueFormatter = [list](const Patch&, float value) {
                return std::string(list[std::clamp<int>(std::lround(value), 0, list.size() - 1)]);
            };
        }
        else if (descriptor.kind == K::Semitones || descriptor.kind == K::Octaves)
        {
            const char* unit = descriptor.kind == K::Octaves ? " oct" : " st";
            param.valueFormatter = [unit](const Patch&, float value) { return fmt::format("{:+.2f}{}", value, unit); };
        }
        else
        {
            param.valueFormatter = sdk::createPercentFormatter();
        }
        auto& section = metadata.layoutFlat.sections[static_cast<unsigned>(descriptor.section)];
        section.elements.push_back({sdk::EditorElement::Type::kParam, static_cast<int>(i)});
    }
    metadata.layout23 = metadata.layoutFlat;
    metadata.initDefaultMacros = [](auto& macros) {
        const auto map = [&](unsigned i, const char* name, Param param, float amount, bool bipolar) {
            macros[i].displayName = name;
            macros[i].modifiers = {{paramId(model, param), amount}};
            macros[i].bipolar = bipolar;
        };
        map(0, "Cutoff", P::Cutoff, 0.65f, true);
        map(1, "Resonance", P::Resonance, 0.65f, true);
        map(2, "Drive", P::Drive, 0.60f, true);
        map(3, "Filter Env", P::ContourAmount, 0.55f, true);
        map(4, model == Model::Mini ? "Osc 3 Mix" : "PWM", model == Model::Mini ? P::Level3 : P::PWM, 0.65f, true);
        map(5, "Release", P::AmpRelease, 0.15f, true);
    };
}

struct PresetValue { Param param; float value; };
struct Preset { std::string_view name; std::span<const PresetValue> values; };

constexpr PresetValue kLadderBass[] = {
    {P::Cutoff, 270}, {P::Resonance, 0.43f}, {P::Drive, 0.53f}, {P::Level3, 0.60f},
    {P::ContourAmount, 3.2f}, {P::FilterDecay, 0.16f}, {P::FilterSustain, 0.12f}, {P::AmpRelease, 0.075f},
};
constexpr PresetValue kCopperLead[] = {
    {P::Cutoff, 1250}, {P::Resonance, 0.55f}, {P::Tune2, 0.13f}, {P::Range3, 3}, {P::Glide, 0.10f},
    {P::LfoPitch, 0.10f}, {P::Drive, 0.33f}, {P::AmpSustain, 0.85f}, {P::FilterSustain, 0.32f},
};
constexpr PresetValue kHollowPulse[] = {
    {P::Wave1, 2}, {P::Pulse, 0.24f}, {P::Range2, 2}, {P::Level2, 0.60f}, {P::Wave3, 0},
    {P::Cutoff, 650}, {P::Resonance, 0.37f}, {P::Noise, 0.14f}, {P::Pink, 1},
    {P::Sync, 5}, {P::LfoWave, 2}, {P::LfoFilter, 0.66f}, {P::ContourAmount, 0.65f},
};
constexpr PresetValue kPulseGlass[] = {
    {P::Wave1, 2}, {P::PWM, 0.52f}, {P::Rate, 0.46f}, {P::Tune2, -0.12f}, {P::Level2, 0.49f},
    {P::Cutoff, 1420}, {P::ContourAmount, 0.8f}, {P::AmpAttack, 0.42f}, {P::AmpRelease, 1.35f},
    {P::AmpSustain, 0.86f}, {P::FilterAttack, 0.72f},
};
constexpr PresetValue kSyncWire[] = {
    {P::Wave2, 1}, {P::Level1, 0.33f}, {P::Level2, 0.81f}, {P::Tune2, 11.0f},
    {P::HardSync, 1}, {P::CrossMod, 0.12f}, {P::Cutoff, 2800}, {P::ContourAmount, 1.5f},
    {P::FilterDecay, 0.29f}, {P::FilterSustain, 0.19f}, {P::Glide, 0.055f},
};
constexpr PresetValue kCopperNotch[] = {
    {P::Filter, 4}, {P::Morph, 0.44f}, {P::Cutoff, 540}, {P::Resonance, 0.57f}, {P::Wave2, 3},
    {P::LfoFilter, 1.22f}, {P::Sync, 3}, {P::Noise, 0.07f}, {P::Drive, 0.38f}, {P::AmpRelease, 0.45f},
};

constexpr Preset kMiniPresets[] = {
    {"Ladder Bass", kLadderBass}, {"Copper Lead", kCopperLead}, {"Hollow Pulse", kHollowPulse},
};
constexpr Preset kOberPresets[] = {
    {"Pulse Glass", kPulseGlass}, {"Sync Wire", kSyncWire}, {"Copper Notch", kCopperNotch},
};

std::span<const Preset> factoryPatches(InstrumentTypeId type)
{
    if (type == InstrumentTypeId::kMiniSynth) return kMiniPresets;
    if (type == InstrumentTypeId::kOberSynth) return kOberPresets;
    return {};
}

}

std::unique_ptr<InstrumentType> createMiniSynth()
{
    return std::make_unique<InstrumentType>(InstrumentType{
        .name = "MINI",
        .createMetadata = &createMetadata<Model::Mini>,
        .createVoice = [](int index) { return std::make_unique<AnalogSynthVoice>(Model::Mini, index); },
    });
}

std::unique_ptr<InstrumentType> createOberSynth()
{
    return std::make_unique<InstrumentType>(InstrumentType{
        .name = "OBER",
        .createMetadata = &createMetadata<Model::Ober>,
        .createVoice = [](int index) { return std::make_unique<AnalogSynthVoice>(Model::Ober, index); },
    });
}

void appendAnalogFactoryPatches(InstrumentTypeId type, std::vector<std::string_view>& names)
{
    for (const auto& preset : factoryPatches(type))
        if (std::find(names.begin(), names.end(), preset.name) == names.end())
            names.push_back(preset.name);
}

bool applyAnalogFactoryPatch(InstrumentTypeId type, std::string_view name, Patch& patch)
{
    const auto presets = factoryPatches(type);
    const auto preset = std::find_if(presets.begin(), presets.end(), [name](const Preset& p) { return p.name == name; });
    if (preset == presets.end()) return false;

    const auto model = type == InstrumentTypeId::kMiniSynth ? Model::Mini : Model::Ober;
    for (auto [param, value] : preset->values)
    {
        const int id = paramId(model, param);
        assert(id >= 0);
        patch.setParamValue(id, value, Patch::ParamChangeSource::kPatchAssign);
    }
    return true;
}
}

#endif
