#pragma once

#include "AudioStream.h"
#include "PerfFx.h"

class Timer;

namespace perffx
{
extern Storage storage;

class Stream : public AudioStream
{
  public:
    Stream() : AudioStream(2, queue), effects(storage) {}
    Rack& rack() { return effects; }
    void setVisible(bool value) { visible.store(value, std::memory_order_relaxed); }
    void setMix(uint8_t value) { mix.store(value, std::memory_order_relaxed); }
    void setMeterEnabled(bool value) { meterEnabled.store(value, std::memory_order_relaxed); }
    float readOutputRms();
#ifdef AUDIO_PERFORMANCE_INFO_ENABLED
    void setTimer(Timer* value) { timer = value; }
    uint32_t peakMicros() const;
    void resetPeak() { peakCycles.store(0, std::memory_order_relaxed); }
#endif

  private:
    void update() override;
    audio_block_t* queue[2];
    Rack effects;
    std::atomic<bool> visible{false};
    std::atomic<uint8_t> mix{100};
    std::atomic<bool> meterEnabled{false};
    static constexpr uint32_t kNoMeterBlock = UINT32_MAX;
    std::atomic<uint32_t> meterBlockPower{kNoMeterBlock};
#ifdef AUDIO_PERFORMANCE_INFO_ENABLED
    Timer* timer = nullptr;
    std::atomic<uint32_t> peakCycles{0};
#endif
};
}
