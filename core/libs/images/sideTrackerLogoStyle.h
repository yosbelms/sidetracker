#pragma once

#include <cstdint>

namespace sideTrackerLogo
{
inline constexpr uint32_t kBackground = 0x313131;
inline constexpr uint32_t kStripeColor = 0x000000;
// Heights relative to a center of 0 and a top of 10.
inline constexpr int kStripeHeights[] = {0, -2, -5, -9};
}
