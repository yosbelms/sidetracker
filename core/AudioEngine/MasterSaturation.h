#pragma once

#include "AudioStream.h"
#include "MasterSaturationSettings.h"
#include "dspApplyGain.h"
#include <atomic>

class MasterSaturation final : public AudioStream
{
  public:
    static constexpr int kDriveLimit = masterSaturationSettings::kDriveLimit;

    MasterSaturation() : AudioStream(2, inputs) {}
    void setMode(SaturationType mode);
    void setDrive(int halfDb);

  private:
    void update() override;

    audio_block_t* inputs[2];
    std::atomic<SaturationType> mode{SaturationType::kSoftClip};
    std::atomic<int32_t> gain{MULTI_UNITYGAIN};
    int32_t previousGain = MULTI_UNITYGAIN;
};
