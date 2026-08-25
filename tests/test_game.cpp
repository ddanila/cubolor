#include "game.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *solutions[LEVEL_COUNT] = {
    "EEESWWSEE", "WWWNEENWW", "ENNEESWSE", "SWSSEENNWS",
    "NNNEESWESWW", "WNWWSSEENW"
};

static WorldDirection direction(char c)
{
    if (c == 'N') return WORLD_NORTH;
    if (c == 'E') return WORLD_EAST;
    if (c == 'S') return WORLD_SOUTH;
    return WORLD_WEST;
}

int main(void)
{
    GameState game;
    unsigned level,view,screen;
    static const WorldDirection expected[4][4] = {
        {WORLD_WEST,WORLD_NORTH,WORLD_EAST,WORLD_SOUTH},
        {WORLD_SOUTH,WORLD_WEST,WORLD_NORTH,WORLD_EAST},
        {WORLD_EAST,WORLD_SOUTH,WORLD_WEST,WORLD_NORTH},
        {WORLD_NORTH,WORLD_EAST,WORLD_SOUTH,WORLD_WEST}
    };
    for(view=0;view<4;++view)for(screen=0;screen<4;++screen)
        assert(game_screen_to_world(screen,view)==expected[view][screen]);

    game_start(&game,0);
    assert(game.border_active);
    assert(game.moves==0);
    assert(game_move(&game,WORLD_NORTH));
    assert(game.border_active && game.moves==0);
    assert(game.paint[0]==COLOR_EMPTY);
    assert(game_undo(&game));
    assert(game.x==0 && game.y==1 && game.border_active);
    assert(game_move(&game,WORLD_EAST));
    assert(!game.border_active && game.moves==1);
    assert(game.paint[0]!=COLOR_EMPTY);
    assert(!game_can_move(&game,WORLD_WEST));
    assert(game_undo(&game));
    assert(game.border_active && game.moves==0);

    for (level = 0; level < LEVEL_COUNT; ++level) {
        const char *walk;
        game_start(&game, level);
        assert(game.border_active);
        assert(!game.solved);
        { unsigned cell; for(cell=0;cell<CELL_COUNT;++cell) assert(game.target[cell]!=COLOR_EMPTY); }
        for (walk = solutions[level]; *walk && !game.solved; ++walk)
            assert(game_move(&game, direction(*walk)));
        assert(game.solved);
        assert(!game.border_active);
        assert(game_matches_target(&game));
        assert(game.moves <= strlen(solutions[level]));
        assert(game_undo(&game));
        assert(!game.solved);
    }
    puts("CUBOLOR HOST TEST PASS");
    return 0;
}
