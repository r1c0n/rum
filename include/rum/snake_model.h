#ifndef RUM_SNAKE_MODEL_H
#define RUM_SNAKE_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#define SNAKE_WIDTH 40u
#define SNAKE_HEIGHT 16u
#define SNAKE_CELLS (SNAKE_WIDTH * SNAKE_HEIGHT)
#define SNAKE_INITIAL_LENGTH 3u

enum snake_direction { SNAKE_UP, SNAKE_RIGHT, SNAKE_DOWN, SNAKE_LEFT };
enum snake_event { SNAKE_MOVED, SNAKE_ATE, SNAKE_LOST, SNAKE_WON, SNAKE_STOPPED };
struct snake_cell { uint8_t x, y; };
struct snake_model {
    struct snake_cell body[SNAKE_CELLS]; /* Ring: head is newest, then walk backwards. */
    struct snake_cell food;
    uint16_t head, length;
    enum snake_direction direction, pending;
    uint32_t random;
    bool turn_queued, alive, won;
};

void snake_model_reset(struct snake_model *model, uint32_t seed);
/* At most one perpendicular turn per move; reversals/repeated direction fail. */
bool snake_model_turn(struct snake_model *model, enum snake_direction direction);
enum snake_event snake_model_step(struct snake_model *model);
struct snake_cell snake_model_cell(const struct snake_model *model, unsigned offset);
unsigned snake_model_score(const struct snake_model *model);

#endif
