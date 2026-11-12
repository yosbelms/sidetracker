#pragma once

#include "DesktopPatch.h"

namespace polysynth
{
std::vector<uint8_t> encodeTrackerPatch(const DesktopPatch& patch);
bool decodeTrackerPatch(std::span<const uint8_t> data, DesktopPatch& patch);
}
