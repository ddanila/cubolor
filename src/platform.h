#ifndef CUBOLOR_PLATFORM_H
#define CUBOLOR_PLATFORM_H

#include "game.h"

enum KeyScan {
    KEY_ESCAPE = 0x01,
    KEY_ENTER = 0x1c,
    KEY_R = 0x13,
    KEY_U = 0x16,
    KEY_SPACE = 0x39,
    KEY_NUM_7 = 0x47,
    KEY_NUM_8 = 0x48,
    KEY_NUM_9 = 0x49,
    KEY_NUM_4 = 0x4b,
    KEY_NUM_6 = 0x4d,
    KEY_NUM_1 = 0x4f,
    KEY_NUM_2 = 0x50,
    KEY_NUM_3 = 0x51
};

#define PROFILE_TIMER_HZ 1193182UL

bool platform_install(void);
void platform_remove(void);
bool key_pressed(unsigned scan);
void key_clear(void);
u32 platform_ticks(void);
void platform_wait_tick(u32 previous);
void platform_profile_init(void);
u16 platform_profile_read(void);

#endif
