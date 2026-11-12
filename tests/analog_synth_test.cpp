#include "AnalogVoice.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <vector>

namespace
{
using namespace analogsynth;

std::vector<float> render(Voice& voice, unsigned frames, float tempo = 120.0f)
{
    std::vector<float> output(frames);
    for (unsigned i = 0; i < frames; i += 128)
        voice.render(std::span(output).subspan(i, std::min(128u, frames - i)), tempo);
    return output;
}

Settings singleOscillator(Model model)
{
    Settings settings = defaults(model);
    settings.oscillators = {{{Wave::Saw, 0, 0, 0.5f}, {}, {}}};
    settings.contourAmount = 0.0f;
    settings.amp = {0.001f, 0.01f, 1.0f, 0.05f};
    settings.cutoff = 14000.0f;
    settings.resonance = 0.0f;
    settings.keyTrack = 0.0f;
    settings.drive = 0.0f;
    return settings;
}

float rms(std::span<const float> pcm)
{
    return std::sqrt(std::inner_product(pcm.begin(), pcm.end(), pcm.begin(), 0.0f) / pcm.size());
}

float frequency(std::span<const float> pcm)
{
    unsigned crossings = 0;
    for (unsigned i = 1; i < pcm.size(); ++i)
        if (pcm[i - 1] < 0.0f && pcm[i] >= 0.0f) ++crossings;
    return crossings * kSampleRate / pcm.size();
}

float magnitude(std::span<const float> pcm, float hz)
{
    const float step = 6.2831853f * hz / kSampleRate;
    const float coefficient = 2 * std::cos(step);
    float previous = 0.0f;
    float before = 0.0f;
    for (float value : pcm)
    {
        const float next = value + coefficient * previous - before;
        before = previous;
        previous = next;
    }
    return std::sqrt(previous * previous + before * before - coefficient * previous * before) / pcm.size();
}
}

TEST(AnalogSynth, HoldsTuningAndReleaseTimeAt128Frames)
{
    for (auto model : {Model::Mini, Model::Ober})
    {
        Voice voice(model);
        voice.setSettings(singleOscillator(model));
        voice.noteOn(69, 1.0f);
        const auto sustained = render(voice, 12000);
        EXPECT_NEAR(frequency(std::span(sustained).subspan(1000)), 440.0f, 4.1f);
        EXPECT_GT(rms(std::span(sustained).subspan(1000)), 0.04f);
        voice.noteOff(69);
        render(voice, 2000);
        EXPECT_TRUE(voice.active());
        const auto released = render(voice, 256);
        EXPECT_FALSE(voice.active());
        EXPECT_EQ(released.back(), 0.0f);
    }
}

TEST(AnalogSynth, MiniFallsBackToLowestHeldNote)
{
    Voice voice(Model::Mini);
    voice.noteOn(57, 0.75f);
    voice.noteOn(72, 0.25f);
    EXPECT_EQ(voice.currentNote(), 57);
    EXPECT_EQ(voice.velocity(), 0.75f);
    EXPECT_TRUE(voice.handlesNote(72));
    voice.noteOn(45, 1.0f);
    EXPECT_EQ(voice.currentNote(), 45);
    voice.noteOff(45);
    EXPECT_EQ(voice.currentNote(), 57);
    voice.noteOff(57);
    EXPECT_EQ(voice.currentNote(), 72);
    voice.noteOff(72);
    EXPECT_EQ(voice.currentNote(), -1);
    EXPECT_EQ(voice.lastNote(), 72);
}

TEST(AnalogSynth, OberLegatoFallsBackAndPolyStealReleasesOnlyNewNote)
{
    auto settings = singleOscillator(Model::Ober);
    settings.legato = true;
    settings.amp.sustain = 0.2f;
    Voice legato(Model::Ober);
    legato.setSettings(settings);
    legato.noteOn(60, 1);
    const float sustained = rms(render(legato, 4096));
    legato.noteOn(67, 1);
    EXPECT_EQ(legato.currentNote(), 67);
    EXPECT_LT(rms(render(legato, 384)), sustained);
    legato.noteOff(67);
    EXPECT_EQ(legato.currentNote(), 60);

    Voice poly(Model::Ober);
    poly.noteOn(60, 1);
    poly.noteOn(67, 1);
    EXPECT_FALSE(poly.handlesNote(60));
    poly.noteOff(60);
    EXPECT_EQ(poly.currentNote(), 67);
    poly.noteOff(67);
    EXPECT_EQ(poly.currentNote(), -1);
    EXPECT_TRUE(poly.active());
}

TEST(AnalogSynth, MiniLadderSingsWithoutOscillators)
{
    Voice voice(Model::Mini);
    auto settings = singleOscillator(Model::Mini);
    settings.oscillators = {};
    settings.resonance = 1.0f;
    settings.cutoff = 500.0f;
    voice.setSettings(settings);
    voice.noteOn(60, 1);
    const auto pcm = render(voice, 22050);
    const auto settled = std::span(pcm).subspan(11025);
    EXPECT_GT(rms(settled), 0.015f);
    EXPECT_GT(frequency(settled), 300.0f);
    EXPECT_LT(frequency(settled), 1000.0f);
    EXPECT_TRUE(std::all_of(pcm.begin(), pcm.end(), [](float v) { return std::isfinite(v) && std::abs(v) < 1; }));
}

TEST(AnalogSynth, HardSyncUsesMasterPhaseAndSawSuppressesFoldedHarmonic)
{
    auto settings = singleOscillator(Model::Ober);
    settings.oscillators[0].level = 0.0f;
    settings.oscillators[1] = {Wave::Saw, 0, 8, 0.5f};
    Voice free(Model::Ober);
    free.setSettings(settings);
    free.noteOn(69, 1);
    const auto freePcm = render(free, 16384);
    Voice sync(Model::Ober);
    settings.hardSync = true;
    sync.setSettings(settings);
    sync.noteOn(69, 1);
    const auto syncPcm = render(sync, 16384);
    EXPECT_GT(magnitude(std::span(syncPcm).subspan(4096), 440.0f),
              8 * magnitude(std::span(freePcm).subspan(4096), 440.0f));

    Voice high(Model::Ober);
    high.setSettings(singleOscillator(Model::Ober));
    high.noteOn(125, 1);
    const auto highPcm = render(high, 16384);
    const auto steady = std::span(highPcm).subspan(4096);
    const float fundamental = 440.0f * std::exp2((125.0f - 69.0f) / 12.0f);
    EXPECT_LT(magnitude(steady, kSampleRate - 3 * fundamental), 0.17f * magnitude(steady, fundamental));
}

TEST(AnalogSynth, SemEndpointsAndNotchAtCutoff)
{
    auto settings = singleOscillator(Model::Ober);
    settings.cutoff = 440.0f;
    settings.resonance = 0.3f;
    settings.oscillators[0].wave = Wave::Sine;
    const auto run = [&](Filter filter, float morph) {
        Voice voice(Model::Ober);
        settings.filter = filter;
        settings.morph = morph;
        voice.setSettings(settings);
        voice.noteOn(69, 1);
        return render(voice, 8192);
    };
    EXPECT_EQ(run(Filter::SEM, 0.0f), run(Filter::Low12, 0.0f));
    EXPECT_EQ(run(Filter::SEM, 1.0f), run(Filter::High12, 0.0f));
    const auto notch = run(Filter::SEM, 0.5f);
    const auto low = run(Filter::Low12, 0.0f);
    EXPECT_LT(magnitude(std::span(notch).subspan(4096), 440.0f),
              0.1f * magnitude(std::span(low).subspan(4096), 440.0f));
}

TEST(AnalogSynth, SyncRetainsLastBpmAndVoiceStateIsIndependent)
{
    auto settings = singleOscillator(Model::Ober);
    settings.lfoSync = LfoSync::Sixteenth;
    settings.lfoPitch = 0.5f;
    Voice reference(Model::Ober), droppedClock(Model::Ober), other(Model::Ober, 1);
    for (auto* voice : {&reference, &droppedClock, &other})
    {
        voice->setSettings(settings);
        voice->noteOn(69, 1);
    }
    EXPECT_EQ(render(reference, 512, 92), render(droppedClock, 512, 92));
    other.setPitchBend(2.0f);
    const auto otherPcm = render(other, 4096, 138);
    const auto referencePcm = render(reference, 4096, 92);
    EXPECT_EQ(referencePcm, render(droppedClock, 4096, 0));
    EXPECT_NE(referencePcm, otherPcm);
}

TEST(AnalogSynth, GlideFinishesAtTheTargetPitch)
{
    Voice voice(Model::Mini);
    auto settings = singleOscillator(Model::Mini);
    settings.oscillators[0].wave = Wave::Sine;
    settings.glide = 0.10f;
    voice.setSettings(settings);
    voice.noteOn(69, 1);
    render(voice, 8192);
    voice.noteOn(57, 1);
    const auto gliding = render(voice, 17000);
    EXPECT_GT(frequency(std::span(gliding).first(2048)), 325.0f);
    EXPECT_NEAR(frequency(std::span(gliding).subspan(5975)), 220.0f, 4.1f);
}

TEST(AnalogSynth, FiltersStayBoundedDuringExtremeAutomation)
{
    for (auto model : {Model::Mini, Model::Ober})
    {
        Voice voice(model);
        auto settings = defaults(model);
        settings.drive = 1.0f;
        settings.resonance = 1.0f;
        settings.hardSync = true;
        settings.crossMod = 0.9f;
        settings.noise = 0.4f;
        settings.oscillators[1].detune = 24.0f;
        voice.setSettings(settings);
        voice.noteOn(102, 1);
        for (auto filter : {Filter::Low12, Filter::High12, Filter::Band12, Filter::Notch, Filter::SEM, Filter::Low24})
        {
            settings.filter = filter;
            for (float cutoff : {25.0f, 16000.0f, 100.0f, 1700.0f})
            {
                settings.cutoff = cutoff;
                voice.setSettings(settings);
                const auto pcm = render(voice, 1024);
                EXPECT_TRUE(std::all_of(pcm.begin(), pcm.end(), [](float v) { return std::isfinite(v) && std::abs(v) < 1; }));
            }
        }
    }
}
