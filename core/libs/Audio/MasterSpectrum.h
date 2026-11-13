#pragma once

#include <cstdint>

namespace masterSpectrum
{
constexpr unsigned kPoints = 48;

void setEnabled(bool enabled);
void capture(const int16_t* left, const int16_t* right, unsigned frames);
bool read(uint8_t (&levels)[kPoints]);
}
