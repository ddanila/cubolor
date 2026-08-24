#include "game.h"

#include <string.h>

struct LevelDefinition {
    const char *name;
    u8 start_x, start_y;
    const char *solution;
};

/* Targets are generated from known legal walks. Keeping the authoring format as
 * walks makes every shipped puzzle provably reachable from its initial cube. */
static const LevelDefinition levels[LEVEL_COUNT] = {
    {"FIRST LIGHT", 0, 0, "EESWWSEE"},
    {"RIBBON",      2, 2, "WWNEENWW"},
    {"TURNABOUT",   0, 2, "NEESWWNEES"},
    {"SPECTRUM",    1, 1, "NWSSEENN"},
    {"CROSSOVER",   1, 0, "WSSEENWEN"},
    {"CUBOLOR",     2, 1, "WNESWWSEENW"}
};

static CubeFaces initial_cube(void)
{
    CubeFaces cube;
    cube.top = COLOR_YELLOW;
    cube.bottom = COLOR_BLUE;
    cube.north = COLOR_MAGENTA;
    cube.east = COLOR_RED;
    cube.south = COLOR_GREEN;
    cube.west = COLOR_CYAN;
    return cube;
}

static void roll(CubeFaces *cube, WorldDirection direction)
{
    CubeFaces old = *cube;
    switch (direction) {
    case WORLD_NORTH:
        cube->top = old.south; cube->north = old.top;
        cube->bottom = old.north; cube->south = old.bottom;
        break;
    case WORLD_EAST:
        cube->top = old.west; cube->east = old.top;
        cube->bottom = old.east; cube->west = old.bottom;
        break;
    case WORLD_SOUTH:
        cube->top = old.north; cube->north = old.bottom;
        cube->bottom = old.south; cube->south = old.top;
        break;
    default:
        cube->top = old.east; cube->east = old.bottom;
        cube->bottom = old.west; cube->west = old.top;
        break;
    }
}

static bool step(unsigned *x, unsigned *y, WorldDirection direction)
{
    if (direction == WORLD_NORTH) {
        if (*y == 0) return false;
        --*y;
    } else if (direction == WORLD_EAST) {
        if (*x + 1 >= BOARD_SIZE) return false;
        ++*x;
    } else if (direction == WORLD_SOUTH) {
        if (*y + 1 >= BOARD_SIZE) return false;
        ++*y;
    } else {
        if (*x == 0) return false;
        --*x;
    }
    return true;
}

static WorldDirection letter_direction(char letter)
{
    if (letter == 'N') return WORLD_NORTH;
    if (letter == 'E') return WORLD_EAST;
    if (letter == 'S') return WORLD_SOUTH;
    return WORLD_WEST;
}

const char *game_level_name(unsigned level)
{
    return levels[level % LEVEL_COUNT].name;
}

void game_start(GameState *game, unsigned level)
{
    GameState author;
    const LevelDefinition *definition = &levels[level % LEVEL_COUNT];
    const char *walk;
    memset(game, 0, sizeof(*game));
    game->level = (u8)(level % LEVEL_COUNT);
    game->x = definition->start_x;
    game->y = definition->start_y;
    game->cube = initial_cube();

    memset(&author, 0, sizeof(author));
    author.x = definition->start_x;
    author.y = definition->start_y;
    author.cube = initial_cube();
    author.paint[author.y * BOARD_SIZE + author.x] = author.cube.bottom;
    for (walk = definition->solution; *walk; ++walk) {
        WorldDirection direction = letter_direction(*walk);
        unsigned x = author.x, y = author.y;
        if (step(&x, &y, direction)) {
            roll(&author.cube, direction);
            author.x = (u8)x; author.y = (u8)y;
            author.paint[y * BOARD_SIZE + x] = author.cube.bottom;
        }
    }
    memcpy(game->target, author.paint, sizeof(game->target));
    game->paint[game->y * BOARD_SIZE + game->x] = game->cube.bottom;
    game->solved = game_matches_target(game);
}

WorldDirection game_screen_to_world(unsigned screen_direction, unsigned view)
{
    /* At view zero, north is screen-up. A clockwise camera quarter turn means
     * screen-up points one world quarter turn clockwise as well. */
    return (WorldDirection)((screen_direction + view) & 3);
}

bool game_can_move(const GameState *game, WorldDirection direction)
{
    unsigned x = game->x, y = game->y;
    return step(&x, &y, direction);
}

bool game_move(GameState *game, WorldDirection direction)
{
    unsigned x = game->x, y = game->y;
    Snapshot *snapshot;
    if (!step(&x, &y, direction) || game->solved) return false;

    snapshot = &game->undo[game->undo_head];
    snapshot->x = game->x; snapshot->y = game->y;
    snapshot->cube = game->cube;
    memcpy(snapshot->paint, game->paint, sizeof(game->paint));
    snapshot->moves = game->moves;
    game->undo_head = (u8)((game->undo_head + 1) % UNDO_COUNT);
    if (game->undo_size < UNDO_COUNT) ++game->undo_size;

    roll(&game->cube, direction);
    game->x = (u8)x; game->y = (u8)y;
    game->paint[y * BOARD_SIZE + x] = game->cube.bottom;
    ++game->moves;
    game->solved = game_matches_target(game);
    return true;
}

bool game_move_screen(GameState *game, ScreenDirection direction)
{
    return game_move(game, game_screen_to_world(direction, game->view));
}

bool game_undo(GameState *game)
{
    Snapshot *snapshot;
    if (game->undo_size == 0) return false;
    game->undo_head = (u8)((game->undo_head + UNDO_COUNT - 1) % UNDO_COUNT);
    snapshot = &game->undo[game->undo_head];
    game->x = snapshot->x; game->y = snapshot->y;
    game->cube = snapshot->cube;
    memcpy(game->paint, snapshot->paint, sizeof(game->paint));
    game->moves = snapshot->moves;
    --game->undo_size;
    game->solved = false;
    return true;
}

bool game_matches_target(const GameState *game)
{
    return memcmp(game->paint, game->target, sizeof(game->paint)) == 0;
}
