#ifndef AUDIOENGINE_EXPORTQUEUES_H_
#define AUDIOENGINE_EXPORTQUEUES_H_

#include "analyze_rms.h"
#include "record_queue.h"
#include "mtAudioExport.h"

class ExportQueues
{
  public:
    enum ExportChannels
    {
        kLeft,
        kRight,
        kChannelsCount
    };

    AudioAnalyzeRMS* getChannelRms(ExportChannels channel);
    AudioRecordQueue* getQueue(ExportChannels channel, mtAudioExport::enStem stem);
    void selectPerfFxTap(bool enabled);
    static constexpr unsigned kFinalMixInput = 0;
    static constexpr unsigned kPerfFxInput = 1;

  private:
    AudioAnalyzeRMS channelRMS[ExportChannels::kChannelsCount];
    class MasterQueue final : public AudioRecordQueue
    {
      public:
        MasterQueue() : AudioRecordQueue(2, inputs) {}
        bool postPerfFx = false;

      private:
        void update() override
        {
            auto* finalMix = receiveReadOnly(kFinalMixInput);
            auto* perfFx = receiveReadOnly(kPerfFxInput);
            auto* unused = postPerfFx ? finalMix : perfFx;
            if (unused) release(unused);
            enqueue(postPerfFx ? perfFx : finalMix);
        }

        audio_block_t* inputs[2];
    };

    AudioRecordQueue queue[kChannelsCount][mtAudioExport::stem_master];
    MasterQueue master[kChannelsCount];
};

#endif /* AUDIOENGINE_EXPORTQUEUES_H_ */
