#ifndef DOS_SCREEN_H
#define DOS_SCREEN_H

/* Full-screen colour text-mode UI primitives (80x25, 16 colours) - the
   classic "whole screen goes blue/red, navigate a boxed form" look
   shared by nearly every real mid-90s DOS installer (Doom's included),
   instead of a scrolling monochrome prompt-and-enter console app. Built
   on Open Watcom's Microsoft-C-compatible graph.h text functions
   (_settextcolor/_setbkcolor/_settextposition/_outtext/_setvideomode),
   not conio.h - Watcom's conio.h doesn't have the Borland/Turbo-C
   gotoxy()/textcolor() extensions this needed. */

/* Standard 16-colour EGA/VGA text palette indices (BIOS attribute byte
   low/high nibble values) - graph.h's grcolor takes these directly. */
#define COL_BLACK        0
#define COL_BLUE         1
#define COL_GREEN        2
#define COL_CYAN         3
#define COL_RED          4
#define COL_MAGENTA      5
#define COL_BROWN        6
#define COL_LIGHTGRAY    7
#define COL_DARKGRAY     8
#define COL_LIGHTBLUE    9
#define COL_LIGHTGREEN   10
#define COL_LIGHTCYAN    11
#define COL_LIGHTRED     12
#define COL_LIGHTMAGENTA 13
#define COL_YELLOW       14
#define COL_WHITE        15

#define SCREEN_COLS 80
#define SCREEN_ROWS 25

/* Switches into 80x25 16-colour text mode and hides the cursor. Call
   once at startup; pair with screen_shutdown() before exiting. */
void screen_init(void);

/* Restores the default video mode and cursor visibility. */
void screen_shutdown(void);

void screen_set_color(int fg, int bg);

/* Fills the whole screen with the given colour (also sets it as the
   current colour for subsequent screen_print_at calls). */
void screen_clear(int fg, int bg);

/* 1-based row/col, matching BIOS text coordinates. */
void screen_print_at(int row, int col, int fg, int bg, const char *text);

/* Prints text horizontally centred within [left,right] (inclusive). */
void screen_print_centered(int row, int left, int right, int fg, int bg, const char *text);

/* Draws a single-line CP437 box-drawing border. */
void screen_draw_box(int top, int left, int bottom, int right, int fg, int bg);

/* Draws the pre-rendered Fragged skull-and-crossbones ASCII logo
   (see logo.h) at the given top-left position. */
void screen_draw_logo(int top, int left, int fg, int bg);

/* Renders a [####......] style progress bar of the given total width
   (including the surrounding brackets) plus a trailing " NN%" label. */
void screen_draw_progress(int row, int left, int width, int percent, int fg, int bg);

/* Key codes returned by screen_read_key() for non-printable keys -
   chosen well outside the 0-255 printable/control-char range so a
   caller can switch on a single int. Printable characters and Enter
   (13) / Backspace (8) / Esc (27) are returned as their literal ASCII
   value; only cursor/function keys need this table. */
#define KEY_UP     1001
#define KEY_DOWN   1002
#define KEY_LEFT   1003
#define KEY_RIGHT  1004
#define KEY_TAB    1005
#define KEY_SHIFT_TAB 1006
#define KEY_F10    1007

int screen_read_key(void);

#endif
