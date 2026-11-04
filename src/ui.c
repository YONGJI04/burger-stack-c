#include "ui.h"
#include "art.h"
#include "terminal.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MAX_WIDTH 96
#define MAX_HEIGHT 32
#define FRAME_BYTES 262144

typedef struct {
    char text[5];
    unsigned char foreground, background;
    bool continuation;
} Cell;
static Cell canvas[MAX_HEIGHT][MAX_WIDTH];
static int width = 80, height = 24;
static bool roomy;

static void cell_set(int x, int y, const char *text, Ink foreground, Ink background)
{
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    Cell *cell = &canvas[y][x];
    if (cell->continuation && x > 0) {
        strcpy(canvas[y][x - 1].text, " ");
        canvas[y][x - 1].continuation = false;
    }
    if (x + 1 < width && canvas[y][x + 1].continuation) {
        strcpy(canvas[y][x + 1].text, " ");
        canvas[y][x + 1].continuation = false;
    }
    snprintf(cell->text, sizeof(cell->text), "%s", text);
    cell->foreground = (unsigned char)foreground;
    cell->background = (unsigned char)background;
    cell->continuation = false;
}

static void fill(int x, int y, int columns, int rows, Ink background)
{
    for (int row = y; row < y + rows; ++row)
        for (int col = x; col < x + columns; ++col)
            cell_set(col, row, " ", INK_TEXT, background);
}

static void blank(void)
{
    int columns, rows;
    terminal_size(&columns, &rows);
    roomy = columns >= 96 && rows >= 32;
    width = roomy ? 96 : 80;
    height = roomy ? 32 : 24;
    memset(canvas, 0, sizeof(canvas));
    fill(0, 0, width, height, INK_BACK);
}

static void text_limit(int x, int y, Ink ink, int limit, const char *text)
{
    if (y < 0 || y >= height || x < 0) return;
    int end = x + limit < width - 1 ? x + limit : width - 1;
    while (*text && x < end) {
        uint32_t cp;
        size_t bytes = utf8_decode(text, &cp);
        int columns = utf8_width(cp);
        if (columns == 0) { text += bytes; continue; }
        if (x + columns > end) break;
        char glyph[5] = {0};
        memcpy(glyph, text, bytes);
        cell_set(x, y, glyph, ink, (Ink)canvas[y][x].background);
        if (columns == 2) {
            canvas[y][x + 1].continuation = true;
            canvas[y][x + 1].text[0] = '\0';
        }
        x += columns;
        text += bytes;
    }
}

static void text_at(int x, int y, Ink ink, const char *format, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    text_limit(x, y, ink, width, buffer);
}

static void rule(int y)
{
    cell_set(0, y, "+", INK_LINE, INK_BACK);
    for (int x = 1; x < width - 1; ++x)
        cell_set(x, y, "-", INK_LINE, INK_BACK);
    cell_set(width - 1, y, "+", INK_LINE, INK_BACK);
}

static void border(const char *title, const char *subtitle)
{
    blank();
    rule(0);
    rule(height - 1);
    for (int y = 1; y < height - 1; ++y) {
        cell_set(0, y, "|", INK_LINE, INK_BACK);
        cell_set(width - 1, y, "|", INK_LINE, INK_BACK);
    }
    text_at(3, 1, INK_GOLD, "%s", title);
    text_at(3, 2, INK_MUTED, "%s", subtitle);
    rule(3);
}

static void present(void)
{
    static char frame[FRAME_BYTES];
    size_t used = 0;
    int last_fg = -1, last_bg = -1;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const Cell *cell = &canvas[y][x];
            if (cell->continuation) continue;
            if (terminal_color()) {
                if (cell->foreground != last_fg) {
                    int n = snprintf(frame + used, sizeof(frame) - used, "\033[38;2;%sm",
                                     art_ink_rgb((Ink)cell->foreground));
                    used += (size_t)n;
                    last_fg = cell->foreground;
                }
                if (cell->background != last_bg) {
                    int n = snprintf(frame + used, sizeof(frame) - used, "\033[48;2;%sm",
                                     art_ink_rgb((Ink)cell->background));
                    used += (size_t)n;
                    last_bg = cell->background;
                }
            }
            size_t count = strlen(cell->text);
            memcpy(frame + used, cell->text, count);
            used += count;
        }
        if (y < height - 1) { frame[used++] = '\r'; frame[used++] = '\n'; }
    }
    frame[used] = '\0';
    terminal_present(frame);
}

static void meter(int x, int y, int columns, double ratio, Ink ink)
{
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    int filled = (int)ceil(ratio * columns);
    for (int i = 0; i < columns; ++i)
        text_at(x + i, y, i < filled ? ink : INK_LINE, "%s",
                terminal_color() ? "━" : i < filled ? "=" : "-");
}

static const char *monochrome_pixel(Ink ink)
{
    switch (ink) {
    case INK_PANEL: return " ";
    case INK_MEAT: case INK_MEAT_LIGHT: case INK_GRILL: return "#";
    case INK_LETTUCE: case INK_LETTUCE_LIGHT: return "~";
    case INK_PICKLE: return "o";
    case INK_TOMATO: case INK_TOMATO_LIGHT: return "O";
    case INK_CHEESE: case INK_CHEESE_LIGHT: return "=";
    case INK_SESAME: return "'";
    case INK_PLATE: case INK_SHADOW: return "_";
    default: return ":";
    }
}

static int draw_burger(int x, int y, int rows, const BurgerStack *burger, int lift)
{
    BurgerArt art;
    art_burger(&art, burger, rows * 2, lift);
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < ART_WIDTH; ++col) {
            Ink upper = (Ink)art.pixels[row * 2][col];
            Ink lower = (Ink)art.pixels[row * 2 + 1][col];
            if (terminal_color()) {
                cell_set(x + col, y + row, "▀", upper, lower);
            } else {
                Ink sample = upper != INK_PANEL ? upper : lower;
                cell_set(x + col, y + row, monochrome_pixel(sample), INK_TEXT, INK_PANEL);
            }
        }
    }
    return y + art.top_row / 2;
}

static BurgerStack house_burger(void)
{
    BurgerStack burger = {.items = {ING_BOTTOM, ING_PATTY, ING_CHEESE, ING_LETTUCE,
                                   ING_TOMATO, ING_PICKLE, ING_TOP}, .top = 7};
    return burger;
}

void ui_menu(int selected, int duration)
{
    border("BURGER STACK  /  햄버거 만들기", "한 층씩 쌓아 올리는 나만의 작은 버거 가게");
    int art_x = roomy ? 56 : 45;
    int art_y = roomy ? 7 : 6;
    fill(art_x - 2, 5, 34, roomy ? 20 : 14, INK_PANEL);
    text_at(6, 6, INK_GOLD, "H A M B U R G E R");
    text_at(6, 8, INK_TEXT, "갓 구운 빵, 맛있는 한 판.");
    text_at(6, 9, INK_MUTED, "주문을 보고, 쌓고, 서빙하세요.");
    text_at(6, 11, INK_CYAN, "%d초 영업 / 목숨 3개", duration);
    const char *items[] = {"1  게임 시작", "2  TOP 10 랭킹", "3  게임 방법", "4  종료"};
    int menu_y = roomy ? 14 : 13;
    for (int i = 0; i < 4; ++i) {
        if (selected == i) fill(5, menu_y + i, 31, 1, INK_PANEL);
        text_at(6, menu_y + i, selected == i ? INK_GOLD : INK_MUTED,
                "%s  %s", selected == i ? ">" : " ", items[i]);
    }
    BurgerStack burger = house_burger();
    draw_burger(art_x, art_y, roomy ? 17 : 12, &burger, 0);
    if (roomy) {
        text_at(58, 24, INK_GOLD, "THE HOUSE SPECIAL");
        text_at(6, 21, INK_MUTED, "첫 주문은 치즈버거로 시작해요.");
        text_at(6, 22, INK_MUTED, "[1] [2] [3] [7] 그리고 [S]!");
    }
    rule(height - 4);
    text_at(3, height - 3, INK_TEXT, "[1~4] 선택    [위/아래] 이동    [Enter] 확인    [Q] 종료");
    text_at(3, height - 2, INK_MUTED, "96 x 32 이상이면 넓은 화면 / 최소 80 x 24 / C11");
    present();
}

void ui_help(void)
{
    border("HOW TO PLAY  /  게임 방법", "주문서의 위에서 아래 순서대로, 햄버거는 바닥부터 쌓아요.");
    text_at(3, 5, INK_GOLD, "[1] 아래 빵  [2] 패티  [3] 치즈  [4] 양상추");
    text_at(3, 6, INK_GOLD, "[5] 토마토   [6] 피클  [7] 위 빵");
    text_at(3, 8, INK_TEXT, "[U / Backspace] 맨 위 재료 빼기    [S / Space] 제출");
    text_at(3, 9, INK_TEXT, "[P] 잠깐 쉬기    [Q / Esc] 영업 마감 후 점수 저장");
    text_at(3, 11, INK_CYAN, "1. 주문서의 > 표시가 다음에 올릴 재료예요.");
    text_at(3, 12, INK_CYAN, "2. + 는 맞는 재료, ! 는 다른 재료. 틀렸다면 U로 되돌리세요.");
    text_at(3, 13, INK_CYAN, "3. 모두 맞으면 S! 빠른 서빙과 연속 성공은 추가 점수를 줘요.");
    text_at(3, 15, INK_TEXT, "잘못 제출하거나 손님이 떠나면 목숨 -1. 대기 손님도 시간이 흘러요.");
    text_at(3, 16, INK_TEXT, "3명 성공할 때마다 단계 상승. 주문과 손님 수가 점점 늘어납니다.");
    text_at(3, 18, INK_GOLD, "첫 주문 연습: [1] 아래 빵 > [2] 패티 > [3] 치즈 > [7] 위 빵 > [S]");
    if (roomy) {
        text_at(3, 21, INK_MUTED, "손님 표정과 시간 막대를 함께 확인하세요.");
        text_at(3, 22, INK_MUTED, "잠깐 쉬거나 창 크기를 조절할 때는 모든 타이머가 멈춥니다.");
        text_at(3, 23, INK_MUTED, "종료 후 이름을 넣으면 이 컴퓨터의 랭킹에 저장할 수 있어요.");
    }
    rule(height - 4);
    text_at(3, height - 3, INK_GOLD, "[Enter / Esc / Q] 메뉴로 돌아가기");
    text_at(3, height - 2, INK_MUTED, "재료와 제출은 Enter 없이 즉시 반영됩니다.");
    present();
}

static size_t recipe_prefix(const Game *game, const Customer *order)
{
    size_t matched = 0;
    if (order != NULL)
        while (matched < game->burger.top && matched < order->length
               && game->burger.items[matched] == order->recipe[matched]) ++matched;
    return matched;
}

static void game_feedback(const Game *game, double now, const Customer *order, size_t matched)
{
    int y = height - 5;
    if (now < game->feedback_until) {
        if (game->feedback == EVENT_CORRECT) {
            fill(2, y, width - 4, 1, INK_PANEL);
            text_at(3, y, INK_GREEN, "서빙 성공! +%d점 / %d연속 성공%s", game->last_points,
                    game->combo, game->served % 3 == 0 && game->served <= 21
                                 ? " / STAGE UP!" : " / 맛있게 드세요!");
        } else if (game->feedback == EVENT_WRONG) {
            text_at(3, y, INK_RED, "주문과 달라요! 목숨 -1. 다음 주문부터 다시 시작해요.");
        } else if (game->feedback == EVENT_TIMEOUT) {
            text_at(3, y, INK_RED, "기다리던 손님이 떠났어요! 목숨 -1. 남은 손님을 확인하세요.");
        }
        return;
    }
    if (now < game->event_until && game->event == EVENT_FULL) {
        text_at(3, y, INK_RED, "접시가 가득 찼어요. U로 맨 위 재료를 빼 주세요.");
    } else if (game->burger.top > matched) {
        text_at(3, y, INK_RED, "순서를 확인하세요! [U] %zu번으로 틀린 재료부터 되돌릴 수 있어요.",
                game->burger.top - matched);
    } else if (order != NULL && matched == order->length) {
        text_at(3, y, INK_GREEN, "완벽하게 쌓았어요! [S / Space]로 손님에게 서빙하세요.");
    } else if (order != NULL) {
        text_at(3, y, INK_TEXT, "다음 재료: [%d] %s  /  잘못 넣었으면 [U]로 되돌리세요.",
                order->recipe[matched], ingredient_name(order->recipe[matched]));
    }
}

void ui_game(const Game *game, double now)
{
    border("BURGER STACK", "");
    text_at(24, 1, INK_TEXT, "SCORE %6d", game->score);
    text_at(45, 1, game_time_left(game, now) <= 15 ? INK_RED : INK_CYAN,
            "TIME %3.0fs", ceil(game_time_left(game, now)));
    text_at(64, 1, INK_GOLD, "STAGE %d", game->stage);
    text_at(3, 2, game->lives == 1 ? INK_RED : INK_GREEN, "LIFE %s %s %s",
            game->lives >= 1 ? "<3" : "--", game->lives >= 2 ? "<3" : "--",
            game->lives >= 3 ? "<3" : "--");
    text_at(25, 2, INK_MUTED, "SERVED %d   COMBO x%d", game->served, game->combo);
    if (game->stage == 8) text_at(57, 2, INK_GOLD, "MAX STAGE");
    else text_at(57, 2, INK_MUTED, "NEXT %d/3", game->served % 3);
    int right = roomy ? 48 : 42;
    int bottom = height - (roomy ? 8 : 7);
    fill(2, 5, right - 5, bottom - 4, INK_PANEL);
    fill(right, 5, width - right - 2, bottom - 4, INK_PANEL);
    text_at(3, 4, INK_CYAN, "ORDER QUEUE / 손님 %zu", game->queue.count);
    text_at(right + 2, 4, INK_GOLD, "MY BURGER / %zu LAYERS", game->burger.top);
    for (size_t i = 0; i < game->queue.count; ++i) {
        const Customer *customer = queue_at(&game->queue, i);
        double remaining = customer->deadline - now;
        double ratio = remaining / (customer->deadline - customer->arrived_at);
        Ink color = ratio > 0.5 ? INK_GREEN : ratio > 0.25 ? INK_GOLD : INK_RED;
        int x = 3 + (int)i * (roomy ? 8 : 7);
        text_at(x, 5, color, "%s", ratio > 0.5 ? "(^_^)" : ratio > 0.25 ? "(-_-)" : "(>_<)");
        text_at(x, 6, INK_MUTED, "#%03u", customer->id);
        text_at(x, 7, color, "%3.0fs", ceil(remaining > 0 ? remaining : 0));
        if (roomy) meter(x, 8, 5, ratio, color);
    }
    const Customer *order = queue_at(&game->queue, 0);
    size_t matched = recipe_prefix(game, order);
    int receipt_y = roomy ? 10 : 8;
    if (order != NULL) {
        text_at(3, receipt_y, INK_CYAN, "주문 #%03u / %zu개 중 %zu개 완료", order->id, order->length, matched);
        int first_row = receipt_y + (roomy ? 2 : 1);
        for (size_t i = 0; i < order->length; ++i) {
            bool placed = i < game->burger.top;
            bool match = placed && game->burger.items[i] == order->recipe[i];
            bool next = i == matched && game->burger.top == matched;
            Ink color = placed ? (match ? INK_GREEN : INK_RED) : next ? INK_GOLD : INK_MUTED;
            if (next) fill(3, first_row + (int)i, right - 8, 1, INK_SHADOW);
            text_at(4, first_row + (int)i, color, "%s %2zu. [%d] %s",
                    placed ? (match ? "+" : "!") : next ? ">" : " ", i + 1,
                    order->recipe[i], ingredient_name(order->recipe[i]));
        }
        if (roomy) {
            meter(4, 22, 33, (double)matched / order->length, INK_GOLD);
            text_at(4, 24, INK_MUTED, "아래 빵부터 순서대로 쌓아 주세요.");
        }
    }
    bool serving = game->submitted_correct && game->submitted.top > 0
                && game->burger.top == 0 && now - game->submitted_at < 0.85;
    const BurgerStack *shown = serving ? &game->submitted : &game->burger;
    int art_x = right + (width - right - 2 - ART_WIDTH) / 2;
    int art_y = 6, art_rows = bottom - art_y + 1;
    int lift = !serving && now - game->last_layer_at < 0.12 ? 2 : 0;
    int top_row = draw_burger(art_x, art_y, art_rows, shown, lift);
    if (shown->top == 0) {
        text_at(art_x + 3, roomy ? 12 : 9, INK_MUTED, "비어 있는 따뜻한 접시");
        text_at(art_x + 3, roomy ? 14 : 11, INK_GOLD, "[1] 아래 빵부터 시작!");
    } else {
        text_at(width - 6, top_row, INK_CYAN, "TOP");
        if (serving) {
            text_at(art_x + 5, 5, INK_GREEN, "*  ORDER UP!  *");
            if (top_row > 8) {
                text_at(art_x + 3, top_row - 2, INK_GOLD, "+");
                text_at(art_x + 24, top_row - 3, INK_GOLD, "*");
            }
        } else if (roomy) text_at(art_x + 5, 5, INK_MUTED, "FRESH OFF THE GRILL");
    }
    if (roomy) {
        Ingredient top = game->burger.top > 0 ? game->burger.items[game->burger.top - 1] : 0;
        if (top) text_at(right + 3, 25, art_ingredient_ink(top), "맨 위: %s  [U] 되돌리기", ingredient_name(top));
        else text_at(right + 3, 25, INK_MUTED, "주문대로 한 층씩 올려 보세요.");
    }
    game_feedback(game, now, order, matched);
    rule(height - 4);
    int key_x = 2;
    const char *names[] = {"아래빵", "패티", "치즈", "양상추", "토마토", "피클", "위빵"};
    const int label_width[] = {10, 8, 8, 10, 10, 8, 8};
    for (int i = 0; i < 7; ++i) {
        text_at(key_x, height - 3, art_ingredient_ink((Ingredient)(i + 1)), "[%d]%s", i + 1, names[i]);
        key_x += label_width[i] + (roomy ? 2 : 0);
    }
    text_at(2, height - 2, INK_TEXT, "[U]되돌리기  [S/Space]서빙  [P]잠깐 쉬기  [Q/Esc]마감");
    present();
}

void ui_pause(const Game *game)
{
    border("TAKE A BREAK  /  잠깐 쉬는 중", "게임 시간과 모든 손님의 기다리는 시간이 멈췄습니다.");
    fill(4, 5, width - 8, height - 10, INK_PANEL);
    text_at(8, 7, INK_GOLD, "PAUSED");
    text_at(8, 9, INK_TEXT, "다시 준비되면 이어서 만들어 봐요.");
    text_at(8, 11, INK_CYAN, "현재 점수 %d / 서빙 %d명", game->score, game->served);
    text_at(8, 14, INK_TEXT, "[P / Enter / Esc] 계속하기");
    text_at(8, 16, INK_MUTED, "[Q] 영업 마감 후 점수 저장");
    rule(height - 4);
    text_at(3, height - 3, INK_GOLD, "쌓아 둔 재료와 주문서는 그대로 보관됩니다.");
    present();
}

void ui_results(const Game *game, const char *name, bool save_failed)
{
    border("SHIFT COMPLETE  /  영업 종료", "오늘도 수고했어요, 셰프!");
    const char *reason = game->ended == END_TIME ? "영업시간이 끝났습니다."
                       : game->ended == END_LIVES ? "목숨을 모두 사용했습니다." : "영업을 마감했습니다.";
    text_at(6, 5, INK_MUTED, "%s", reason);
    text_at(6, 7, INK_GOLD, "FINAL SCORE  %d", game->score);
    text_at(6, 9, INK_TEXT, "서빙 성공      %d명", game->served);
    text_at(6, 10, INK_TEXT, "실패 / 떠난 손님 %d명", game->missed);
    text_at(6, 11, INK_CYAN, "최고 콤보      %d연속", game->best_combo);
    text_at(6, 12, INK_CYAN, "도달 단계      %d", game->stage);
    text_at(46, 7, INK_GOLD, "%s", game->served >= 21 ? "MASTER CHEF" : game->served >= 9 ? "GREAT SHIFT" : "NICE COOKING");
    text_at(46, 9, INK_MUTED, "다음 영업에 다시 만나요!");
    if (roomy) {
        BurgerStack burger = house_burger();
        fill(55, 11, 32, 13, INK_PANEL);
        draw_burger(56, 11, 13, &burger, 0);
    }
    int input_y = roomy ? 20 : 15;
    text_at(6, input_y, INK_TEXT, "랭킹에 남길 이름 (한글 10자)");
    fill(6, input_y + 2, 30, 1, INK_PANEL);
    text_at(7, input_y + 2, INK_GOLD, "> %s_", name);
    if (save_failed)
        text_at(3, height - 5, INK_RED, "저장 실패: 경로와 쓰기 권한을 확인하세요. Enter로 다시 시도.");
    else text_at(6, height - 5, INK_MUTED, "이름을 비워두면 '셰프'로 저장합니다.");
    rule(height - 4);
    text_at(3, height - 3, INK_TEXT, "[Enter] 저장  [Backspace] 이름 수정  [Esc] 저장하지 않고 메뉴");
    text_at(3, height - 2, INK_MUTED, "이 컴퓨터의 랭킹에 저장됩니다.");
    present();
}

void ui_ranking(const Ranking *ranking, bool loaded, bool saved)
{
    border("HALL OF FAME  /  TOP 10", saved ? "점수를 저장했습니다! 다음 영업에서도 만나요." : "우리 가게 최고의 셰프들");
    text_at(8, 5, INK_MUTED, "RANK       NAME                           SCORE");
    if (!loaded) text_at(8, 8, INK_RED, "랭킹 파일을 읽을 수 없습니다. 경로와 권한을 확인하세요.");
    else if (ranking->count == 0) text_at(8, 9, INK_GOLD, "아직 기록이 없어요. 첫 번째 셰프가 되어 보세요!");
    else {
        for (size_t i = 0; i < ranking->count && i < RANKING_TOP_COUNT; ++i) {
            Ink ink = i == 0 ? INK_GOLD : i < 3 ? INK_CYAN : INK_TEXT;
            int row = 7 + (int)i;
            if (i % 2 == 0) fill(6, row, width - 12, 1, INK_PANEL);
            text_at(8, row, ink, "%2zu", i + 1);
            text_limit(19, row, ink, 20, ranking->entries[i].name);
            text_at(50, row, ink, "%10d", ranking->entries[i].score);
        }
    }
    if (loaded && ranking->skipped > 0)
        text_at(8, height - 6, INK_MUTED, "읽을 수 없는 기록 %zu개는 건너뛰었습니다.", ranking->skipped);
    rule(height - 4);
    text_at(3, height - 3, INK_GOLD, "[Enter / Esc / Q] 메뉴로 돌아가기");
    text_at(3, height - 2, INK_MUTED, "점수 내림차순 / 동점은 먼저 저장한 순서 / 컴퓨터별 저장");
    present();
}

bool ui_terminal_fits(void)
{
    int columns, rows;
    terminal_size(&columns, &rows);
    return columns >= 80 && rows >= 24;
}

void ui_small_terminal(int columns, int rows)
{
    char message[256];
    snprintf(message, sizeof(message),
             "\033[2J창을 80 x 24 이상으로 늘려주세요.\r\n현재: %d x %d\r\n"
             "크기 조절 중에는 시간이 멈춥니다.\r\n[Q / Esc] 돌아가기", columns, rows);
    terminal_present(message);
}
