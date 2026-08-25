#ifndef CUBOLOR_GAME_H
#define CUBOLOR_GAME_H

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned long u32;

#define BOARD_SIZE 3
#define FIELD_SIZE 5
#define INNER_OFFSET 1
#define CELL_COUNT 9
#define LEVEL_COUNT 6
#define UNDO_COUNT 64

enum Color {
    COLOR_EMPTY = 0,
    COLOR_BLUE = 1,
    COLOR_GREEN = 2,
    COLOR_CYAN = 3,
    COLOR_RED = 4,
    COLOR_MAGENTA = 5,
    COLOR_YELLOW = 6,
    COLOR_WHITE = 7,
    COLOR_DARK_GREY = 8,
    COLOR_LIGHT_GREY = 9,
    COLOR_BLACK = 10
};

enum WorldDirection {
    WORLD_NORTH = 0,
    WORLD_EAST = 1,
    WORLD_SOUTH = 2,
    WORLD_WEST = 3
};

enum ScreenDirection {
    SCREEN_UP_LEFT = 0,
    SCREEN_UP_RIGHT = 1,
    SCREEN_DOWN_RIGHT = 2,
    SCREEN_DOWN_LEFT = 3
};

struct CubeFaces {
    u8 top, bottom, north, east, south, west;
};

struct Snapshot {
    u8 x, y;
    CubeFaces cube;
    u8 paint[CELL_COUNT];
    u16 moves;
    bool border_active;
};

struct GameState {
    u8 level;
    u8 x, y;
    u8 view;
    CubeFaces cube;
    u8 paint[CELL_COUNT];
    u8 target[CELL_COUNT];
    u16 moves;
    bool border_active;
    Snapshot undo[UNDO_COUNT];
    u8 undo_head;
    u8 undo_size;
    bool solved;
};

void game_start(GameState *game, unsigned level);
WorldDirection game_screen_to_world(unsigned screen_direction, unsigned view);
bool game_can_move(const GameState *game, WorldDirection direction);
bool game_move(GameState *game, WorldDirection direction);
bool game_move_screen(GameState *game, ScreenDirection direction);
bool game_undo(GameState *game);
bool game_matches_target(const GameState *game);
bool game_is_inner(unsigned x, unsigned y);
const char *game_level_name(unsigned level);

#endif
