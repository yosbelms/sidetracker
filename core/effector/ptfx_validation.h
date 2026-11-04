#pragma once
#include "tracker_plugin.h"
#include <stddef.h>
namespace ptfx
{
bool validateHeader(const PtfxHeader&, size_t fileSize, uint32_t codeCapacity, uint32_t scratchCapacity);
bool validateStrings(const PtfxHeader&, const uint8_t* code);
}
