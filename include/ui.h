#ifndef BURGER_UI_H
#define BURGER_UI_H

#include "game.h"
#include "ranking.h"

void ui_menu(int selected, int duration);
void ui_help(void);
void ui_game(const Game *game, double now);
void ui_pause(const Game *game);
void ui_results(const Game *game, const char *name, bool save_failed);
void ui_ranking(const Ranking *ranking, bool loaded, bool saved);
void ui_small_terminal(int columns, int rows);
bool ui_terminal_fits(void);

#endif
