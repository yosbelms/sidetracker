#include "SoftwareDisplay.h"
#include "FT81x.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numbers>
#include <stdexcept>
#include <string>

namespace
{
int signedVertex(uint32_t value)
{
    const int coordinate = value & 0x7fff;
    return coordinate >= 0x4000 ? coordinate - 0x8000 : coordinate;
}
}

SoftwareDisplay::SoftwareDisplay(SDL_Renderer& renderer)
    : renderer(renderer), ram(1024 * 1024)
{
    SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
}

void SoftwareDisplay::writeDataRAMG(const uint8_t* data, uint32_t size, uint32_t address)
{
    if (address > ram.size() || size > ram.size() - address)
        throw std::out_of_range("FT812 RAM_G write");
    std::memcpy(ram.data() + address, data, size);
}

void SoftwareDisplay::writeFontRAMG(const uint8_t* data, uint32_t size, uint32_t address)
{
    writeDataRAMG(data, size, address);
}

void SoftwareDisplay::readDataRAMG(uint8_t* data, uint32_t size, uint32_t address)
{
    if (address > ram.size() || size > ram.size() - address)
        throw std::out_of_range("FT812 RAM_G read");
    std::memcpy(data, ram.data() + address, size);
}

uint8_t SoftwareDisplay::sendString(const char* string, uint32_t length)
{
    const size_t size = length ? std::min<size_t>(length, std::strlen(string)) : std::strlen(string);
    const size_t words = (size + 4) / 4;
    const size_t start = commands.size();
    commands.resize(start + words);
    for (size_t i = 0; i < size; ++i)
        commands[start + i / 4] |= uint32_t(uint8_t(string[i])) << (8 * (i % 4));
    return words * 4;
}

void SoftwareDisplay::color(uint32_t rgb, uint8_t alpha)
{
    SDL_SetRenderDrawColor(&renderer, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255, alpha);
}

void SoftwareDisplay::setClip()
{
    SDL_RenderSetClipRect(&renderer, &state.clip);
}

void SoftwareDisplay::rect(int x0, int y0, int x1, int y1)
{
    const SDL_Rect area{std::min(x0, x1), std::min(y0, y1),
                        std::abs(x1 - x0) + 1, std::abs(y1 - y0) + 1};
    SDL_RenderFillRect(&renderer, &area);
}

void SoftwareDisplay::circle(int x, int y, int radius)
{
    for (int dy = -radius; dy <= radius; ++dy)
    {
        const int dx = std::sqrt(radius * radius - dy * dy);
        SDL_RenderDrawLine(&renderer, x - dx, y + dy, x + dx, y + dy);
    }
}

void SoftwareDisplay::BitmapTransform::translate(double x, double y)
{
    c += a * x + b * y;
    f += d * x + e * y;
}

void SoftwareDisplay::BitmapTransform::rotate(uint16_t angle)
{
    // The sampler needs the inverse of the requested visual rotation.
    const double radians = -angle * (2 * std::numbers::pi / 65536);
    const double cosine = std::cos(radians), sine = std::sin(radians);
    const double nextA = a * cosine + b * sine;
    const double nextD = d * cosine + e * sine;
    b = b * cosine - a * sine;
    e = e * cosine - d * sine;
    a = nextA;
    d = nextD;
}

void SoftwareDisplay::bitmap(int x, int y, int handle, int cell)
{
    const auto& b = bitmaps[handle];
    if (b.format != L4 && b.format != L8)
        return;
    if (b.font)
    {
        if (cell >= 128 || !ram[b.metrics + cell])
            return;
        cell -= b.firstChar;
    }
    if (cell < 0)
        return;

    const size_t start = b.source + size_t(cell) * b.stride * b.height;
    if (start + size_t(b.stride) * b.height > ram.size())
        return;
    const int sourceWidth = b.stride * (b.format == L4 ? 2 : 1);
    const auto& transform = state.transform;
    // FT812 transforms map output pixel centres back into the source bitmap.
    for (int dy = 0; dy < b.drawHeight; ++dy)
    {
        for (int dx = 0; dx < b.drawWidth; ++dx)
        {
            const double px = dx + 0.5, py = dy + 0.5;
            const int sx = std::floor(transform.a * px + transform.b * py + transform.c);
            const int sy = std::floor(transform.d * px + transform.e * py + transform.f);
            if (sx < 0 || sx >= sourceWidth || sy < 0 || sy >= b.height)
                continue;
            const auto* row = ram.data() + start + size_t(sy) * b.stride;
            const int level = b.format == L8 ? row[sx] :
                17 * (sx % 2 ? row[sx / 2] & 15 : row[sx / 2] >> 4);
            if (level)
            {
                color(state.color, level * state.alpha / 255);
                SDL_RenderDrawPoint(&renderer, x + dx, y + dy);
            }
        }
    }
}

void SoftwareDisplay::text(int x, int y, int handle, int options, const char* value)
{
    if (handle < 0 || handle >= int(bitmaps.size()) || !bitmaps[handle].font)
        return;
    const auto& b = bitmaps[handle];
    const auto advance = [&](unsigned char ch) { return ch < 128 ? ram[b.metrics + ch] : 0; };
    int w = 0;
    for (const char* ch = value; *ch; ++ch)
        w += advance(*ch);
    if (options & OPT_CENTERX)
        x -= w / 2;
    else if (options & OPT_RIGHTX)
        x -= w;
    if (options & OPT_CENTERY)
        y -= b.height / 2;
    for (const char* ch = value; *ch; ++ch)
    {
        bitmap(x, y, handle, uint8_t(*ch));
        x += advance(*ch);
    }
}

void SoftwareDisplay::vertex(int x, int y, int handle, int cell)
{
    color(state.color, state.alpha);
    switch (state.prim)
    {
        case BITMAPS: bitmap(x, y, handle, cell); break;
        case 2: /* POINTS */ circle(x, y, state.pointSize / 16); break;
        case RECTS:
            if (hasVertex)
            {
                const int inset = std::max(0, state.lineWidth / 16 - 1);
                rect(std::min(previousX, x) - inset, std::min(previousY, y) - inset,
                     std::max(previousX, x) + inset, std::max(previousY, y) + inset);
            }
            break;
        case LINES:
        case LINE_STRIP:
            if (hasVertex)
                SDL_RenderDrawLine(&renderer, previousX, previousY, x, y);
            break;
    }
    previousX = x;
    previousY = y;
    hasVertex = state.prim == LINE_STRIP || !hasVertex;
}

void SoftwareDisplay::displayWord(uint32_t word)
{
    if (word >> 31)
    {
        vertex((word >> 21) & 511, (word >> 12) & 511, (word >> 7) & 31, word & 127);
        return;
    }
    if (word >> 30)
    {
        vertex(signedVertex(word >> 15) / (1 << state.fraction),
               signedVertex(word) / (1 << state.fraction), state.handle, state.cell);
        return;
    }

    auto& b = bitmaps[state.handle];
    switch (word >> 24)
    {
        case 0x00: break; // DISPLAY
        case 0x01: b.source = word & 0x3fffff; b.font = false; break;
        case 0x02: state.clear = word & 0xffffff; break;
        case 0x04: state.color = word & 0xffffff; break;
        case 0x05: state.handle = word & 31; break;
        case 0x06: state.cell = word & 127; break;
        case 0x07:
            b.format = (word >> 19) & 31;
            b.stride = (word >> 9) & 1023;
            b.height = word & 511;
            break;
        case 0x08: b.drawWidth = (word >> 9) & 511; b.drawHeight = word & 511; break;
        case 0x0b: // BLEND_FUNC
            switch (word & 7)
            {
                case ZERO: state.blend = SDL_BLENDMODE_NONE; break;
                case ONE: state.blend = SDL_BLENDMODE_ADD; break;
                default: state.blend = SDL_BLENDMODE_BLEND; break;
            }
            SDL_SetRenderDrawBlendMode(&renderer, state.blend);
            break;
        case 0x0d: state.pointSize = word & 8191; break;
        case 0x0e: state.lineWidth = word & 4095; break;
        case 0x10: state.alpha = word & 255; break;
        case 0x1b: state.clip.x = (word >> 11) & 2047; state.clip.y = word & 2047; setClip(); break;
        case 0x1c: state.clip.w = (word >> 12) & 4095; state.clip.h = word & 4095; setClip(); break;
        case 0x1f: state.prim = word & 15; hasVertex = false; break;
        case 0x21: state.prim = 0; hasVertex = false; break;
        case 0x22: contexts.push_back(state); break;
        case 0x23:
            if (!contexts.empty())
            {
                state = contexts.back();
                contexts.pop_back();
                SDL_SetRenderDrawBlendMode(&renderer, state.blend);
                setClip();
            }
            break;
        case 0x26:
            if (word & 4)
            {
                color(state.clear);
                SDL_RenderFillRect(&renderer, &state.clip);
            }
            break;
        case 0x27: state.fraction = word & 7; break;
        case 0x28: b.stride |= ((word >> 2) & 3) << 10; b.height |= (word & 3) << 9; break;
        case 0x29: b.drawWidth |= ((word >> 2) & 3) << 9; b.drawHeight |= (word & 3) << 9; break;
        default: break; // Other graphics state (stencil, tags and transforms) is not modeled yet.

    }
}

void SoftwareDisplay::endCoProList()
{
    if (commands.empty())
        return;
    if (commands[0] == CMD_MEMCPY && commands.size() >= 4 && commands[2] == RAM_DL)
    {
        if (commands[3] <= list.size() * sizeof(uint32_t))
            writeDataRAMG(reinterpret_cast<const uint8_t*>(list.data()), commands[3], commands[1]);
        return;
    }
    if (commands[0] == CMD_DLSTART &&
        std::find(commands.begin(), commands.end(), CMD_SWAP) == commands.end())
    {
        list.assign(commands.begin() + 1, commands.end());
        return;
    }

    size_t i = 0;
    const auto next = [&] { return commands.at(i++); };
    while (i < commands.size())
    {
        const auto word = next();
        switch (word)
        {
            case CMD_DLSTART:
                state = {};
                contexts.clear();
                hasVertex = false;
                setClip();
                SDL_SetRenderDrawBlendMode(&renderer, SDL_BLENDMODE_BLEND);
                break;
            case CMD_APPEND:
            {
                const auto addr = next();
                const auto bytes = next();
                if (addr <= ram.size() && bytes <= ram.size() - addr && bytes % 4 == 0)
                {
                    std::vector<uint32_t> fragment(bytes / 4);
                    std::memcpy(fragment.data(), ram.data() + addr, bytes);
                    commands.insert(commands.begin() + i, fragment.begin(), fragment.end());
                }
                break;
            }
            case CMD_NUMBER:
            {
                const auto xy = next();
                const auto args = next();
                const auto number = next();
                char value[48];
                const int digits = (args >> 16) & 31;
                if ((args >> 16) & OPT_SIGNED)
                    std::snprintf(value, sizeof(value), "%0*d", digits, int32_t(number));
                else
                    std::snprintf(value, sizeof(value), "%0*u", digits, number);
                text(int16_t(xy), int16_t(xy >> 16), args & 65535, args >> 16, value);
                break;
            }
            case CMD_GRADIENT:
            {
                const auto a = next();
                const auto rgb0 = next();
                const auto b = next();
                const auto rgb1 = next();
                const int y0 = int16_t(a >> 16), y1 = int16_t(b >> 16);
                for (int y = state.clip.y; y < std::min(height, state.clip.y + state.clip.h); ++y)
                {
                    const float t = y0 == y1 ? 0 : std::clamp(float(y - y0) / (y1 - y0), 0.f, 1.f);
                    uint32_t rgb = 0;
                    for (int shift = 0; shift < 24; shift += 8)
                        rgb |= uint32_t(((rgb0 >> shift) & 255) * (1 - t) + ((rgb1 >> shift) & 255) * t) << shift;
                    color(rgb);
                    SDL_RenderDrawLine(&renderer, state.clip.x, y, state.clip.x + state.clip.w - 1, y);
                }
                break;
            }
            case CMD_SETBASE: next(); break;
            case CMD_SETBITMAP:
            {
                auto& b = bitmaps[state.handle];
                b.source = next();
                const auto formatWidth = next(), bitmapHeight = next();
                b.format = formatWidth & 65535;
                b.drawWidth = formatWidth >> 16;
                b.height = b.drawHeight = bitmapHeight & 65535;
                b.stride = b.format == L4 ? (b.drawWidth + 1) / 2 : b.drawWidth;
                b.font = false;
                break;
            }
            case CMD_LOADIDENTITY: matrix = {}; break;
            case CMD_SETMATRIX: state.transform = matrix; break;
            case CMD_ROTATE: matrix.rotate(next()); break;
            case CMD_TRANSLATE:
            {
                const auto x = int32_t(next());
                const auto y = int32_t(next());
                matrix.translate(x / 65536.0, y / 65536.0);
                break;
            }
            case CMD_SWAP: SDL_RenderPresent(&renderer); break;
            case CMD_SETFONT2:
            {
                auto& b = bitmaps.at(next());
                b.metrics = next();
                b.firstChar = next();
                b.font = b.metrics <= ram.size() - 148;
                break;
            }
            case CMD_TEXT:
            {
                const auto xy = next();
                const auto args = next();
                std::string value;
                bool done = false;
                while (!done)
                {
                    const auto chars = next();
                    for (int shift = 0; shift < 32; shift += 8)
                    {
                        const auto ch = char(chars >> shift);
                        if (ch == 0)
                        {
                            done = true;
                            break;
                        }
                        value += ch;
                    }
                }
                text(int16_t(xy), int16_t(xy >> 16), args & 65535, args >> 16, value.c_str());
                break;
            }
            default: displayWord(word); break;
        }
    }
}
