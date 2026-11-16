#include "AnalogSynthVoice.h"
#include "J106Voice.h"
#include "PolySynth/MidiEvent.h"
#include "PolySynth/sdk/InstrumentVoice.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
using namespace polysynth;

class TestPatch final : public Patch
{
  public:
    explicit TestPatch(const InstrumentType& type) : type(type)
    {
        type.createMetadata(*this, metadata);
        for (const auto& param : metadata.params) values.push_back(param.defaultValue);
        metadata.initDefaultMacros(metadata.macros);
    }

    int id(std::string_view name) const
    {
        const auto it = std::find_if(metadata.params.begin(), metadata.params.end(),
            [name](const Metadata::Param& p) { return p.displayName == name; });
        return it == metadata.params.end() ? -1 : static_cast<int>(it - metadata.params.begin());
    }

    optional_paramvalue_t getParamValue(int i) const override
    {
        return i >= 0 && static_cast<unsigned>(i) < values.size() ? values[i] : std::nullopt;
    }
    optional_paramvalue_t getParamValueEditMapped(int i) const override { return getParamValue(i); }
    optional_paramvalue_t getParamFinalValue(int i) const override { return getParamValue(i); }
    void setParamValue(int i, optional_paramvalue_t v, ParamChangeSource) override
    {
        values.at(i) = v;
        if (metadata.params.at(i).invalidatesOtherMetadata) type.createMetadata(*this, metadata);
    }
    void setParamValueEditMapped(int i, optional_paramvalue_t v, ParamChangeSource source) override
    {
        setParamValue(i, v, source);
    }
    void subscribeParamUpdates(std::weak_ptr<ParamChangeListener>) override {}
    void unsubscribeParamUpdates(const ParamChangeListener&) override {}
    void startBatchUpdate() override {}
    void finishBatchUpdate() override {}
    optional_paramvalue_t getMacroValue(int) const override { return std::nullopt; }
    void setMacroValue(int, optional_paramvalue_t, tiny_optional<int>) override {}
    void setMacroModifier(int, int, tiny_optional<int>) override {}
    void setMacroModifierAmount(int, int, float) override {}
    tiny_optional<float> getMacroModifierAmount(int, int) override { return std::nullopt; }
    void clearMacroModifiers() override {}
    void changeMacroModifierAmount(int, int, float) override {}
    void setMacroBipolar(int, bool) override {}
    void setMacroDisplayName(int, std::string) override {}
    const Sample* getSample(int) const override { return nullptr; }
    LoadStatus loadSample(int, const std::string&) override { return {}; }
    const Metadata& getMetadata() const override { return metadata; }
    const Sequencer& getSequencer() const override { return sequencer; }
    void setSequence(const Sequencer::Sequence&, int) override {}
    void setSequencerConfig(const Sequencer::Config&) override {}

  private:
    const InstrumentType& type;
    Metadata metadata;
    Sequencer sequencer;
    std::vector<optional_paramvalue_t> values;
};

using Block = std::array<float, kAudioBufferSize>;

void note(InstrumentVoice& voice, unsigned key, bool on = true)
{
    MidiEvent event{};
    event.type = on ? MidiEvent::Type::kNoteOn : MidiEvent::Type::kNoteOff;
    event.data.note = {static_cast<uint8_t>(key), 100};
    voice.onMidiEvent({120.0f}, event);
}

Block block(InstrumentVoice& voice)
{
    Block pcm{};
    Block right{};
    voice.renderVoiceAudio({120.0f}, std::nullopt, {pcm, right}, {});
    return pcm;
}

void assign(InstrumentVoice& voice, const TestPatch& patch)
{
    for (unsigned i = 0; i < patch.getMetadata().params.size(); ++i)
        voice.onParamChange(i, patch.getParamValue(i));
}
}

TEST(AnalogSynthSDK, RegistersVoiceLimitsMacroTargetsAndAutomatableParameters)
{
    const auto mini = createMiniSynth();
    const auto ober = createOberSynth();
    EXPECT_EQ(mini->name, "MINI");
    TestPatch bass(*mini);
    TestPatch pad(*ober);
    EXPECT_EQ(bass.getMetadata().instrumentTypeId, 9);
    EXPECT_EQ(bass.getMetadata().maxVoices, 1);
    EXPECT_EQ(pad.getMetadata().maxVoices, 4);
    for (const auto* patch : {&bass, &pad})
    {
        EXPECT_EQ(patch->getMetadata().macros[0].modifiers.front().paramId.value(), patch->id("Cutoff"));
        EXPECT_EQ(patch->getMetadata().macros[1].modifiers.front().paramId.value(), patch->id("Resonance"));
    }
    pad.setParamValue(pad.id("Play Mode"), 1.0f, Patch::ParamChangeSource::kKnob);
    EXPECT_EQ(pad.getMetadata().maxVoices, 1);
    auto first = ober->createVoice(0);
    auto second = ober->createVoice(0);
    assign(*first, pad);
    assign(*second, pad);
    note(*first, 60);
    note(*second, 60);
    EXPECT_EQ(block(*first), block(*second));
    first->onParamChange(pad.id("Cutoff"), 25.0f);
    EXPECT_NE(block(*first), block(*second));
    note(*first, 67);
    note(*first, 67, false);
    EXPECT_TRUE(first->handlesNote(60));
    EXPECT_TRUE(second->handlesNote(60));
    EXPECT_EQ(second->getLastNote().value(), 60);
}

TEST(AnalogSynthSDK, ListsAndLoadsBuiltInPatches)
{
    const auto check = [](const InstrumentType& instrument, InstrumentTypeId type,
                          std::string_view name, float cutoff) {
        std::vector<std::string_view> names{name}; // A saved patch with the same name takes precedence.
        appendAnalogFactoryPatches(type, names);
        ASSERT_EQ(names.size(), 3u);
        EXPECT_EQ(std::ranges::count(names, name), 1);

        TestPatch patch(instrument);
        ASSERT_TRUE(applyAnalogFactoryPatch(type, name, patch));
        EXPECT_FLOAT_EQ(patch.getParamValue(patch.id("Cutoff")).value(), cutoff);
        EXPECT_FALSE(applyAnalogFactoryPatch(type, "Missing Patch", patch));
    };
    check(*createMiniSynth(), InstrumentTypeId::kMiniSynth, "Ladder Bass", 270.0f);
    check(*createOberSynth(), InstrumentTypeId::kOberSynth, "Pulse Glass", 1420.0f);
}

TEST(J106SynthSDK, ChorusWidensTheVoiceAndDryOutputRemainsCentered)
{
    const auto j106 = createJ106Synth();
    TestPatch wide(*j106);
    EXPECT_EQ(j106->name, "J106");
    EXPECT_EQ(wide.getMetadata().instrumentTypeId, static_cast<int>(InstrumentTypeId::kJ106Synth));
    EXPECT_EQ(wide.getMetadata().maxVoices, 6);
    EXPECT_EQ(wide.getMetadata().outputChannels, Patch::Metadata::OutputChannels::kStereo);
    ASSERT_GE(wide.id("Chorus"), 0);

    TestPatch dry(*j106);
    dry.setParamValue(dry.id("Chorus"), 0.f, Patch::ParamChangeSource::kPatchAssign);
    auto chorus = j106->createVoice(0);
    auto center = j106->createVoice(0);
    assign(*chorus, wide);
    assign(*center, dry);
    note(*chorus, 60);
    note(*center, 60);

    float wideDifference = 0.f;
    float dryPeak = 0.f;
    for (unsigned i = 0; i < 48; ++i)
    {
        Block left{}, right{};
        AudioBufferStereoRef output{left, right};
        chorus->renderVoiceAudio({120.f}, std::nullopt, output, {});
        chorus->renderPostProcessAudio({120.f}, output);
        for (size_t frame = 0; frame < left.size(); ++frame)
            wideDifference += std::abs(left[frame] - right[frame]);

        center->renderVoiceAudio({120.f}, std::nullopt, output, {});
        center->renderPostProcessAudio({120.f}, output);
        ASSERT_EQ(left, right);
        for (auto value : left)
            dryPeak = std::max(dryPeak, std::abs(value));
    }
    EXPECT_GT(wideDifference, 0.1f);
    EXPECT_GT(dryPeak, 0.02f);
}

TEST(J106SynthSDK, FilterStaysBoundedAndVoicesReleaseIndependently)
{
    const auto j106 = createJ106Synth();
    TestPatch patch(*j106);
    patch.setParamValue(patch.id("Resonance"), 1.f, Patch::ParamChangeSource::kPatchAssign);
    patch.setParamValue(patch.id("Release"), 0.02f, Patch::ParamChangeSource::kPatchAssign);
    auto first = j106->createVoice(0);
    auto second = j106->createVoice(1);
    assign(*first, patch);
    assign(*second, patch);
    note(*first, 60);
    note(*second, 67);
    float peak = 0.f;
    for (int i = 0; i < 192; ++i)
    {
        first->onParamChange(patch.id("Cutoff"), 20.f + (i % 16) * 1000.f);
        for (float sample : block(*first))
        {
            ASSERT_TRUE(std::isfinite(sample));
            peak = std::max(peak, std::abs(sample));
        }
    }
    EXPECT_GT(peak, 0.02f);
    EXPECT_LT(peak, 1.f);
    note(*first, 60, false);
    for (int i = 0; i < 40; ++i)
        block(*first);
    EXPECT_FALSE(first->isActive());
    EXPECT_TRUE(second->handlesNote(67));
    EXPECT_TRUE(second->isActive());
}
