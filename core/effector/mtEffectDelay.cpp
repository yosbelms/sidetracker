#include "mtEffectDelay.h"
#include "mtStructs.h"
#include <cmath>
#include <algorithm>
#include <cr_section_macros.h>
#include <string.h>

__NOINIT(EXTERNAL_RAM) mtEffectDelay::ChannelParams mtEffectDelay::channelParams[kChannels];

void mtEffectDelay::setParameter(float& value, int idx)
{
    switch (idx)
    {
        case 0:
            effectDelayParams.feedback = value;
            calcLastVoiceIdx();
            break;
        case 1: effectDelayParams.time = (uint16_t)value; break;
        default: assert(false); break;
    }
}

bool mtEffectDelay::startProcess()
{
    if (effectDelayParams.feedback <= 0 || effectDelayParams.time <= 0 ||
        getExpectedProcessLength() > EDIT_BUFF_SIZE / 2)
    {
        return false;
    }

    for (int channel = 0; channel < kChannels; channel++)
    {
        for (uint8_t i = 0; i <= lastVoiceIdx; i++)
            channelParams[channel].feedbackVoiceMult[i] = powf(effectDelayParams.feedback, i);
        channelParams[channel].processedSamples = 0;
    }

    state = true;
    return true;
}
int32_t mtEffectDelay::processBlock()
{
    if (!state) return -1;
    const uint32_t delaySamples = uint32_t(effectDelayParams.time * 44.1f);
    const uint32_t total = getExpectedProcessLength();
    const uint32_t position = channelParams[0].processedSamples;
    const uint32_t count = std::min<uint32_t>(8192, total - position);
    for (int channel = 0; channel < kChannels; ++channel)
    {
        const int16_t* src = confirmed[channel].selection.addr;
        int16_t* dst = processed[channel].selection.addr;
        const uint32_t inputLength = confirmed[channel].selection.length;
        for (uint32_t i = position; i < position + count; ++i)
        {
            int32_t sum = 0;
            const uint32_t first = i < inputLength ? 0 : (i - inputLength) / delaySamples + 1;
            const uint32_t last = std::min<uint32_t>(lastVoiceIdx, i / delaySamples);
            for (uint32_t echo = first; echo <= last; ++echo)
                sum += src[i - echo * delaySamples] * channelParams[channel].feedbackVoiceMult[echo];
            dst[i] = std::clamp<int32_t>(sum, MIN_SIGNED_16BIT, MAX_SIGNED_16BIT);
        }
        channelParams[channel].processedSamples += count;
    }
    if (position + count == total)
    {
        state = false;
        for (int channel = 0; channel < kChannels; ++channel)
        {
            processed[channel].selection.length = total;
            processed[channel].area.length = confirmed[channel].area.length + total - confirmed[channel].selection.length;
        }
    }
    return count;
}
bool mtEffectDelay::getProcessState()
{
    return state;
}
uint32_t mtEffectDelay::getExpectedProcessLength()
{
    constexpr float MILISECOND_TIME = 44.1f;
    uint32_t timeInSamples = (uint32_t)((float)effectDelayParams.time * MILISECOND_TIME);

    return confirmed[0].selection.length + lastVoiceIdx * timeInSamples;
}

void mtEffectDelay::calcLastVoiceIdx()
{
    uint8_t maxVoiceNumber = 0;
    float feedbackGain = 1.0;

    while (feedbackGain > 0.01f)
    {
        feedbackGain *= effectDelayParams.feedback;
        maxVoiceNumber++;
        if (maxVoiceNumber == MAX_DELAY_VOICE_NUMBER)
            break;
    }

    lastVoiceIdx = maxVoiceNumber - 1;
}

mtEffectDelay sampleEditorDelay;
//***********
