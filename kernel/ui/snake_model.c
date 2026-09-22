#include <rum/memory.h>
#include <rum/snake_model.h>

struct snake_cell snake_model_cell(const struct snake_model *model, unsigned offset)
{
    if (offset >= model->length) return (struct snake_cell){255, 255};
    return model->body[(model->head + SNAKE_CELLS - offset) % SNAKE_CELLS];
}

static bool occupied(const struct snake_model *model, struct snake_cell cell, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        struct snake_cell body = snake_model_cell(model, i);
        if (body.x == cell.x && body.y == cell.y) return true;
    }
    return false;
}

static void place_food(struct snake_model *model)
{
    model->random = model->random * 1664525u + 1013904223u;
    unsigned start = model->random % SNAKE_CELLS;
    /* Bounded even on a nearly full board; never place food on the snake. */
    for (unsigned i = 0; i < SNAKE_CELLS; ++i) {
        unsigned index = (start + i) % SNAKE_CELLS;
        struct snake_cell cell = {index % SNAKE_WIDTH, index / SNAKE_WIDTH};
        if (!occupied(model, cell, model->length)) { model->food = cell; return; }
    }
}

void snake_model_reset(struct snake_model *model, uint32_t seed)
{
    memset(model, 0, sizeof *model);
    model->head = SNAKE_INITIAL_LENGTH - 1;
    model->length = SNAKE_INITIAL_LENGTH;
    model->direction = model->pending = SNAKE_RIGHT;
    model->random = seed;
    model->alive = true;
    for (unsigned i = 0; i < SNAKE_INITIAL_LENGTH; ++i)
        model->body[i] = (struct snake_cell){SNAKE_WIDTH / 2 - 2 + i, SNAKE_HEIGHT / 2};
    place_food(model);
}

bool snake_model_turn(struct snake_model *model, enum snake_direction direction)
{
    if (!model->alive || model->turn_queued || (unsigned)direction > SNAKE_LEFT ||
        direction == model->direction || ((unsigned)direction + 2) % 4 == (unsigned)model->direction)
        return false;
    model->pending = direction;
    model->turn_queued = true;
    return true;
}

enum snake_event snake_model_step(struct snake_model *model)
{
    if (!model->alive) return SNAKE_STOPPED;
    struct snake_cell head = snake_model_cell(model, 0);
    model->direction = model->pending;
    model->turn_queued = false;
    int x = head.x, y = head.y;
    switch (model->direction) {
    case SNAKE_UP: --y; break;
    case SNAKE_RIGHT: ++x; break;
    case SNAKE_DOWN: ++y; break;
    case SNAKE_LEFT: --x; break;
    }
    if (x < 0 || x >= (int)SNAKE_WIDTH || y < 0 || y >= (int)SNAKE_HEIGHT) {
        model->alive = false;
        return SNAKE_LOST;
    }
    struct snake_cell next = {(uint8_t)x, (uint8_t)y};
    bool eating = next.x == model->food.x && next.y == model->food.y;
    /* A non-growing move may enter the tail cell that is leaving this turn. */
    if (occupied(model, next, model->length - (eating ? 0 : 1))) {
        model->alive = false;
        return SNAKE_LOST;
    }
    model->head = (model->head + 1) % SNAKE_CELLS;
    model->body[model->head] = next;
    if (!eating) return SNAKE_MOVED;
    if (++model->length == SNAKE_CELLS) {
        model->alive = false;
        model->won = true;
        model->food = (struct snake_cell){255, 255};
        return SNAKE_WON;
    }
    place_food(model);
    return SNAKE_ATE;
}

unsigned snake_model_score(const struct snake_model *model)
{
    return model->length - SNAKE_INITIAL_LENGTH;
}
