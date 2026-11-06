#include "SampleFxPreview.h"
#include "interruptsControl.h"

void SampleFxPreview::start(samplefx::RealtimeChain* value)
{
    DISABLE_AUDIO_INTERRUPTS();
    chain = value;
    ENABLE_AUDIO_INTERRUPTS();
}
void SampleFxPreview::stop()
{
    DISABLE_AUDIO_INTERRUPTS();
    chain = nullptr;
    ENABLE_AUDIO_INTERRUPTS();
}
void SampleFxPreview::update()
{
    auto* current = chain;
    if (!current) return;
    audio_block_t* left = allocate();
    if (!left) return;
    audio_block_t* right = allocate();
    if (!right) { release(left); return; }
    const unsigned count = current->process(left->data, right->data, AUDIO_BLOCK_SAMPLES);
    memset(left->data + count, 0, (AUDIO_BLOCK_SAMPLES - count) * sizeof(int16_t));
    memset(right->data + count, 0, (AUDIO_BLOCK_SAMPLES - count) * sizeof(int16_t));
    if (count) { transmit(left, 0); transmit(right, 1); }
    release(left); release(right);
    if (current->finished()) chain = nullptr;
}
