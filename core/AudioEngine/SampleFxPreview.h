#pragma once
#include "AudioStream.h"
#include "SampleFxRealtime.h"

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
  private:
    samplefx::RealtimeChain* volatile chain = nullptr;
};
