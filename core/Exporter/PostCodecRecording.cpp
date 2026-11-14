#include "PostCodecRecording.h"

#ifndef TRACKER_DESKTOP
#include "fileManager.h"
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#include <algorithm>
#include <cstdio>

extern uint8_t externalRamScratchBuff[];
PostCodecRecording postCodecRecording;

namespace
{
#ifdef PLATFORM_IMX
constexpr uint32_t sampleRate = 44100;
#else
constexpr uint32_t sampleRate = 44118;
#endif
constexpr uint32_t ringFrames = 16384;
constexpr uint32_t ringBytes = ringFrames * 2 * sizeof(int16_t);
static_assert(SHARED_SCRATCH_MEMORY_SIZE - ringBytes >= (256 + 64) * 1024);

void songEnded()
{
    postCodecRecording.endSong();
}
}

bool PostCodecRecording::start(FileSystem& fs, const char* projectName)
{
    if (active() || inputIsolated())
    {
        failure = "Waiting for the codec input to return.";
        return false;
    }
    failure = nullptr;
    songLength = 0;
    while (songLength < SONG_MAX && mtProject.song.playlist[songLength] != PLAYLIST_EMPTY_SLOT)
        ++songLength;
    if (!fs.isSdCardInserted())
    {
        failure = "Insert the SD card to record.";
        return false;
    }
    if (!songLength)
    {
        failure = "The song is empty.";
        return false;
    }

    unsigned number = 0;
    do
    {
        snprintf(folder, sizeof(folder), "Export/%s/%s_RecordOutput_%04u",
                 projectName, projectName, ++number);
    } while (number < 9999 && fs.exists(folder));

    auto* ring = reinterpret_cast<int16_t*>(externalRamScratchBuff +
                                            SHARED_SCRATCH_MEMORY_SIZE - ringBytes);
    if (fs.exists(folder) || !fs.mkdir(0, folder))
    {
        failure = "Could not create the recording folder.";
        return false;
    }
    if (!capture.open(fs, folder, ring, ringFrames, sampleRate))
    {
        fs.remove(folder);
        failure = "Could not open the recording on SD.";
        return false;
    }

    sequencer.stop();
    sequencer.stopManualNotes();
    engine.getSdPlayer()->stop();
    engine.getSampleFxPreview()->stop();
    engine.clearDelay();
    engine.clearReverb();
    savedClock = mtConfig.midi.clkIn;
    savedMetronome = mtConfig.metronome.state;
    savedPlaylistPos = mtProject.song.playlistPos;
    savedPattern = mtProject.values.actualPattern;
    mtConfig.midi.clkIn = clockIn_Internal;
    mtConfig.metronome.state = 0;

    received.store(0, std::memory_order_relaxed);
    lastSound.store(0, std::memory_order_relaxed);
    songEnded.store(false, std::memory_order_relaxed);
    isolated.store(true, std::memory_order_release);
    engine.disableLineInImpact();
#ifdef PLATFORM_IMX
    engine.forceRegularOutputForCapture(true);
#endif

    phase = Phase::Settling;
    if (!engine.routePostCodecToI2s(true))
    {
        stop("Could not switch the codec to recording.", false);
        return false;
    }
    // Count fresh I2S blocks after the register write.
    received.store(0, std::memory_order_relaxed);
    routeTimer = 0;
    return true;
}

void PostCodecRecording::receive(const int16_t* stereo, uint32_t frames)
{
    const uint32_t end = received.fetch_add(frames, std::memory_order_relaxed) + frames;
    capture.receive(stereo, frames);
    if (!songEnded.load(std::memory_order_relaxed) || !stereo || !frames) return;

    int16_t low[2] = {stereo[0], stereo[1]};
    int16_t high[2] = {low[0], low[1]};
    for (uint32_t i = 1; i < frames; ++i)
    {
        for (unsigned channel = 0; channel < 2; ++channel)
        {
            const auto sample = stereo[2 * i + channel];
            low[channel] = std::min(low[channel], sample);
            high[channel] = std::max(high[channel], sample);
        }
        if (high[0] - low[0] > 8 || high[1] - low[1] > 8)
        {
            lastSound.store(end, std::memory_order_relaxed);
            return;
        }
    }
}

void PostCodecRecording::endSong()
{
    sequencer.stop();
    songEnded.store(true, std::memory_order_release);
}

void PostCodecRecording::update()
{
    if (retryInput && routeTimer >= 250)
    {
        routeTimer = 0;
        if (engine.routePostCodecToI2s(false))
        {
            retryInput = false;
            // Allow the old codec DMA data to pass with the input still isolated.
            received.store(0, std::memory_order_relaxed);
        }
    }
    if (phase == Phase::Idle)
    {
        if (!retryInput && inputIsolated() && received.load(std::memory_order_relaxed) >=
                                             2 * AUDIO_BLOCK_SAMPLES)
        {
            isolated.store(false, std::memory_order_release);
            engine.enableLineInImpact();
        }
        return;
    }

    if (!newFileManager.getFileSystem()->isSdCardInserted())
    {
        stop("SD card removed during recording.", false);
        return;
    }

    if (phase == Phase::Settling)
    {
        if (received.load(std::memory_order_relaxed) < 2 * AUDIO_BLOCK_SAMPLES)
        {
            if (routeTimer >= 1000) stop("No audio returned from the codec.", false);
            return;
        }
        if (!sequencer.prepareSongRecording())
        {
            stop("Could not load the first song pattern.", false);
            return;
        }
        sequencer.setOnSongEnd(::songEnded);
        sequencer.setRecordingSong(true);
        phase = Phase::Song;
        capture.start();
        sequencer.startPreparedSongRecording();
        return;
    }

    // Low-level SD writes arbitrate with in-flight sector reads. Keep voice
    // prefetch enabled so streamed instruments keep playing during capture.
    capture.drain();
    if (capture.result() != MasterCapture::Result::Recording)
    {
        stop(capture.result() == MasterCapture::Result::Overrun ? "SD could not keep up with audio."
                                                               : "Could not write the recording.",
             false);
        return;
    }

    const uint32_t frames = received.load(std::memory_order_relaxed);
    if (phase == Phase::Song)
    {
        if (songEnded.load(std::memory_order_acquire))
        {
            phase = Phase::Tail;
            endStartFrame = frames;
            silenceTarget = sampleRate * std::max(300, engine.getDelayTime()) / 1000;
        }
        else if (!sequencer.isPlay())
        {
            // Recheck after isPlay() in case song end arrived during this update.
            if (!songEnded.load(std::memory_order_acquire))
                stop("Playback stopped before the song ended.", false);
        }
        return;
    }

    const auto last = std::max(endStartFrame, lastSound.load(std::memory_order_relaxed));
    if (frames - endStartFrame >= 60 * sampleRate ||
        (frames >= last && frames - last >= silenceTarget))
        stop(nullptr, true);
}

void PostCodecRecording::cancel()
{
    if (active()) stop(nullptr, false);
}

void PostCodecRecording::stop(const char* reason, bool save)
{
    capture.stop();
    phase = Phase::Idle;
    sequencer.setRecordingSong(false);
    sequencer.setOnSongEnd(nullCallback);
    if (sequencer.isPlay()) sequencer.stop();
    mtConfig.midi.clkIn = savedClock;
    mtConfig.metronome.state = savedMetronome;
#ifdef PLATFORM_IMX
    engine.forceRegularOutputForCapture(false);
#endif
    retryInput = !engine.routePostCodecToI2s(false);
    routeTimer = 0;
    received.store(0, std::memory_order_relaxed);

    auto* fs = newFileManager.getFileSystem();
    if (save && !retryInput && fs->isSdCardInserted()) capture.finish();
    else capture.cancel();
    if (capture.result() != MasterCapture::Result::Complete && fs->isSdCardInserted())
        fs->remove(folder);
    failure = reason;
    if (retryInput)
        failure = "Codec input restore failed. Retrying.";
    else if (save && capture.result() != MasterCapture::Result::Complete)
        failure = "Could not finish writing the recording.";

    newFileManager.clearRequestedPatternLoad();
    if (mtProject.values.actualPattern != savedPattern &&
        fs->isSdCardInserted() && newFileManager.loadWorkspacePatternNow(savedPattern, true))
    {
        DISABLE_GLOBAL_INTERRUPTS();
        sequencer.switchRamPatternsNow();
        ENABLE_GLOBAL_INTERRUPTS();
    }
    newFileManager.setSongPos(savedPlaylistPos);
}

uint8_t PostCodecRecording::progress() const
{
    if (phase == Phase::Tail) return 99;
    if (phase != Phase::Song) return 0;
    const unsigned steps = std::max(1, int(sequencer.getPatternLength()));
    const auto played = mtProject.song.playlistPos * steps + sequencer.getActualPos();
    return std::min(98u, 100u * played / (songLength * steps));
}
#endif
