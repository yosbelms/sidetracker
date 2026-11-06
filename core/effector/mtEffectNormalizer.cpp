#include "mtEffectNormalizer.h"
#include "util_math.h"
#include <algorithm>
mtEffectNormalizer sampleEditorNormalizer;

void mtEffectNormalizer::setParameter(float& value, int idx)
{
    assert(idx == 0);
    norm_level = (int)value;
}

bool mtEffectNormalizer::startProcess()
{
    length = confirmed[0].selection.length;

    for (int channel = 0; channel < kChannels; channel++)
    {
        channelParams[channel].srcAddr = confirmed[channel].selection.addr;
        channelParams[channel].dstAddr = processed[channel].selection.addr;
        channelParams[channel].peak = 0;
        channelParams[channel].amp = 0;
    }

    processedSamples = 0;
    analysisSamples = 0;
    analyzing = true;
    state = true;

    return true;
}
int32_t mtEffectNormalizer::processBlock()
{
    if (!state)
        return -1;

    if (analyzing)
    {
        const uint32_t count = std::min<uint32_t>(8192, length - analysisSamples);
        for (int ch = 0; ch < kChannels; ++ch)
            channelParams[ch].peak = std::max(channelParams[ch].peak,
                find_max(channelParams[ch].srcAddr + analysisSamples, count));
        analysisSamples += count;
        if (analysisSamples == length)
        {
            const int target = map<int>(norm_level, 0, 100, 0, MAX_SIGNED_16BIT);
            for (int ch = 0; ch < kChannels; ++ch)
                channelParams[ch].amp = channelParams[ch].peak > 0 ? float(target) / channelParams[ch].peak : 0;
            analyzing = false;
        }
        return 0;
    }
    uint32_t dif = length - processedSamples;
    uint32_t processedBlockLength = dif > 8192 ? 8192 : dif;

    for (int channel = 0; channel < kChannels; channel++)
    {
        for (uint16_t i = 0; i < processedBlockLength; i++)
        {
            int32_t processVal = channelParams[channel].amp * (*channelParams[channel].srcAddr);

            if (processVal > MAX_SIGNED_16BIT)
                processVal = MAX_SIGNED_16BIT;
            if (processVal < MIN_SIGNED_16BIT)
                processVal = MIN_SIGNED_16BIT;

            *channelParams[channel].dstAddr = (int16_t)processVal;
            channelParams[channel].dstAddr++;
            channelParams[channel].srcAddr++;
        }
    }
    if (processedBlockLength < 8192)
    {
        state = 0;
    }

    processedSamples += processedBlockLength;

    if (!state)
    {
        for (int channel = 0; channel < kChannels; channel++)
        {
            processed[channel].selection.length = processedSamples; // redundancy
            processed[channel].area.length =
              confirmed[channel].area.length + (processed[channel].selection.length -
                                                confirmed[channel].selection.length); // redundancy
        }
    }

    return processedBlockLength;
}
bool mtEffectNormalizer::getProcessState()
{
    return state;
}
uint32_t mtEffectNormalizer::getExpectedProcessLength()
{
    return confirmed[0].selection.length;
}

int mtEffectNormalizer::find_max(int16_t* ptr, int len)
{
    int max = 0;
    if ((ptr == nullptr) || (len == 0))
        return 0;

    while (len--)
    {
        int abs = (*ptr > 0) ? *ptr : -(*ptr);
        if (abs > max)
            max = abs;

        ptr++;
    }

    return max;
}
