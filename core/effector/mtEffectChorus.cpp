#include "mtEffectChorus.h"

#include "string.h"
#include <algorithm>
#include <cr_section_macros.h>

mtEffectChorus sampleEditorChorus;

__NOINIT(EXTERNAL_RAM) int16_t chorusDelayBuffer[mtEffect::kChannels][CHORUS_BUFFER_LENGTH];

void mtEffectChorus::setParameter(float& value, int idx)
{

    switch (idx)
    {
        case 0: delay_length = (int)value / 2; break;
        case 1: num_chorus = (int)value; break;
        default: assert(false); break;
    }

    return;
}

bool mtEffectChorus::startProcess()
{
    length = confirmed[0].selection.length;

    for (int channel = 0; channel < kChannels; channel++)
    {
        channelParams[channel].srcAddr = confirmed[channel].selection.addr;
        channelParams[channel].dstAddr = processed[channel].selection.addr;

        channelParams[channel].l_delayline = &chorusDelayBuffer[channel][0];
        channelParams[channel].l_circ_idx = 0;
        memset(channelParams[channel].l_delayline, 0, 2 * delay_length);
    }

    processedSamples = 0;
    state = true;
    return true;
}
int32_t mtEffectChorus::processBlock()
{
    if (!state) return -1;
    const uint32_t count = std::min<uint32_t>(8192, length - processedSamples);
    for (int channel = 0; channel < kChannels; ++channel)
    {
        uint32_t offset = 0;
        for (; offset + 128 <= count; offset += 128)
            calculate(channelParams[channel].srcAddr + offset, channelParams[channel].dstAddr + offset, channel);
        if (offset < count)
        {
            int16_t input[128] = {}, output[128];
            memcpy(input, channelParams[channel].srcAddr + offset, (count - offset) * sizeof(int16_t));
            calculate(input, output, channel);
            memcpy(channelParams[channel].dstAddr + offset, output, (count - offset) * sizeof(int16_t));
        }
        channelParams[channel].srcAddr += count;
        channelParams[channel].dstAddr += count;
    }
    processedSamples += count;
    if (processedSamples == length)
    {
        state = false;
        for (int channel = 0; channel < kChannels; ++channel)
        {
            processed[channel].selection.length = length;
            processed[channel].area.length = confirmed[channel].area.length;
        }
    }
    return count;
}
bool mtEffectChorus::getProcessState()
{
    return state;
}
uint32_t mtEffectChorus::getExpectedProcessLength()
{
    return confirmed[0].selection.length;
}

void mtEffectChorus::calculate(int16_t* src, int16_t* dst, int channel) // process 128 samples
{
    constexpr uint16_t PROCESSED_BLOCK_LENGTH = 128;
    int sum;
    int c_idx;

    if (channelParams[channel].l_delayline == nullptr)
        return;

    if (num_chorus <= 1)
    {

        for (int i = 0; i < PROCESSED_BLOCK_LENGTH; i++)
        {
            channelParams[channel].l_circ_idx++;
            if (channelParams[channel].l_circ_idx >= delay_length)
            {
                channelParams[channel].l_circ_idx = 0;
            }
            channelParams[channel].l_delayline[channelParams[channel].l_circ_idx] = *src++;
        }

        src -= PROCESSED_BLOCK_LENGTH;

        for (int i = 0; i < PROCESSED_BLOCK_LENGTH; i++)
        {
            *dst++ = *src++;
        }

        return;
    }

    uint32_t tmp = delay_length / (num_chorus - 1) - 1;

    for (int i = 0; i < PROCESSED_BLOCK_LENGTH; i++)
    {
        channelParams[channel].l_circ_idx++;

        if (channelParams[channel].l_circ_idx >= delay_length)
        {
            channelParams[channel].l_circ_idx = 0;
        }

        channelParams[channel].l_delayline[channelParams[channel].l_circ_idx] = *src++;
        sum = 0;
        c_idx = channelParams[channel].l_circ_idx;

        for (int k = 0; k < num_chorus; k++)
        {
            sum += channelParams[channel].l_delayline[c_idx];
            if (num_chorus > 1)
                c_idx -= tmp;

            if (c_idx < 0)
                c_idx += delay_length;
        }

        *dst++ = sum / num_chorus;
    }
}
