#include <rum/tools.h>
#include "model.h"

#define WIDTH 80u
#define HEIGHT 24u
#define LEFT 19u
#define TOP 4u
static struct snake_model model;
static struct rum_terminal_cell frame[HEIGHT][WIDTH];
static unsigned best, stored_best;
static int paused, save_failed;

static unsigned decimal(char *text, unsigned value)
{
    unsigned n = 0;
    do { text[n++] = (char)('0' + value % 10); value /= 10; } while (value);
    for (unsigned i = 0; i < n / 2; ++i) { char c = text[i]; text[i] = text[n - 1 - i]; text[n - 1 - i] = c; }
    text[n] = 0; return n;
}
static unsigned load_best(void)
{
    rum_result_t fd = rum_open("/SNAKE.DAT", RUM_OPEN_READ, 0);
    if (fd < 0) return 0;
    unsigned char bytes[5]; rum_result_t count = 0; int failed = 0;
    while ((unsigned)count < sizeof bytes) {
        rum_result_t read = rum_read((rum_handle_t)fd, bytes + count, sizeof bytes - (unsigned)count);
        if (read < 0) { failed = 1; break; }
        if (!read) break;
        count += read;
    }
    rum_result_t closed = rum_close((rum_handle_t)fd);
    if (failed || count <= 0 || count > 4 || closed < 0) return 0;
    if (bytes[count - 1] == '\n') --count;
    if (!count) return 0;
    unsigned value = 0;
    for (rum_result_t i = 0; i < count; ++i) {
        if (bytes[i] < '0' || bytes[i] > '9') return 0;
        value = value * 10 + bytes[i] - '0';
    }
    return value <= SNAKE_CELLS - SNAKE_INITIAL_LENGTH ? value : 0;
}
static void save_best(void)
{
    if (best <= stored_best) return;
    char text[12]; unsigned n = decimal(text, best); text[n++] = '\n';
    if (rum_replace("/SNAKE.DAT", text, n) < 0) save_failed = 1;
    else { stored_best = best; save_failed = 0; }
}
static void cell(unsigned x, unsigned y, char c, unsigned color)
{
    if (x < WIDTH && y < HEIGHT) frame[y][x] = (struct rum_terminal_cell){(uint8_t)c, (uint8_t)color, RUM_COLOR_BLACK, 0};
}
static void text(unsigned x, unsigned y, const char *value, unsigned color)
{
    while (*value && x < WIDTH) cell(x++, y, *value++, color);
}
static rum_result_t render(void)
{
    for (unsigned y = 0; y < HEIGHT; ++y) for (unsigned x = 0; x < WIDTH; ++x) cell(x, y, ' ', RUM_COLOR_LIGHT_GREY);
    text(LEFT, 0, "rum / snake", RUM_COLOR_LIGHT_CYAN);
    char digits[12];
    text(LEFT, 2, "score: ", RUM_COLOR_WHITE); decimal(digits, snake_model_score(&model));
    text(LEFT + 7, 2, digits, RUM_COLOR_LIGHT_GREEN);
    text(LEFT + 17, 2, "best: ", RUM_COLOR_WHITE); decimal(digits, best);
    text(LEFT + 23, 2, digits, RUM_COLOR_YELLOW);
    for (unsigned x = 0; x < SNAKE_WIDTH + 2; ++x) {
        cell(LEFT + x, TOP, '#', RUM_COLOR_CYAN); cell(LEFT + x, TOP + SNAKE_HEIGHT + 1, '#', RUM_COLOR_CYAN);
    }
    for (unsigned y = 0; y < SNAKE_HEIGHT; ++y) {
        cell(LEFT, TOP + 1 + y, '#', RUM_COLOR_CYAN); cell(LEFT + SNAKE_WIDTH + 1, TOP + 1 + y, '#', RUM_COLOR_CYAN);
    }
    if (!model.won) cell(LEFT + 1 + model.food.x, TOP + 1 + model.food.y, '*', RUM_COLOR_YELLOW);
    for (unsigned i = model.length; i-- > 0;) {
        struct snake_cell body = snake_model_cell(&model, i);
        cell(LEFT + 1 + body.x, TOP + 1 + body.y, i ? 'o' : '@',
             i ? RUM_COLOR_GREEN : model.alive || model.won ? RUM_COLOR_LIGHT_GREEN : RUM_COLOR_LIGHT_RED);
    }
    text(LEFT, 22, model.won ? "Island conquered! R to play again, Q to leave." :
        !model.alive ? "Game over. R to restart, Q to return to the shell." :
        paused ? "Paused. P resumes; R starts a new game." : "Eat * and stay clear of walls and your tail.", RUM_COLOR_LIGHT_GREY);
    text(LEFT, 23, "WASD move  P pause  R restart  Q quit", RUM_COLOR_DARK_GREY);
    for (unsigned y = 0; y < HEIGHT; ++y) {
        rum_result_t result = rum_terminal_cells(0, y, frame[y], WIDTH);
        if (result < 0) return result;
    }
    return rum_terminal_cursor(0, 0, 0);
}
int main(int argc, char **argv)
{
    (void)argv;
    if (argc != 1) { tool_print("Usage: snake\n"); return 1; }
    struct rum_terminal_capabilities caps;
    rum_result_t result = rum_terminal_query(&caps);
    if (result < 0) { tool_error("snake", result); return 1; }
    if (caps.width < WIDTH || caps.height < HEIGHT || !caps.tick_hz || caps.max_cells < WIDTH ||
        caps.max_events < RUM_TERM_MAX_EVENTS || (caps.features & RUM_TERM_FEATURES) != RUM_TERM_FEATURES) {
        tool_print("snake: unsupported terminal.\n"); return 1;
    }
    result = rum_terminal_begin();
    if (result < 0) { tool_error("snake", result); return 1; }
    best = stored_best = load_best();
    uint32_t now = caps.ticks, last_step = now, step_ticks = caps.tick_hz * 3u / 20u;
    if (!step_ticks) step_ticks = 1;
    snake_model_reset(&model, now ^ 0x72756Du);
    tool_print("rum / snake\nWASD move, P pause, R restart, Q quit.\nrum_snake_started\n");
    result = render();
    int quit = 0;
    while (result >= 0 && !quit) {
        uint32_t elapsed = now - last_step;
        uint32_t wait_ms = paused || !model.alive ? RUM_TERM_MAX_WAIT_MS :
            elapsed >= step_ticks ? 0 : ((step_ticks - elapsed) * 1000u + caps.tick_hz - 1) / caps.tick_hz;
        struct rum_terminal_event events[RUM_TERM_MAX_EVENTS];
        result = rum_terminal_read(events, RUM_TERM_MAX_EVENTS, wait_ms, &now);
        if (result < 0) break;
        for (rum_result_t i = 0; i < result; ++i) {
            unsigned c = events[i].value;
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (c == 'q') { quit = 1; break; }
            if (c == 'r') {
                save_best(); snake_model_reset(&model, now ^ 0x9E3779B9u); paused = 0; last_step = now;
                tool_print("rum_snake_restarted\n");
            } else if (c == 'p' && model.alive) {
                paused = !paused; last_step = now;
                tool_print(paused ? "rum_snake_paused\n" : "rum_snake_resumed\n");
            } else {
                switch (c) {
                case 'w': (void)snake_model_turn(&model, SNAKE_UP); break;
                case 'd': (void)snake_model_turn(&model, SNAKE_RIGHT); break;
                case 's': (void)snake_model_turn(&model, SNAKE_DOWN); break;
                case 'a': (void)snake_model_turn(&model, SNAKE_LEFT); break;
                default: break;
                }
            }
        }
        if (!quit && !paused && model.alive && (uint32_t)(now - last_step) >= step_ticks) {
            last_step = now;
            enum snake_event event = snake_model_step(&model);
            unsigned score = snake_model_score(&model); if (score > best) best = score;
            if (event == SNAKE_LOST || event == SNAKE_WON) {
                save_best(); tool_print(event == SNAKE_WON ? "rum_snake_won\n" : "rum_snake_game_over\n");
            } else if (event == SNAKE_ATE) {
                tool_print("snake: score="); tool_number((int32_t)score); tool_print("\n");
            }
        }
        if (!quit) result = render();
    }
    save_best();
    if (result >= 0) tool_print("rum_snake_quit\n");
    rum_result_t ended = rum_terminal_end();
    if (result < 0 || ended < 0) { tool_error("snake", result < 0 ? result : ended); return 1; }
    tool_print("Snake score: "); tool_number((int32_t)snake_model_score(&model));
    tool_print(" | best: "); tool_number((int32_t)best); tool_print("\n");
    if (save_failed) tool_print("Best score could not be saved to disk.\n");
    return 0;
}
