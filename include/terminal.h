#ifndef BURGER_TERMINAL_H
#define BURGER_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KEY_NONE (-1)
#define KEY_EOF (-2)
#define KEY_UP 256
#define KEY_DOWN 257
#define KEY_OTHER 258

bool terminal_open(bool color);
void terminal_close(void);
int terminal_key(int timeout_ms);
double terminal_now(void);
void terminal_size(int *columns, int *rows);
bool terminal_interrupted(void);
bool terminal_color(void);
void terminal_set_color(bool color);
void terminal_present(const char *frame);
void terminal_flush_input(void);
size_t utf8_decode(const char *text, uint32_t *codepoint);
int utf8_width(uint32_t codepoint);
size_t utf8_previous(const char *text, size_t length);
bool utf8_name_valid(const char *text);

#endif
