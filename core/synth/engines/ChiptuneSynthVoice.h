#pragma once

#include "PolySynth/sdk/InstrumentType.h"
#include <memory>

namespace polysynth
{
std::unique_ptr<InstrumentType> createChiptuneSynth();
void randomizeChiptunePatch(Patch& patch, uint32_t seed);
} // namespace polysynth
