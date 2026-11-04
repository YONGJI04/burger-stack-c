#define _POSIX_C_SOURCE 200809L
#include "terminal.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <conio.h>
#include <io.h>
#include <windows.h>
static DWORD saved_output_mode;
static UINT saved_codepage;
static HANDLE output_handle;
#else
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
static struct termios saved_termios;
#endif

static bool opened;
static bool use_color;
static volatile sig_atomic_t interrupted;
static char previous_frame[262144];

static void on_signal(int signal_number)
{
    (void)signal_number;
    interrupted = 1;
}

bool terminal_open(bool color)
{
#ifdef _WIN32
    if (!_isatty(_fileno(stdin)) || !_isatty(_fileno(stdout)))
        return false;
    output_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (!GetConsoleMode(output_handle, &saved_output_mode))
        return false;
    if (!SetConsoleMode(output_handle, saved_output_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        return false;
    saved_codepage = GetConsoleOutputCP();
    SetConsoleOutputCP(CP_UTF8);
#else
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)
        || tcgetattr(STDIN_FILENO, &saved_termios) != 0)
        return false;
    struct termios raw = saved_termios;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | IEXTEN);
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0)
        return false;
#endif
    interrupted = 0;
    previous_frame[0] = '\0';
    opened = true;
    use_color = color;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
#ifndef _WIN32
    /* Keep terminal settings under our control; Ctrl+C still exits cleanly. */
    signal(SIGTSTP, SIG_IGN);
#endif
    atexit(terminal_close);
    fputs("\033[?1049h\033[2J\033[H\033[?25l", stdout);
    fflush(stdout);
    return true;
}

void terminal_close(void)
{
    if (!opened)
        return;
    fputs("\033[0m\033[?25h\033[?1049l", stdout);
    fflush(stdout);
#ifdef _WIN32
    SetConsoleOutputCP(saved_codepage);
    SetConsoleMode(output_handle, saved_output_mode);
#else
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
#endif
    opened = false;
}

static int read_byte(int timeout_ms)
{
#ifdef _WIN32
    static unsigned char utf8[4];
    static int utf8_count, utf8_index;
    if (utf8_index < utf8_count) return utf8[utf8_index++];
    double until = terminal_now() + timeout_ms / 1000.0;
    while (!_kbhit()) {
        if (interrupted)
            return KEY_EOF;
        if (terminal_now() >= until)
            return KEY_NONE;
        Sleep(1);
    }
    wint_t value = _getwch();
    if (value == 0 || value == 224) {
        wint_t extended = _getwch();
        if (extended == 72) return KEY_UP;
        if (extended == 80) return KEY_DOWN;
        return KEY_OTHER;
    }
    if (value < 128) return (int)value;
    WCHAR input[2] = {(WCHAR)value, 0};
    int units = 1;
    if (value >= 0xd800 && value <= 0xdbff) {
        input[1] = (WCHAR)_getwch();
        units = 2;
    }
    utf8_count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, units,
                                   (char *)utf8, sizeof(utf8), NULL, NULL);
    utf8_index = 0;
    return utf8_count > 0 ? utf8[utf8_index++] : KEY_OTHER;
#else
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(STDIN_FILENO, &read_set);
    struct timeval timeout = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    int ready = select(STDIN_FILENO + 1, &read_set, NULL, NULL, &timeout);
    if (ready < 0)
        return errno == EINTR && !interrupted ? KEY_NONE : KEY_EOF;
    if (ready == 0)
        return KEY_NONE;
    unsigned char byte;
    ssize_t count = read(STDIN_FILENO, &byte, 1);
    return count == 1 ? byte : KEY_EOF;
#endif
}

int terminal_key(int timeout_ms)
{
    if (interrupted)
        return KEY_EOF;
    int key = read_byte(timeout_ms);
    if (key != 27)
        return key;
    int next = read_byte(25);
    if (next == KEY_NONE || next == KEY_EOF)
        return 27;
    if (next == '[' || next == 'O') {
        /* Consume the complete sequence, including modified arrows/F-keys. */
        for (int i = 0; i < 24; ++i) {
            int code = read_byte(10);
            if (code < 0) break;
            if (code >= 0x40 && code <= 0x7e) {
                if (code == 'A') return KEY_UP;
                if (code == 'B') return KEY_DOWN;
                return KEY_OTHER;
            }
        }
    }
    return KEY_OTHER;
}

double terminal_now(void)
{
#ifdef _WIN32
    LARGE_INTEGER counter, frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
#endif
}

void terminal_size(int *columns, int *rows)
{
    *columns = 80;
    *rows = 24;
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO info;
    if (GetConsoleScreenBufferInfo(output_handle, &info)) {
        *columns = info.srWindow.Right - info.srWindow.Left + 1;
        *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    }
#else
    struct winsize size;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0) {
        if (size.ws_col > 0) *columns = size.ws_col;
        if (size.ws_row > 0) *rows = size.ws_row;
    }
#endif
}

bool terminal_interrupted(void) { return interrupted != 0; }
bool terminal_color(void) { return use_color; }
void terminal_set_color(bool color) { use_color = color; previous_frame[0] = '\0'; }

void terminal_present(const char *frame)
{
    static int previous_columns, previous_rows;
    int columns, rows;
    terminal_size(&columns, &rows);
    bool resized = columns != previous_columns || rows != previous_rows;
    previous_columns = columns;
    previous_rows = rows;
    /* Avoid repainting unchanged menus/frames, especially over SSH. */
    if (opened && !resized && strcmp(previous_frame, frame) == 0) return;
    size_t length = strlen(frame);
    if (length < sizeof(previous_frame)) memcpy(previous_frame, frame, length + 1);
    else previous_frame[0] = '\0';
    if (opened && resized) fputs("\033[2J", stdout);
    fputs("\033[H", stdout);
    fputs(frame, stdout);
    fputs("\033[0m\033[J", stdout);
    fflush(stdout);
}

void terminal_flush_input(void)
{
#ifdef _WIN32
    while (_kbhit()) (void)_getch();
#else
    tcflush(STDIN_FILENO, TCIFLUSH);
#endif
}

size_t utf8_decode(const char *text, uint32_t *codepoint)
{
    const unsigned char *s = (const unsigned char *)text;
    uint32_t cp;
    size_t count;
    if (s[0] < 0x80) { *codepoint = s[0]; return s[0] ? 1 : 0; }
    if (s[0] >= 0xc2 && s[0] <= 0xdf) { cp = s[0] & 0x1fu; count = 2; }
    else if (s[0] >= 0xe0 && s[0] <= 0xef) { cp = s[0] & 0x0fu; count = 3; }
    else if (s[0] >= 0xf0 && s[0] <= 0xf4) { cp = s[0] & 0x07u; count = 4; }
    else { *codepoint = 0xfffd; return 1; }
    for (size_t i = 1; i < count; ++i) {
        if (s[i] == 0 || (s[i] & 0xc0u) != 0x80u) {
            *codepoint = 0xfffd; return 1;
        }
        cp = (cp << 6) | (s[i] & 0x3fu);
    }
    if ((count == 2 && cp < 0x80) || (count == 3 && cp < 0x800)
        || (count == 4 && cp < 0x10000) || cp > 0x10ffff
        || (cp >= 0xd800 && cp <= 0xdfff)) {
        *codepoint = 0xfffd; return 1;
    }
    *codepoint = cp;
    return count;
}

int utf8_width(uint32_t cp)
{
    if (cp < 32 || (cp >= 0x7f && cp < 0xa0)) return 0;
    if ((cp >= 0x300 && cp <= 0x36f) || (cp >= 0xfe00 && cp <= 0xfe0f)) return 0;
    if (cp >= 0x1100 && (cp <= 0x115f || cp == 0x2329 || cp == 0x232a
        || (cp >= 0x2e80 && cp <= 0xa4cf) || (cp >= 0xac00 && cp <= 0xd7a3)
        || (cp >= 0xf900 && cp <= 0xfaff) || (cp >= 0xfe10 && cp <= 0xfe6f)
        || (cp >= 0xff01 && cp <= 0xff60) || (cp >= 0xffe0 && cp <= 0xffe6)
        || (cp >= 0x1f300 && cp <= 0x1faff) || cp >= 0x20000)) return 2;
    return 1;
}

size_t utf8_previous(const char *text, size_t length)
{
    if (length == 0) return 0;
    --length;
    while (length > 0 && ((unsigned char)text[length] & 0xc0u) == 0x80u)
        --length;
    return length;
}

bool utf8_name_valid(const char *text)
{
    if (*text == '\0') return false;
    while (*text) {
        uint32_t cp;
        size_t count = utf8_decode(text, &cp);
        if (cp == 0xfffd || cp < 32 || (cp >= 0x7f && cp <= 0x9f)
            || (cp >= 0x200b && cp <= 0x200f) || (cp >= 0x2028 && cp <= 0x202e)
            || (cp >= 0x2060 && cp <= 0x206f) || cp == 0xfeff)
            return false;
        text += count;
    }
    return true;
}
