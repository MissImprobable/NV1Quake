#include <dos.h>
#include <conio.h>
#include <stdio.h>
#include <string.h>
#include "screen.h"
#include "logo.h"

/* Direct BIOS int 10h + raw video memory writes (segment 0xB800), not
   Watcom's graph.h library - graph.h's _setvideomode() does extra
   hardware-capability probing beyond a plain "set mode 3" BIOS call,
   which was found (via a minimal isolated test) to hang completely
   under an older DOSBox build. Direct int 10h + video-memory writes is
   also just the more authentic technique real mid-90s DOS installers
   used in the first place, and has no such probing to hang on. */

#define VIDEO_SEG 0xB800

static unsigned char far *video_mem(void)
{
    return (unsigned char far *)MK_FP(VIDEO_SEG, 0);
}

void screen_init(void)
{
    union REGS regs;
    regs.h.ah = 0x00;
    regs.h.al = 0x03; /* mode 3: 80x25, 16-colour text */
    int86(0x10, &regs, &regs);

    /* Hide the cursor (start scanline > end scanline is the documented
       BIOS convention for "no cursor"). */
    regs.h.ah = 0x01;
    regs.h.ch = 0x20;
    regs.h.cl = 0x00;
    int86(0x10, &regs, &regs);
}

void screen_shutdown(void)
{
    union REGS regs;
    regs.h.ah = 0x01;
    regs.h.ch = 0x06;
    regs.h.cl = 0x07;
    int86(0x10, &regs, &regs); /* restore a normal-looking cursor */

    regs.h.ah = 0x00;
    regs.h.al = 0x03; /* back to plain 80x25 text so the DOS prompt is normal */
    int86(0x10, &regs, &regs);
}

static unsigned char attr_byte(int fg, int bg)
{
    return (unsigned char)((bg << 4) | (fg & 0x0F));
}

void screen_set_color(int fg, int bg)
{
    /* No persistent "current colour" state needed - every draw call
       takes fg/bg explicitly and writes both character + attribute
       bytes directly, unlike the old graph.h-based approach. */
    (void)fg;
    (void)bg;
}

void screen_clear(int fg, int bg)
{
    unsigned char far *vid = video_mem();
    unsigned char attr = attr_byte(fg, bg);
    int i;
    for (i = 0; i < SCREEN_COLS * SCREEN_ROWS; i++)
    {
        vid[i * 2] = ' ';
        vid[i * 2 + 1] = attr;
    }
}

void screen_print_at(int row, int col, int fg, int bg, const char *text)
{
    unsigned char far *vid = video_mem();
    unsigned char attr = attr_byte(fg, bg);
    int offset = ((row - 1) * SCREEN_COLS + (col - 1)) * 2;
    int i;

    for (i = 0; text[i] != '\0'; i++)
    {
        if (col - 1 + i >= SCREEN_COLS)
        {
            break; /* clip at the right edge instead of wrapping into the next row */
        }
        vid[offset + i * 2] = (unsigned char)text[i];
        vid[offset + i * 2 + 1] = attr;
    }
}

void screen_print_centered(int row, int left, int right, int fg, int bg, const char *text)
{
    int width = right - left + 1;
    int len = (int)strlen(text);
    int col = left + (width - len) / 2;
    if (col < left)
    {
        col = left;
    }
    screen_print_at(row, col, fg, bg, text);
}

void screen_draw_box(int top, int left, int bottom, int right, int fg, int bg)
{
    int row, col;
    char cell[2];
    cell[1] = '\0';

    cell[0] = '\xDA'; /* top-left */
    screen_print_at(top, left, fg, bg, cell);
    cell[0] = '\xBF'; /* top-right */
    screen_print_at(top, right, fg, bg, cell);
    cell[0] = '\xC0'; /* bottom-left */
    screen_print_at(bottom, left, fg, bg, cell);
    cell[0] = '\xD9'; /* bottom-right */
    screen_print_at(bottom, right, fg, bg, cell);

    cell[0] = '\xC4'; /* horizontal */
    for (col = left + 1; col < right; col++)
    {
        screen_print_at(top, col, fg, bg, cell);
        screen_print_at(bottom, col, fg, bg, cell);
    }

    cell[0] = '\xB3'; /* vertical */
    for (row = top + 1; row < bottom; row++)
    {
        screen_print_at(row, left, fg, bg, cell);
        screen_print_at(row, right, fg, bg, cell);
    }
}

void screen_draw_logo(int top, int left, int fg, int bg)
{
    int i;
    for (i = 0; i < LOGO_HEIGHT; i++)
    {
        screen_print_at(top + i, left, fg, bg, g_logo[i]);
    }
}

void screen_draw_progress(int row, int left, int width, int percent, int fg, int bg)
{
    char bar[128];
    int innerWidth = width - 2; /* minus the two brackets */
    int filled;
    int i, p;
    char label[8];

    if (innerWidth > (int)sizeof(bar) - 1)
    {
        innerWidth = (int)sizeof(bar) - 1;
    }
    if (percent < 0)
    {
        percent = 0;
    }
    if (percent > 100)
    {
        percent = 100;
    }
    filled = (innerWidth * percent) / 100;

    p = 0;
    bar[p++] = '[';
    for (i = 0; i < innerWidth; i++)
    {
        bar[p++] = (i < filled) ? '\xDB' : '\xB0';
    }
    bar[p++] = ']';
    bar[p] = '\0';

    sprintf(label, " %3d%%", percent);

    screen_print_at(row, left, fg, bg, bar);
    screen_print_at(row, left + width, fg, bg, label);
}

int screen_read_key(void)
{
    int c = getch();
    if (c == 0 || c == 0xE0)
    {
        int c2 = getch();
        switch (c2)
        {
        case 72:
            return KEY_UP;
        case 80:
            return KEY_DOWN;
        case 75:
            return KEY_LEFT;
        case 77:
            return KEY_RIGHT;
        case 15:
            return KEY_SHIFT_TAB;
        case 68:
            return KEY_F10;
        default:
            return 0;
        }
    }
    if (c == 9)
    {
        return KEY_TAB;
    }
    return c;
}
