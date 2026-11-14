#include "ExportQueues.h"

AudioAnalyzeRMS* ExportQueues::getChannelRms(ExportChannels channel)
{
    return &channelRMS[channel];
}

AudioRecordQueue* ExportQueues::getQueue(ExportChannels channel, mtAudioExport::enStem stem)
{
    return stem == mtAudioExport::stem_master ? &master[channel] : &queue[channel][stem];
}

void ExportQueues::selectPerfFxTap(bool enabled)
{
    for (auto& channel : master) channel.postPerfFx = enabled;
}
