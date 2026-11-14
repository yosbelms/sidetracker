#pragma once

#include "mtStructs.h"
#include <cmath>

namespace masterFaderPosition
{
constexpr int kUnity = DEFAULT_DRY_MIX_VOLUME;
static_assert(kUnity == DEFAULT_DELAY_VOLUME && kUnity == DEFAULT_REVERB_VOLUME &&
              kUnity == DEFAULT_LINE_IN_VOLUME);

constexpr int mix(unsigned percent) { return (percent * kUnity + 50) / 100; }

inline int synth(float db, float minimum, float maximum)
{
    return db <= 0 ? std::lround((db - minimum) * kUnity / -minimum)
                   : kUnity + std::lround(db * (100 - kUnity) / maximum);
}
}
