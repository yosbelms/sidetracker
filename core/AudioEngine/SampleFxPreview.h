#pragma once
#include "AudioStream.h"
#include "SampleFxRealtime.h"
class Timer;

// Dedicated audition source; the sequencer, track effects and master chain do
// not process the editor's preview. The final preview gain still applies.
class SampleFxPreview : public AudioStream
{
  public:
    SampleFxPreview() : AudioStream(0, nullptr) {}
    void start(samplefx::RealtimeChain* value);
    void stop();
    bool isPlaying() const { return chain != nullptr; }
    void update() override;
    void setBudgetTimer(Timer* value) { timer=value; }
    void requestFadeOut();
#ifndef TRACKER_DESKTOP
    void finishAudioBlock(unsigned callbackUs);
#endif
  private:
    samplefx::RealtimeChain* volatile chain = nullptr;
    Timer* timer=nullptr;
    volatile unsigned fadeRemaining=0;
#ifndef TRACKER_DESKTOP
    samplefx::RealtimeChain* measuredChain = nullptr;
    unsigned chainUs = 0;
#endif
};
