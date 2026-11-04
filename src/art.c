#include "art.h"

#include <string.h>

const char *art_ink_rgb(Ink ink)
{
    static const char *rgb[INK_COUNT] = {
        "20;25;29", "27;34;38", "240;230;205", "140;155;153", "59;76;78",
        "255;190;92", "255;123;110", "160;210;136", "116;204;196",
        "221;143;61", "247;183;91", "159;86;37", "255;232;175",
        "101;47;31", "151;77;43", "66;32;24",
        "248;188;44", "255;219;95", "201;61;47", "245;98;66",
        "57;132;57", "136;197;73", "88;149;69", "198;215;203",
        "43;59;60"
    };
    return rgb[ink >= 0 && ink < INK_COUNT ? ink : INK_TEXT];
}

Ink art_ingredient_ink(Ingredient item)
{
    switch (item) {
    case ING_BOTTOM: case ING_TOP: return INK_GOLD;
    case ING_PATTY: return INK_MEAT_LIGHT;
    case ING_CHEESE: return INK_CHEESE_LIGHT;
    case ING_LETTUCE: case ING_PICKLE: return INK_GREEN;
    case ING_TOMATO: return INK_RED;
    default: return INK_TEXT;
    }
}

static void pixel(BurgerArt *art, int x, int y, Ink ink)
{
    if (x >= 0 && x < ART_WIDTH && y >= 0 && y < art->height)
        art->pixels[y][x] = (unsigned char)ink;
}

static void layer(BurgerArt *art, Ingredient item, int top, int height)
{
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < ART_WIDTH; ++x) {
            Ink ink = INK_PANEL;
            switch (item) {
            case ING_TOP: {
                int step = height == 1 ? 5 : y * 5 / (height - 1);
                int inset[] = {9, 5, 3, 2, 1, 1};
                if (x >= inset[step] && x < ART_WIDTH - inset[step]) {
                    ink = step == 5 ? INK_CRUST : y < height / 2 ? INK_BUN_LIGHT : INK_BUN;
                    if (step > 0 && step < 5 && (x + y * 9) % 11 == 0
                        && x > inset[step] + 2 && x < ART_WIDTH - inset[step] - 2)
                        ink = INK_SESAME;
                }
                break;
            }
            case ING_BOTTOM: {
                int inset = y == height - 1 ? 4 : 1;
                if (x >= inset && x < ART_WIDTH - inset)
                    ink = y == 0 ? INK_BUN_LIGHT : y == height - 1 ? INK_CRUST : INK_BUN;
                break;
            }
            case ING_PATTY:
                if (x >= 1 && x < ART_WIDTH - 1) {
                    ink = y == 0 || y == height - 1 ? INK_MEAT : INK_MEAT_LIGHT;
                    if ((x + y * 2) % 7 < 2) ink = INK_GRILL;
                }
                break;
            case ING_CHEESE:
                if (y == 0 || (x > 2 && x < ART_WIDTH - 3))
                    ink = y == 0 ? INK_CHEESE_LIGHT : INK_CHEESE;
                if (height > 2 && y == height - 1 && x != 6 && x != 7 && x != 22)
                    ink = INK_PANEL;
                break;
            case ING_LETTUCE:
                if (y != height - 1 || x % 6 < 4)
                    ink = (x + y * 3) % 7 < 3 ? INK_LETTUCE_LIGHT : INK_LETTUCE;
                break;
            case ING_TOMATO:
                if (x >= 2 && x < ART_WIDTH - 2) {
                    ink = y == 0 ? INK_TOMATO_LIGHT : INK_TOMATO;
                    if (height > 1 && y == 1 && x % 8 == 5) ink = INK_BUN_LIGHT;
                }
                break;
            case ING_PICKLE:
                if (x > 1 && x < ART_WIDTH - 2 && x % 10 >= 3 && x % 10 <= 8)
                    ink = y == 0 ? INK_LETTUCE_LIGHT : INK_PICKLE;
                break;
            default: break;
            }
            pixel(art, x, top + y, ink);
        }
    }
}

void art_burger(BurgerArt *art, const BurgerStack *burger, int height, int lift)
{
    memset(art->pixels, INK_PANEL, sizeof(art->pixels));
    art->height = height < 4 ? 4 : height > ART_HEIGHT ? ART_HEIGHT : height;
    size_t count = burger->top < BURGER_MAX_LAYERS ? burger->top : BURGER_MAX_LAYERS;
    int heights[BURGER_MAX_LAYERS] = {0};
    int total = 0;
    for (size_t i = 0; i < count; ++i) {
        Ingredient item = burger->items[i];
        heights[i] = item == ING_TOP ? 6 : item == ING_BOTTOM ? 4
                   : item == ING_PICKLE ? 2 : 3;
        total += heights[i];
    }
    /* Keep tall/custom stacks visible by reducing layer thickness gradually. */
    while (total > art->height - 2) {
        int tallest = -1;
        for (size_t i = 0; i < count; ++i)
            if (heights[i] > 1 && (tallest < 0 || heights[i] > heights[tallest]))
                tallest = (int)i;
        if (tallest < 0) break;
        --heights[tallest];
        --total;
    }
    int cursor = art->height - 2;
    for (size_t i = 0; i < count; ++i) {
        cursor -= heights[i];
        int offset = i + 1 == count ? lift : 0;
        if (offset > cursor) offset = cursor;
        if (offset < 0) offset = 0;
        layer(art, burger->items[i], cursor - offset, heights[i]);
        if (i + 1 == count) art->top_row = cursor - offset;
    }
    if (count == 0) art->top_row = art->height - 2;
    for (int x = 0; x < ART_WIDTH; ++x) {
        pixel(art, x, art->height - 2, INK_PLATE);
        if (x >= 3 && x < ART_WIDTH - 3) pixel(art, x, art->height - 1, INK_SHADOW);
    }
}
