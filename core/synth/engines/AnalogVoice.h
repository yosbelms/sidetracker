#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace analogsynth
{
inline constexpr float kSampleRate = 44100.0f;

enum class Model { Mini, Ober };
enum class Wave : uint8_t { Off, Saw, Pulse, Triangle, SawPulse, Sine, Shark, WidePulse, NarrowPulse };
enum class Filter : uint8_t { Low12, High12, Band12, Notch, SEM, Low24 };
enum class LfoWave : uint8_t { Triangle, Saw, Square, Sine, Ramp, SampleHold, SmoothRandom };
enum class LfoSync : uint8_t
{
    Free, Bar, Half, Quarter, Eighth, Sixteenth, EighthTriplet, EighthDotted,
    TwoBars, FourBars, SixteenthTriplet, ThirtySecond
};

struct OscillatorSettings
{
    Wave wave = Wave::Off;
    float range = 0.0f;    // octave offset
    float detune = 0.0f;   // semitones
    float level = 0.0f;
};

struct EnvelopeSettings
{
    float attack = 0.008f;
    float decay = 0.22f;
    float sustain = 0.65f;
    float release = 0.20f;

    bool operator==(const EnvelopeSettings&) const = default;
};

struct Settings
{
    std::array<OscillatorSettings, 3> oscillators{};
    EnvelopeSettings amp{};
    EnvelopeSettings contour{};
    Filter filter = Filter::Low24;
    LfoWave lfoWave = LfoWave::Triangle;
    LfoSync lfoSync = LfoSync::Free;
    float noise = 0.0f;
    float drive = 0.18f;
    float cutoff = 1800.0f;
    float resonance = 0.18f;
    float keyTrack = 0.33f;
    float contourAmount = 2.0f; // octaves
    float morph = 0.5f;
    float pulseWidth = 0.5f;
    float pulseWidth2 = 0.5f;
    float pwm = 0.0f;
    float crossMod = 0.0f;
    float lfoRate = 4.0f;
    float lfoPitch = 0.0f; // semitones
    float lfoFilter = 0.0f; // octaves
    float lfoAmp = 0.0f;
    float osc2Contour = 0.0f; // semitones
    unsigned pitchTarget = 2; // oscillator 1, oscillator 2, both
    unsigned pwmTarget = 2;
    float glide = 0.0f; // seconds to reach the next note
    float level = 0.70f;
    bool pink = false;
    bool hardSync = false;
    bool legato = false;
    bool lfoRetrigger = false;
};

Settings defaults(Model model);

class Voice
{
  public:
    explicit Voice(Model model, unsigned voiceIndex = 0);

    void setSettings(const Settings& settings);
    void setPitchBend(float semitones);
    void noteOn(int note, float velocity);
    void noteOff(int note);
    void clearPreviousNotes();
    void render(std::span<float> output, float tempo);

    bool active() const;
    bool handlesNote(int note) const;
    int currentNote() const;
    int lastNote() const { return previousNote; }
    float velocity() const;

  private:
    struct Oscillator
    {
        float phase = 0.0f;
        float pending = 0.0f;
        float previous = 0.0f;
        float wrapAt = -1.0f;
        float tick(Wave wave, float increment, float width, float resetAt = -1.0f);
    };

    struct Envelope
    {
        enum class Stage : uint8_t { Idle, Attack, Decay, Sustain, Release };
        void configure(const EnvelopeSettings& settings);
        void trigger();
        void release();
        float tick();
        bool active() const { return stage != Stage::Idle; }

        Stage stage = Stage::Idle;
        float level = 0.0f;
        float progress = 0.0f;
        float sustain = 0.0f;
        std::array<float, 3> speed{};
        std::array<float, 3> coefficient{};
    };

    struct Note { int pitch; float velocity; };
    int selectedIndex() const;
    void trigger(const Note& note, bool overlap);
    void updateControls();
    float filterSample(float input);
    float noiseSample();

    Model model;
    Settings params;
    std::array<Oscillator, 3> oscillators{};
    Envelope amp;
    Envelope contour;
    std::array<Note, 16> held{};
    unsigned heldCount = 0;
    int previousNote = -1;
    uint32_t noiseState;
    uint32_t lfoRandomState;
    std::array<float, 3> pinkState{};
    std::array<float, 4> ladder{};
    std::array<float, 2> svf{};
    std::array<float, 3> increments{};
    std::array<float, 3> oscillatorRatios{};
    float feedbackLag = 0.0f;
    float lfoPhase = 0.0f;
    float lfoIncrement = 0.0f;
    float lfoPrevious = 0.0f;
    float lfoNext = 0.0f;
    float tremolo = 1.0f;
    float pitch = 0.0f;
    float targetPitch = 0.0f;
    float glideStep = 0.0f;
    float bend = 0.0f;
    float noteVelocity = 1.0f;
    float cutoffTarget = 0.0f;
    float smoothedCutoff = 0.0f;
    float smoothedResonance = 0.0f;
    std::array<float, 2> width = {0.5f, 0.5f};
    float filterG = 0.0f;
    float lagG = 0.0f;
    float feedback = 0.0f;
    float inverseLoopGain = 1.0f;
    float svfG = 0.0f;
    float svfDamping = 2.0f;
    float svfDenominator = 1.0f;
    float lastTempo = 120.0f;
    unsigned framesUntilUpdate = 0;
};
}
