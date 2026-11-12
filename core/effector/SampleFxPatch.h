#pragma once

#include "SampleFxChain.h"

namespace samplefx
{
constexpr unsigned kMaxPatchNameBytes = 43;
constexpr size_t kPatchFileBytes = 16 + (kMaxStages + 1) * 100;

bool encodePatch(const ChainDefinition& chain, uint8_t* bytes, size_t size);
bool decodePatch(const uint8_t* bytes, size_t size, ChainDefinition& chain);
}
