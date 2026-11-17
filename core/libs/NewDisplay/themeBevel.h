#pragma once

#include "FT812.h"
#include "Themes.h"

inline void drawBeveledFrame(const Theme& theme, int x, int y, int w, int h, bool recessed)
{
    const int right = x + w - 1;
    const int bottom = y + h - 1;
    const auto* colors = theme.bevelColors;
    API_SAVE_CONTEXT();
    API_LINE_WIDTH(8);
    API_BEGIN(LINE_STRIP);
    API_COLOR(recessed ? colors[1] : colors[0]);
    API_VERTEX2F(x, bottom - 1);
    API_VERTEX2F(x, y);
    API_VERTEX2F(right - 1, y);
    API_END();

    API_BEGIN(LINE_STRIP);
    API_COLOR(recessed ? colors[0] : colors[2]);
    API_VERTEX2F(x, bottom);
    API_VERTEX2F(right, bottom);
    API_VERTEX2F(right, y);
    API_END();

    API_BEGIN(LINE_STRIP);
    API_COLOR(recessed ? colors[2] : colors[1]);
    if (recessed)
    {
        API_VERTEX2F(x + 1, bottom - 2);
        API_VERTEX2F(x + 1, y + 1);
        API_VERTEX2F(right - 2, y + 1);
    }
    else
    {
        API_VERTEX2F(x + 1, bottom - 1);
        API_VERTEX2F(right - 1, bottom - 1);
        API_VERTEX2F(right - 1, y + 1);
    }
    API_END();

    API_BEGIN(LINE_STRIP);
    API_COLOR(colors[3]);
    if (recessed)
    {
        API_VERTEX2F(x + 1, bottom - 1);
        API_VERTEX2F(right - 1, bottom - 1);
        API_VERTEX2F(right - 1, y + 1);
    }
    else
    {
        API_VERTEX2F(x + 1, bottom - 2);
        API_VERTEX2F(x + 1, y + 1);
        API_VERTEX2F(right - 2, y + 1);
    }
    API_END();
    API_RESTORE_CONTEXT();
}
