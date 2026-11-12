#pragma once

#include <array>
#include <cstdint>

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

constexpr Glyph mini[] = {m, i, n, i};
constexpr Glyph ober[] = {o, b, e, r};
constexpr auto miniSynthSmall = badge<80, 28, 2>(mini);
constexpr auto miniSynthMedium = badge<108, 40, 3>(mini);
constexpr auto miniOber = badge<80, 28, 2>(ober);
constexpr auto mediumOber = badge<108, 40, 3>(ober);
}
