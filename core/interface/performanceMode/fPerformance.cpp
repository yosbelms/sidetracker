#include "performanceMode.h"
#include "core/interfaceMenuPopup.h"
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtConfig.h"
#include "mtPadsBacklight.h"
#include "mtSequencer.h"

cPerformanceMode performanceMode;

namespace
{
void switchModule(uint8_t button)
{
    ModuleBase::eventFunct(eventSwitchModule, &performanceMode, &button, nullptr);
}

void togglePlaybackMode()
{
    if (newFileManager.isPatternLoading() || sequencer.isLastPatternStep()) return;

    const auto position = mtProject.song.playlistPos;
    if (sequencer.isSongMode()) sequencer.loopCurrentPattern(position);
    else sequencer.continueSong(position);
    sequencer.setOnPatternOrSelectionEnd(nullCallback);
    sequencer.setOnGlobalStep(nullCallback);
}

void play()
{
    const bool shift = ModuleBase::buttons->isButtonPressed(interfaceButtonShift);
    if (sequencer.getSeqState() == Sequencer::SEQ_STATE_STOP)
    {
        if (shift) sequencer.playSong();
        else sequencer.playPattern();
    }
    else if (shift) sequencer.setOnGlobalStep(togglePlaybackMode);
    else sequencer.stop();
}

void pad(uint8_t key, uint8_t state, int16_t)
{
    if (key >= punch::kColumns * punch::kLevels) return;
    auto& rack = engine.getAudioOutput()->getPunchFx().rack();
    const auto token = punch::owner(punch::Source::Pads, key);
    if (state == buttonPress)
        rack.press(token, punch::columns[key % punch::kColumns].effect, key / punch::kColumns);
    else if (state == buttonRelease)
        rack.release(token);
}

void releasePads()
{
    engine.getAudioOutput()->getPunchFx().rack().releaseSource(punch::Source::Pads);
}
}

void cPerformanceMode::start(uint32_t options)
{
    tracksPage = options == 2;
    moduleRefresh = 1;
    engine.getAudioOutput()->getPunchFx().setVisible(true);

    constexpr uint8_t modules[] = {
        interfaceButtonParams, interfaceButtonFile, interfaceButtonConfig, interfaceButtonMaster,
        interfaceButtonSamplePlay, interfaceButtonSampleEdit, interfaceButtonSampleRec,
        interfaceButtonSampleLoad, interfaceButtonSong, interfaceButtonPattern, interfaceButtonGame
    };
    for (auto button : modules) ip->setButtonObj(button, buttonPress, switchModule);
    ip->setButtonObj(interfaceButtonPerformance, buttonPress, [] { performanceMode.switchTracksPage(); });
    ip->setButtonObj(interfaceButtonPlay, buttonPress, play);
    ip->setButtonObj(interfaceButtonDelete, buttonPress, releasePads);
    ip->setButtonObj(interfaceButtonMiniMenu, [](uint8_t state) {
        if (state == buttonPress) mtMenuPopup.showModulePopup(&performanceMode);
        else if (state == buttonRelease) mtMenuPopup.hideModulePopup(&performanceMode);
    });
    for (uint8_t button = interfaceButton0; button <= interfaceButton7; ++button)
        ip->setButtonObj(button, buttonPress, [](uint8_t key) {
            performanceMode.toggleMute(key - interfaceButton0);
        });
    ip->setPadsGlobal(pad);
    showPerformanceFxes();
}

void cPerformanceMode::stop()
{
    releasePads();
    engine.getAudioOutput()->getPunchFx().setVisible(false);
    moduleRefresh = 0;
    padsBacklight.clearGlobalPads(1, 1, 1);
}

void cPerformanceMode::switchTracksPage()
{
    tracksPage ^= 1;
    showTracks();
    display.synchronizeRefresh();
}

void cPerformanceMode::toggleMute(uint8_t button)
{
    const auto track = button + (tracksPage ? kSampleTracks : 0);
    if (track < kSampleTracks)
    {
        auto& mute = mtProject.values.trackMute[track];
        mute = !mute;
        engine.muteTrack(track, mute);
    }
    else
    {
        auto& mute = mtProject.values.midiTrackMute[track - kSampleTracks];
        mute = !mute;
    }
    newFileManager.setProjectStructChanged();
    showTracks();
    display.synchronizeRefresh();
}

void cPerformanceMode::update()
{
    auto& rack = engine.getAudioOutput()->getPunchFx().rack();
    bool changed = false;
    for (unsigned i = 0; i < punch::kColumns; ++i)
    {
        const auto row = rack.soundingLevel(i);
        if (row != displayedRow[i])
        {
            showColumn(i, row);
            changed = true;
        }
    }
    if (changed) display.synchronizeRefresh();
}
