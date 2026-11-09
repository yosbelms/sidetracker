#pragma once
#include "Palette.h"
namespace samplefx { namespace palette {
struct ProfileResult {
    uint32_t maximumTicks=0;
    uint64_t totalTicks=0;
    unsigned blocks=0;
    uint32_t signature=2166136261u;
    bool success=false;
};
// On device pass Timer::clockCycles, stopped playback and a private arena.
// Host clocks and QEMU instruction clocks must be labelled as such by callers.
ProfileResult profile(Node, unsigned channels, Memory, void* clockContext,
                      uint32_t (*clock)(void*));
} }
