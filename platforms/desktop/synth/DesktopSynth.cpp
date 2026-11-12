#include "DesktopPatch.h"

#include "PolySynth/FileSystem.h"
#include "PolySynth/Grid.h"
#include "PolySynth/GridMapper.h"
#include "PolySynth/GridMapperStore.h"
#include "PolySynth/Instrument.h"
#include "PolySynth/MidiSink.h"
#include "PolySynth/MidiSource.h"
#include "PolySynth/Mixer.h"
#include "PolySynth/PatchPool.h"
#include "PolySynth/SampleStore.h"
#include "PolySynth/sdk/InstrumentVoice.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace polysynth
{
void InstrumentTypeRegistry::addInstrumentType(int id, std::unique_ptr<InstrumentType> type)
{
    instrumentTypes.insert_or_assign(id, std::move(type));
}

std::shared_ptr<const InstrumentType> InstrumentTypeRegistry::getInstrumentType(int id) const
{
    const auto type = instrumentTypes.find(id);
    return type == instrumentTypes.end() ? nullptr : type->second;
}

const chordscheme_t& Musicology::rootNoteChordScheme()
{
    static const auto root = chordscheme_t{}.set(kChordMaxWidth / 2);
    return root;
}

namespace
{
using EventType = MidiEvent::Type;
using VoiceContext = InstrumentVoice::InstrumentVoiceContext;
using MonoBuffer = std::array<sample_t, kAudioBufferSize>;

void clear(AudioBufferStereoRef buffer)
{
    for (auto channel : buffer) std::ranges::fill(channel, 0.0f);
}

class DesktopInstrument final : public Instrument
{
  public:
    DesktopInstrument(int id, const VoiceContext& context) : id(id), context(context) {}

    void assignPatch(std::shared_ptr<Patch> newPatch) override
    {
        patch = std::static_pointer_cast<DesktopPatch>(std::move(newPatch));
        for (auto& slot : voices) slot = {};
        voiceCount = 0;
        resizeVoices();
    }

    void connect(MidiSource& source) override
    {
        callback = std::make_shared<MidiSource::MidiEventCallback>(
            [this](const MidiEvent& event, int) { onEvent(event); });
        source.subscribeMidiEvents(id, callback);
    }

    void setMidiChannel(int value) override { channel = value; }
    int getMidiChannel() const override { return channel; }
    void setLocalMode(LocalMode value) override { localMode = value; }
    LocalMode getLocalMode() const override { return localMode; }
    int getVoiceCount() const override { return voiceCount; }

    void setVoiceCount(int count) override
    {
        requestedVoices = std::clamp(count, 0, int(voices.size()));
        resizeVoices();
    }

    void render(AudioBufferStereoRef output, std::span<AudioBufferRef> aux)
    {
        const bool stereo = patch->getMetadata().outputChannels == Patch::Metadata::OutputChannels::kStereo;
        for (auto& slot : activeSlots())
        {
            auto& voice = *slot.voice;
            if (!voice.isActive()) continue;
            applyParams(slot);
            MonoBuffer left{}, right{};
            voice.renderVoiceAudio(context, std::nullopt, {left, right}, aux);
            for (size_t i = 0; i < left.size(); ++i)
            {
                output[0][i] += left[i];
                output[1][i] += stereo ? right[i] : left[i];
            }
        }
        if (voiceCount) voices[0].voice->renderPostProcessAudio(context, output);
    }

  private:
    struct Slot
    {
        std::unique_ptr<InstrumentVoice> voice;
        uint64_t order = 0;
        uint64_t revision = 0;
        float velocity = 1.0f;
        float pressure = 0.0f;
        bool paramsDirty = true;
    };

    std::span<Slot> activeSlots() { return {voices.data(), static_cast<size_t>(voiceCount)}; }

    void resizeVoices()
    {
        if (!patch) return;
        const int count = std::min(requestedVoices, patch->getMetadata().maxVoices);
        for (int i = count; i < voiceCount; ++i) voices[i] = {};
        for (int i = voiceCount; i < count; ++i)
        {
            auto& slot = voices[i];
            slot.voice = patch->getInstrumentType().createVoice(i);
            slot.voice->setPatch(patch);
            applyParams(slot);
        }
        voiceCount = count;
    }

    void applyParams(Slot& slot)
    {
        if (!slot.paramsDirty && slot.revision == patch->revision()) return;
        const auto& params = patch->getMetadata().params;
        for (size_t i = 0; i < params.size(); ++i)
        {
            const auto value = patch->voiceValue(i, slot.velocity, slot.pressure);
            slot.voice->onParamChange(i, value ? value : params[i].defaultValue);
        }
        slot.revision = patch->revision();
        slot.paramsDirty = false;
    }

    Slot& chooseVoice(note_t note)
    {
        const auto slots = activeSlots();
        auto same = std::ranges::find_if(slots, [note](const Slot& slot) { return slot.voice->handlesNote(note); });
        if (same != slots.end()) return *same;
        return *std::ranges::min_element(slots, {}, [](const Slot& slot) {
            const int priority = !slot.voice->isActive() ? 0 : !slot.voice->handlesAnyNote() ? 1 : 2;
            return std::pair(priority, slot.order);
        });
    }

    void noteOn(const MidiEvent& event)
    {
        if (!voiceCount) return;
        const auto note = event.data.note.note;
        auto& slot = chooseVoice(note);
        if (voiceCount > 1 && slot.voice->handlesAnyNote() && !slot.voice->handlesNote(note))
        {
            slot.voice->clearPreviousNotes();
            if (const auto previous = slot.voice->getLastNote())
            {
                MidiEvent off{EventType::kNoteOff, event.channel, {}};
                off.data.note.note = *previous;
                slot.voice->onMidiEvent(context, off);
            }
        }
        slot.velocity = event.data.note.velocity / 127.0f;
        slot.pressure = channelPressure;
        slot.paramsDirty = true;
        slot.order = ++noteOrder;
        applyParams(slot);
        slot.voice->onMidiEvent(context, event);
        updateVelocity(slot);
    }

    void noteOff(MidiEvent event)
    {
        event.type = EventType::kNoteOff;
        for (auto& slot : activeSlots())
        {
            if (!slot.voice->handlesNote(event.data.note.note)) continue;
            slot.voice->onMidiEvent(context, event);
            updateVelocity(slot);
        }
    }

    void updateVelocity(Slot& slot)
    {
        const float velocity = slot.voice->getCurrentNoteVelocity().value_or(slot.velocity);
        if (velocity != slot.velocity)
        {
            slot.velocity = velocity;
            slot.paramsDirty = true;
        }
    }

    void allNotesOff()
    {
        for (auto& slot : activeSlots())
        {
            if (!slot.voice->handlesAnyNote()) continue;
            slot.voice->clearPreviousNotes();
            if (const auto note = slot.voice->getLastNote())
            {
                MidiEvent off{EventType::kNoteOff, static_cast<uint8_t>(channel), {}};
                off.data.note.note = *note;
                slot.voice->onMidiEvent(context, off);
            }
        }
    }

    void controlChange(const MidiEvent::Data::ControlChange& control)
    {
        if (control.id == MidiEvent::Data::ControlChange::kAllNotesOffCC)
        {
            allNotesOff();
            return;
        }
        const auto& metadata = patch->getMetadata();
        const int macro = control.id - Patch::Metadata::Macro::kMacroCCStart;
        if (macro >= 0 && macro < int(kUserMacroCount))
        {
            using CC = MidiEvent::Data::ControlChange;
            constexpr float range = Patch::kMaxMacroValue;
            float value = control.value * (range / CC::kMaxValue);
            if (metadata.macros[macro].bipolar)
            {
                constexpr float center = range * 0.5f;
                const int offset = int(control.value) - CC::kMidValue;
                const int halfRange = offset <= 0 ? CC::kMidValue : CC::kMaxValue - CC::kMidValue;
                value = center + offset * (center / halfRange);
            }
            patch->setMacroValue(macro, value, std::nullopt);
            return;
        }
        for (size_t i = 0; i < metadata.params.size(); ++i)
            if (metadata.params[i].midiCC == control.id)
                patch->setParamValueEditMapped(i, control.value / 127.0f, Patch::ParamChangeSource::kMidiPort);
    }

    void onEvent(const MidiEvent& event)
    {
        if (event.type <= EventType::kContinue)
        {
            if (event.type == EventType::kStop) allNotesOff();
            return;
        }
        if (!patch || (channel != MidiEvent::kChannelOmni && event.channel != channel)) return;
        switch (event.type)
        {
        case EventType::kNoteOn:
            if (event.data.note.velocity) noteOn(event);
            else noteOff(event);
            break;
        case EventType::kNoteOff:
            noteOff(event);
            break;
        case EventType::kControlChange:
            controlChange(event.data.controlChange);
            break;
        case EventType::kPitchBendChange:
            for (auto& slot : activeSlots()) slot.voice->onMidiEvent(context, event);
            break;
        case EventType::kChannelAftertouch:
            channelPressure = event.data.channelAftertouch.velocity / 127.0f;
            for (auto& slot : activeSlots())
            {
                slot.pressure = channelPressure;
                slot.paramsDirty = true;
            }
            break;
        case EventType::kPolyAftertouch:
            for (auto& slot : activeSlots())
                if (slot.voice->handlesNote(event.data.note.note))
                {
                    slot.pressure = event.data.note.velocity / 127.0f;
                    slot.paramsDirty = true;
                }
            break;
        default: break;
        }
    }

    const int id;
    const VoiceContext& context;
    std::shared_ptr<DesktopPatch> patch;
    std::shared_ptr<MidiSource::MidiEventCallback> callback;
    std::array<Slot, 8> voices;
    uint64_t noteOrder = 0;
    int voiceCount = 0;
    int requestedVoices = 0;
    int channel = 0;
    float channelPressure = 0.0f;
    LocalMode localMode = LocalMode::kOn;
};

// Tracker supplies the sequencer, mixer and MIDI source. Only its synth and patch APIs
// need a desktop implementation.
class DesktopSynth final : public Synth
{
  public:
    explicit DesktopSynth(const Allocator& allocator)
    {
        InstrumentTypeRegistry::registerInstrumentTypes(registry, allocator);
    }

    std::unique_ptr<PatchStore> createPatchStore(FileSystemId id, bool typeFolders) override
    {
        return createDesktopPatchStore(registry, *filesystems.at(id), typeFolders);
    }

    std::shared_ptr<Instrument> createInstrument(int id) override
    {
        return std::make_shared<DesktopInstrument>(id, context);
    }

    void addFileSystem(FileSystemId id, std::unique_ptr<FileSystem> fileSystem) override
    {
        filesystems.insert_or_assign(id, std::move(fileSystem));
    }

    std::string getInstrumentName(int id) override
    {
        const auto type = registry.getInstrumentType(id);
        return type ? type->name : "";
    }

    std::vector<int> getInstrumentTypeIds() override
    {
        std::vector<int> ids;
        for (const auto& entry : registry.getInstrumentTypes()) ids.push_back(entry.first);
        std::ranges::sort(ids);
        return ids;
    }

    std::vector<std::string_view> getInstrumentTypeNames() override
    {
        std::vector<std::string_view> names;
        for (int id : getInstrumentTypeIds()) names.push_back(registry.getInstrumentType(id)->name);
        return names;
    }

    void setup(std::span<std::shared_ptr<Instrument>> newInstruments,
               std::span<std::shared_ptr<Instrument>>, std::shared_ptr<Mixer>,
               TempoProvider tempoProvider) override
    {
        for (size_t i = 0; i < std::min(newInstruments.size(), instruments.size()); ++i)
            instruments[i] = std::static_pointer_cast<DesktopInstrument>(newInstruments[i]);
        getTempo = std::move(tempoProvider);
    }

    void disconnectInstrument(int id) override { instruments.at(id).reset(); }

    void renderAudioBuffer(AudioBufferStereoRef output) override
    {
        clear(output);
        updateTempo();
        for (const auto& instrument : instruments)
            if (instrument) instrument->render(output, {});
    }

    void renderAudioBuffer(AudioBufferStereoRef output, std::span<AudioBufferRef> aux, int id) override
    {
        clear(output);
        for (auto send : aux) std::ranges::fill(send, 0.0f);
        updateTempo();
        if (const auto& instrument = instruments[id]) instrument->render(output, aux);
    }

    std::unique_ptr<PatchPool> createPatchPool(std::shared_ptr<PatchStore>, int,
        std::function<tiny::optional<int>(const std::string&)>) override { return {}; }
    std::unique_ptr<Mixer> createMixer(int, int) override { return {}; }
    std::unique_ptr<Grid> createGrid() override { return {}; }
    std::unique_ptr<GridMapperStore> createGridMapperStore(FileSystemId) override { return {}; }
    std::unique_ptr<GridMapper> createGridMapper() override { return {}; }
    std::unique_ptr<SampleStore> createSampleStore(FileSystemId) override { return {}; }
    std::shared_ptr<Sequencer> createSequencer() override { return {}; }
    void addMidiSource(const std::string&, std::shared_ptr<MidiSource>) override {}
    void addMidiSink(const std::string&, std::unique_ptr<MidiSink>) override {}
    void initSamplePool(size_t, std::shared_ptr<SampleStore>) override {}
    void updateAudio() override {}
    void updateMain() override {}

  private:
    void updateTempo()
    {
        if (!getTempo) return;
        const float tempo = getTempo().value_or(context.tempo);
        if (std::isfinite(tempo) && tempo > 0.0f) context.tempo = tempo;
    }

    InstrumentTypeRegistry registry;
    std::map<FileSystemId, std::unique_ptr<FileSystem>> filesystems;
    VoiceContext context{kDefaultTempo};
    std::array<std::shared_ptr<DesktopInstrument>, kMaxNumberOfInstruments> instruments;
    TempoProvider getTempo;
};
}

std::shared_ptr<Synth> Synth::createSynth(Allocator allocator)
{
    return std::make_shared<DesktopSynth>(allocator);
}
}
