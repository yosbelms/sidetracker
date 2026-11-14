#pragma once

#include <array>
#include <cstdint>

#include "chip_med_108x40_L2.h"
#include "chip_mini_nomark_80x28_L2.h"
#include "sideTrackerLogoStyle.h"

namespace synthbadges
{
using Glyph = std::array<uint8_t, 7>;
constexpr Glyph m = {17, 27, 21, 21, 17, 17, 17};
constexpr Glyph i = {31, 4, 4, 4, 4, 4, 31};
constexpr Glyph n = {17, 25, 25, 21, 19, 19, 17};
constexpr Glyph o = {14, 17, 17, 17, 17, 17, 14};
constexpr Glyph b = {30, 17, 17, 30, 17, 17, 30};
constexpr Glyph e = {31, 16, 16, 30, 16, 16, 31};
constexpr Glyph r = {30, 17, 17, 30, 20, 18, 17};

template<unsigned Width, unsigned Height, unsigned Scale, unsigned Count>
constexpr auto badge(const Glyph (&letters)[Count])
{
    std::array<uint8_t, Width * Height / 4> bitmap{};
    constexpr unsigned left = (Width - (6 * Count - 1) * Scale) / 2;
    constexpr unsigned top = (Height - 7 * Scale) / 2;
    for (unsigned y = 0; y < 7 * Scale; ++y)
        for (unsigned x = 0; x < (6 * Count - 1) * Scale; ++x)
        {
            const unsigned column = x / Scale;
            if (column % 6 == 5 || !(letters[column / 6][y / Scale] & (16 >> (column % 6))))
                continue;
            const unsigned pixel = (y + top) * Width + x + left;
            bitmap[pixel / 4] |= 3 << (6 - 2 * (pixel % 4));
        }
    return bitmap;
}

// Mirror CHIP's clean right edge to keep its antialiased corners while
// dropping the lettering and any marks at the left of the source bitmap.
// Each pixel is drawn by one mask so the stripes preserve edge opacity.
template<unsigned Width, unsigned Height>
constexpr auto background(const uint8_t (&chip)[Width * Height / 4])
{
    constexpr unsigned maskBytes = Width * Height / 4;
    std::array<uint8_t, 2 * maskBytes> masks{};
    for (unsigned y = 0; y < Height; ++y)
    {
        bool striped = false;
        constexpr int halfHeight = Height / 2;
        for (int height : sideTrackerLogo::kStripeHeights)
            striped |= int(y) == halfHeight - height * halfHeight / 10;

        const auto setPixel = [&](unsigned x, unsigned level) {
            const unsigned pixel = y * Width + x;
            const unsigned packed = level << (6 - 2 * (pixel % 4));
            const unsigned layer = striped ? maskBytes : 0;
            masks[layer + pixel / 4] |= packed;
        };

        bool inside = false;
        for (unsigned offset = 0; offset < Width / 2; ++offset)
        {
            const unsigned x = Width - 1 - offset;
            const unsigned pixel = y * Width + x;
            const unsigned level = inside ? 3 : (chip[pixel / 4] >> (6 - 2 * (pixel % 4))) & 3;
            setPixel(x, level);
            setPixel(offset, level);
            inside |= level == 3;
        }
    }
    return masks;
}

constexpr auto smallBackground = background<80, 28>(chip_mini_nomark_80x28_L2);
constexpr auto mediumBackground = background<108, 40>(chip_med_108x40_L2);

constexpr Glyph mini[] = {m, i, n, i};
constexpr Glyph ober[] = {o, b, e, r};
constexpr auto miniSynthSmall = badge<80, 28, 2>(mini);
constexpr auto miniSynthMedium = badge<108, 40, 3>(mini);
constexpr auto miniOber = badge<80, 28, 2>(ober);
constexpr auto mediumOber = badge<108, 40, 3>(ober);
}
