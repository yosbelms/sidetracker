
#include "FT812.h"

#include "display.h"
#include "displayControls.h"
#include "startScreenControl.h"

namespace
{
struct Glyph
{
    int width;
    uint8_t rows[7];
};

constexpr Glyph e{5, {0, 0, 0b01110, 0b10001, 0b11111, 0b10000, 0b01111}};
constexpr Glyph r{4, {0, 0, 0b1011, 0b1100, 0b1000, 0b1000, 0b1000}};

constexpr Glyph wordmark[] = {
    {5, {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}}, // S
    {1, {1, 0, 1, 1, 1, 1, 1}},                                       // i
    {5, {0b00001, 0b00001, 0b01111, 0b10001, 0b10001, 0b10001, 0b01111}}, // d
    e,
    {5, {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}}, // T
    r,
    {5, {0, 0, 0b01110, 0b00001, 0b01111, 0b10001, 0b01111}}, // a
    {4, {0, 0, 0b0111, 0b1000, 0b1000, 0b1000, 0b0111}},      // c
    {5, {0b10000, 0b10000, 0b10001, 0b10010, 0b11100, 0b10010, 0b10001}}, // k
    e,
    r,
};

void logoRect(int x, int y, int w, int h)
{
    API_VERTEX2F(x, y);
    API_VERTEX2F(x + w - 1, y + h - 1);
}

void drawWordmark(int center, int top, int pixel)
{
    int width = -1;
    for (const auto& glyph : wordmark)
        width += glyph.width + 1;

    API_BEGIN(RECTS);
    int x = center - width * pixel / 2;
    for (const auto& glyph : wordmark)
    {
        for (int row = 0; row < 7; ++row)
        {
            int run = -1;
            for (int col = 0; col <= glyph.width; ++col)
            {
                const bool filled = col < glyph.width && (glyph.rows[row] & (1 << (glyph.width - col - 1)));
                if (filled && run < 0)
                    run = col;
                else if (!filled && run >= 0)
                {
                    logoRect(x + run * pixel, top + row * pixel, (col - run) * pixel, pixel);
                    run = -1;
                }
            }
        }
        x += (glyph.width + 1) * pixel;
    }

    API_END();
}
}

void drawSideTrackerLogo()
{
    constexpr int center = 400;
    constexpr int badgeY = 139;
    constexpr int badgeRadius = 51;
    constexpr uint32_t logoGray = 0x313131; // Solid fill in both original startup bitmaps.

    API_SAVE_CONTEXT();
    API_VERTEX_FORMAT(0);
    API_LINE_WIDTH(16);

    API_COLOR(logoGray);
    API_POINT_SIZE(16 * badgeRadius);
    API_BEGIN(FTPOINTS);
    API_VERTEX2F(center, badgeY);
    API_END();

    // Logo coordinates use the circle center as Y=0 and the top as Y=10.
    constexpr int lineHeights[] = {0, -2, -5, -9};
    constexpr int lineHalfThickness = 1;
    API_COLOR(0x000000);
    API_LINE_WIDTH(8);
    API_BEGIN(LINES);
    for (const int height : lineHeights)
    {
        const int lineY = badgeY - height * badgeRadius / 10;
        for (int y = lineY - lineHalfThickness; y <= lineY + lineHalfThickness; ++y)
        {
            const int offset = y - badgeY;
            const int chordRadiusSquared = badgeRadius * badgeRadius - offset * offset;
            // Round outward to reach the visible edge of the antialiased circle.
            int halfWidth = badgeRadius;
            while (halfWidth > 0 && (halfWidth - 1) * (halfWidth - 1) >= chordRadiusSquared)
                --halfWidth;

            API_VERTEX2F(center - halfWidth, y);
            API_VERTEX2F(center + halfWidth, y);
        }
    }
    API_END();

    const auto& monogram = bitmaps[displaySideTrackerMonogram];
    const int monogramX = center - monogram.width / 2;
    const int monogramY = badgeY - monogram.height / 2;
    API_BITMAP_HANDLE(0);
    API_BITMAP_SOURCE(monogram.address);
    API_BITMAP_LAYOUT(monogram.format, monogram.linestride, monogram.height);
    API_BITMAP_SIZE(NEAREST, BORDER, BORDER, monogram.width, monogram.height);
    API_BEGIN(BITMAPS);
    API_VERTEX2F(monogramX, monogramY);
    API_END();

    API_LINE_WIDTH(16);
    API_COLOR(logoGray);
    drawWordmark(center, 213, 7);
    API_RESTORE_CONTEXT();
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
cStartScreen::cStartScreen(strControlProperties* properties)
  : cDisplayControl(controlIdStartScreen, 1)
{
    colorsCount = 2;
    colors = display.getTheme().startScreenColors;

    if (properties == nullptr)
    {
        posX = 0;
        posY = 0;
        text = nullptr;
        data = nullptr;
        value = 0;
        width = 0;
        height = 0;
        style = 0;
        return;
    }

    colors = (properties->colors == nullptr) ? colors : properties->colors;

    posX = properties->x;
    posY = properties->y;

    text = properties->text;
    value = properties->value;

    width = properties->w;
    height = properties->h;

    data = (char*)(properties->data);

    setStyle(properties->style);
}

cStartScreen::~cStartScreen()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cStartScreen::setStyle(uint32_t style)
{
    this->style = style;

    textStyle = (style & controlStyleCenterX ? OPT_CENTERX : 0) |
                (style & controlStyleCenterY ? OPT_CENTERY : 0) |
                (style & controlStyleRightX ? OPT_RIGHTX : 0);

    textFont = FONT_INDEX_FROM_STYLE;
    textFont = (textFont >= 0) ? textFont : 0;
    textFont = fonts[textFont].handle;
}

void cStartScreen::setText(const char* text)
{
    this->text = text;
}

void cStartScreen::setValue(int value)
{
    this->value = value;
}

void cStartScreen::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cStartScreen::setData(void* data)
{
    data = (char*)(data);
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cStartScreen::update()
{
    API_SAVE_CONTEXT();
    drawSideTrackerLogo();

    uint16_t barWidth = (5 * width) / 7;
    uint16_t barX = posX + (width / 7);

    uint16_t barHeight = 3;
    uint16_t barY = posY + ((3 * height) / 6);

    uint16_t textX = posX + (width / 2);
    uint16_t textY = posY + ((2 * height) / 7);

    if (value < 0)
        value = 0;
    else if (value > 100)
        value = 100;

    uint16_t barFillY = (barWidth * value) / 100;

    API_BLEND_FUNC(SRC_ALPHA, ZERO);

    API_COLOR(colors[1]);

    API_LINE_WIDTH(16);
    API_BEGIN(RECTS);
    API_VERTEX2F(barX + 1, barY + 1);
    API_VERTEX2F(barX + barFillY - 1, barY + barHeight - 1);
    API_END();

    API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);

    API_COLOR(colors[0]);
    if (text != nullptr)
    {
        API_CMD_TEXT(textX, textY, textFont, OPT_CENTER, text);
    }
    if (data != nullptr)
    {
        API_CMD_TEXT(10, 457, textFont, 0, "v");
        API_CMD_TEXT(22, 457, textFont, 0, data);
    }

    API_RESTORE_CONTEXT();
}
