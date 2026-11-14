#include "MasterSaturation.h"
#include <algorithm>
#include <cmath>

void MasterSaturation::setMode(SaturationType value)
{
    mode.store(static_cast<unsigned>(value) < SaturationType::kSaturationTypeEnd
                 ? value : SaturationType::kSoftClip, std::memory_order_relaxed);
}

void MasterSaturation::setDrive(int halfDb)
{
    halfDb = std::clamp(halfDb, -kDriveLimit, kDriveLimit);
    gain.store(static_cast<int32_t>(std::lround(MULTI_UNITYGAIN * std::pow(10.0f, halfDb / 40.0f))),
               std::memory_order_relaxed);
}

AUDIO_CORE_RAM void MasterSaturation::update()
{
    const auto curve = mode.load(std::memory_order_relaxed);
    auto target = gain.load(std::memory_order_relaxed);
    const bool bypass = curve == SaturationType::kHardClip &&
                        target == MULTI_UNITYGAIN && previousGain == MULTI_UNITYGAIN;

    for (unsigned channel = 0; channel < 2; ++channel)
    {
        auto* block = receiveReadOnly(channel);
        if (!block) continue;
        if (!bypass)
        {
            auto* processed = block->ref_count == 1 ? block : allocate();
            if (processed)
            {
                int32_t driven[AUDIO_BLOCK_SAMPLES];
                auto start = previousGain;
                applyGainAndCopy(driven, block->data, target, start);
                if (curve == SaturationType::kSoftClip)
                {
                    // The cubic peaks at sqrt(6.72 / 3) times full scale.
                    // Extend it flat past that point to prevent foldback at high Drive.
                    constexpr int32_t limit = 49042;
                    for (auto& sample : driven) sample = std::clamp(sample, -limit, limit);
                }
                saturate(processed->data, driven, curve);
                if (processed != block) release(block);
                block = processed;
            }
        }
        transmit(block, channel);
        release(block);
    }
    previousGain = target;
}
