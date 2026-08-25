#include "video.h"

#include "platform.h"

#include <conio.h>
#include <dos.h>
#include <fcntl.h>
#include <i86.h>
#include <io.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define SCREEN_W 640
#define SCREEN_H 350
#define PITCH 80
#define PAGE_BYTES 28000
#define OUTLINE_Y 64
#define OUTLINE_BYTE_X 10
#define OUTLINE_BYTE_W 55
#define OUTLINE_H 286
#define OUTLINE_BYTE_MAX 4096

struct Point { int x, y; };
struct Point3 { int x, y, z; };
struct OutlineByte { u16 offset,next; };

/* Reads load all four EGA latches before a masked write.  They must survive
 * optimization even when the C value itself is discarded. */
static volatile unsigned char __far *vram =
    (volatile unsigned char __far *)MK_FP(0xa000, 0);
static unsigned draw_page;
static unsigned display_page;
static bool profiling;
static bool vsync_enabled = true;
static bool quad_clip_enabled;
static int quad_clip_x1,quad_clip_y1,quad_clip_x2,quad_clip_y2;
static VideoProfile profile;
static unsigned char current_bit_mask = 0;
static signed char cached_level[2] = {-1,-1};
static Point cell_cache[40][CELL_COUNT][4];
static short depth_cache[40][CELL_COUNT];
static Point3 roll_vertex[4][MOTION_STEPS][8];
static Point3 rest_vertex[8];
static u8 visible_face[40][5][MOTION_STEPS];
static OutlineByte __far outline_byte[OUTLINE_BYTE_MAX];
static u8 __far outline_mask[OUTLINE_BYTE_W*OUTLINE_H];
static unsigned outline_byte_count;
static u16 outline_head[256];
static bool sparse_wireframe;

static const u8 cube_face_vertex[6][4] = {
    {1,3,7,5}, {2,6,7,3}, {0,4,6,2},
    {0,1,5,4}, {4,5,7,6}, {0,2,3,1}
};

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
static const short roll_cos[MOTION_STEPS+1] = {
    256,253,243,228,208,181,150,116,80,40,0
};
static const short roll_sin[MOTION_STEPS+1] = {
    0,40,80,116,150,181,208,228,243,253,256
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

static bool mode_is_ega_high_resolution(void)
{
    union REGS regs;
    regs.w.ax=0x0f00;
    int86(0x10,&regs,&regs);
    return regs.h.al==0x10;
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

/* The EGA bit mask preserves pixels from the internal latches.  A read from
 * this exact byte must immediately precede its masked write. */
static void masked_write(unsigned offset, unsigned char color);
#pragma aux masked_write = \
    "mov dx,0a000h" \
    "mov es,dx" \
    "mov ah,es:[di]" \
    "mov es:[di],al" \
    parm [di] [al] modify [ax dx es];

static void apply_outline_bucket(unsigned page,const OutlineByte __far *bytes,unsigned first);
/* Every byte in a bucket shares the EGA bit mask selected by the caller.
 * Write mode 2 plus color zero clears that mask in all four enabled planes. */
#pragma aux apply_outline_bucket = \
    "push ds" \
    "push bp" \
    "mov ds,dx" \
    "mov ax,0a000h" \
    "mov es,ax" \
    "mov dx,di" \
    "mov bp,si" \
    "bucket_loop:" \
    "mov si,bx" \
    "shl si,1" \
    "shl si,1" \
    "add si,bp" \
    "mov di,[si]" \
    "add di,dx" \
    "mov al,es:[di]" \
    "xor al,al" \
    "mov es:[di],al" \
    "mov bx,[si+2]" \
    "cmp bx,0ffffh" \
    "jne bucket_loop" \
    "pop bp" \
    "pop ds" \
    parm [di] [dx si] [bx] modify [ax bx dx di si es];

static void apply_outline_plane(unsigned page,const u8 __far *mask);
#pragma aux apply_outline_plane = \
    "push ds" \
    "mov ds,dx" \
    "mov ax,0a000h" \
    "mov es,ax" \
    "add di,5130" \
    "mov bx,286" \
    "outline_row_loop:" \
    "mov cx,55" \
    "outline_byte_loop:" \
    "lodsb" \
    "or al,al" \
    "jz short outline_skip_byte" \
    "not al" \
    "and es:[di],al" \
    "outline_skip_byte:" \
    "inc di" \
    "loop outline_byte_loop" \
    "add di,25" \
    "dec bx" \
    "jnz outline_row_loop" \
    "pop ds" \
    parm [di] [dx si] modify [ax bx cx di si es];

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
        masked_write(offset,color);
    } else {
        set_bit_mask(left_mask); masked_write(offset,color);
        set_bit_mask(0xff);
        if (last > first + 1)
            fill_run_386(draw_page + (unsigned)y * PITCH + first + 1,
                         last - first - 1, color);
        set_bit_mask(right_mask);
        masked_write(draw_page + (unsigned)y * PITCH + last,color);
    }
}

static void clear_page(unsigned char color)
{
    set_bit_mask(0xff);
    fill_run_386(draw_page, PAGE_BYTES, color);
}

static void clear_byte_rect(unsigned byte_x, unsigned y, unsigned byte_width,
                            unsigned height, unsigned char color)
{
    unsigned row;
    set_bit_mask(0xff);
    for(row=0;row<height;++row)
        fill_run_386(draw_page+(y+row)*PITCH+byte_x,byte_width,color);
}

static void quad(Point a, Point b, Point c, Point d, unsigned char color)
{
    Point p[4]; int left[160],right[160]; int min_y=a.y,max_y=a.y,y,edge,height;
    p[0]=a;p[1]=b;p[2]=c;p[3]=d;
    if(b.y<min_y)min_y=b.y;if(c.y<min_y)min_y=c.y;if(d.y<min_y)min_y=d.y;
    if(b.y>max_y)max_y=b.y;if(c.y>max_y)max_y=c.y;if(d.y>max_y)max_y=d.y;
    height=max_y-min_y;
    if(height<=0 || height>160) return;
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
    for(y=0;y<height;++y)if(left[y]<=right[y]) {
        int screen_y=min_y+y,lx=left[y],rx=right[y];
        if(quad_clip_enabled) {
            if(screen_y<quad_clip_y1 || screen_y>quad_clip_y2)continue;
            if(lx<quad_clip_x1)lx=quad_clip_x1;if(rx>quad_clip_x2)rx=quad_clip_x2;
        }
        if(lx<=rx)hline(lx,rx,screen_y,color);
    }
}

static void inset_quad(Point *p)
{
    int center_x=(p[0].x+p[1].x+p[2].x+p[3].x)/4;
    int center_y=(p[0].y+p[1].y+p[2].y+p[3].y)/4;unsigned i;
    for(i=0;i<4;++i) {
        if(p[i].x<center_x)++p[i].x;else if(p[i].x>center_x)--p[i].x;
        if(p[i].y<center_y)++p[i].y;else if(p[i].y>center_y)--p[i].y;
    }
}

static void outlined_quad(Point *p,unsigned char color)
{
    Point inside[4];unsigned i;
    quad(p[0],p[1],p[2],p[3],0);
    for(i=0;i<4;++i)inside[i]=p[i];inset_quad(inside);
    quad(inside[0],inside[1],inside[2],inside[3],color);
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
                unsigned bits=font[index][row],column; int run=-1;
                for(column=0;column<=5;++column) {
                    bool set=column<5 && (bits&(16U>>column))!=0;
                    if(set && run<0) run=(int)column;
                    if(!set && run>=0) {
                        hline(x+run,x+(int)column-1,y+row,color);
                        run=-1;
                    }
                }
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
    point.x = center_x + ((long)cos_table[angle]*hx-(long)sin_table[angle]*hy)*30/256;
    point.y = center_y + ((long)sin_table[angle]*hx+(long)cos_table[angle]*hy)*12/256;
    return point;
}

static Point project_3d(const Point3 *world,int angle)
{
    Point point;
    point.x=300+((long)cos_table[angle]*world->x-(long)sin_table[angle]*world->y)*30/65536L;
    point.y=186+((long)sin_table[angle]*world->x+(long)cos_table[angle]*world->y)*12/65536L-
            (long)world->z*18/256L;
    return point;
}

static u8 cube_visible_mask(const Point3 *vertices,int angle)
{
    Point screen[8]; u8 mask=0; unsigned vertex,face;
    for(vertex=0;vertex<8;++vertex) screen[vertex]=project_3d(&vertices[vertex],angle);
    for(face=0;face<6;++face) {
        long area=0; unsigned corner;
        for(corner=0;corner<4;++corner) {
            Point a=screen[cube_face_vertex[face][corner]];
            Point b=screen[cube_face_vertex[face][(corner+1)&3]];
            area+=(long)a.x*b.y-(long)a.y*b.x;
        }
        if(area>0) mask|=(u8)(1U<<face);
    }
    return mask;
}

static void init_cube_cache(void)
{
    unsigned vertex,direction,step,angle;
    for(vertex=0;vertex<8;++vertex) {
        rest_vertex[vertex].x=vertex&1 ? 256 : -256;
        rest_vertex[vertex].y=vertex&2 ? 256 : -256;
        rest_vertex[vertex].z=vertex&4 ? 512 : 0;
    }
    for(direction=0;direction<4;++direction) for(step=0;step<MOTION_STEPS;++step)
        for(vertex=0;vertex<8;++vertex) {
            Point3 point=rest_vertex[vertex]; int c=roll_cos[step],s=roll_sin[step];
            int pivot,first,second;
            if(direction==WORLD_EAST || direction==WORLD_WEST) {
                pivot=direction==WORLD_EAST ? 256 : -256;
                if(direction==WORLD_WEST) s=-s;
                first=point.x-pivot; second=point.z;
                point.x=pivot+((long)first*c+(long)second*s)/256L;
                point.z=((long)-first*s+(long)second*c)/256L;
            } else {
                pivot=direction==WORLD_SOUTH ? 256 : -256;
                if(direction==WORLD_SOUTH) s=-s;
                first=point.y-pivot; second=point.z;
                point.y=pivot+((long)first*c-(long)second*s)/256L;
                point.z=((long)first*s+(long)second*c)/256L;
            }
            roll_vertex[direction][step][vertex]=point;
        }
    for(angle=0;angle<40;++angle) {
        for(step=0;step<MOTION_STEPS;++step)
            visible_face[angle][4][step]=cube_visible_mask(rest_vertex,angle);
        for(direction=0;direction<4;++direction) for(step=0;step<MOTION_STEPS;++step)
            visible_face[angle][direction][step]=cube_visible_mask(roll_vertex[direction][step],angle);
    }
}

static void cell_points(unsigned x,unsigned y,int angle,Point *points)
{
    unsigned index=y*BOARD_SIZE+x;
    points[0]=cell_cache[angle][index][0];points[1]=cell_cache[angle][index][1];
    points[2]=cell_cache[angle][index][2];points[3]=cell_cache[angle][index][3];
}

static void init_cell_cache(void)
{
    unsigned angle,x,y;
    for(angle=0;angle<40;++angle) for(y=0;y<BOARD_SIZE;++y) for(x=0;x<BOARD_SIZE;++x) {
        unsigned index=y*BOARD_SIZE+x; Point *points=cell_cache[angle][index];
        points[0]=project_half((int)x*2-3,(int)y*2-3,angle,300,186);
        points[1]=project_half((int)x*2-1,(int)y*2-3,angle,300,186);
        points[2]=project_half((int)x*2-1,(int)y*2-1,angle,300,186);
        points[3]=project_half((int)x*2-3,(int)y*2-1,angle,300,186);
        depth_cache[angle][index]=(short)(points[0].y+points[2].y);
    }
}

static unsigned char color_of(u8 semantic)
{
    return semantic < 11 ? ega_color[semantic] : 7;
}

static void draw_cell(unsigned x,unsigned y,u8 paint,int angle)
{
    Point p[4];unsigned char fill=paint?color_of(paint):8;
    cell_points(x,y,angle,p);quad(p[0],p[1],p[2],p[3],fill);
}

static Point field_vertex(unsigned x,unsigned y,int angle)
{
    return project_half((int)x*2-5,(int)y*2-5,angle,300,186);
}

static void ring_points(unsigned side,int angle,unsigned drop_step,Point *p)
{
    int offset;
    if(side==0) {
        p[0]=field_vertex(0,0,angle);p[1]=field_vertex(5,0,angle);
        p[2]=field_vertex(4,1,angle);p[3]=field_vertex(1,1,angle);offset=drop_step*3;
    } else if(side==1) {
        p[0]=field_vertex(5,0,angle);p[1]=field_vertex(5,5,angle);
        p[2]=field_vertex(4,4,angle);p[3]=field_vertex(4,1,angle);offset=drop_step*4;
    } else if(side==2) {
        p[0]=field_vertex(5,5,angle);p[1]=field_vertex(0,5,angle);
        p[2]=field_vertex(1,4,angle);p[3]=field_vertex(4,4,angle);offset=drop_step*5;
    } else {
        p[0]=field_vertex(0,5,angle);p[1]=field_vertex(0,0,angle);
        p[2]=field_vertex(1,1,angle);p[3]=field_vertex(1,4,angle);offset=drop_step*4;
    }
    p[0].y+=offset;p[1].y+=offset;p[2].y+=offset;p[3].y+=offset;
}

static void draw_outer_ring(int angle,unsigned drop_step,unsigned mask)
{
    unsigned side;
    for(side=0;side<4;++side)if(mask&(1U<<side)) {
        Point p[4];ring_points(side,angle,drop_step,p);
        quad(p[0],p[1],p[2],p[3],7);
    }
}

static bool ring_side_intersects(unsigned side,int angle,int x1,int y1,int x2,int y2)
{
    Point p[4];int min_x,max_x,min_y,max_y;unsigned i;
    ring_points(side,angle,0,p);min_x=max_x=p[0].x;min_y=max_y=p[0].y;
    for(i=1;i<4;++i) {
        if(p[i].x<min_x)min_x=p[i].x;if(p[i].x>max_x)max_x=p[i].x;
        if(p[i].y<min_y)min_y=p[i].y;if(p[i].y>max_y)max_y=p[i].y;
    }
    return max_x>=x1 && min_x<=x2 && max_y>=y1 && min_y<=y2;
}

static void add_outline_byte(unsigned offset,unsigned char draw_mask)
{
    OutlineByte __far *item;
    if(!draw_mask)return;
    if(!sparse_wireframe) {
        unsigned y=offset/PITCH,byte=offset%PITCH;
        if(y>=OUTLINE_Y && y<OUTLINE_Y+OUTLINE_H &&
           byte>=OUTLINE_BYTE_X && byte<OUTLINE_BYTE_X+OUTLINE_BYTE_W)
            outline_mask[(y-OUTLINE_Y)*OUTLINE_BYTE_W+byte-OUTLINE_BYTE_X]|=draw_mask;
        return;
    }
    if(outline_byte_count>=OUTLINE_BYTE_MAX)return;
    item=&outline_byte[outline_byte_count++];
    item->offset=(u16)offset;
    item->next=outline_head[draw_mask];
    outline_head[draw_mask]=(u16)(outline_byte_count-1);
}

static void outline_span(int x1,int x2,int y,int clip_x1,int clip_y1,int clip_x2,int clip_y2)
{
    if(y<clip_y1 || y>clip_y2 || x2<clip_x1 || x1>clip_x2)return;
    if(x1<clip_x1)x1=clip_x1;if(x2>clip_x2)x2=clip_x2;
    if(y<0 || y>=SCREEN_H || x2<0 || x1>=SCREEN_W)return;
    if(x1<0)x1=0;if(x2>=SCREEN_W)x2=SCREEN_W-1;
    {
        unsigned first=(unsigned)x1>>3,last=(unsigned)x2>>3;
        unsigned row=(unsigned)y*PITCH;
        unsigned char left=(unsigned char)(0xff>>(x1&7));
        unsigned char right=(unsigned char)(0xff<<(7-(x2&7)));
        if(first==last)add_outline_byte(row+first,(u8)(left&right));
        else {
            unsigned byte;add_outline_byte(row+first,left);
            for(byte=first+1;byte<last;++byte)add_outline_byte(row+byte,0xff);
            add_outline_byte(row+last,right);
        }
    }
}

static void outline_line(Point a,Point b,int x1,int y1,int x2,int y2)
{
    int dx,dy,y,first_y,last_y;long current,step;
    if(a.y>b.y){Point swap=a;a=b;b=swap;}dx=b.x-a.x;dy=b.y-a.y;
    if(dy==0){int left=a.x<b.x?a.x:b.x,right=a.x>b.x?a.x:b.x;
        outline_span(left,right,a.y,x1,y1,x2,y2);return;}
    first_y=a.y>y1?a.y:y1;last_y=b.y<y2?b.y:y2;
    step=((long)dx*65536L)/dy;
    current=(long)a.x*65536L+step*(first_y-a.y);
    for(y=first_y;y<=last_y;++y) {
        int pixel=(int)(current/65536L);
        int next=y==b.y?pixel:(int)((current+step)/65536L);
        int left=pixel<next?pixel:next,right=pixel>next?pixel:next;
        outline_span(left,right,y,x1,y1,x2,y2);
        current+=step;
    }
}

static void field_wireframe(const GameState *game,const Motion *motion,int angle,
                            int x1,int y1,int x2,int y2)
{
    unsigned i;bool whole=game->border_active || motion->drop_after_roll;
    if(motion->kind==MOTION_DROP) {
        unsigned side;
        for(i=1;i<=4;++i) {
            outline_line(field_vertex(i,1,angle),field_vertex(i,4,angle),x1,y1,x2,y2);
            outline_line(field_vertex(1,i,angle),field_vertex(4,i,angle),x1,y1,x2,y2);
        }
        for(side=0;side<4;++side) {
            Point p[4];unsigned edge;ring_points(side,angle,motion->step,p);
            for(edge=0;edge<4;++edge)outline_line(p[edge],p[(edge+1)&3],x1,y1,x2,y2);
        }
    } else {
        unsigned first=whole?0:1,last=whole?5:4;
        for(i=first;i<=last;++i) {
            outline_line(field_vertex(i,first,angle),field_vertex(i,last,angle),x1,y1,x2,y2);
            outline_line(field_vertex(first,i,angle),field_vertex(last,i,angle),x1,y1,x2,y2);
        }
    }
}

static void draw_cube(const GameState *game, const Motion *motion, int angle)
{
    Point screen[8]; u8 face_color[6];
    CubeFaces faces=motion->kind==MOTION_ROLL ? motion->cube_before : game->cube;
    int center_x=(motion->kind==MOTION_ROLL ? motion->from_x : game->x)*512-1024;
    int center_y=(motion->kind==MOTION_ROLL ? motion->from_y : game->y)*512-1024;
    unsigned vertex,face,direction=4,step=0; const Point3 *geometry;
    face_color[0]=faces.east; face_color[1]=faces.south;
    face_color[2]=faces.west; face_color[3]=faces.north;
    face_color[4]=faces.top; face_color[5]=faces.bottom;
    if(motion->kind==MOTION_ROLL) {
        direction=(unsigned)(((game->x-motion->from_x)>0) ? WORLD_EAST :
            ((game->x-motion->from_x)<0) ? WORLD_WEST :
            ((game->y-motion->from_y)>0) ? WORLD_SOUTH : WORLD_NORTH);
        step=motion->step; geometry=roll_vertex[direction][step];
    } else geometry=rest_vertex;
    for(vertex=0;vertex<8;++vertex) {
        Point3 point=geometry[vertex]; point.x+=center_x; point.y+=center_y;
        screen[vertex]=project_3d(&point,angle);
    }
    for(face=0;face<6;++face) {
        if(visible_face[angle][direction][step]&(1U<<face)) {
            Point p[4]; unsigned corner;
            for(corner=0;corner<4;++corner) p[corner]=screen[cube_face_vertex[face][corner]];
            outlined_quad(p,color_of(face_color[face]));
        }
    }
}

static void flush_sparse_wireframe(void)
{
    unsigned mask;
    if(!outline_byte_count)return;
    for(mask=1;mask<256;++mask)if(outline_head[mask]!=0xffffU) {
        set_bit_mask((u8)mask);
        apply_outline_bucket(draw_page,outline_byte,outline_head[mask]);
    }
    set_bit_mask(0xff);
}

static void flush_mask_wireframe(void)
{
    unsigned plane;
    outpw(0x3ce,0x0005);outpw(0x3ce,0xff08);
    for(plane=0;plane<4;++plane) {
        outpw(0x3c4,0x0002|((1U<<plane)<<8));
        outpw(0x3ce,0x0004|(plane<<8));
        apply_outline_plane(draw_page,outline_mask);
    }
    outpw(0x3c4,0x0f02);outpw(0x3ce,0x0004);outpw(0x3ce,0x0205);
    current_bit_mask=0xff;
}

static void reset_wireframe(void)
{
    outline_byte_count=0;
    if(sparse_wireframe)_fmemset(outline_head,0xff,sizeof(outline_head));
    else _fmemset(outline_mask,0,sizeof(outline_mask));
}

static void draw_field_wireframe(const GameState *game,const Motion *motion,int angle,
                                 bool dirty,int x1,int y1,int x2,int y2)
{
    sparse_wireframe=motion->kind==MOTION_TURN;
    reset_wireframe();
    field_wireframe(game,motion,angle,
                    dirty?x1:0,dirty?y1:0,dirty?x2:SCREEN_W-1,dirty?y2:SCREEN_H-1);
    if(sparse_wireframe)flush_sparse_wireframe();
    else flush_mask_wireframe();
}


static void draw_target(const GameState *game)
{
    unsigned x,y;
    text(567,16,"TARGET",15);
    for (y=0;y<BOARD_SIZE;++y) for(x=0;x<BOARD_SIZE;++x) {
        int left=536+(int)x*34, top=45+(int)y*27;
        unsigned char c=color_of(game->target[y*BOARD_SIZE+x]);
        int row; for(row=0;row<23;++row) hline(left,left+29,top+row,c);
        hline(left,left+29,top,0); hline(left,left+29,top+22,0);
    }
}

static void roll_dirty_bounds(const GameState *game,const Motion *motion,int angle,
                              int *x1,int *y1,int *x2,int *y2)
{
    Point from=project_half(motion->from_x*2-4,motion->from_y*2-4,angle,300,186);
    Point to=project_half(game->x*2-4,game->y*2-4,angle,300,186);
    *x1=(from.x<to.x ? from.x : to.x)-55; *x2=(from.x>to.x ? from.x : to.x)+55;
    *y1=(from.y<to.y ? from.y : to.y)-60; *y2=(from.y>to.y ? from.y : to.y)+28;
    if(*x1<80)*x1=80;if(*x2>519)*x2=519;
    if(*y1<64)*y1=64;if(*y2>321)*y2=321;
    *x1&=~7; *x2|=7;
}

static bool cell_intersects(unsigned index,int angle,int x1,int y1,int x2,int y2)
{
    Point *points=cell_cache[angle][index];int min_x=points[0].x,max_x=points[0].x;
    int min_y=points[0].y,max_y=points[0].y;unsigned corner;
    for(corner=1;corner<4;++corner) {
        if(points[corner].x<min_x)min_x=points[corner].x;
        if(points[corner].x>max_x)max_x=points[corner].x;
        if(points[corner].y<min_y)min_y=points[corner].y;
        if(points[corner].y>max_y)max_y=points[corner].y;
    }
    return max_x>=x1 && min_x<=x2 && max_y>=y1 && min_y<=y2;
}

static void draw_number(int x,int y,unsigned value,unsigned char color)
{
    char buf[6]; int pos=5; buf[pos]=0;
    do { buf[--pos]=(char)('0'+value%10); value/=10; } while(value && pos);
    text(x,y,buf+pos,color);
}

bool video_init(void)
{
    set_mode(0x10);
    if(!mode_is_ega_high_resolution()) { set_mode(3); return false; }
    outpw(0x3ce,0x0205); /* write mode 2: CPU low nibble selects plane color */
    outpw(0x3ce,0x0003);
    outpw(0x3ce,0x0001);
    outpw(0x3ce,0x0000);
    init_cell_cache(); init_cube_cache(); draw_page=PAGE_BYTES; display_page=0;
    cached_level[0]=-1; cached_level[1]=-1;
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
    cached_level[draw_page ? 1 : 0]=-1;
    for(row=0;row<60;++row) hline(72+row*2,568-row*2,36+row,13);
    text(300,63,"CUBOLOR",15);
    text(273,142,"ROLL PAINT MATCH",14);
    text(258,166,"BORDER ROLLS ARE FREE",7);
    text(263,190,"NUM 4 8 6 2  MOVE",11);
    text(260,218,"NUM 7 1    TURN LEFT",10);
    text(257,246,"NUM 9 3    TURN RIGHT",12);
    text(288,304,"PRESS ENTER",15);
}

void video_render_game(const GameState *game,const Motion *motion)
{
    u16 started;int angle=angle_for(game,motion);unsigned order[9],i,j;
    unsigned page=draw_page ? 1 : 0; bool fresh=cached_level[page]!=(signed char)game->level;
    bool dirty_roll=!fresh && motion->kind==MOTION_ROLL; int dirty_x1=0,dirty_y1=0,dirty_x2=0,dirty_y2=0;
    started=platform_profile_read();
    if(fresh) clear_page(1);
    else if(dirty_roll) {
        roll_dirty_bounds(game,motion,angle,&dirty_x1,&dirty_y1,&dirty_x2,&dirty_y2);
        clear_byte_rect((unsigned)dirty_x1>>3,(unsigned)dirty_y1,
                        (unsigned)(dirty_x2-dirty_x1+1)>>3,
                        (unsigned)(dirty_y2-dirty_y1+1),1);
    }
    else {
        /* The rotating board and rolling cube never leave this byte-aligned box. */
        clear_byte_rect(10,64,55,286,1);
    }
    if(profiling) profile.clear_ticks+=elapsed(started);
    started=platform_profile_read();
    quad_clip_enabled=dirty_roll;
    if(dirty_roll) {
        quad_clip_x1=dirty_x1;quad_clip_y1=dirty_y1;
        quad_clip_x2=dirty_x2;quad_clip_y2=dirty_y2;
    }
    if(game->border_active || motion->drop_after_roll || motion->kind==MOTION_DROP) {
        unsigned ring_mask=15;
        if(dirty_roll) {
            unsigned side;ring_mask=0;
            for(side=0;side<4;++side)
                if(ring_side_intersects(side,angle,dirty_x1,dirty_y1,dirty_x2,dirty_y2))
                    ring_mask|=1U<<side;
        }
        draw_outer_ring(angle,motion->kind==MOTION_DROP?motion->step:0,ring_mask);
    }
    for(i=0;i<9;++i)order[i]=i;
    for(i=1;i<9;++i) { unsigned value=order[i];int depth=depth_cache[angle][value];j=i;
        while(j>0) { int other=depth_cache[angle][order[j-1]];
            if(other<=depth)break;order[j]=order[j-1];--j; }
        order[j]=value;
    }
    for(i=0;i<9;++i) {
        unsigned index=order[i];u8 paint=game->paint[index];
        if(dirty_roll && !cell_intersects(index,angle,dirty_x1,dirty_y1,dirty_x2,dirty_y2))continue;
        if(motion->kind==MOTION_ROLL && game_is_inner(game->x,game->y) &&
           index==(unsigned)(game->y-INNER_OFFSET)*BOARD_SIZE+(game->x-INNER_OFFSET))
            paint=motion->destination_paint_before;
        draw_cell(index%3,index/3,paint,angle);
    }
    quad_clip_enabled=false;
    draw_field_wireframe(game,motion,angle,dirty_roll,
                         dirty_x1,dirty_y1,dirty_x2,dirty_y2);
    if(profiling) profile.board_ticks+=elapsed(started);
    started=platform_profile_read();draw_cube(game,motion,angle);
    if(profiling) profile.cube_ticks+=elapsed(started);
    started=platform_profile_read();
    if(fresh) {
        text(16,16,"LEVEL",7); draw_number(50,16,game->level+1,15);
        text(16,42,game_level_name(game->level),14);
        text(16,326,"MOVES",7);
        draw_target(game);
        cached_level[page]=(signed char)game->level;
    }
    if(fresh || !dirty_roll)text(340,326,"U UNDO  R RESET",7);
    draw_number(50,326,game->moves,15);
    if(profiling) { profile.hud_ticks+=elapsed(started); ++profile.frames; }
}

void video_render_complete(const GameState *game)
{
    Motion motion; memset(&motion,0,sizeof(motion)); video_render_game(game,&motion);
    { int row; for(row=0;row<68;++row) hline(92,508,126+row,0); }
    text(273,143,"PICTURE COMPLETE",14);
    text(288,172,"PRESS ENTER",15);
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
    static const char header[]="P6\n640 350\n255\n";
    static const unsigned char rgb[16][3] = {
        {0,0,0},{0,0,170},{0,170,0},{0,170,170},{170,0,0},{170,0,170},{170,85,0},{170,170,170},
        {85,85,85},{85,85,255},{85,255,85},{85,255,255},{255,85,85},{255,85,255},{255,255,85},{255,255,255}
    };
    unsigned char row[SCREEN_W*3];int file=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_BINARY,S_IREAD|S_IWRITE);
    unsigned y,byte,bit,plane;
    if(file<0)return false;
    if(write(file,header,sizeof(header)-1)!=(int)(sizeof(header)-1)){close(file);return false;}
    outpw(0x3ce,0x0005);
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
        if(write(file,row,sizeof(row))!=(int)sizeof(row)){close(file);return false;}
    }
    if(close(file)!=0)return false;
    outpw(0x3ce,0x0205);current_bit_mask=0;return true;
}
