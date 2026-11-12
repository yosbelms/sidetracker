#pragma once

#include "PolySynth/PatchStore.h"
#include "PolySynth/sdk/InstrumentTypeRegistry.h"

namespace polysynth
{
class DesktopPatch final : public Patch
{
  public:
    explicit DesktopPatch(std::shared_ptr<const InstrumentType> type);
    const InstrumentType& getInstrumentType() const { return *type; }
    uint64_t revision() const { return version; }
    optional_paramvalue_t voiceValue(int id, float velocity, float pressure) const;

    optional_paramvalue_t getParamValue(int id) const override;
    optional_paramvalue_t getParamValueEditMapped(int id) const override;
    void setParamValueEditMapped(int id, optional_paramvalue_t value, ParamChangeSource source) override;
    optional_paramvalue_t getParamFinalValue(int id) const override;
    void setParamValue(int id, optional_paramvalue_t value, ParamChangeSource source) override;
    void subscribeParamUpdates(std::weak_ptr<ParamChangeListener>) override {}
    void unsubscribeParamUpdates(const ParamChangeListener&) override {}
    void startBatchUpdate() override {}
    void finishBatchUpdate() override {}
    optional_paramvalue_t getMacroValue(int id) const override;
    void setMacroValue(int id, optional_paramvalue_t value, tiny_optional<int>) override;
    void setMacroModifier(int id, int index, tiny_optional<int> param) override;
    void setMacroModifierAmount(int id, int index, float amount) override;
    tiny_optional<float> getMacroModifierAmount(int id, int index) override;
    void clearMacroModifiers() override;
    void changeMacroModifierAmount(int id, int index, float delta) override;
    void setMacroBipolar(int id, bool bipolar) override;
    void setMacroDisplayName(int id, std::string name) override;
    const Sample* getSample(int) const override { return nullptr; }
    LoadStatus loadSample(int, const std::string&) override { return LoadStatus::kFileError; }
    const Metadata& getMetadata() const override { return metadata; }
    const Sequencer& getSequencer() const override { return sequencer; }
    void setSequence(const Sequencer::Sequence& sequence, int index) override;
    void setSequencerConfig(const Sequencer::Config& config) override { sequencer.config = config; }
    void restore(std::vector<optional_paramvalue_t> params, std::vector<Metadata::Macro> macros,
                 std::vector<uint8_t> extraFields = {});
    std::span<const uint8_t> getExtraFields() const { return extraFields; }

  private:
    Metadata::Macro::ParamModifier* modifier(int id, int index);
    std::shared_ptr<const InstrumentType> type;
    Metadata metadata;
    Sequencer sequencer;
    std::vector<optional_paramvalue_t> params;
    std::vector<uint8_t> extraFields;
    std::array<optional_paramvalue_t, kUserMacroCount> macros{};
    uint64_t version = 0;
};

std::unique_ptr<PatchStore> createDesktopPatchStore(const InstrumentTypeRegistry& registry,
                                                     FileSystem& fs, bool typeFolders);
}
