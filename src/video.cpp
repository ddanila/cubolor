#include "video.h"

#include "platform.h"

#include <conio.h>
#include <dos.h>
#include <i86.h>
#include <stdio.h>
#include <string.h>

#define SCREEN_W 320
#define SCREEN_H 200
#define PITCH 40
#define PAGE_BYTES 8000

struct Point { int x, y; };

static unsigned char __far *vram = (unsigned char __far *)MK_FP(0xa000, 0);
static unsigned draw_page;
static unsigned display_page;
static bool profiling;
static bool vsync_enabled = true;
static VideoProfile profile;
static unsigned char current_bit_mask = 0;

static const unsigned char ega_color[11] = {
    8, 9, 10, 11, 12, 13, 14, 15, 8, 7, 0
};

/* cos/sin for 45 degrees + n*9 degrees, scaled by 256. */
static const short cos_table[40] = {
    181,150,116,80,40,0,-40,-80,-116,-150,
    -181,-208,-228,-243,-253,-256,-253,-243,-228,-208,
    -181,-150,-116,-80,-40,0,40,80,116,150,
    181,208,228,243,253,256,253,243,228,208
};
static const short sin_table[40] = {
    181,208,228,243,253,256,253,243,228,208,
    181,150,116,80,40,0,-40,-80,-116,-150,
    -181,-208,-228,-243,-253,-256,-253,-243,-228,-208,
    -181,-150,-116,-80,-40,0,40,80,116,150
};

static const unsigned char font[37][7] = {
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
    {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
    {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,30,1,1,17,14},
    {6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
    {14,17,17,15,1,2,12},{0,0,0,31,0,0,0}
};

static u32 elapsed(u16 start) { return (u16)(start - platform_profile_read()); }

static void set_mode(unsigned mode)
{
    union REGS regs;
    regs.w.ax = mode;
    int86(0x10, &regs, &regs);
}

static void set_bit_mask(unsigned char mask)
{
    if (mask == current_bit_mask) return;
    outpw(0x3ce, ((unsigned)mask << 8) | 8);
    current_bit_mask = mask;
}

static void fill_run_386(unsigned offset, unsigned count, unsigned char color);
#pragma aux fill_run_386 = \
    "mov dx,0a000h" \
    "mov es,dx" \
    "cld" \
    "rep stosb" \
    parm [di] [cx] [al] modify [cx di dx es];

static void hline(int x1, int x2, int y, unsigned char color)
{
    unsigned offset, first, last;
    unsigned char left_mask, right_mask;
    if (y < 0 || y >= SCREEN_H || x2 < 0 || x1 >= SCREEN_W) return;
    if (x1 < 0) x1 = 0;
    if (x2 >= SCREEN_W) x2 = SCREEN_W - 1;
    if (x2 < x1) return;
    first = (unsigned)x1 >> 3;
    last = (unsigned)x2 >> 3;
    offset = draw_page + (unsigned)y * PITCH + first;
    left_mask = (unsigned char)(0xff >> (x1 & 7));
    right_mask = (unsigned char)(0xff << (7 - (x2 & 7)));
    if (first == last) {
        set_bit_mask(left_mask & right_mask);
        (void)vram[offset]; vram[offset] = color;
    } else {
        set_bit_mask(left_mask); (void)vram[offset]; vram[offset] = color;
        set_bit_mask(0xff);
        if (last > first + 1)
            fill_run_386(draw_page + (unsigned)y * PITCH + first + 1,
                         last - first - 1, color);
        set_bit_mask(right_mask);
        (void)vram[draw_page + (unsigned)y * PITCH + last];
        vram[draw_page + (unsigned)y * PITCH + last] = color;
    }
}

static void clear_page(unsigned char color)
{
    set_bit_mask(0xff);
    fill_run_386(draw_page, PAGE_BYTES, color);
}

static void quad(Point a, Point b, Point c, Point d, unsigned char color)
{
    Point p[4]; int left[96],right[96]; int min_y=a.y,max_y=a.y,y,edge,height;
    p[0]=a;p[1]=b;p[2]=c;p[3]=d;
    if(b.y<min_y)min_y=b.y;if(c.y<min_y)min_y=c.y;if(d.y<min_y)min_y=d.y;
    if(b.y>max_y)max_y=b.y;if(c.y>max_y)max_y=c.y;if(d.y>max_y)max_y=d.y;
    height=max_y-min_y;
    if(height<=0 || height>96) return;
    for(y=0;y<height;++y) { left[y]=32767; right[y]=-32767; }
    for(edge=0;edge<4;++edge) {
        Point p1=p[edge],p2=p[(edge+1)&3]; long x,step;
        if(p1.y>p2.y) { Point swap=p1;p1=p2;p2=swap; }
        if(p1.y==p2.y) continue;
        x=(long)p1.x*256L;
        step=(long)(p2.x-p1.x)*256L/(p2.y-p1.y);
        for(y=p1.y;y<p2.y;++y) {
            int row=y-min_y,xi=(int)(x/256L);
            if(row>=0 && row<height) {
                if(xi<left[row]) left[row]=xi;
                if(xi>right[row]) right[row]=xi;
            }
            x+=step;
        }
    }
    for(y=0;y<height;++y)
        if(left[y]<=right[y]) hline(left[y],right[y],min_y+y,color);
}

static unsigned glyph_index(char c)
{
    if (c >= 'A' && c <= 'Z') return (unsigned)(c-'A');
    if (c >= '0' && c <= '9') return 26U+(unsigned)(c-'0');
    return 36;
}

static void text(int x, int y, const char *value, unsigned char color)
{
    while (*value) {
        if (*value != ' ') {
            unsigned index = glyph_index(*value); int row;
            for (row=0; row<7; ++row) {
                unsigned bits=font[index][row]; unsigned shift=(unsigned)x&7;
                unsigned pattern=bits<<(11-shift); unsigned offset;
                offset=draw_page+(unsigned)(y+row)*PITCH+((unsigned)x>>3);
                if(pattern>>8) { unsigned char mask=(unsigned char)(pattern>>8);
                    set_bit_mask(mask); (void)vram[offset]; vram[offset]=color; }
                if(pattern&0xff) { unsigned char mask=(unsigned char)pattern;
                    set_bit_mask(mask); (void)vram[offset+1]; vram[offset+1]=color; }
            }
        }
        x += 6; ++value;
    }
}

static int angle_for(const GameState *game, const Motion *motion)
{
    int angle = game->view * MOTION_STEPS;
    if (motion->kind == MOTION_TURN) {
        angle = motion->view_from * MOTION_STEPS + motion->turn_delta * motion->step;
        while (angle < 0) angle += 40;
        angle %= 40;
    }
    return angle;
}

static Point project_half(int hx, int hy, int angle, int center_x, int center_y)
{
    Point point;
    point.x = center_x + ((long)cos_table[angle]*hx-(long)sin_table[angle]*hy)*22/256;
    point.y = center_y + ((long)sin_table[angle]*hx+(long)cos_table[angle]*hy)*11/256;
    return point;
}

static void cell_points(unsigned x, unsigned y, int angle, Point *points)
{
    points[0]=project_half((int)x*2-3,(int)y*2-3,angle,145,104);
    points[1]=project_half((int)x*2-1,(int)y*2-3,angle,145,104);
    points[2]=project_half((int)x*2-1,(int)y*2-1,angle,145,104);
    points[3]=project_half((int)x*2-3,(int)y*2-1,angle,145,104);
}

static unsigned char color_of(u8 semantic)
{
    return semantic < 11 ? ega_color[semantic] : 7;
}

static void draw_cell(unsigned x, unsigned y, u8 paint, int angle)
{
    Point p[4]; unsigned char fill = paint ? color_of(paint) : 8;
    cell_points(x,y,angle,p);
    quad(p[0],p[1],p[2],p[3],fill);
}

static u8 visible_side(const CubeFaces *cube, int angle, bool right)
{
    unsigned view = ((unsigned)(angle + 5) / 10) & 3;
    if (view == 0) return right ? cube->east : cube->south;
    if (view == 1) return right ? cube->south : cube->west;
    if (view == 2) return right ? cube->west : cube->north;
    return right ? cube->north : cube->east;
}

static void draw_cube(const GameState *game, const Motion *motion, int angle)
{
    int hx = game->x*2-2, hy = game->y*2-2;
    int lift = 0, lean_x = 0;
    CubeFaces faces = game->cube;
    Point center, top, right, bottom, left, top_peak, right_peak, left_peak;
    if (motion->kind == MOTION_ROLL) {
        lift = motion->step <= 5 ? motion->step : 10-motion->step;
        lean_x = (motion->step-5)/2;
        if (motion->step < 5) faces=motion->cube_before;
    }
    center=project_half(hx,hy,angle,145,104);
    if (motion->kind == MOTION_ROLL) {
        Point from=project_half(motion->from_x*2-2,motion->from_y*2-2,angle,145,104);
        center.x=from.x+(center.x-from.x)*motion->step/MOTION_STEPS;
        center.y=from.y+(center.y-from.y)*motion->step/MOTION_STEPS;
    }
    center.y -= lift;
    top.x=center.x+lean_x; top.y=center.y-24;
    right.x=center.x+18; right.y=center.y-15;
    bottom.x=center.x; bottom.y=center.y-6;
    left.x=center.x-18; left.y=center.y-15;
    top_peak=top; right_peak=right; left_peak=left;
    top_peak.y-=15; right_peak.y-=15; left_peak.y-=15;
    quad(top_peak,right_peak,top,left_peak,color_of(faces.top));
    quad(right_peak,right,bottom,top,color_of(visible_side(&faces,angle,true)));
    quad(left_peak,top_peak,top,left,color_of(visible_side(&faces,angle,false)));
}

static void draw_target(const GameState *game)
{
    unsigned x,y;
    text(246,14,"TARGET",15);
    for (y=0;y<BOARD_SIZE;++y) for(x=0;x<BOARD_SIZE;++x) {
        int left=251+(int)x*18, top=29+(int)y*12;
        unsigned char c=color_of(game->target[y*BOARD_SIZE+x]);
        int row; for(row=0;row<10;++row) hline(left,left+15,top+row,c);
        hline(left,left+15,top,0); hline(left,left+15,top+9,0);
    }
}

static void draw_number(int x,int y,unsigned value,unsigned char color)
{
    char buf[6]; int pos=5; buf[pos]=0;
    do { buf[--pos]=(char)('0'+value%10); value/=10; } while(value && pos);
    text(x,y,buf+pos,color);
}

bool video_init(void)
{
    set_mode(0x0d);
    outpw(0x3ce,0x0205); /* write mode 2: CPU low nibble selects plane color */
    outpw(0x3ce,0x0003);
    outpw(0x3ce,0x0001);
    outpw(0x3ce,0x0000);
    draw_page=PAGE_BYTES; display_page=0;
    clear_page(0); video_present(); clear_page(0); video_present();
    return true;
}

void video_shutdown(void) { set_mode(3); }

void video_present(void)
{
    u16 started=platform_profile_read(); unsigned address=draw_page;
    if (vsync_enabled) while (!(inp(0x3da)&8)) { }
    outpw(0x3d4,0x0c | (address & 0xff00));
    outpw(0x3d4,0x0d | ((address & 0xff)<<8));
    display_page=draw_page; draw_page = draw_page ? 0 : PAGE_BYTES;
    if(profiling) profile.present_ticks+=elapsed(started);
}

void video_vsync_enable(bool enabled) { vsync_enabled=enabled; }

void video_render_title(void)
{
    int row; clear_page(1);
    for(row=0;row<34;++row) hline(36+row,282-row,24+row,13);
    text(91,37,"CUBOLOR",15);
    text(66,82,"ROLL PAINT MATCH",14);
    text(50,109,"NUM 4 8 6 2  MOVE",11);
    text(50,123,"NUM 7 1    TURN LEFT",10);
    text(50,137,"NUM 9 3    TURN RIGHT",12);
    text(68,166,"PRESS ENTER",15);
}

void video_render_game(const GameState *game,const Motion *motion)
{
    u16 started; int angle=angle_for(game,motion); unsigned order[9],i,j;
    started=platform_profile_read(); clear_page(1);
    if(profiling) profile.clear_ticks+=elapsed(started);
    started=platform_profile_read();
    for(i=0;i<9;++i) order[i]=i;
    for(i=1;i<9;++i) { unsigned value=order[i]; Point p[4]; int depth; j=i;
        cell_points(value%3,value/3,angle,p); depth=p[0].y+p[2].y;
        while(j>0) { Point q[4]; int other; cell_points(order[j-1]%3,order[j-1]/3,angle,q); other=q[0].y+q[2].y;
            if(other<=depth) break; order[j]=order[j-1]; --j; }
        order[j]=value;
    }
    for(i=0;i<9;++i) { unsigned index=order[i]; draw_cell(index%3,index/3,game->paint[index],angle); }
    if(profiling) profile.board_ticks+=elapsed(started);
    started=platform_profile_read(); draw_cube(game,motion,angle);
    if(profiling) profile.cube_ticks+=elapsed(started);
    started=platform_profile_read();
    text(9,12,"LEVEL",7); draw_number(46,12,game->level+1,15);
    text(9,24,game_level_name(game->level),14);
    text(9,181,"MOVES",7); draw_number(46,181,game->moves,15);
    text(173,181,"U UNDO  R RESET",7); draw_target(game);
    if(profiling) { profile.hud_ticks+=elapsed(started); ++profile.frames; }
}

void video_render_complete(const GameState *game)
{
    Motion motion; memset(&motion,0,sizeof(motion)); video_render_game(game,&motion);
    { int row; for(row=0;row<38;++row) hline(54,232,72+row,0); }
    text(88,82,"PICTURE COMPLETE",14);
    text(75,99,"PRESS ENTER",15);
}

void video_profile_enable(bool enabled) { profiling=enabled; }
void video_profile_reset(void) { memset(&profile,0,sizeof(profile)); }
void video_profile_get(VideoProfile *result) { *result=profile; }

u32 video_page_checksum(void)
{
    u32 hash=2166136261UL; unsigned plane,i;
    outpw(0x3ce,0x0005); current_bit_mask=0; set_bit_mask(0xff);
    for(plane=0;plane<4;++plane) {
        outpw(0x3ce,4 | (plane<<8));
        for(i=0;i<PAGE_BYTES;++i) { hash^=vram[display_page+i]; hash*=16777619UL; }
    }
    outpw(0x3ce,0x0205); current_bit_mask=0;
    return hash;
}

bool video_write_ppm(const char *path)
{
    static const unsigned char rgb[16][3] = {
        {0,0,0},{0,0,170},{0,170,0},{0,170,170},{170,0,0},{170,0,170},{170,85,0},{170,170,170},
        {85,85,85},{85,85,255},{85,255,85},{85,255,255},{255,85,85},{255,85,255},{255,255,85},{255,255,255}
    };
    unsigned char row[SCREEN_W*3]; FILE *file=fopen(path,"wb"); unsigned y,byte,bit,plane;
    if(file==0) return false;
    fputs("P6\n320 200\n255\n",file); outpw(0x3ce,0x0005);
    for(y=0;y<SCREEN_H;++y) {
        for(byte=0;byte<PITCH;++byte) {
            unsigned char planes[4];
            for(plane=0;plane<4;++plane) { outpw(0x3ce,4|(plane<<8)); planes[plane]=vram[display_page+y*PITCH+byte]; }
            for(bit=0;bit<8;++bit) {
                unsigned mask=0x80U>>bit,index=0,pixel=byte*8+bit;
                for(plane=0;plane<4;++plane) if(planes[plane]&mask) index|=1U<<plane;
                row[pixel*3]=rgb[index][0];row[pixel*3+1]=rgb[index][1];row[pixel*3+2]=rgb[index][2];
            }
        }
        if(fwrite(row,1,sizeof(row),file)!=sizeof(row)) { fclose(file); return false; }
    }
    fclose(file); outpw(0x3ce,0x0205); current_bit_mask=0; return true;
}
