#ifndef BURGER_GAME_H
#define BURGER_GAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BURGER_MAX_LAYERS 12
#define BURGER_QUEUE_CAPACITY 5
#define BURGER_INITIAL_LIVES 3
#define BURGER_DEFAULT_SECONDS 120

typedef enum {
    ING_BOTTOM = 1, ING_PATTY, ING_CHEESE, ING_LETTUCE,
    ING_TOMATO, ING_PICKLE, ING_TOP
} Ingredient;

/* index 0 is the bottom of the burger; top is the number of items. */
typedef struct {
    Ingredient items[BURGER_MAX_LAYERS];
    size_t top;
} BurgerStack;

typedef struct {
    unsigned id;
    Ingredient recipe[BURGER_MAX_LAYERS];
    size_t length;
    int stage;
    double arrived_at;
    double deadline;
} Customer;

/* rear is the next empty slot. count distinguishes empty from full. */
typedef struct {
    Customer items[BURGER_QUEUE_CAPACITY];
    size_t front;
    size_t rear;
    size_t count;
} CustomerQueue;

typedef enum {
    EVENT_READY, EVENT_ADDED, EVENT_UNDONE, EVENT_EMPTY,
    EVENT_FULL, EVENT_CORRECT, EVENT_WRONG, EVENT_TIMEOUT,
    EVENT_NO_CUSTOMER
} GameEvent;

typedef enum { END_NONE, END_TIME, END_LIVES, END_QUIT } EndReason;

typedef struct {
    BurgerStack burger;
    BurgerStack submitted;
    CustomerQueue queue;
    uint32_t random_state;
    unsigned next_id;
    int score;
    int lives;
    int served;
    int missed;
    int combo;
    int best_combo;
    int stage;
    int last_points;
    double started_at;
    double ends_at;
    double last_tick;
    double next_arrival;
    GameEvent event;
    double event_until;
    GameEvent feedback;
    double feedback_until;
    double last_layer_at;
    double submitted_at;
    bool submitted_correct;
    EndReason ended;
} Game;

void stack_init(BurgerStack *stack);
bool stack_push(BurgerStack *stack, Ingredient ingredient);
bool stack_pop(BurgerStack *stack, Ingredient *removed);
bool stack_matches(const BurgerStack *stack, const Customer *customer);
void queue_init(CustomerQueue *queue);
bool queue_enqueue(CustomerQueue *queue, Customer customer);
bool queue_dequeue(CustomerQueue *queue, Customer *removed);
const Customer *queue_at(const CustomerQueue *queue, size_t index);

int game_stage(int served);
int game_queue_target(int stage);
double game_patience(int stage);
double game_arrival_interval(int stage);
void game_init(Game *game, uint32_t seed, double now, double duration);
void game_tick(Game *game, double now);
void game_input(Game *game, int key, double now);
double game_time_left(const Game *game, double now);
void game_shift_clocks(Game *game, double seconds);
const char *ingredient_name(Ingredient ingredient);
const char *ingredient_short_name(Ingredient ingredient);
bool game_invariant(const Game *game);

#endif
