#include "Output.h"
#include "MasterSpectrum.h"
#include <algorithm>

void AudioOutputStream::begin(void)
{
}

AUDIO_CORE_RAM void AudioOutputStream::update()
{
    if (outBuffers[0] == nullptr)
    {
        return;
    }

    audio_block_t* blockLeft = receiveReadOnly(kOutChannels * source[0]); // input 0 = left channel
    audio_block_t* blockRight =
      receiveReadOnly(kOutChannels * source[1] + 1); // input 1 = right channel

    if (blockLeft)
    {
        memcpy(outBuffers[0], blockLeft->data, sizeof(blockLeft->data));
    }
    else
    {
        memset(outBuffers[0], 0, sizeof(blockLeft->data));
    }
    if (blockRight)
    {
        memcpy(outBuffers[1], blockRight->data, sizeof(blockRight->data));
    }
    else
    {
        memset(outBuffers[1], 0, sizeof(blockLeft->data));
    }

    uint32_t blockPeak = 0;
    for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i)
    {
        const int left = blockLeft ? blockLeft->data[i] : 0;
        const int right = blockRight ? blockRight->data[i] : 0;
        const uint32_t leftMagnitude = left < 0 ? -left : left;
        const uint32_t rightMagnitude = right < 0 ? -right : right;
        blockPeak = std::max(blockPeak, std::max(leftMagnitude, rightMagnitude));
    }
    if (blockPeak > peak)
        peak = blockPeak;

    if (!renderMode && source[0] == 0 && source[1] == 0)
        masterSpectrum::capture(blockLeft ? blockLeft->data : nullptr,
                                blockRight ? blockRight->data : nullptr,
                                AUDIO_BLOCK_SAMPLES);

    if (blockLeft)
        release(blockLeft);
    if (blockRight)
        release(blockRight);
}

uint32_t AudioOutputStream::readPeak()
{
    DISABLE_GLOBAL_INTERRUPTS();
    const uint32_t value = peak;
    peak = 0;
    ENABLE_GLOBAL_INTERRUPTS();
    return value;
}

void AudioOutputStream::selectSource(int channel, int idx)
{
    assert(channel < kOutChannels);
    assert(idx < kSources);

    source[channel] = idx;
}

AUDIO_CORE_FLASH void AudioOutputStream::setOutBuffers(void* left, void* right)
{
    outBuffers[0] = left;
    outBuffers[1] = right;
}
