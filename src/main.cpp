#include "game.h"
#include "platform.h"
#include "video.h"

#include <stdio.h>
#include <string.h>

static Motion no_motion(void)
{
    Motion motion; memset(&motion,0,sizeof(motion)); return motion;
}

static int selftest(void)
{
    GameState game; Motion motion=no_motion(); u32 checksum; unsigned frame;
    game_start(&game,0);
    if(!video_init()) { puts("CUBOLOR SELFTEST FAIL MODE"); return 1; }
    video_render_game(&game,&motion); video_present();
    checksum=video_page_checksum();
    motion.kind=MOTION_ROLL; motion.step=0; motion.from_x=game.x; motion.from_y=game.y;
    motion.cube_before=game.cube;
    game_move(&game,WORLD_EAST);
    motion.drop_after_roll=1;
    for(frame=0;frame<MOTION_STEPS;++frame) {
        motion.step=(u8)frame;video_render_game(&game,&motion);video_present();
        checksum^=video_page_checksum();
    }
    motion.kind=MOTION_DROP;motion.drop_after_roll=0;
    for(frame=0;frame<MOTION_STEPS;++frame) {
        motion.step=(u8)frame;video_render_game(&game,&motion);video_present();
        checksum^=video_page_checksum();
    }
    motion=no_motion(); video_render_game(&game,&motion); video_present();
    checksum^=video_page_checksum();
    motion.kind=MOTION_TURN;motion.view_from=game.view;motion.turn_delta=1;
    game.view=(u8)((game.view+1)&3);
    for(frame=0;frame<MOTION_STEPS;++frame) {
        motion.step=(u8)frame;video_render_game(&game,&motion);video_present();
        checksum^=video_page_checksum();
    }
    video_shutdown();
    if(checksum==0) { puts("CUBOLOR SELFTEST FAIL"); return 1; }
    printf("CUBOLOR SELFTEST PASS CRC=%08lX\n",checksum);
    return 0;
}

static int benchmark(void)
{
    GameState game,rolling_game; Motion motion=no_motion(); VideoProfile p;
    u32 total=0; unsigned frame; u32 fps10;
    game_start(&game,5); game_start(&rolling_game,0);
    motion.from_x=rolling_game.x; motion.from_y=rolling_game.y;
    motion.cube_before=rolling_game.cube; motion.destination_paint_before=COLOR_EMPTY;
    game_move(&rolling_game,WORLD_EAST);
    if(!video_init()) { puts("CUBOLOR BENCH FAIL MODE"); return 1; }
    platform_profile_init(); video_vsync_enable(false);
    video_profile_reset(); video_profile_enable(true);
    for(frame=0;frame<200;++frame) {
        u16 start=platform_profile_read();
        motion.step=(u8)((frame/2)%MOTION_STEPS);
        if(frame&1) {
            motion.kind=MOTION_ROLL; motion.drop_after_roll=1;
            video_render_game(&rolling_game,&motion);
        } else {
            motion.kind=MOTION_TURN; motion.view_from=0; motion.turn_delta=1;
            motion.drop_after_roll=0; video_render_game(&game,&motion);
        }
        video_present();
        total+=(u16)(start-platform_profile_read());
    }
    video_profile_get(&p); video_profile_enable(false); video_shutdown();
    if(total==0) total=1;
    fps10=(PROFILE_TIMER_HZ*10UL*200UL)/total;
    printf("CUBOLOR BENCH frames=200 ticks=%lu fps10=%lu hz=%lu\n",total,fps10,PROFILE_TIMER_HZ);
    printf("CUBOLOR PROFILE frames=%u clear=%lu board=%lu cube=%lu hud=%lu present=%lu\n",
           p.frames,p.clear_ticks,p.board_ticks,p.cube_ticks,p.hud_ticks,p.present_ticks);
    return 0;
}

static int capture(bool rolling,bool turning,bool title)
{
    GameState game; Motion motion=no_motion(); bool ok; unsigned frame;
    game_start(&game,rolling ? 0 : 3); game.view=rolling ? 0 : 1;
    if(!video_init()) { puts("CUBOLOR CAPTURE FAIL MODE"); return 1; }
    if(rolling) {
        motion.kind=MOTION_ROLL; motion.step=0;
        motion.from_x=game.x; motion.from_y=game.y; motion.cube_before=game.cube;
        game_move(&game,WORLD_EAST); motion.drop_after_roll=1;
    }
    if(turning) {
        motion.kind=MOTION_TURN; motion.view_from=0; motion.turn_delta=1;
    }
    if(title) { video_render_title(); video_present(); }
    else if(rolling || turning) {
        for(frame=0;frame<=5;++frame) {
            motion.step=(u8)frame; video_render_game(&game,&motion); video_present();
        }
    } else { video_render_game(&game,&motion); video_present(); }
    ok=video_write_ppm("CUBOLOR.PPM");
    video_shutdown(); puts(ok?"CUBOLOR CAPTURE PASS":"CUBOLOR CAPTURE FAIL");
    fflush(stdout); return ok?0:1;
}

static void begin_roll(GameState *game,Motion *motion,ScreenDirection direction)
{
    WorldDirection world=game_screen_to_world(direction,game->view);
    bool border_was_active=game->border_active;
    unsigned destination_x=game->x,destination_y=game->y;
    if(!game_can_move(game,world)) return;
    motion->kind=MOTION_ROLL; motion->step=0;
    motion->from_x=game->x; motion->from_y=game->y; motion->cube_before=game->cube;
    if(world==WORLD_NORTH) --destination_y;
    else if(world==WORLD_EAST) ++destination_x;
    else if(world==WORLD_SOUTH) ++destination_y;
    else --destination_x;
    motion->destination_paint_before=game_is_inner(destination_x,destination_y) ?
        game->paint[(destination_y-INNER_OFFSET)*BOARD_SIZE+(destination_x-INNER_OFFSET)] : COLOR_EMPTY;
    game_move(game,world);
    motion->drop_after_roll=(u8)(border_was_active && !game->border_active);
}

int main(int argc,char **argv)
{
    GameState game; Motion motion=no_motion(); bool running=true; bool title=true;
    if(argc>1 && strcmp(argv[1],"-selftest")==0) return selftest();
    if(argc>1 && strcmp(argv[1],"-benchmark")==0) return benchmark();
    if(argc>1 && strcmp(argv[1],"-capture")==0) return capture(false,false,false);
    if(argc>1 && strcmp(argv[1],"-capture-roll")==0) return capture(true,false,false);
    if(argc>1 && strcmp(argv[1],"-capture-turn")==0) return capture(false,true,false);
    if(argc>1 && strcmp(argv[1],"-capture-title")==0) return capture(false,false,true);
    game_start(&game,0);
    if(!platform_install()) { puts("CUBOLOR: keyboard/timer install failed"); return 1; }
    if(!video_init()) {
        platform_remove(); puts("CUBOLOR: EGA 640x350 mode 10h unavailable"); return 1;
    }
    while(running) {
        u32 tick=platform_ticks();
        if(title) {
            video_render_title();
            if(key_pressed(KEY_ENTER)||key_pressed(KEY_SPACE)) { title=false; key_clear(); }
            if(key_pressed(KEY_ESCAPE)) running=false;
        } else if(game.solved && motion.kind==MOTION_NONE) {
            video_render_complete(&game);
            if(key_pressed(KEY_ENTER)||key_pressed(KEY_SPACE)) {
                game_start(&game,(game.level+1)%LEVEL_COUNT); motion=no_motion(); key_clear();
            }
            if(key_pressed(KEY_ESCAPE)) running=false;
        } else {
            if(motion.kind!=MOTION_NONE) {
                if(++motion.step>=MOTION_STEPS) {
                    if(motion.kind==MOTION_ROLL && motion.drop_after_roll) {
                        motion.kind=MOTION_DROP; motion.step=0; motion.drop_after_roll=0;
                    } else motion=no_motion();
                }
            } else {
                if(key_pressed(KEY_NUM_4)) begin_roll(&game,&motion,SCREEN_LEFT);
                else if(key_pressed(KEY_NUM_8)) begin_roll(&game,&motion,SCREEN_UP);
                else if(key_pressed(KEY_NUM_6)) begin_roll(&game,&motion,SCREEN_RIGHT);
                else if(key_pressed(KEY_NUM_2)) begin_roll(&game,&motion,SCREEN_DOWN);
                else if(key_pressed(KEY_NUM_7)||key_pressed(KEY_NUM_1)) {
                    motion.kind=MOTION_TURN; motion.view_from=game.view; motion.turn_delta=-1; motion.step=0;
                    game.view=(u8)((game.view+3)&3);
                } else if(key_pressed(KEY_NUM_9)||key_pressed(KEY_NUM_3)) {
                    motion.kind=MOTION_TURN; motion.view_from=game.view; motion.turn_delta=1; motion.step=0;
                    game.view=(u8)((game.view+1)&3);
                } else if(key_pressed(KEY_U)) game_undo(&game);
                else if(key_pressed(KEY_R)) { unsigned view=game.view; game_start(&game,game.level); game.view=(u8)view; }
                else if(key_pressed(KEY_ESCAPE)) running=false;
            }
            video_render_game(&game,&motion);
        }
        video_present(); platform_wait_tick(tick);
    }
    video_shutdown(); platform_remove();
    return 0;
}
