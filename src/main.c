#include "game.h"
#include "ranking.h"
#include "terminal.h"
#include "ui.h"

#include <errno.h>
#include <locale.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct {
    const char *ranking_path;
    int duration;
    uint32_t seed;
    bool fixed_seed;
    bool color;
    bool demo;
} Options;

static void usage(const char *program)
{
    printf("BURGER STACK - C11 console game\n\n"
           "Usage: %s [options]\n"
           "  --duration SECONDS   Round length, 1..3600 (default: 120)\n"
           "  --seed NUMBER        Reproducible orders, 0..4294967295\n"
           "  --ranking PATH       Score file (default: ranking.txt)\n"
           "  --no-color           Disable colored text\n"
           "  --demo               Print an example game screen and exit\n"
           "  --help               Show this help\n"
           "  --version            Show version\n\n"
           "Keys: 1..7 ingredients, U undo, S/Space submit, Q/Esc end round.\n"
           "Play in a UTF-8 terminal at least 80 columns by 24 rows.\n", program);
}

static bool number(const char *text, unsigned long maximum, unsigned long *value)
{
    if (*text < '0' || *text > '9') return false;
    errno = 0;
    char *end;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed > maximum) return false;
    *value = parsed;
    return true;
}

static int parse_options(int argc, char **argv, Options *options)
{
    *options = (Options){"ranking.txt", BURGER_DEFAULT_SECONDS, 0, false, true, false};
    if (getenv("NO_COLOR") != NULL) options->color = false;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            usage(argv[0]); return 1;
        }
        if (strcmp(arg, "--version") == 0) { puts("BURGER STACK 1.0.0"); return 1; }
        if (strcmp(arg, "--no-color") == 0) { options->color = false; continue; }
        if (strcmp(arg, "--demo") == 0) { options->demo = true; continue; }
        if ((strcmp(arg, "--duration") == 0 || strcmp(arg, "--seed") == 0
             || strcmp(arg, "--ranking") == 0) && i + 1 < argc) {
            const char *value = argv[++i];
            unsigned long parsed;
            if (strcmp(arg, "--ranking") == 0) {
                if (*value == '\0') return -1;
                options->ranking_path = value;
            } else if (strcmp(arg, "--duration") == 0) {
                if (!number(value, 3600, &parsed) || parsed == 0) return -1;
                options->duration = (int)parsed;
            } else {
                if (!number(value, UINT32_MAX, &parsed)) return -1;
                options->seed = (uint32_t)parsed;
                options->fixed_seed = true;
            }
            continue;
        }
        fprintf(stderr, "Unknown or incomplete option: %s\n", arg);
        return -1;
    }
    return 0;
}

/* A too-small terminal pauses every clock, including waiting customers. */
static void shift_clocks(Game *game, double seconds)
{
    game->started_at += seconds;
    game->ends_at += seconds;
    game->last_tick += seconds;
    game->next_arrival += seconds;
    game->event_until += seconds;
    for (size_t i = 0; i < game->queue.count; ++i) {
        Customer *customer = &game->queue.items[(game->queue.front + i) % BURGER_QUEUE_CAPACITY];
        customer->arrived_at += seconds;
        customer->deadline += seconds;
    }
}

static bool wait_for_size(Game *game)
{
    double before = terminal_now();
    while (!ui_terminal_fits()) {
        int columns, rows;
        terminal_size(&columns, &rows);
        ui_small_terminal(columns, rows);
        int key = terminal_key(100);
        if (key == KEY_EOF || key == 27 || key == 'q' || key == 'Q') return false;
    }
    if (game != NULL) shift_clocks(game, terminal_now() - before);
    return !terminal_interrupted();
}

static bool back_to_menu(int key)
{
    return key == '\r' || key == '\n' || key == 27 || key == 'q' || key == 'Q';
}

static void show_ranking(const Options *options, bool saved)
{
    Ranking ranking;
    bool loaded = ranking_load(options->ranking_path, &ranking);
    while (!terminal_interrupted() && wait_for_size(NULL)) {
        ui_ranking(&ranking, loaded, saved);
        int key = terminal_key(100);
        if (key == KEY_EOF || back_to_menu(key)) break;
    }
    ranking_free(&ranking);
}

static int name_width(const char *name)
{
    int width = 0;
    while (*name) {
        uint32_t cp;
        size_t count = utf8_decode(name, &cp);
        width += utf8_width(cp);
        name += count;
    }
    return width;
}

static void save_result(const Options *options, const Game *game)
{
    char name[RANKING_NAME_BYTES] = "";
    char pending[5] = "";
    size_t pending_length = 0, expected = 0;
    bool failed = false;
    terminal_flush_input();
    while (!terminal_interrupted() && wait_for_size(NULL)) {
        ui_results(game, name, failed);
        int key = terminal_key(100);
        if (key == KEY_EOF || key == 27) return;
        if (key == '\r' || key == '\n') {
            size_t length = strlen(name);
            while (length > 0 && name[length - 1] == ' ') name[--length] = '\0';
            size_t start = 0;
            while (name[start] == ' ') ++start;
            if (start > 0) memmove(name, name + start, strlen(name + start) + 1);
            if (*name == '\0') strcpy(name, "셰프");
            if (ranking_append(options->ranking_path, name, game->score)) {
                show_ranking(options, true);
                return;
            }
            failed = true;
        } else if (key == 127 || key == 8) {
            name[utf8_previous(name, strlen(name))] = '\0';
            pending_length = expected = 0;
        } else if (key >= 32 && key <= 255) {
            unsigned char byte = (unsigned char)key;
            if (byte < 128) {
                pending[0] = (char)byte;
                pending_length = expected = 1;
            } else if (byte >= 0xc2 && byte <= 0xf4) {
                pending[0] = (char)byte;
                pending_length = 1;
                expected = byte < 0xe0 ? 2 : byte < 0xf0 ? 3 : 4;
            } else if (byte >= 0x80 && byte <= 0xbf && pending_length > 0 && pending_length < expected) {
                pending[pending_length++] = (char)byte;
            } else {
                pending_length = expected = 0;
            }
            if (pending_length > 0 && pending_length == expected) {
                pending[pending_length] = '\0';
                size_t length = strlen(name);
                if (utf8_name_valid(pending) && length + pending_length < sizeof(name)
                    && name_width(name) + name_width(pending) <= 20)
                    memcpy(name + length, pending, pending_length + 1);
                pending_length = expected = 0;
            }
        }
    }
}

static void play(const Options *options)
{
    Game game;
    uint32_t seed = options->fixed_seed ? options->seed
                  : (uint32_t)time(NULL) ^ (uint32_t)(terminal_now() * 1000.0);
    game_init(&game, seed, terminal_now(), options->duration);
    while (!terminal_interrupted() && game.ended == END_NONE) {
        if (!wait_for_size(&game)) { game.ended = END_QUIT; break; }
        double now = terminal_now();
        game_tick(&game, now);
        ui_game(&game, now);
        if (game.ended != END_NONE) break;
        int key = terminal_key(50);
        if (key == KEY_EOF) return;
        if (key != KEY_NONE) game_input(&game, key, terminal_now());
    }
    if (!terminal_interrupted()) save_result(options, &game);
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    Options options;
    int parsed = parse_options(argc, argv, &options);
    if (parsed > 0) return EXIT_SUCCESS;
    if (parsed < 0) { usage(argv[0]); return EXIT_FAILURE; }
    if (options.demo) {
        Game game;
        game_init(&game, 42, 0, BURGER_DEFAULT_SECONDS);
        game_tick(&game, 8);
        const Customer *customer = queue_at(&game.queue, 0);
        for (size_t i = 0; i < customer->length; ++i) stack_push(&game.burger, customer->recipe[i]);
        ui_game(&game, 8);
        puts("");
        return EXIT_SUCCESS;
    }
    if (!terminal_open(options.color)) {
        fputs("Please run this game in an interactive UTF-8 terminal (80 x 24 or larger).\n"
              "VS Code: use Terminal > New Terminal, not the Output/Debug panel.\n"
              "Windows: use Windows Terminal on Windows 10/11.\n", stderr);
        return EXIT_FAILURE;
    }
    int selected = 0;
    bool running = true;
    while (running && !terminal_interrupted() && wait_for_size(NULL)) {
        ui_menu(selected, options.duration);
        int key = terminal_key(100);
        if (key == KEY_EOF || key == 'q' || key == 'Q' || key == 27) break;
        if (key == KEY_UP || key == 'w' || key == 'W') selected = (selected + 3) % 4;
        if (key == KEY_DOWN || key == 's' || key == 'S') selected = (selected + 1) % 4;
        bool activate = key == '\r' || key == '\n';
        if (key >= '1' && key <= '4') { selected = key - '1'; activate = true; }
        if (!activate) continue;
        switch (selected) {
        case 0: play(&options); break;
        case 1: show_ranking(&options, false); break;
        case 2:
            while (!terminal_interrupted() && wait_for_size(NULL)) {
                ui_help();
                key = terminal_key(100);
                if (key == KEY_EOF || back_to_menu(key)) break;
            }
            break;
        default: running = false; break;
        }
    }
    terminal_close();
    puts("Burger Stack closed. See you next shift!");
    return EXIT_SUCCESS;
}
