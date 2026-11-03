#include "ui.h"
#include "terminal.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define WIDTH 80
#define HEIGHT 24
#define FRAME_BYTES 32768

enum Color { WHITE, MUTED, GOLD, RED, GREEN, CYAN, BROWN, YELLOW };
typedef struct { char text[5]; unsigned char color; bool continuation; } Cell;
static Cell canvas[HEIGHT][WIDTH];

static const char *color_code(int color)
{
    static const char *codes[] = {
        "\033[97m", "\033[90m", "\033[1;33m", "\033[91m", "\033[92m",
        "\033[96m", "\033[33m", "\033[93m"
    };
    return codes[color];
}

static void blank(void)
{
    memset(canvas, 0, sizeof(canvas));
    for (int y = 0; y < HEIGHT; ++y)
        for (int x = 0; x < WIDTH; ++x)
            strcpy(canvas[y][x].text, " ");
}

static void text_at(int x, int y, int color, const char *format, ...)
{
    if (y < 0 || y >= HEIGHT) return;
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    const char *p = buffer;
    while (*p && x < WIDTH) {
        uint32_t cp;
        size_t bytes = utf8_decode(p, &cp);
        int width = utf8_width(cp);
        if (width == 0) { p += bytes; continue; }
        if (x < 0) { x += width; p += bytes; continue; }
        if (x + width > WIDTH) break;
        Cell *cell = &canvas[y][x];
        memcpy(cell->text, p, bytes);
        cell->text[bytes] = '\0';
        cell->color = (unsigned char)color;
        cell->continuation = false;
        if (width == 2) {
            canvas[y][x + 1].continuation = true;
            canvas[y][x + 1].text[0] = '\0';
        }
        x += width;
        p += bytes;
    }
}

static void rule(int y)
{
    for (int x = 1; x < WIDTH - 1; ++x) text_at(x, y, MUTED, "-");
    text_at(0, y, MUTED, "+");
    text_at(WIDTH - 1, y, MUTED, "+");
}

static void ranking_name_at(int x, int y, int color, const char *name)
{
    char clipped[RANKING_NAME_BYTES];
    size_t used = 0;
    int columns = 0;
    while (*name) {
        uint32_t cp;
        size_t bytes = utf8_decode(name, &cp);
        int width = utf8_width(cp);
        if (columns + width > 20 || used + bytes >= sizeof(clipped)) break;
        memcpy(clipped + used, name, bytes);
        used += bytes;
        columns += width;
        name += bytes;
    }
    clipped[used] = '\0';
    text_at(x, y, color, "%s", clipped);
}

static void border(const char *title, const char *subtitle)
{
    blank();
    rule(0);
    rule(HEIGHT - 1);
    for (int y = 1; y < HEIGHT - 1; ++y) {
        text_at(0, y, MUTED, "|");
        text_at(WIDTH - 1, y, MUTED, "|");
    }
    text_at(3, 1, GOLD, "%s", title);
    text_at(3, 2, MUTED, "%s", subtitle);
    rule(3);
}

static void present(void)
{
    char frame[FRAME_BYTES];
    size_t used = 0;
    int last_color = -1;
    for (int y = 0; y < HEIGHT; ++y) {
        for (int x = 0; x < WIDTH; ++x) {
            Cell *cell = &canvas[y][x];
            if (cell->continuation) continue;
            if (terminal_color() && cell->color != last_color) {
                const char *code = color_code(cell->color);
                size_t count = strlen(code);
                memcpy(frame + used, code, count);
                used += count;
                last_color = cell->color;
            }
            size_t count = strlen(cell->text);
            memcpy(frame + used, cell->text, count);
            used += count;
        }
        if (y < HEIGHT - 1) {
            frame[used++] = '\r';
            frame[used++] = '\n';
        }
    }
    frame[used] = '\0';
    terminal_present(frame);
}

static int ingredient_color(Ingredient item)
{
    switch (item) {
    case ING_BOTTOM: case ING_TOP: return GOLD;
    case ING_PATTY: return BROWN;
    case ING_CHEESE: return YELLOW;
    case ING_LETTUCE: case ING_PICKLE: return GREEN;
    case ING_TOMATO: return RED;
    default: return WHITE;
    }
}

static const char *ingredient_art(Ingredient item)
{
    switch (item) {
    case ING_BOTTOM: return "  \\__________________/  ";
    case ING_PATTY: return " [####################] ";
    case ING_CHEESE: return "  /VVVVVVVVVVVVVVVVVV\\  ";
    case ING_LETTUCE: return " ~^~^~^~^~^~^~^~^~^~^~  ";
    case ING_TOMATO: return "  (O)(O)(O)(O)(O)(O)    ";
    case ING_PICKLE: return "   [o] [o] [o] [o]     ";
    case ING_TOP: return "  / .  '  .  '  .  ' \\  ";
    default: return "";
    }
}

void ui_menu(int selected, int duration)
{
    border("BURGER STACK  /  햄버거 만들기", "자료구조로 만드는 작은 햄버거 가게");
    text_at(8, 6, GOLD, "H A M B U R G E R");
    text_at(8, 8, WHITE, "주문대로 쌓고, 맛있게 서빙!");
    text_at(8, 10, MUTED, "%d초 영업  /  목숨 3개  /  최고 단계 8", duration);
    const char *items[] = {"1  게임 시작", "2  TOP 10 랭킹", "3  게임 방법", "4  종료"};
    for (int i = 0; i < 4; ++i)
        text_at(8, 13 + i, selected == i ? GOLD : WHITE,
                "%s  %s", selected == i ? ">" : " ", items[i]);
    Ingredient illustration[] = {ING_TOP, ING_TOMATO, ING_LETTUCE, ING_CHEESE, ING_PATTY, ING_BOTTOM};
    for (int i = 0; i < 6; ++i)
        text_at(49, 8 + i, ingredient_color(illustration[i]), "%s", ingredient_art(illustration[i]));
    text_at(51, 15, MUTED, "====================");
    rule(20);
    text_at(3, 21, CYAN, "[1~4] 선택    [위/아래] 이동    [Enter] 확인    [Q] 종료");
    text_at(3, 22, MUTED, "STACK: push / pop    QUEUE: first in, first out    C11");
    present();
}

void ui_help(void)
{
    border("HOW TO PLAY  /  게임 방법", "주문서의 숫자를 위에서 아래로 읽으며, 햄버거는 아래부터 쌓아요.");
    text_at(3, 5, GOLD, "[1] 아래 빵  [2] 패티  [3] 치즈  [4] 양상추");
    text_at(3, 6, GOLD, "[5] 토마토   [6] 피클  [7] 위 빵");
    text_at(3, 8, WHITE, "[U] 맨 위 재료 되돌리기 (pop)    [S / Space] 제출");
    text_at(3, 9, WHITE, "[Q / Esc] 영업 마감 후 점수 저장 화면으로");
    text_at(3, 11, CYAN, "1. 맨 앞 손님의 주문서에 표시된 순서대로 숫자키를 누르세요.");
    text_at(3, 12, CYAN, "2. 순서와 개수가 모두 맞으면 성공! 빠른 서빙과 콤보는 추가 점수.");
    text_at(3, 13, CYAN, "3. 잘못 제출하거나 손님의 인내심이 바닥나면 목숨이 1개 줄어요.");
    text_at(3, 15, WHITE, "3명 성공할 때마다 단계 상승: 주문은 길어지고 손님은 급해집니다.");
    text_at(3, 16, WHITE, "대기 중인 손님도 시간이 흐릅니다. 표정과 남은 초를 확인하세요.");
    text_at(3, 17, WHITE, "시간이 끝나거나 목숨이 0이면 종료. 이름을 넣어 TOP 10에 도전!");
    text_at(3, 19, MUTED, "예시: 아래 빵 > 패티 > 치즈 > 위 빵  =  [1] [2] [3] [7] [S]");
    rule(20);
    text_at(3, 21, GOLD, "[Enter / Esc / Q] 메뉴로 돌아가기");
    text_at(3, 22, MUTED, "재료와 제출은 Enter 없이 즉시 반영됩니다. 권장 화면: 80 x 24 이상");
    present();
}

void ui_game(const Game *game, double now)
{
    border("BURGER STACK", "");
    text_at(21, 1, WHITE, "SCORE %5d", game->score);
    text_at(39, 1, game_time_left(game, now) <= 15 ? RED : CYAN,
            "TIME %3.0fs", ceil(game_time_left(game, now)));
    text_at(56, 1, GOLD, "STAGE %d", game->stage);
    text_at(3, 2, game->lives == 1 ? RED : GREEN,
            "LIFE %s %s %s", game->lives >= 1 ? "<3" : "--",
            game->lives >= 2 ? "<3" : "--", game->lives >= 3 ? "<3" : "--");
    text_at(29, 2, MUTED, "SERVED %d     COMBO x%d     NEXT %d/3", game->served,
            game->combo, game->served % 3);
    text_at(3, 4, CYAN, "ORDER QUEUE / 손님 %zu", game->queue.count);
    text_at(45, 4, GOLD, "MY BURGER / 재료 %zu개", game->burger.top);
    for (int y = 4; y < 19; ++y) text_at(41, y, MUTED, "|");
    for (size_t i = 0; i < game->queue.count; ++i) {
        const Customer *customer = queue_at(&game->queue, i);
        double remaining = customer->deadline - now;
        double ratio = remaining / (customer->deadline - customer->arrived_at);
        const char *face = ratio > 0.5 ? "(^_^)" : ratio > 0.25 ? "(-_-)" : "(>_<)";
        int color = ratio > 0.5 ? GREEN : ratio > 0.25 ? YELLOW : RED;
        int x = 3 + (int)i * 7;
        text_at(x, 5, color, "%s", face);
        text_at(x, 6, MUTED, "#%03u", customer->id);
        text_at(x, 7, color, "%3.0fs", ceil(remaining > 0 ? remaining : 0));
    }
    const Customer *customer = queue_at(&game->queue, 0);
    if (customer != NULL) {
        text_at(3, 8, CYAN, "주문 #%03u / 아래부터 순서대로", customer->id);
        for (size_t i = 0; i < customer->length; ++i) {
            bool placed = i < game->burger.top;
            bool match = placed && game->burger.items[i] == customer->recipe[i];
            int color = placed ? (match ? GREEN : RED) : WHITE;
            text_at(3, 9 + (int)i, color, "%s %2zu. [%d] %s",
                    placed ? (match ? "+" : "!") : " ", i + 1,
                    customer->recipe[i], ingredient_name(customer->recipe[i]));
        }
    }
    if (game->burger.top == 0) {
        text_at(47, 10, MUTED, "아직 비어 있는 접시");
        text_at(46, 12, WHITE, "[1] 아래 빵부터 시작!");
    } else {
        for (size_t i = 0; i < game->burger.top; ++i) {
            Ingredient item = game->burger.items[i];
            int y = 17 - (int)i;
            text_at(44, y, ingredient_color(item), "%s", ingredient_art(item));
            if (i + 1 == game->burger.top) text_at(70, y, CYAN, "< TOP");
        }
    }
    text_at(45, 18, MUTED, "============================");
    const char *message = "주문서를 보고 숫자키로 재료를 쌓으세요.";
    int message_color = WHITE;
    if (now < game->event_until) {
        switch (game->event) {
        case EVENT_CORRECT:
            text_at(3, 19, GREEN, "서빙 성공! +%d점  /  %d연속 성공", game->last_points, game->combo);
            message = NULL; break;
        case EVENT_WRONG: message = "주문과 달라요! 목숨 -1. 다음 손님의 주문을 확인하세요."; message_color = RED; break;
        case EVENT_TIMEOUT: message = "기다리던 손님이 떠났어요! 목숨 -1."; message_color = RED; break;
        case EVENT_EMPTY: message = "아직 재료가 없어요. 숫자키로 재료를 올리세요."; break;
        case EVENT_FULL: message = "더 쌓을 수 없어요! U로 되돌리거나 S로 제출하세요."; message_color = RED; break;
        case EVENT_UNDONE: message = "맨 위 재료를 하나 뺐어요. (stack pop)"; break;
        default: break;
        }
    }
    if (message != NULL) text_at(3, 19, message_color, "%s", message);
    rule(20);
    text_at(2, 21, GOLD, "[1]아래빵 [2]패티 [3]치즈 [4]양상추 [5]토마토 [6]피클 [7]위빵");
    text_at(2, 22, CYAN, "[U]되돌리기(pop)  [S/Space]제출  [Q/Esc]마감   |   Enter 없이 입력");
    present();
}

void ui_results(const Game *game, const char *name, bool save_failed)
{
    border("SHIFT COMPLETE  /  영업 종료", "오늘도 수고했어요, 셰프!");
    const char *reason = game->ended == END_TIME ? "영업시간이 끝났습니다."
                       : game->ended == END_LIVES ? "목숨을 모두 사용했습니다." : "영업을 마감했습니다.";
    text_at(8, 5, MUTED, "%s", reason);
    text_at(8, 7, GOLD, "FINAL SCORE     %d", game->score);
    text_at(8, 9, WHITE, "서빙 성공       %d명", game->served);
    text_at(8, 10, WHITE, "실패 / 떠난 손님 %d명", game->missed);
    text_at(8, 11, CYAN, "최고 콤보       %d연속", game->best_combo);
    text_at(8, 12, CYAN, "도달 단계       %d", game->stage);
    text_at(8, 15, WHITE, "랭킹에 남길 이름 (최대 20칸, 한글 10자)");
    text_at(8, 17, GOLD, "> %s_", name);
    if (save_failed)
        text_at(3, 19, RED, "저장 실패: 경로와 쓰기 권한을 확인하세요. Enter로 다시 시도.");
    else
        text_at(8, 19, MUTED, "이름을 비워두면 '셰프'로 저장합니다.");
    rule(20);
    text_at(3, 21, CYAN, "[Enter] 점수 저장    [Backspace] 이름 수정    [Esc] 저장하지 않고 메뉴");
    text_at(3, 22, MUTED, "결과는 이 컴퓨터의 ranking.txt에 보관됩니다.");
    present();
}

void ui_ranking(const Ranking *ranking, bool loaded, bool saved)
{
    border("HALL OF FAME  /  TOP 10", saved ? "점수를 저장했습니다! 다음 영업에서도 만나요." : "우리 가게 최고의 셰프들");
    text_at(8, 5, MUTED, "RANK       NAME                           SCORE");
    if (!loaded) {
        text_at(8, 8, RED, "랭킹 파일을 읽을 수 없습니다. 경로와 권한을 확인하세요.");
    } else if (ranking->count == 0) {
        text_at(8, 9, GOLD, "아직 기록이 없어요. 첫 번째 셰프가 되어 보세요!");
    } else {
        for (size_t i = 0; i < ranking->count && i < RANKING_TOP_COUNT; ++i) {
            int color = i == 0 ? GOLD : i < 3 ? CYAN : WHITE;
            text_at(8, 7 + (int)i, color, "%2zu", i + 1);
            ranking_name_at(19, 7 + (int)i, color, ranking->entries[i].name);
            text_at(50, 7 + (int)i, color, "%10d", ranking->entries[i].score);
        }
    }
    if (loaded && ranking->skipped > 0)
        text_at(8, 18, MUTED, "읽을 수 없는 기록 %zu개는 건너뛰었습니다.", ranking->skipped);
    rule(20);
    text_at(3, 21, GOLD, "[Enter / Esc / Q] 메뉴로 돌아가기");
    text_at(3, 22, MUTED, "점수 내림차순 / 동점은 먼저 저장한 기록 순서 / 기록은 컴퓨터별 저장");
    present();
}

bool ui_terminal_fits(void)
{
    int columns, rows;
    terminal_size(&columns, &rows);
    return columns >= WIDTH && rows >= HEIGHT;
}

void ui_small_terminal(int columns, int rows)
{
    char message[256];
    snprintf(message, sizeof(message),
             "\033[2J창을 80 x 24 이상으로 늘려주세요.\r\n현재: %d x %d\r\n"
             "크기 조절 중에는 시간이 멈춥니다.\r\n[Q / Esc] 종료", columns, rows);
    terminal_present(message);
}
