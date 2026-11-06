#ifndef AUDIOENGINE_AUDIOOUTPUT_H_
#define AUDIOENGINE_AUDIOOUTPUT_H_

#include "Audio.h"
#include "SampleFxPreview.h"

class AudioOutput
{
  public:
    enum OutputChannels
    {
        kLeft,
        kRight,
        kChannelsCount
    };

    enum FinalMixInputs
    {
        kStream = 0,
        kSdCardPreview = 1,
        kSampleFxPreview = 2,
        kTestWaveform = 4,
        kMetronomeTick = 5,
        kNesEmulatorSound = 6,
        kFinalMixerSize
    };

    AudioOutput();
#ifndef PLATFORM_IMX
    AudioMux* getLimiterSidechainMux();
#else
    UsbOutputStream* getUsbStream();
    AudioMux* getLimiterSidechainMux(OutputChannels channel);
#endif
    AudioEffectLimiter* getLimiter(OutputChannels channel);
    AudioMixer<FinalMixInputs::kFinalMixerSize>* getFinalMixer(OutputChannels channel);
    AudioOutputStream* getStream();
    SampleFxPreview* getSampleFxPreview() { return &sampleFxPreview; }

  private:
#ifndef PLATFORM_IMX
    AudioMux limiterSidechainMux;
#else
    AudioMux limiterSidechainMux[OutputChannels::kChannelsCount];
    UsbOutputStream usbOutput;
#endif
    SampleFxPreview sampleFxPreview;
    AudioEffectLimiter limiter[OutputChannels::kChannelsCount];
    AudioOutputStream outputStream;
    AudioMixer<FinalMixInputs::kFinalMixerSize> finalMixer[OutputChannels::kChannelsCount];
    std::vector<AudioConnection*> connections;
};

#endif /* AUDIOENGINE_AUDIOOUTPUT_H_ */
