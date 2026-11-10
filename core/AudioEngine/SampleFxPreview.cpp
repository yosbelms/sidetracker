#include "SampleFxPreview.h"
#include "interruptsControl.h"
#include "Platform.h"

void SampleFxPreview::start(samplefx::RealtimeChain* value)
{
    DISABLE_AUDIO_INTERRUPTS();
    chain = value;
    fadeRemaining=0;
    ENABLE_AUDIO_INTERRUPTS();
}
void SampleFxPreview::stop()
{
    DISABLE_AUDIO_INTERRUPTS();
    chain = nullptr;
    fadeRemaining=0;
    ENABLE_AUDIO_INTERRUPTS();
}
void SampleFxPreview::requestFadeOut()
{
    DISABLE_AUDIO_INTERRUPTS();
    if(chain && !fadeRemaining) fadeRemaining=512;
    ENABLE_AUDIO_INTERRUPTS();
}
#ifndef TRACKER_DESKTOP
void SampleFxPreview::finishAudioBlock(unsigned callbackUs)
{
    if (measuredChain)
    {
        measuredChain->reportDuration(chainUs, callbackUs);
        measuredChain = nullptr;
    }
}
#endif

AUDIO_CORE_RAM void SampleFxPreview::update()
{
    auto* current = chain;
    if (!current) return;
    audio_block_t* left = allocate();
    if (!left) return;
    audio_block_t* right = allocate();
    if (!right) { release(left); return; }
#ifndef TRACKER_DESKTOP
    const unsigned cyclesPerUs = current->usesNative() && timer ? timer->clockCyclesPerUs() : 0;
    const uint32_t before = cyclesPerUs ? timer->clockCycles() : 0;
#endif
    const unsigned count = current->process(left->data, right->data, AUDIO_BLOCK_SAMPLES);
#ifndef TRACKER_DESKTOP
    if (cyclesPerUs)
    {
        chainUs = uint32_t(timer->clockCycles() - before) / cyclesPerUs;
        measuredChain = current;
    }
#endif
    bool faded=false;
    if(fadeRemaining) for(unsigned i=0;i<count;++i) {
        left->data[i]=int32_t(left->data[i])*fadeRemaining/512;
        right->data[i]=int32_t(right->data[i])*fadeRemaining/512;
        if(fadeRemaining) --fadeRemaining;
        if(!fadeRemaining) faded=true;
    }
    memset(left->data + count, 0, (AUDIO_BLOCK_SAMPLES - count) * sizeof(int16_t));
    memset(right->data + count, 0, (AUDIO_BLOCK_SAMPLES - count) * sizeof(int16_t));
    if (count) { transmit(left, 0); transmit(right, 1); }
    release(left); release(right);
    if (current->finished() || faded) chain = nullptr;
}
