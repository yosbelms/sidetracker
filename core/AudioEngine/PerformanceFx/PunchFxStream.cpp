#include "PunchFxStream.h"
#include "Platform.h"
#include "analyze_rms.h"
#include "mtSequencer.h"
#include <cmath>

namespace punch
{
#ifdef AUDIO_PERFORMANCE_INFO_ENABLED
uint32_t Stream::peakMicros() const
{
    if (!timer) return 0;
    const auto ticksPerUs = timer->clockCyclesPerUs();
    return ticksPerUs ? (peakCycles.load(std::memory_order_relaxed) + ticksPerUs - 1) / ticksPerUs : 0;
}
#endif

float Stream::readOutputRms()
{
    DISABLE_GLOBAL_INTERRUPTS();
    const int64_t sum = meterSum;
    const uint32_t blocks = meterBlocks;
    meterSum = 0;
    meterBlocks = 0;
    ENABLE_GLOBAL_INTERRUPTS();

    if (!blocks) return -1.0f;
    return std::sqrt(float(sum) / (float(blocks) * (2 * AUDIO_BLOCK_SAMPLES))) / 32768.0f;
}

void Stream::update()
{
#ifdef AUDIO_PERFORMANCE_INFO_ENABLED
    const auto start = timer ? timer->clockCycles() : 0;
#endif
    audio_block_t* dry[] = {receiveReadOnly(0), receiveReadOnly(1)};
    const int16_t* left = dry[0] ? dry[0]->data : nullptr;
    const int16_t* right = dry[1] ? dry[1]->data : nullptr;
#ifdef PLATFORM_IMX
    constexpr float sampleRate = 44100.0f;
#else
    constexpr float sampleRate = AUDIO_SAMPLE_RATE_EXACT;
#endif

    audio_block_t* wet[2] = {};
    if (!renderMode)
    {
        effects.beginBlock(sequencer.getCurrentTempo(), sampleRate,
                           mix.load(std::memory_order_relaxed),
                           visible.load(std::memory_order_relaxed) || sequencer.isPlay());
        if (effects.wet())
        {
            for (unsigned ch = 0; ch < 2; ++ch)
                wet[ch] = dry[ch] && dry[ch]->ref_count == 1 ? dry[ch] : allocate();
        }
        const bool available = wet[0] && wet[1];
        effects.process(left, right, available ? wet[0]->data : nullptr,
                        available ? wet[1]->data : nullptr, AUDIO_BLOCK_SAMPLES);
    }

    const bool available = wet[0] && wet[1];
    const bool meter = !renderMode && meterEnabled.load(std::memory_order_relaxed);
    for (unsigned ch = 0; ch < 2; ++ch)
    {
        auto* output = available ? wet[ch] : dry[ch];
        if (output)
        {
            if (meter) meterSum = AudioAnalyzeRMS::accumulateSquares(*output, meterSum);
            transmit(output, ch);
        }
        if (wet[ch] && wet[ch] != dry[ch]) release(wet[ch]);
        if (dry[ch]) release(dry[ch]);
    }
    if (meter) ++meterBlocks;
#ifdef AUDIO_PERFORMANCE_INFO_ENABLED
    if (timer && !renderMode)
    {
        const auto cycles = timer->clockCycles() - start;
        if (cycles > peakCycles.load(std::memory_order_relaxed))
            peakCycles.store(cycles, std::memory_order_relaxed);
    }
#endif
}
}
