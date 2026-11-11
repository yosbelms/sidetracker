#pragma once

#include "SampleFxChain.h"

namespace samplefx
{
constexpr unsigned kMaxPresetNameBytes = 43;
constexpr size_t kPresetFileBytes = 16 + (kMaxStages + 1) * 100;

bool encodePreset(const ChainDefinition& chain, uint8_t* bytes, size_t size);
bool decodePreset(const uint8_t* bytes, size_t size, ChainDefinition& chain);
}
