#ifndef BURGER_ART_H
#define BURGER_ART_H

#include "game.h"

#define ART_WIDTH 30
#define ART_HEIGHT 48

/* A terminal cell contains two vertically stacked color pixels (▀). */
typedef enum {
    INK_BACK, INK_PANEL, INK_TEXT, INK_MUTED, INK_LINE,
    INK_GOLD, INK_RED, INK_GREEN, INK_CYAN,
    INK_BUN, INK_BUN_LIGHT, INK_CRUST, INK_SESAME,
    INK_MEAT, INK_MEAT_LIGHT, INK_GRILL,
    INK_CHEESE, INK_CHEESE_LIGHT, INK_TOMATO, INK_TOMATO_LIGHT,
    INK_LETTUCE, INK_LETTUCE_LIGHT, INK_PICKLE, INK_PLATE,
    INK_SHADOW, INK_COUNT
} Ink;

typedef struct {
    unsigned char pixels[ART_HEIGHT][ART_WIDTH];
    int height;
    int top_row;
} BurgerArt;

void art_burger(BurgerArt *art, const BurgerStack *burger, int height, int lift);
const char *art_ink_rgb(Ink ink);
Ink art_ingredient_ink(Ingredient ingredient);

#endif
