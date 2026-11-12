#include "AnalogSynthVoice.h"
#include "PolySynth/MidiEvent.h"
#include "PolySynth/sdk/InstrumentVoice.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>

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
