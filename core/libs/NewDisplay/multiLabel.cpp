#include "FT812.h"

#include "display.h"
#include "displayControls.h"
#include "multiLabelControl.h"

#include <string.h>

cMultiLabel::cMultiLabel(strControlProperties* properties)
  : cDisplayControl(controlIdMultiLabel, 1)
{
    colorsCount = 3;
    colors = display.getTheme().defaultTextColors;

    if (properties == nullptr)
    {
        posX = 0;
        posY = 0;
        text = nullptr;
        data = nullptr;
        width = 0;
        height = 0;
        style = 0;
        value = 0;
        return;
    }

    colors = (properties->colors == nullptr) ? colors : properties->colors;

    posX = properties->x;
    posY = properties->y;

    text = properties->text;

    value = properties->value;

    data = (strMultiLabelData*)(properties->data);

    width = properties->w;
    height = properties->h;

    setStyle(properties->style);
}

cMultiLabel::~cMultiLabel()
{
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cMultiLabel::setStyle(uint32_t style)
{
    this->style = style;

    textStyle = (style & controlStyleCenterX ? OPT_CENTERX : 0) |
                (style & controlStyleCenterY ? OPT_CENTERY : 0) |
                (style & controlStyleRightX ? OPT_RIGHTX : 0);

    uint32_t fontIndex = FONT_INDEX_FROM_STYLE;
    fontIndex = (fontIndex >= 0) ? fontIndex : 0;
    font = &fonts[fontIndex];
}

void cMultiLabel::setText(const char* text)
{
    this->text = text;
}

void cMultiLabel::setValue(int value)
{
    this->value = value;
}

void cMultiLabel::setColors(const uint32_t* colors)
{
    this->colors = colors;
}

void cMultiLabel::setData(void* data)
{
    this->data = (strMultiLabelData*)(data);
}

//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
void cMultiLabel::update()
{
    const auto drawLine = [this](int16_t y, const char* line)
    {
        int16_t x = posX + data->xOffset;
        const char* point = strchr(line, '.');
        if (point && point != line && point[-1] >= '0' && point[-1] <= '9' &&
            point[1] >= '0' && point[1] <= '9')
        {
            // Tighten the period's advance in monospaced fonts. Proportional fonts
            // may already give it a narrower advance.
            const int trim = font->data['.'] - 2 * font->data[uint8_t(point[1])] / 3;
            if (trim > 0)
            {
                const auto prefixLength = point - line + 1;
                const int width = getTextWidth(font, line, strlen(line)) - trim;
                if (textStyle & OPT_CENTERX) x -= width / 2;
                else if (textStyle & OPT_RIGHTX) x -= width;
                const uint16_t options = textStyle & ~(OPT_CENTERX | OPT_RIGHTX);
                API_CMD_TEXT(x, y, font->handle, options, line, prefixLength);
                x += getTextWidth(font, line, prefixLength) - trim;
                API_CMD_TEXT(x, y, font->handle, options, point + 1);
                return;
            }
        }
        API_CMD_TEXT(x, y, font->handle, textStyle, line);
    };

    int16_t border_x = posX; //, border_y = posY-2;
    int16_t border_y = posY;
    int16_t lines_space = font->height;

    int16_t text_y = posY + data->yOffset / 2;
    int16_t text_h = height - data->yOffset - data->yForFxTextHole;
    const uint8_t headerIndex = data->textLinesCount - 1;
    const char* header = data->text[headerIndex];
    const bool hasHeader = header && *header;
    const uint16_t headerHeight = (height - data->yForFxTextHole) / 5;

    if (data->textLinesCount > 0 && data->textLinesCount < 6 && height > font->height)
    {
        lines_space = (text_h - font->height * data->textLinesCount) / (data->textLinesCount + 1);
    }

    if (style & controlStyleCenterX)
    {
        border_x = posX - (width / 2);
    }
    if (style & controlStyleCenterY)
    {
        border_y = posY - (height / 2);
        text_y = text_y + lines_space + font->height / 2;
    }

    if (style & controlStyleBackground)
    {
        if (style & controlStyleNoTransparency)
        {
            API_BLEND_FUNC(SRC_ALPHA, ZERO);
        }

        API_COLOR(colors[1]);

        if (style & controlStyleRoundedBorder)
            API_LINE_WIDTH(32);
        else
            API_LINE_WIDTH(16);

        uint16_t performanceRectHeight = ((height - data->yForFxTextHole) * 4) / 5;
        uint16_t performanceRectY = border_y + headerHeight + data->yForFxTextHole;

        if (hasHeader)
        {
            API_BEGIN(RECTS);
            API_VERTEX2F(border_x, border_y);
            API_VERTEX2F(border_x + width, border_y + headerHeight);
            API_END();
        }

        API_BEGIN(RECTS);
        API_VERTEX2F(border_x, performanceRectY);
        API_VERTEX2F(border_x + width, performanceRectY + performanceRectHeight);
        API_END();

        if (style & controlStyleBottomShadow)
        {
            API_BLEND_FUNC(SRC_ALPHA, ZERO);

            API_SAVE_CONTEXT();
            uint16_t grad_y = posY + height - 10;
            uint16_t grad_h = 12;
            API_SCISSOR_XY(border_x, grad_y);
            API_SCISSOR_SIZE(width + 1, grad_h);
            API_CMD_GRADIENT(0, grad_y, colors[1], 0, grad_y + grad_h, 0x0);
            API_RESTORE_CONTEXT();

            //			API_COLOR(colors[0]);
            //			API_LINE_WIDTH(1);
            //			API_BEGIN(LINES);
            //			API_VERTEX2F(border_x-1, posY);
            //			API_VERTEX2F(border_x-1, posY+height);
            //			API_VERTEX2F(border_x+width+1, posY);
            //			API_VERTEX2F(border_x+width+1, posY+height);
            //			API_END();

            API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
        }

        if (style & controlStyleNoTransparency)
        {
            API_BLEND_FUNC(SRC_ALPHA, ONE_MINUS_SRC_ALPHA);
        }
    }
    if (style & controlStyleBorder)
    {
        API_COLOR(colors[2]);

        API_LINE_WIDTH(24);

        API_BEGIN(LINE_STRIP);
        API_VERTEX2F(border_x, border_y);
        API_VERTEX2F(border_x + width, border_y);
        API_VERTEX2F(border_x + width, border_y + height);
        API_VERTEX2F(border_x, border_y + height);
        API_VERTEX2F(border_x, border_y);
        API_END();
    }

    if (hasHeader)
    {
        if (data->selected > 0 && data->selected - 1 == headerIndex)
            API_COLOR(colors[2]);
        else
            API_COLOR(colors[0]);

        drawLine(text_y, header);
    }

    text_y = border_y + headerHeight + data->yForFxTextHole + data->yOffset / 2;

    for (uint8_t i = 0; i < (data->textLinesCount - 1); i++)
    {
        if (data->text[i] != nullptr && *data->text[i] != 0)
        {

            if (data->selected > 0 && data->selected - 1 == i)
                API_COLOR(colors[2]);
            else
                API_COLOR(colors[0]);

            drawLine(text_y, data->text[i]);
        }

        text_y = text_y + (lines_space + font->height);
    }
}
