#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <rum/snake_model.h>

static bool same(struct snake_cell a, struct snake_cell b) { return a.x == b.x && a.y == b.y; }

static void valid(const struct snake_model *model)
{
    bool occupied[SNAKE_CELLS] = {0};
    assert(model->length >= 3 && model->length <= SNAKE_CELLS && model->head < SNAKE_CELLS);
    for (unsigned i = 0; i < model->length; ++i) {
        struct snake_cell cell = snake_model_cell(model, i);
        assert(cell.x < SNAKE_WIDTH && cell.y < SNAKE_HEIGHT);
        unsigned index = cell.y * SNAKE_WIDTH + cell.x;
        assert(!occupied[index]); occupied[index] = true;
        if (i) {
            struct snake_cell previous = snake_model_cell(model, i - 1);
            int dx = previous.x - cell.x, dy = previous.y - cell.y;
            assert((dx * dx + dy * dy) == 1);
        }
    }
    if (!model->won) {
        assert(model->food.x < SNAKE_WIDTH && model->food.y < SNAKE_HEIGHT);
        assert(!occupied[model->food.y * SNAKE_WIDTH + model->food.x]);
    }
}

int main(void)
{
    struct snake_model model, copy;
    for (uint32_t seed = 0; seed < 1000; ++seed) {
        snake_model_reset(&model, seed); snake_model_reset(&copy, seed);
        assert(!memcmp(&model, &copy, sizeof model)); valid(&model);
    }
    snake_model_reset(&model, 42);
    assert(snake_model_score(&model) == 0 && !snake_model_turn(&model, SNAKE_LEFT));
    assert(!snake_model_turn(&model, SNAKE_RIGHT) && !snake_model_turn(&model, (enum snake_direction)-1));
    assert(snake_model_turn(&model, SNAKE_UP) && !snake_model_turn(&model, SNAKE_LEFT));
    model.food = (struct snake_cell){20, 7};
    assert(snake_model_step(&model) == SNAKE_ATE && model.length == 4 && snake_model_score(&model) == 1);
    assert(same(snake_model_cell(&model, 0), (struct snake_cell){20, 7})); valid(&model);
    assert(!snake_model_turn(&model, SNAKE_DOWN) && snake_model_turn(&model, SNAKE_LEFT));
    assert(snake_model_step(&model) == SNAKE_MOVED); valid(&model);
    assert(same(snake_model_cell(&model, model.length), (struct snake_cell){255, 255}));

    /* The departing tail is legal, but an interior body cell is fatal. */
    snake_model_reset(&model, 1);
    model.length = 4; model.head = 3; model.direction = model.pending = SNAKE_LEFT;
    model.food = (struct snake_cell){0, 0};
    memcpy(model.body, (struct snake_cell[]){{2, 3}, {3, 3}, {3, 2}, {2, 2}}, 8);
    assert(snake_model_turn(&model, SNAKE_DOWN) && snake_model_step(&model) == SNAKE_MOVED);
    assert(same(snake_model_cell(&model, 0), (struct snake_cell){2, 3})); valid(&model);
    model.length = 6; model.head = 5; model.direction = model.pending = SNAKE_LEFT;
    memcpy(model.body, (struct snake_cell[]){{1, 2}, {1, 3}, {2, 3}, {3, 3}, {3, 2}, {2, 2}}, 12);
    assert(snake_model_turn(&model, SNAKE_DOWN) && snake_model_step(&model) == SNAKE_LOST);
    assert(!model.alive && !model.won && snake_model_step(&model) == SNAKE_STOPPED);
    assert(!snake_model_turn(&model, SNAKE_UP)); valid(&model);

    snake_model_reset(&model, 2); model.food = (struct snake_cell){0, 0};
    for (unsigned i = 0; i < 19; ++i) assert(snake_model_step(&model) == SNAKE_MOVED);
    assert(snake_model_step(&model) == SNAKE_LOST && !model.alive); valid(&model);

    /* Repeatedly circle a square to cross the ring's end without growing. */
    snake_model_reset(&model, 3); model.food = (struct snake_cell){0, 0};
    enum snake_direction turns[] = { SNAKE_DOWN, SNAKE_RIGHT, SNAKE_UP, SNAKE_LEFT };
    for (unsigned i = 0; i < 2500; ++i) {
        assert(snake_model_turn(&model, turns[i % 4]));
        assert(snake_model_step(&model) == SNAKE_MOVED); valid(&model);
    }

    /* A connected serpentine body with two free cells finishes the entire board. */
    snake_model_reset(&model, 4);
    model.length = SNAKE_CELLS - 2; model.head = model.length - 1;
    model.direction = model.pending = SNAKE_LEFT;
    for (unsigned i = 0; i < model.length; ++i) {
        unsigned index = SNAKE_CELLS - 1 - i, y = index / SNAKE_WIDTH, x = index % SNAKE_WIDTH;
        model.body[i] = (struct snake_cell){ y % 2 ? SNAKE_WIDTH - 1 - x : x, y };
    }
    model.food = (struct snake_cell){1, 0}; valid(&model);
    assert(snake_model_step(&model) == SNAKE_ATE && same(model.food, (struct snake_cell){0, 0}));
    valid(&model);
    assert(snake_model_step(&model) == SNAKE_WON && model.won && !model.alive && model.length == SNAKE_CELLS);
    assert(snake_model_score(&model) == SNAKE_CELLS - 3 && snake_model_step(&model) == SNAKE_STOPPED);
    valid(&model);
    puts("PASS: Snake deterministic food, growth, turns/reversals, tail/body/wall collisions, ring wrap, full-board win");
    return 0;
}
