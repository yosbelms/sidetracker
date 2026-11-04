#pragma once
#include "AudioStream.h"
#include <algorithm>
#include <array>
#include <iterator>

namespace ce::test
{
using Samples = std::array<int16_t, AUDIO_BLOCK_SAMPLES>;

class BlockSource : public AudioStream
{
  public:
    BlockSource() : AudioStream(0, nullptr) {}

    bool push(const Samples& samples)
    {
        audio_block_t* block = allocate();
        if (!block) return false;
        std::copy(samples.begin(), samples.end(), block->data);
        blockIndex = block->memory_pool_index;
        transmit(block);
        release(block);
        return true;
    }

    void update() override {}
    uint16_t blockIndex = 0;
};

class BlockSink : public AudioStream
{
  public:
    BlockSink() : AudioStream(1, queue) {}

    void update() override
    {
        audio_block_t* block = receiveReadOnly();
        received = block != nullptr;
        if (block)
        {
            std::copy(std::begin(block->data), std::end(block->data), samples.begin());
            blockIndex = block->memory_pool_index;
            release(block);
        }
    }

    Samples samples{};
    bool received = false;
    uint16_t blockIndex = 0;

  private:
    audio_block_t* queue[1];
};

inline audio_block_t audioPool[70];
}  // namespace ce::test
