#include "mtEffectFlanger.h"
#include "platform_math.h"
#include "string.h"
#include <algorithm>
#include <cr_section_macros.h>

mtEffectFlanger sampleEditorFlanger;

__NOINIT(EXTERNAL_RAM) int16_t flangerDelayBuffer[mtEffect::kChannels][FLANGER_BUFFER_LENGTH];

void mtEffectFlanger::setParameter(float& value, int idx)
{
    switch (idx)
    {
        case 0: delay_length = (int)value / 2; break;
        case 1: offset_percent = (int)value; break;
        case 2: delay_depth = (int)value; break;
        case 3: delay_rate_incr = (value * 2147483648.0) / 44117.64706; break;
        default: assert(false); break;
    }

    return;
}

bool mtEffectFlanger::startProcess()
{
    length = confirmed[0].selection.length;

    for (int channel = 0; channel < kChannels; channel++)
    {
        channelParams[channel].srcAddr = confirmed[channel].selection.addr;
        channelParams[channel].dstAddr = processed[channel].selection.addr;

        channelParams[channel].delay_offset_idx = (offset_percent * delay_length) / 100.0;
        channelParams[channel].l_delay_rate_index = 0;
        channelParams[channel].l_circ_idx = 0;
        channelParams[channel].l_delayline = &flangerDelayBuffer[channel][0];

        if (channelParams[channel].delay_offset_idx < -1)
        {
            channelParams[channel].delay_offset_idx = 0;
        }
        if (channelParams[channel].delay_offset_idx >= delay_length)
        {
            channelParams[channel].delay_offset_idx = delay_length - 1;
        }

        memset(channelParams[channel].l_delayline, 0, 2 * delay_length);
    }

    processedSamples = 0;
    state = true;
    return true;
}
int32_t mtEffectFlanger::processBlock()
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
bool mtEffectFlanger::getProcessState()
{
    return state;
}
uint32_t mtEffectFlanger::getExpectedProcessLength()
{
    return confirmed[0].selection.length;
}

void mtEffectFlanger::calculate(int16_t* src, int16_t* dst, int channel) // process 128 samples
{
    constexpr uint16_t PROCESSED_BLOCK_LENGTH = 128;
    constexpr int FLANGE_DELAY_PASSTHRU = 0;
    int idx;
    int16_t frac;
    int idx1;
    int16_t tempBuf[PROCESSED_BLOCK_LENGTH];

    memcpy(tempBuf, src, 2 * PROCESSED_BLOCK_LENGTH);

    if (channelParams[channel].delay_offset_idx == FLANGE_DELAY_PASSTHRU)
    {
        for (int i = 0; i < PROCESSED_BLOCK_LENGTH; i++)
        {
            channelParams[channel].l_circ_idx++;
            if (channelParams[channel].l_circ_idx >= delay_length)
                channelParams[channel].l_circ_idx = 0;
            channelParams[channel].l_delayline[channelParams[channel].l_circ_idx] = *src;
            *dst++ = *src++;
        }
        return;
    }
    for (int i = 0; i < PROCESSED_BLOCK_LENGTH; i++)
    {
        channelParams[channel].l_circ_idx++;

        if (channelParams[channel].l_circ_idx >= delay_length)
            channelParams[channel].l_circ_idx = 0;

        channelParams[channel].l_delayline[channelParams[channel].l_circ_idx] = *src++;
        frac = SIN_Q15((int16_t)((channelParams[channel].l_delay_rate_index >> 16) & 0x7fff));
        idx = (frac * delay_depth) >> 15;
        idx = channelParams[channel].l_circ_idx - (channelParams[channel].delay_offset_idx + idx);
        // Offset plus modulation can exceed one complete ring at UI extremes.
        idx %= delay_length;
        if (idx < 0) idx += delay_length;

        if (frac < 0)
            idx1 = idx - 1;
        else
            idx1 = idx + 1;

        if (idx1 < 0)
            idx1 += delay_length;
        if (idx1 >= delay_length)
            idx1 -= delay_length;

        frac = (channelParams[channel].l_delay_rate_index >> 1) & 0x7fff;
        frac = (((int)(channelParams[channel].l_delayline[idx1] -
                       channelParams[channel].l_delayline[idx]) *
                 frac) >>
                15);
        *dst++ = (channelParams[channel].l_delayline[channelParams[channel].l_circ_idx] +
                  channelParams[channel].l_delayline[idx] + frac) /
                 2;

        channelParams[channel].l_delay_rate_index += delay_rate_incr;
        if (channelParams[channel].l_delay_rate_index & 0x80000000)
            channelParams[channel].l_delay_rate_index &= 0x7fffffff;
    }
}
