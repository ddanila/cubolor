#ifndef CUBOLOR_VIDEO_H
#define CUBOLOR_VIDEO_H

#include "game.h"

#define MOTION_STEPS 10

enum MotionKind {
    MOTION_NONE = 0,
    MOTION_ROLL = 1,
    MOTION_TURN = 2,
    MOTION_DROP = 3
};

struct Motion {
    u8 kind;
    u8 step;
    signed char turn_delta;
    u8 view_from;
    u8 from_x, from_y;
    u8 drop_after_roll;
    u8 destination_paint_before;
    CubeFaces cube_before;
};

struct VideoProfile {
    u32 clear_ticks;
    u32 board_ticks;
    u32 cube_ticks;
    u32 hud_ticks;
    u32 present_ticks;
    u16 frames;
};

bool video_init(void);
void video_shutdown(void);
void video_render_title(void);
void video_render_game(const GameState *game, const Motion *motion);
void video_render_complete(const GameState *game);
void video_present(void);
void video_vsync_enable(bool enabled);
void video_profile_enable(bool enabled);
void video_profile_reset(void);
void video_profile_get(VideoProfile *result);
u32 video_page_checksum(void);
bool video_write_ppm(const char *path);

#endif
