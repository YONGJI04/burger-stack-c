#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include "art.h"
#include "ranking.h"
#include "terminal.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#else
#include <io.h>
#define unlink _unlink
#endif

static unsigned checks;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); exit(1); } } while (0)

static void test_stack(void)
{
    BurgerStack stack;
    stack_init(&stack);
    CHECK(!stack_pop(&stack, NULL));
    CHECK(!stack_push(&stack, (Ingredient)0));
    CHECK(!stack_push(&stack, (Ingredient)8));
    for (size_t i = 0; i < BURGER_MAX_LAYERS; ++i)
        CHECK(stack_push(&stack, (Ingredient)(1 + i % 7)));
    CHECK(!stack_push(&stack, ING_TOP));
    for (size_t i = BURGER_MAX_LAYERS; i > 0; --i) {
        Ingredient removed;
        CHECK(stack_pop(&stack, &removed));
        CHECK(removed == (Ingredient)(1 + (i - 1) % 7));
    }
    CHECK(stack.top == 0);
    Customer order = {.length = 3, .recipe = {ING_BOTTOM, ING_PATTY, ING_TOP}};
    CHECK(!stack_matches(&stack, &order));
    CHECK(stack_push(&stack, ING_BOTTOM));
    CHECK(stack_push(&stack, ING_TOP));
    CHECK(stack_push(&stack, ING_PATTY));
    CHECK(!stack_matches(&stack, &order));
    stack_init(&stack);
    for (size_t i = 0; i < order.length; ++i) stack_push(&stack, order.recipe[i]);
    CHECK(stack_matches(&stack, &order));
    stack_push(&stack, ING_TOP);
    CHECK(!stack_matches(&stack, &order));
}

static void test_queue(void)
{
    CustomerQueue queue;
    queue_init(&queue);
    CHECK(!queue_dequeue(&queue, NULL));
    CHECK(queue_at(&queue, 0) == NULL);
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
        for (unsigned i = 0; i < BURGER_QUEUE_CAPACITY; ++i) {
            Customer customer = {.id = cycle * BURGER_QUEUE_CAPACITY + i};
            CHECK(queue_enqueue(&queue, customer));
        }
        CHECK(queue.front == queue.rear);
        CHECK(!queue_enqueue(&queue, (Customer){0}));
        for (unsigned i = 0; i < BURGER_QUEUE_CAPACITY; ++i) {
            Customer customer;
            CHECK(queue_at(&queue, 0)->id == cycle * BURGER_QUEUE_CAPACITY + i);
            CHECK(queue_dequeue(&queue, &customer));
            CHECK(customer.id == cycle * BURGER_QUEUE_CAPACITY + i);
        }
        /* Move both indices away from zero to exercise wrapped full queues. */
        CHECK(queue_enqueue(&queue, (Customer){.id = 999}));
        CHECK(queue_dequeue(&queue, NULL));
    }
    CHECK(queue_at(&queue, SIZE_MAX) == NULL);
}

static void solve(Game *game, double *now)
{
    Customer order = *queue_at(&game->queue, 0);
    for (size_t i = 0; i < order.length; ++i) {
        *now += 0.01;
        game_input(game, '0' + order.recipe[i], *now);
    }
    *now += 0.01;
    game_input(game, 'S', *now);
}

static void test_gameplay(void)
{
    Game game;
    game_init(&game, 42, 1000, 120);
    CHECK(game.queue.count == 1 && game.lives == 3 && game.stage == 1);
    CHECK(game_invariant(&game));
    game_input(&game, 'u', 1000);
    CHECK(game.event == EVENT_EMPTY);
    game_input(&game, '1', 1000);
    game_input(&game, 'u', 1000);
    CHECK(game.burger.top == 0);
    double now = 1000;
    solve(&game, &now);
    CHECK(game.score == 215 && game.served == 1 && game.combo == 1);
    CHECK(game.burger.top == 0 && game.queue.count == 1);
    solve(&game, &now);
    solve(&game, &now);
    CHECK(game.stage == 2 && game.combo == 3 && game.best_combo == 3);
    CHECK(queue_at(&game.queue, 0)->length == 5);
    game_input(&game, 's', now);
    CHECK(game.lives == 2 && game.combo == 0 && game.missed == 1);
    game_input(&game, 's', now);
    game_input(&game, 's', now);
    CHECK(game.ended == END_LIVES && game.lives == 0 && game.missed == 3);
    int score = game.score;
    game_input(&game, '1', now + 5);
    CHECK(game.score == score && game.burger.top == 0);

    game_init(&game, 4, 0, 120);
    now = 0;
    for (int i = 0; i < 35; ++i) {
        solve(&game, &now);
        CHECK(game_invariant(&game));
    }
    CHECK(game.stage == 8 && game.served == 35 && game.combo == 35);
    CHECK(game_queue_target(8) == 5 && game_patience(8) < game_patience(1));
    CHECK(game_arrival_interval(8) < game_arrival_interval(1));
    CHECK(queue_at(&game.queue, 0)->length >= 8);
}

static void test_time(void)
{
    Game game;
    game_init(&game, 1, 0, 120);
    game_tick(&game, 8);
    CHECK(game.queue.count == 2);
    game_input(&game, '1', 9);
    game_tick(&game, 32);
    CHECK(game.lives == 2 && game.burger.top == 0);
    CHECK(queue_at(&game.queue, 0)->id == 2);
    game_tick(&game, 40);
    CHECK(game.lives == 1);
    game_tick(&game, 1000);
    CHECK(game.ended == END_LIVES && game.lives == 0);

    game_init(&game, 1, 0, 1);
    game_input(&game, '1', 1);
    CHECK(game.ended == END_TIME && game.burger.top == 0);
    CHECK(game_time_left(&game, 10) == 0);

    /* All queue positions expire independently, not just the head. */
    game_init(&game, 1, 0, 120);
    game_tick(&game, 8);
    game.queue.items[(game.queue.front + 1) % BURGER_QUEUE_CAPACITY].deadline = 10;
    game_input(&game, '1', 9);
    game_tick(&game, 10);
    CHECK(game.lives == 2 && game.queue.count == 1);
    CHECK(game.burger.top == 1 && queue_at(&game.queue, 0)->id == 1);

    game_init(&game, 1, 0, 120);
    game_tick(&game, 8);
    game.queue.items[(game.queue.front + 1) % BURGER_QUEUE_CAPACITY].deadline = 32;
    game_tick(&game, 32);
    CHECK(game.lives == 1 && game.missed == 2);

    /* A frozen screen must have the same effect as regular time updates. */
    Game fine, coarse;
    game_init(&fine, 928, 0, 120);
    game_init(&coarse, 928, 0, 120);
    for (int i = 1; i <= 250; ++i) game_tick(&fine, i * 0.25);
    game_tick(&coarse, 62.5);
    CHECK(fine.lives == coarse.lives && fine.ended == coarse.ended);
    CHECK(fine.missed == coarse.missed && fine.queue.count == coarse.queue.count);
    CHECK(fine.random_state == coarse.random_state);
    for (size_t i = 0; i < fine.queue.count; ++i)
        CHECK(queue_at(&fine.queue, i)->id == queue_at(&coarse.queue, i)->id);

    game_init(&game, 1, 10, 120);
    game_input(&game, '1', 9);
    game_input(&game, '1', NAN);
    game_input(&game, '1', INFINITY);
    CHECK(game.burger.top == 0 && game.lives == 3);
    game_input(&game, 'q', 10);
    CHECK(game.ended == END_QUIT);
}

static void test_pause_and_feedback(void)
{
    Game game;
    game_init(&game, 42, 100, 120);
    const Customer *customer = queue_at(&game.queue, 0);
    CHECK(customer->recipe[2] == ING_CHEESE);
    double now = 100;
    solve(&game, &now);
    CHECK(game.submitted_correct && game.submitted.top == 4);
    CHECK(game.feedback == EVENT_CORRECT);
    double remaining = game_time_left(&game, now);
    double customer_remaining = queue_at(&game.queue, 0)->deadline - now;
    double feedback_remaining = game.feedback_until - now;
    game_input(&game, '1', now);
    CHECK(game.feedback == EVENT_CORRECT && game.event == EVENT_ADDED);
    CHECK(game.submitted.top == 4 && game.burger.top == 1);
    game_shift_clocks(&game, 60);
    now += 60;
    game_tick(&game, now);
    CHECK(fabs(game_time_left(&game, now) - remaining) < 0.000001);
    CHECK(fabs(queue_at(&game.queue, 0)->deadline - now - customer_remaining) < 0.000001);
    CHECK(fabs(game.feedback_until - now - feedback_remaining) < 0.000001);
    CHECK(game.lives == 3 && game.burger.top == 1);
    game_shift_clocks(&game, -5);
    game_shift_clocks(&game, NAN);
    CHECK(game_invariant(&game));
}

static void test_burger_art(void)
{
    /* Exercise every ingredient, full stacks and both layout sizes under ASan. */
    for (int item = ING_BOTTOM; item <= ING_TOP; ++item) {
        BurgerStack burger;
        stack_init(&burger);
        for (size_t count = 0; count <= BURGER_MAX_LAYERS; ++count) {
            for (int rows = 24; rows <= 40; rows += 2) {
                BurgerArt art;
                art_burger(&art, &burger, rows, 2);
                CHECK(art.height == rows && art.top_row >= 0 && art.top_row < rows);
                for (int y = 0; y < rows; ++y)
                    for (int x = 0; x < ART_WIDTH; ++x)
                        CHECK(art.pixels[y][x] < INK_COUNT);
                CHECK(art.pixels[rows - 2][0] == INK_PLATE);
            }
            if (count < BURGER_MAX_LAYERS) CHECK(stack_push(&burger, (Ingredient)item));
        }
    }
}

static void test_ranking(void)
{
    char path[128];
#ifdef _WIN32
    snprintf(path, sizeof(path), "build/ranking-test-%lu.tmp", (unsigned long)time(NULL));
    FILE *file = fopen(path, "wb");
    CHECK(file != NULL);
#else
    strcpy(path, "/tmp/burger-ranking-XXXXXX");
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    FILE *file = fdopen(fd, "wb");
    CHECK(file != NULL);
#endif
    fputs("bad line\n-3\tnegative\n2147483648\toverflow\n5\t\033[31m\n", file);
    fputs("100\t먼저\n100\t나중\n50\tlast", file);
    CHECK(fclose(file) == 0);
    CHECK(ranking_append(path, "김 셰프", 150));
    CHECK(!ranking_append(path, "a\nb", 200));
    CHECK(!ranking_append(path, "a\tb", 200));
    CHECK(!ranking_append(path, "", 200));
    CHECK(!ranking_append(path, "name", -1));
    Ranking ranking;
    CHECK(ranking_load(path, &ranking));
    CHECK(ranking.count == 4 && ranking.skipped == 4);
    CHECK(strcmp(ranking.entries[0].name, "김 셰프") == 0);
    CHECK(strcmp(ranking.entries[1].name, "먼저") == 0);
    CHECK(strcmp(ranking.entries[2].name, "나중") == 0);
    ranking_free(&ranking);
    for (int i = 0; i < 100; ++i) CHECK(ranking_append(path, "player", i));
    CHECK(ranking_load(path, &ranking));
    CHECK(ranking.count == 104);
    for (size_t i = 1; i < ranking.count; ++i)
        CHECK(ranking.entries[i - 1].score >= ranking.entries[i].score);
    ranking_free(&ranking);
    CHECK(unlink(path) == 0);
    CHECK(ranking_load(path, &ranking));
    CHECK(ranking.count == 0);
    ranking_free(&ranking);
    CHECK(!ranking_append("/nonexistent-burger-directory/ranking.txt", "x", 1));
    CHECK(!utf8_name_valid("\xf0\x80\x80\x80"));
    CHECK(!utf8_name_valid("\xed\xa0\x80"));
    CHECK(!utf8_name_valid("\xe3\x80"));
    CHECK(utf8_name_valid("한글 Chef"));
    CHECK(utf8_previous("가나다", 9) == 6);
}

static uint32_t fuzz_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void fuzz_games(uint32_t seed, unsigned rounds)
{
    for (unsigned round = 0; round < rounds; ++round) {
        Game game;
        uint32_t state = seed + round;
        game_init(&game, fuzz_random(&state), 0, 120);
        double now = 0;
        for (int step = 0; step < 2000 && game.ended == END_NONE; ++step) {
            unsigned action = fuzz_random(&state) % 20;
            now += (fuzz_random(&state) % 150) / 100.0;
            game_tick(&game, now);
            if (game.ended != END_NONE) break;
            if (action < 12) {
                const Customer *customer = queue_at(&game.queue, 0);
                bool prefix = customer != NULL && game.burger.top <= customer->length;
                if (prefix)
                    for (size_t i = 0; i < game.burger.top; ++i)
                        if (game.burger.items[i] != customer->recipe[i]) prefix = false;
                if (!prefix) game_input(&game, 'u', now);
                else if (game.burger.top == customer->length) game_input(&game, 's', now);
                else game_input(&game, '0' + customer->recipe[game.burger.top], now);
            } else if (action == 12) game_input(&game, 'u', now);
            else if (action == 13) game_input(&game, 's', now);
            else if (action == 14) game_input(&game, '1' + (int)(fuzz_random(&state) % 7), now);
            else if (action == 15) { now += 12; game_tick(&game, now); }
            CHECK(game_invariant(&game));
        }
        CHECK(game.ended != END_NONE);
        CHECK(game_invariant(&game));
    }
}

int main(int argc, char **argv)
{
    uint32_t seed = argc > 1 ? (uint32_t)strtoul(argv[1], NULL, 10) : 20261104;
    unsigned rounds = argc > 2 ? (unsigned)strtoul(argv[2], NULL, 10) : 1000;
    test_stack();
    test_queue();
    test_gameplay();
    test_time();
    test_pause_and_feedback();
    test_burger_art();
    test_ranking();
    fuzz_games(seed, rounds);
    printf("PASS: %u checks; %u simulated games; seed=%u\n", checks, rounds, seed);
    return 0;
}
