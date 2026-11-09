#pragma once
#include <stdint.h>

namespace samplefx
{
// Shared post-chain linear blend. Q15 includes exact dry/wet endpoints.
class ChainMix
{
  public:
    void reset(unsigned percent) { gain = target = int32_t(percent * 32768 / 100); remaining = 0; }
    void set(unsigned percent)
    {
        const int32_t next = int32_t(percent * 32768 / 100);
        if (next == target) return;
        target = next;
        const int32_t delta = target - gain;
        step = delta / int32_t(kRamp);
        remainder = unsigned(delta < 0 ? -delta : delta) % kRamp;
        direction = delta < 0 ? -1 : 1;
        error = 0;
        remaining = kRamp;
    }
    int32_t advance()
    {
        if (remaining)
        {
            gain += step;
            error += remainder;
            if (error >= kRamp) { gain += direction; error -= kRamp; }
            if (!--remaining) gain = target;
        }
        return gain;
    }
    bool fullyWet() const { return !remaining && gain == 32768; }
    static int16_t blend(int16_t dry, int16_t wet, int32_t gain)
    {
        return int16_t(int32_t(dry) + ((int32_t(wet - int32_t(dry)) * gain + 16384) >> 15));
    }
  private:
    static constexpr unsigned kRamp = 441; // 10 ms at the editor's 44.1 kHz.
    int32_t gain = 32768, target = 32768, step = 0, direction = 1;
    unsigned remaining = 0, remainder = 0, error = 0;
};
}
