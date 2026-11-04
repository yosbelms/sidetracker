#pragma once
#include <cstdint>
#include <vector>

namespace ce::test
{
inline void append16(std::vector<uint8_t>& data, uint16_t value)
{
    data.push_back(uint8_t(value));
    data.push_back(uint8_t(value >> 8));
}
inline void append32(std::vector<uint8_t>& data, uint32_t value)
{
    append16(data, uint16_t(value));
    append16(data, uint16_t(value >> 16));
}
inline std::vector<uint8_t> makeWav(const std::vector<int16_t>& samples, uint16_t channels = 1)
{
    std::vector<uint8_t> data;
    append32(data, 0x46464952);
    append32(data, 36 + uint32_t(samples.size() * 2));
    append32(data, 0x45564157);
    append32(data, 0x20746d66);
    append32(data, 16);
    append16(data, 1);
    append16(data, channels);
    append32(data, 44100);
    append32(data, 44100 * channels * 2);
    append16(data, channels * 2);
    append16(data, 16);
    append32(data, 0x61746164);
    append32(data, uint32_t(samples.size() * 2));
    for (int16_t sample : samples) append16(data, uint16_t(sample));
    return data;
}
}  // namespace ce::test
