#pragma once

#ifndef TRACKER_DESKTOP
#include "MasterCapture.h"
#include "elapsedMillis.h"
#include "mtStructs.h"

class PostCodecRecording
{
  public:
    bool start(FileSystem& fs, const char* projectName);
    void update();
    void cancel();
    void receive(const int16_t* stereo, uint32_t frames);
    void endSong();
    bool inputIsolated() const { return isolated.load(std::memory_order_acquire); }
    bool active() const { return phase != Phase::Idle; }
    uint8_t progress() const;
    const char* error() const { return failure; }

  private:
    enum class Phase { Idle, Settling, Song, Tail };
    void stop(const char* reason, bool save);

    MasterCapture capture;
    Phase phase = Phase::Idle;
    const char* failure = nullptr;
    uint8_t savedClock = 0;
    uint8_t savedMetronome = 0;
    uint8_t savedPlaylistPos = 0;
    uint8_t savedPattern = 0;
    uint16_t songLength = 0;
    uint32_t silenceTarget = 0;
    uint32_t endStartFrame = 0;
    std::atomic<uint32_t> received{0};
    std::atomic<uint32_t> lastSound{0};
    std::atomic<bool> isolated{false};
    std::atomic<bool> songEnded{false};
    bool retryInput = false;
    elapsedMillis routeTimer{0};
    char folder[PATCH_SIZE];
};

extern PostCodecRecording postCodecRecording;
#endif
