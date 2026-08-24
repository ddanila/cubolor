#include "platform.h"

#include <conio.h>
#include <dos.h>
#include <string.h>

#define GAME_TIMER_DIVISOR 23864U

static volatile unsigned char keys[128];
static volatile unsigned char edges[128];
static volatile u32 ticks;
static u32 bios_accumulator;
static void (__interrupt __far *old_keyboard)(void);
static void (__interrupt __far *old_timer)(void);
static bool installed;

static void __interrupt __far keyboard_handler(void)
{
    unsigned char code = (unsigned char)inp(0x60);
    unsigned char scan = code & 0x7f;
    unsigned char control;
    if (scan < 128 && scan != 0x60) {
        if (code & 0x80) keys[scan] = 0;
        else {
            if (!keys[scan]) edges[scan] = 1;
            keys[scan] = 1;
        }
    }
    control = (unsigned char)inp(0x61);
    outp(0x61, control | 0x80);
    outp(0x61, control);
    outp(0x20, 0x20);
}

static void __interrupt __far timer_handler(void)
{
    ++ticks;
    bios_accumulator += GAME_TIMER_DIVISOR;
    if (bios_accumulator >= 65536UL) {
        bios_accumulator -= 65536UL;
        _chain_intr(old_timer);
    } else {
        outp(0x20, 0x20);
    }
}

bool platform_install(void)
{
    memset((void *)keys, 0, sizeof(keys));
    memset((void *)edges, 0, sizeof(edges));
    ticks = 0; bios_accumulator = 0;
    old_keyboard = _dos_getvect(9);
    old_timer = _dos_getvect(8);
    if (old_keyboard == 0 || old_timer == 0) return false;
    _disable();
    _dos_setvect(9, keyboard_handler);
    _dos_setvect(8, timer_handler);
    outp(0x43, 0x36);
    outp(0x40, GAME_TIMER_DIVISOR & 0xff);
    outp(0x40, GAME_TIMER_DIVISOR >> 8);
    _enable();
    installed = true;
    return true;
}

void platform_remove(void)
{
    if (!installed) return;
    _disable();
    outp(0x43, 0x36);
    outp(0x40, 0);
    outp(0x40, 0);
    _dos_setvect(8, old_timer);
    _dos_setvect(9, old_keyboard);
    _enable();
    installed = false;
}

bool key_pressed(unsigned scan)
{
    bool result;
    if (scan >= 128) return false;
    _disable();
    result = edges[scan] != 0;
    edges[scan] = 0;
    _enable();
    return result;
}

void key_clear(void)
{
    _disable();
    memset((void *)edges, 0, sizeof(edges));
    _enable();
}

u32 platform_ticks(void)
{
    u32 result;
    _disable(); result = ticks; _enable();
    return result;
}

void platform_wait_tick(u32 previous)
{
    while (platform_ticks() == previous) { }
}

void platform_profile_init(void)
{
    outp(0x61, (inp(0x61) & 0xfd) | 1);
    outp(0x43, 0xb4);
    outp(0x42, 0);
    outp(0x42, 0);
}

u16 platform_profile_read(void)
{
    unsigned count;
    outp(0x43, 0x80);
    count = (unsigned)inp(0x42);
    count |= (unsigned)inp(0x42) << 8;
    return (u16)count;
}
