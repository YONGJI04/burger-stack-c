#include "game.h"

#include <ctype.h>
#include <math.h>
#include <string.h>

static bool ingredient_valid(Ingredient item)
{
    return item >= ING_BOTTOM && item <= ING_TOP;
}

void stack_init(BurgerStack *stack)
{
    memset(stack, 0, sizeof(*stack));
}

bool stack_push(BurgerStack *stack, Ingredient ingredient)
{
    if (stack->top >= BURGER_MAX_LAYERS || !ingredient_valid(ingredient))
        return false;
    stack->items[stack->top++] = ingredient;
    return true;
}

bool stack_pop(BurgerStack *stack, Ingredient *removed)
{
    if (stack->top == 0)
        return false;
    Ingredient item = stack->items[--stack->top];
    stack->items[stack->top] = 0;
    if (removed != NULL)
        *removed = item;
    return true;
}

bool stack_matches(const BurgerStack *stack, const Customer *customer)
{
    if (stack->top != customer->length)
        return false;
    for (size_t i = 0; i < stack->top; ++i)
        if (stack->items[i] != customer->recipe[i])
            return false;
    return true;
}

void queue_init(CustomerQueue *queue)
{
    memset(queue, 0, sizeof(*queue));
}

bool queue_enqueue(CustomerQueue *queue, Customer customer)
{
    if (queue->count >= BURGER_QUEUE_CAPACITY)
        return false;
    queue->items[queue->rear] = customer;
    queue->rear = (queue->rear + 1) % BURGER_QUEUE_CAPACITY;
    ++queue->count;
    return true;
}

bool queue_dequeue(CustomerQueue *queue, Customer *removed)
{
    if (queue->count == 0)
        return false;
    if (removed != NULL)
        *removed = queue->items[queue->front];
    memset(&queue->items[queue->front], 0, sizeof(Customer));
    queue->front = (queue->front + 1) % BURGER_QUEUE_CAPACITY;
    --queue->count;
    return true;
}

const Customer *queue_at(const CustomerQueue *queue, size_t index)
{
    if (index >= queue->count)
        return NULL;
    return &queue->items[(queue->front + index) % BURGER_QUEUE_CAPACITY];
}

int game_stage(int served)
{
    int stage = 1 + served / 3;
    return stage > 8 ? 8 : stage;
}

int game_queue_target(int stage)
{
    int target = 2 + (stage - 1) / 2;
    return target > BURGER_QUEUE_CAPACITY ? BURGER_QUEUE_CAPACITY : target;
}

double game_patience(int stage)
{
    double seconds = 32.0 - 2.5 * (stage - 1);
    return seconds < 14.0 ? 14.0 : seconds;
}

double game_arrival_interval(int stage)
{
    double seconds = 8.0 - 0.7 * (stage - 1);
    return seconds < 3.0 ? 3.0 : seconds;
}

/* Seeded xorshift32 keeps tests repeatable without global rand() state. */
static uint32_t random_next(Game *game)
{
    uint32_t value = game->random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    game->random_state = value;
    return value;
}

static void add_customer(Game *game, double now)
{
    Customer customer = {0};
    customer.id = game->next_id++;
    customer.stage = game->stage;
    customer.arrived_at = now;
    customer.deadline = now + game_patience(game->stage);
    customer.length = (size_t)(4 + game->stage / 2);
    if (game->stage >= 3)
        customer.length += random_next(game) % 2;
    if (customer.length > BURGER_MAX_LAYERS)
        customer.length = BURGER_MAX_LAYERS;
    customer.recipe[0] = ING_BOTTOM;
    customer.recipe[1] = ING_PATTY;
    for (size_t i = 2; i + 1 < customer.length; ++i)
        customer.recipe[i] = (Ingredient)(ING_PATTY + random_next(game) % 5);
    customer.recipe[customer.length - 1] = ING_TOP;
    queue_enqueue(&game->queue, customer);
}

static void event_set(Game *game, GameEvent event, double now)
{
    game->event = event;
    game->event_until = now + 2.5;
}

void game_init(Game *game, uint32_t seed, double now, double duration)
{
    memset(game, 0, sizeof(*game));
    game->random_state = seed == 0 ? UINT32_C(0x9e3779b9) : seed;
    game->next_id = 1;
    game->lives = BURGER_INITIAL_LIVES;
    game->stage = 1;
    game->started_at = now;
    game->last_tick = now;
    game->ends_at = now + duration;
    game->next_arrival = now + game_arrival_interval(game->stage);
    event_set(game, EVENT_READY, now);
    add_customer(game, now);
}

/* Process deadlines and arrivals chronologically, even after a slow frame.
 * Only the head can be served; any waiting customer may run out of patience.
 * Rotate the circular queue once to remove expired entries in FIFO order. */
static void expire_customers(Game *game, double now)
{
    size_t count = game->queue.count;
    for (size_t i = 0; i < count; ++i) {
        Customer customer = {0};
        if (!queue_dequeue(&game->queue, &customer))
            break;
        if (customer.deadline <= now) {
            if (i == 0)
                stack_init(&game->burger);
            if (game->lives > 0) {
                --game->lives;
                ++game->missed;
            }
            game->combo = 0;
            event_set(game, EVENT_TIMEOUT, now);
        } else {
            queue_enqueue(&game->queue, customer);
        }
    }
}

void game_tick(Game *game, double now)
{
    if (game->ended != END_NONE || !isfinite(now) || now < game->last_tick)
        return;
    double limit = now < game->ends_at ? now : game->ends_at;
    for (;;) {
        double next = game->next_arrival;
        for (size_t i = 0; i < game->queue.count; ++i) {
            double deadline = queue_at(&game->queue, i)->deadline;
            if (deadline < next)
                next = deadline;
        }
        /* At the round boundary, ending the round takes precedence. */
        if (next > limit || next >= game->ends_at)
            break;
        expire_customers(game, next);
        if (game->lives == 0) {
            game->ended = END_LIVES;
            game->last_tick = next;
            return;
        }
        if (game->next_arrival <= next) {
            if (game->queue.count < (size_t)game_queue_target(game->stage))
                add_customer(game, next);
            game->next_arrival = next + game_arrival_interval(game->stage);
        }
        if (game->queue.count == 0)
            add_customer(game, next);
    }
    game->last_tick = limit;
    if (now >= game->ends_at)
        game->ended = END_TIME;
}

void game_input(Game *game, int key, double now)
{
    game_tick(game, now);
    if (game->ended != END_NONE || now < game->last_tick || !isfinite(now))
        return;
    if (key >= '1' && key <= '7') {
        bool ok = stack_push(&game->burger, (Ingredient)(key - '0'));
        event_set(game, ok ? EVENT_ADDED : EVENT_FULL, now);
        return;
    }
    if (key < 0 || key > 127)
        return;
    key = toupper((unsigned char)key);
    if (key == 'U' || key == 'Z' || key == 8 || key == 127) {
        bool ok = stack_pop(&game->burger, NULL);
        event_set(game, ok ? EVENT_UNDONE : EVENT_EMPTY, now);
    } else if (key == 'S' || key == ' ') {
        const Customer *customer = queue_at(&game->queue, 0);
        if (customer == NULL) {
            event_set(game, EVENT_NO_CUSTOMER, now);
            return;
        }
        if (stack_matches(&game->burger, customer)) {
            ++game->combo;
            if (game->combo > game->best_combo)
                game->best_combo = game->combo;
            int speed = (int)((customer->deadline - now) / 2.0);
            int combo_bonus = game->combo > 10 ? 10 : game->combo;
            game->last_points = 100 + (int)customer->length * 10 + speed * 5
                              + (combo_bonus - 1) * 20;
            game->score += game->last_points;
            ++game->served;
            game->stage = game_stage(game->served);
            event_set(game, EVENT_CORRECT, now);
        } else {
            --game->lives;
            ++game->missed;
            game->combo = 0;
            game->last_points = 0;
            event_set(game, EVENT_WRONG, now);
        }
        queue_dequeue(&game->queue, NULL);
        stack_init(&game->burger);
        if (game->lives == 0) {
            game->ended = END_LIVES;
        } else if (game->queue.count == 0) {
            add_customer(game, now);
            game->next_arrival = now + game_arrival_interval(game->stage);
        }
    } else if (key == 'Q' || key == 27) {
        game->ended = END_QUIT;
    }
}

double game_time_left(const Game *game, double now)
{
    double remaining = game->ends_at - now;
    return remaining > 0 ? remaining : 0;
}

const char *ingredient_name(Ingredient ingredient)
{
    static const char *names[] = {
        "?", "아래 빵", "패티", "치즈", "양상추", "토마토", "피클", "위 빵"
    };
    return ingredient_valid(ingredient) ? names[ingredient] : names[0];
}

const char *ingredient_short_name(Ingredient ingredient)
{
    static const char *names[] = {
        "?", "BOTTOM BUN", "PATTY", "CHEESE", "LETTUCE", "TOMATO", "PICKLE", "TOP BUN"
    };
    return ingredient_valid(ingredient) ? names[ingredient] : names[0];
}

bool game_invariant(const Game *game)
{
    const CustomerQueue *queue = &game->queue;
    if (game->burger.top > BURGER_MAX_LAYERS || queue->count > BURGER_QUEUE_CAPACITY
        || queue->front >= BURGER_QUEUE_CAPACITY || queue->rear >= BURGER_QUEUE_CAPACITY
        || queue->rear != (queue->front + queue->count) % BURGER_QUEUE_CAPACITY
        || game->lives < 0 || game->lives > BURGER_INITIAL_LIVES || game->score < 0
        || game->stage != game_stage(game->served) || game->random_state == 0)
        return false;
    for (size_t i = 0; i < game->burger.top; ++i)
        if (!ingredient_valid(game->burger.items[i]))
            return false;
    for (size_t i = 0; i < queue->count; ++i) {
        const Customer *customer = queue_at(queue, i);
        if (customer->length < 3 || customer->length > BURGER_MAX_LAYERS
            || customer->recipe[0] != ING_BOTTOM
            || customer->recipe[customer->length - 1] != ING_TOP
            || customer->deadline <= customer->arrived_at)
            return false;
        for (size_t j = 0; j < customer->length; ++j)
            if (!ingredient_valid(customer->recipe[j]))
                return false;
    }
    return true;
}
