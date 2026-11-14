#pragma once

#include <algorithm>
#include <cstdint>

namespace masterSaturationSettings
{
constexpr int kDriveLimit = 24;
constexpr uint8_t kDriveTag = 0xa6;

inline int8_t readDrive(const uint8_t (&reserved)[7])
{
    if (reserved[0] != kDriveTag || reserved[1] > 2 * kDriveLimit) return 0;
    return static_cast<int8_t>(int(reserved[1]) - kDriveLimit);
}

inline void writeDrive(uint8_t (&reserved)[7], int drive)
{
    reserved[0] = kDriveTag;
    reserved[1] = static_cast<uint8_t>(std::clamp(drive, -kDriveLimit, kDriveLimit) + kDriveLimit);
}
}
