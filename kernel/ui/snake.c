#include <rum/heap.h>
#include <rum/ramfs.h>
#include <rum/serial.h>
#include <rum/snake.h>
#include <rum/snake_model.h>
#include <rum/terminal.h>

#define LEFT 19u
#define TOP 4u

struct snake_game {
    struct snake_model model;
    uint32_t last_step;
    bool paused;
};
static struct snake_game *snake_game;
static unsigned best, stored_best;

static size_t decimal(char *text, unsigned value)
{
    size_t length = 0;
    do { text[length++] = (char)('0' + value % 10); value /= 10; } while (value);
    for (size_t i = 0; i < length / 2; ++i) {
        char c = text[i]; text[i] = text[length - 1 - i]; text[length - 1 - i] = c;
    }
    text[length] = '\0';
    return length;
}

static void print(const char *text)
{
    terminal_writestring(text);
    serial_writestring(text);
}

static unsigned load_best(void)
{
    const unsigned char *data;
    size_t size;
    if (!ramfs_read("snake.score", &data, &size) || !size || size > 4) return 0;
    if (data[size - 1] == '\n') --size;
    if (!size) return 0;
    unsigned value = 0;
    for (size_t i = 0; i < size; ++i) {
        if (data[i] < '0' || data[i] > '9') return 0;
        value = value * 10 + data[i] - '0';
    }
    return value <= SNAKE_CELLS - SNAKE_INITIAL_LENGTH ? value : 0;
}

static bool save_best(void)
{
    if (best <= stored_best) return true;
    char text[12];
    size_t length = decimal(text, best);
    text[length++] = '\n';
    if (!ramfs_put("snake.score", text, length)) {
        serial_writestring("snake: could not save best score.\n");
        return false;
    }
    stored_best = best;
    return true;
}

static void cell(unsigned x, unsigned y, char c, enum vga_color color)
{
    (void)terminal_put_at(x, y, c, color, VGA_BLACK);
}

static void text(unsigned x, unsigned y, const char *string, enum vga_color color)
{
    while (*string && x < 80) cell(x++, y, *string++, color);
}

static void render(void)
{
    for (unsigned y = 0; y < 24; ++y)
        for (unsigned x = 0; x < 80; ++x) cell(x, y, ' ', VGA_LIGHT_GREY);
    text(LEFT, 0, "rum / snake", VGA_LIGHT_CYAN);
    char number[12];
    text(LEFT, 2, "score: ", VGA_WHITE);
    decimal(number, snake_model_score(&snake_game->model));
    text(LEFT + 7, 2, number, VGA_LIGHT_GREEN);
    text(LEFT + 17, 2, "best: ", VGA_WHITE);
    decimal(number, best);
    text(LEFT + 23, 2, number, VGA_YELLOW);
    for (unsigned x = 0; x < SNAKE_WIDTH + 2; ++x) {
        cell(LEFT + x, TOP, '#', VGA_CYAN);
        cell(LEFT + x, TOP + SNAKE_HEIGHT + 1, '#', VGA_CYAN);
    }
    for (unsigned y = 0; y < SNAKE_HEIGHT; ++y) {
        cell(LEFT, TOP + 1 + y, '#', VGA_CYAN);
        cell(LEFT + SNAKE_WIDTH + 1, TOP + 1 + y, '#', VGA_CYAN);
    }
    const struct snake_model *model = &snake_game->model;
    if (!model->won)
        cell(LEFT + 1 + model->food.x, TOP + 1 + model->food.y, '*', VGA_YELLOW);
    for (unsigned i = model->length; i-- > 0;) {
        struct snake_cell body = snake_model_cell(model, i);
        cell(LEFT + 1 + body.x, TOP + 1 + body.y, i ? 'o' : '@',
             i ? VGA_GREEN : (model->alive || model->won ? VGA_LIGHT_GREEN : VGA_LIGHT_RED));
    }
    text(LEFT, 22, model->won ? "Island conquered! R to play again, Q to leave." :
                     !model->alive ? "Game over. R to restart, Q to return to the shell." :
                     snake_game->paused ? "Paused. P resumes; R starts a new game." :
                     "Eat * and stay clear of walls and your tail.", VGA_LIGHT_GREY);
    text(LEFT, 23, "WASD move  P pause  R restart  Q quit", VGA_DARK_GREY);
}

bool snake_active(void) { return snake_game != NULL; }

bool snake_start(uint32_t now)
{
    if (snake_game) return false;
    snake_game = kmalloc(sizeof *snake_game);
    if (!snake_game) { print("Cannot start Snake: out of memory.\n"); return false; }
    stored_best = load_best();
    if (stored_best > best) best = stored_best;
    snake_model_reset(&snake_game->model, now ^ 0x72756Du);
    snake_game->last_step = now;
    snake_game->paused = false;
    terminal_clear();
    terminal_cursor_visible(false);
    serial_writestring("\x1b[2J\x1b[Hrum / snake\n"
                       "WASD move, P pause, R restart, Q quit.\nrum_snake_started\n");
    render();
    return true;
}

void snake_receive(char character, uint32_t now)
{
    if (!snake_game) return;
    if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
    if (character == 'q') {
        unsigned score = snake_model_score(&snake_game->model);
        bool saved = save_best();
        (void)kfree(snake_game);
        snake_game = NULL;
        terminal_cursor_visible(true);
        terminal_clear();
        serial_writestring("\x1b[2J\x1b[H");
        char number[12];
        print("Snake score: "); decimal(number, score); print(number);
        print(" | best: "); decimal(number, best); print(number); print("\n");
        if (!saved) print("Best score remains in memory; RAM file could not be saved.\n");
        serial_writestring("rum_snake_quit\n");
    } else if (character == 'r') {
        (void)save_best();
        snake_model_reset(&snake_game->model, now ^ 0x9E3779B9u);
        snake_game->last_step = now;
        snake_game->paused = false;
        render();
        serial_writestring("rum_snake_restarted\n");
    } else if (character == 'p' && snake_game->model.alive) {
        snake_game->paused = !snake_game->paused;
        snake_game->last_step = now;
        render();
        serial_writestring(snake_game->paused ? "rum_snake_paused\n" : "rum_snake_resumed\n");
    } else {
        switch (character) {
        case 'w': (void)snake_model_turn(&snake_game->model, SNAKE_UP); break;
        case 'd': (void)snake_model_turn(&snake_game->model, SNAKE_RIGHT); break;
        case 's': (void)snake_model_turn(&snake_game->model, SNAKE_DOWN); break;
        case 'a': (void)snake_model_turn(&snake_game->model, SNAKE_LEFT); break;
        default: break;
        }
    }
}

bool snake_tick_due(uint32_t now)
{
    return snake_game && snake_game->model.alive && !snake_game->paused &&
           (uint32_t)(now - snake_game->last_step) >= SNAKE_STEP_TICKS;
}

void snake_tick(uint32_t now)
{
    if (!snake_tick_due(now)) return;
    /* One step after a delay, without a burst of catch-up movement. */
    snake_game->last_step = now;
    enum snake_event event = snake_model_step(&snake_game->model);
    unsigned score = snake_model_score(&snake_game->model);
    if (score > best) best = score;
    render();
    if (event == SNAKE_LOST || event == SNAKE_WON) {
        (void)save_best();
        serial_writestring(event == SNAKE_WON ? "rum_snake_won\n" : "rum_snake_game_over\n");
    } else if (event == SNAKE_ATE) {
        char number[12];
        serial_writestring("snake: score="); decimal(number, score); serial_writestring(number);
        serial_writestring("\n");
    }
}
