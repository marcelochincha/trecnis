#include <core/font8x8_basic.hpp>
#include <core/sr_text.hpp>

inline void draw_outline(framebuffer &fb, int x, int y, uint32_t color)
{

    for (int dy = -1; dy <= 1; dy++)
    {
        for (int dx = -1; dx <= 1; dx++)
        {
            int px = x + dx;
            int py = y + dy;
            if (px >= 0 && px < fb.width && py >= 0 && py < fb.height)
            {
                fb.colorBuffer[py * fb.width + px] = color;
            }
        }
    }
}


inline bool can_draw(framebuffer &fb, int x, int y)
{
    return x >= 0 && x < fb.width && y >= 0 && y < fb.height;
}



inline bool is_char_pixel(const uint8_t *glyph, int x, int y)
{
    if (x < 0 || x >= 8 || y < 0 || y >= 8)
        return false;
    return (glyph[y] & (1 << x)) != 0;
}

void draw_char(framebuffer &fb, int x, int y, char c, uint32_t color, uint32_t outlineColor)
{
    const uint8_t *glyph = font8x8_basic[(uint8_t)c];

    for (int row = 0; row < 8; row++)
    {

        for (int col = 0; col < 8; col++)
        {



            if (!can_draw(fb, x + col, y + row))
                continue;

            if (!is_char_pixel(glyph, col, row))
                continue;


            for (int dy = -1; dy <= 1; dy++)
            {
                for (int dx = -1; dx <= 1; dx++)
                {
                    int px = x + col + dx;
                    int py = y + row + dy;
                    if (dx == 0 && dy == 0)
                        fb.colorBuffer[py * fb.width + px] = color;
                    else if (can_draw(fb, px, py) && !is_char_pixel(glyph, col + dx, row + dy))
                        fb.colorBuffer[py * fb.width + px] = outlineColor;
                }
            }
        }
    }
}

void draw_text(framebuffer &fb, int x, int y, const char *text, uint32_t color, uint32_t outlineColor)
{
    int startX = x;
    while (*text)
    {
        if (*text == '\n')
        {
            y += 8;
            x = startX;
            text++;
            continue;
        }
        draw_char(fb, x, y, *text, color, outlineColor);
        x += 8;
        text++;
    }
}