#pragma once

#include "PolySynth/config.h"
#include "PolySynth/sdk/InstrumentType.h"

#include <string_view>

namespace polysynth
{
std::unique_ptr<InstrumentType> createMiniSynth();
std::unique_ptr<InstrumentType> createOberSynth();
void appendAnalogFactoryPatches(InstrumentTypeId type, std::vector<std::string_view>& names);
bool applyAnalogFactoryPatch(InstrumentTypeId type, std::string_view name, Patch& patch);
}
