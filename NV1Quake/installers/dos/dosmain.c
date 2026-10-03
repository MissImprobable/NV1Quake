#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "dosmain.h"
#include "screen.h"
#include "logo.h"
#include "notice_text.h"   /* generated from NOTICE.TXT by build-installers.ps1 */

/* Layout (80x25, 1-based BIOS text coordinates). Every row 1..25 is
   spoken for exactly once - no slack left to add anything without
   shrinking something else:
     row 1        title
     rows 2-15    logo (14 rows, its own last row is already blank so
                  no separate gap row is needed before the subtitle)
     row 16       subtitle
     row 17       box top border
     rows 18-22   5 fields (install dir / player name / resolution /
                  invert mouse / the Install button)
     row 23       box bottom border
     row 24       status text (hint during the form, filename/percent
                  during extraction, final result once done)
     row 25       progress bar (also doubles as the field-navigation
                  hint's underline space during the form - left blank) */
#define TITLE_ROW     1
#define LOGO_TOP      2
#define LOGO_LEFT     ((SCREEN_COLS - LOGO_WIDTH) / 2)
#define SUBTITLE_ROW  (LOGO_TOP + LOGO_HEIGHT)
#define BOX_TOP       (SUBTITLE_ROW + 1)
#define BOX_LEFT      8
#define BOX_RIGHT     71
#define BOX_BOTTOM    (BOX_TOP + 6)
#define FIELD_LABEL_COL (BOX_LEFT + 2)
#define FIELD_VALUE_COL (BOX_LEFT + 22)
#define FIELD_ROW(i) (BOX_TOP + 1 + (i))
#define STATUS_ROW    (BOX_BOTTOM + 1)
#define PROGRESS_ROW  (STATUS_ROW + 1)

/* Fields display at most this many characters - installDir's own
   buffer is 64 bytes (a silent /INSTALLDIR= from the client can be
   that long), but draw_field's valueBuf is sized for this width, not
   the full buffer, so truncate defensively here rather than trusting
   every caller to have already capped it. */
#define FIELD_DISPLAY_WIDTH 40

#define FIELD_INSTALLDIR 0
#define FIELD_PLAYERNAME 1
#define FIELD_RESOLUTION 2
#define FIELD_INVERTMOUSE 3
#define FIELD_INSTALL_BUTTON 4
#define FIELD_COUNT 5

static const struct
{
    int w, h;
    const char *label;
} RES_PRESETS[] = {
    {320, 200, "320x200  "},
    {640, 480, "640x480  "},
    {800, 600, "800x600  "},
    {1024, 768, "1024x768 "},
};
#define RES_PRESET_COUNT 4

static int g_showGameOptions;

static void draw_chrome(const GameProfile *profile)
{
    char subtitle[80];

    screen_clear(COL_LIGHTGRAY, COL_BLACK);
    screen_print_centered(TITLE_ROW, 1, SCREEN_COLS, COL_LIGHTRED, COL_BLACK, profile->title);

    screen_draw_logo(LOGO_TOP, LOGO_LEFT, COL_LIGHTRED, COL_BLACK);

    sprintf(subtitle, "%s - Setup", profile->appName);
    screen_print_centered(SUBTITLE_ROW, 1, SCREEN_COLS, COL_WHITE, COL_BLACK, subtitle);

    screen_draw_box(BOX_TOP, BOX_LEFT, BOX_BOTTOM, BOX_RIGHT, COL_LIGHTRED, COL_BLACK);
}

/* Redraws one field's label + current value, highlighted (inverted
   black-on-red) if it currently has focus. */
static void draw_field(int index, int focused,
                        const char *installDir, const char *playerName,
                        int resPresetIndex, int invertMouse)
{
    int row = FIELD_ROW(index);
    int fg = focused ? COL_BLACK : COL_WHITE;
    int bg = focused ? COL_LIGHTRED : COL_BLACK;
    char valueBuf[FIELD_DISPLAY_WIDTH + 8];
    char truncated[FIELD_DISPLAY_WIDTH + 1];

    if (!g_showGameOptions &&
        (index == FIELD_PLAYERNAME || index == FIELD_RESOLUTION || index == FIELD_INVERTMOUSE))
    {
        return;
    }

    switch (index)
    {
    case FIELD_INSTALLDIR:
        screen_print_at(row, FIELD_LABEL_COL, COL_LIGHTGRAY, COL_BLACK, "Install directory:");
        strncpy(truncated, installDir, FIELD_DISPLAY_WIDTH);
        truncated[FIELD_DISPLAY_WIDTH] = '\0';
        sprintf(valueBuf, "%-40s", truncated);
        screen_print_at(row, FIELD_VALUE_COL, fg, bg, valueBuf);
        break;
    case FIELD_PLAYERNAME:
        screen_print_at(row, FIELD_LABEL_COL, COL_LIGHTGRAY, COL_BLACK, "Player name:");
        strncpy(truncated, playerName, FIELD_DISPLAY_WIDTH);
        truncated[FIELD_DISPLAY_WIDTH] = '\0';
        sprintf(valueBuf, "%-40s", truncated);
        screen_print_at(row, FIELD_VALUE_COL, fg, bg, valueBuf);
        break;
    case FIELD_RESOLUTION:
        screen_print_at(row, FIELD_LABEL_COL, COL_LIGHTGRAY, COL_BLACK, "Screen resolution:");
        sprintf(valueBuf, "< %s >", RES_PRESETS[resPresetIndex].label);
        screen_print_at(row, FIELD_VALUE_COL, fg, bg, valueBuf);
        break;
    case FIELD_INVERTMOUSE:
        screen_print_at(row, FIELD_LABEL_COL, COL_LIGHTGRAY, COL_BLACK, "Invert mouse:");
        sprintf(valueBuf, "< %-3s >", invertMouse ? "Yes" : "No");
        screen_print_at(row, FIELD_VALUE_COL, fg, bg, valueBuf);
        break;
    case FIELD_INSTALL_BUTTON:
        /* with the game fields hidden the button moves up under the install directory */
        if (!g_showGameOptions)
        {
            row = FIELD_ROW(2);
        }
        screen_print_centered(row, BOX_LEFT + 1, BOX_RIGHT - 1, fg, bg, "[ INSTALL ]");
        break;
    }
}

static void draw_all_fields(int focusIndex, const char *installDir, const char *playerName,
                             int resPresetIndex, int invertMouse)
{
    int i;
    for (i = 0; i < FIELD_COUNT; i++)
    {
        draw_field(i, i == focusIndex, installDir, playerName, resPresetIndex, invertMouse);
    }
}

/* Next/previous visible field; skips the game-specific fields when hidden. */
static int next_field(int f, int dir)
{
    do
    {
        f = (f + dir + FIELD_COUNT) % FIELD_COUNT;
    } while (!g_showGameOptions &&
             (f == FIELD_PLAYERNAME || f == FIELD_RESOLUTION || f == FIELD_INVERTMOUSE));
    return f;
}

/* Runs the interactive full-screen form. Fills args with the user's
   choices. Returns when the user activates the Install button - Esc
   exits the whole program immediately (matches real installers, which
   never confirm-before-quit on this screen either). */
static void run_form(const GameProfile *profile, ProvisioningArgs *args)
{
    char installDir[64];
    char playerName[32];
    int resPresetIndex = 1; /* default 640x480 */
    int invertMouse = 0;
    int focus = FIELD_INSTALLDIR;
    int editLen;

    g_showGameOptions = profile->showGameOptions;

    strncpy(installDir, args->hasInstallDir ? args->installDir : profile->defaultInstallDir, sizeof(installDir) - 1);
    installDir[sizeof(installDir) - 1] = '\0';
    strncpy(playerName, args->hasPlayerName ? args->playerName : "DUKE", sizeof(playerName) - 1);
    playerName[sizeof(playerName) - 1] = '\0';
    if (args->hasInvertMouse)
    {
        invertMouse = args->invertMouse;
    }
    if (args->hasRes)
    {
        int i;
        for (i = 0; i < RES_PRESET_COUNT; i++)
        {
            if (RES_PRESETS[i].w == args->resWidth && RES_PRESETS[i].h == args->resHeight)
            {
                resPresetIndex = i;
                break;
            }
        }
    }

    draw_chrome(profile);
    screen_print_at(STATUS_ROW, 1, COL_BLACK, COL_BLACK,
                     "                                                                                ");
    screen_print_centered(STATUS_ROW, 1, SCREEN_COLS, COL_DARKGRAY, COL_BLACK,
                           "UP/DOWN or TAB: move   LEFT/RIGHT: change   ENTER: select   ESC: quit");
    draw_all_fields(focus, installDir, playerName, resPresetIndex, invertMouse);

    for (;;)
    {
        int key = screen_read_key();

        switch (key)
        {
        case 27: /* Esc */
            screen_shutdown();
            exit(1);

        case KEY_UP:
        case KEY_SHIFT_TAB:
            draw_field(focus, 0, installDir, playerName, resPresetIndex, invertMouse);
            focus = next_field(focus, -1);
            draw_field(focus, 1, installDir, playerName, resPresetIndex, invertMouse);
            break;

        case KEY_DOWN:
        case KEY_TAB:
            draw_field(focus, 0, installDir, playerName, resPresetIndex, invertMouse);
            focus = next_field(focus, 1);
            draw_field(focus, 1, installDir, playerName, resPresetIndex, invertMouse);
            break;

        case KEY_LEFT:
            if (focus == FIELD_RESOLUTION)
            {
                resPresetIndex = (resPresetIndex == 0) ? RES_PRESET_COUNT - 1 : resPresetIndex - 1;
                draw_field(focus, 1, installDir, playerName, resPresetIndex, invertMouse);
            }
            else if (focus == FIELD_INVERTMOUSE)
            {
                invertMouse = !invertMouse;
                draw_field(focus, 1, installDir, playerName, resPresetIndex, invertMouse);
            }
            break;

        case KEY_RIGHT:
            if (focus == FIELD_RESOLUTION)
            {
                resPresetIndex = (resPresetIndex + 1) % RES_PRESET_COUNT;
                draw_field(focus, 1, installDir, playerName, resPresetIndex, invertMouse);
            }
            else if (focus == FIELD_INVERTMOUSE)
            {
                invertMouse = !invertMouse;
                draw_field(focus, 1, installDir, playerName, resPresetIndex, invertMouse);
            }
            break;

        case 8: /* Backspace */
            if (focus == FIELD_INSTALLDIR)
            {
                editLen = (int)strlen(installDir);
                if (editLen > 0)
                {
                    installDir[editLen - 1] = '\0';
                }
                draw_field(focus, 1, installDir, playerName, resPresetIndex, invertMouse);
            }
            else if (focus == FIELD_PLAYERNAME)
            {
                editLen = (int)strlen(playerName);
                if (editLen > 0)
                {
                    playerName[editLen - 1] = '\0';
                }
                draw_field(focus, 1, installDir, playerName, resPresetIndex, invertMouse);
            }
            break;

        case 13: /* Enter */
            if (focus == FIELD_INSTALL_BUTTON)
            {
                strncpy(args->installDir, installDir, sizeof(args->installDir) - 1);
                args->installDir[sizeof(args->installDir) - 1] = '\0';
                args->hasInstallDir = 1;
                strncpy(args->playerName, playerName, sizeof(args->playerName) - 1);
                args->playerName[sizeof(args->playerName) - 1] = '\0';
                args->hasPlayerName = 1;
                args->resWidth = RES_PRESETS[resPresetIndex].w;
                args->resHeight = RES_PRESETS[resPresetIndex].h;
                args->hasRes = 1;
                args->invertMouse = invertMouse;
                args->hasInvertMouse = 1;
                return;
            }
            draw_field(focus, 0, installDir, playerName, resPresetIndex, invertMouse);
            focus = next_field(focus, 1);
            draw_field(focus, 1, installDir, playerName, resPresetIndex, invertMouse);
            break;

        default:
            if (key >= 32 && key < 127)
            {
                if (focus == FIELD_INSTALLDIR)
                {
                    /* Capped to FIELD_DISPLAY_WIDTH, not installDir's
                       full 64-byte capacity - draw_field's "%-40s"
                       formatting into a 48-byte valueBuf would overflow
                       if a longer string were ever allowed through
                       here. A silent /INSTALLDIR= from the client can
                       still carry a longer path; only interactive
                       typing is capped. */
                    editLen = (int)strlen(installDir);
                    if (editLen < FIELD_DISPLAY_WIDTH)
                    {
                        installDir[editLen] = (char)key;
                        installDir[editLen + 1] = '\0';
                    }
                    draw_field(focus, 1, installDir, playerName, resPresetIndex, invertMouse);
                }
                else if (focus == FIELD_PLAYERNAME)
                {
                    editLen = (int)strlen(playerName);
                    if (editLen < (int)sizeof(playerName) - 1)
                    {
                        playerName[editLen] = (char)key;
                        playerName[editLen + 1] = '\0';
                    }
                    draw_field(focus, 1, installDir, playerName, resPresetIndex, invertMouse);
                }
            }
            break;
        }
    }
}

static char g_lastProgressFile[13];
static unsigned int g_lastPercent;

static void progress_callback(const char *fileName, unsigned int current, unsigned int total)
{
    unsigned int percent = (current * 100) / total;
    char status[80];

    if (percent == g_lastPercent && strcmp(fileName, g_lastProgressFile) == 0)
    {
        return;
    }
    g_lastPercent = percent;
    strncpy(g_lastProgressFile, fileName, sizeof(g_lastProgressFile) - 1);
    g_lastProgressFile[sizeof(g_lastProgressFile) - 1] = '\0';

    sprintf(status, "Extracting: %-12s (%u of %u)                    ", fileName, current, total);
    screen_print_at(STATUS_ROW, BOX_LEFT + 1, COL_WHITE, COL_BLACK, status);
    screen_draw_progress(PROGRESS_ROW, BOX_LEFT + 1, 50, (int)percent, COL_LIGHTRED, COL_BLACK);
}

#define NOTICE_TOP    4
#define NOTICE_ROWS   17

/* Scrollable licence/notice text. UP/DOWN scroll, ENTER continues, ESC
   cancels. Returns 1 to continue, 0 if cancelled. */
static int show_notice(const GameProfile *profile)
{
    int first = 0;
    int maxFirst = NOTICE_LINE_COUNT - NOTICE_ROWS;
    int i, key;
    char line[SCREEN_COLS + 1];

    if (maxFirst < 0)
    {
        maxFirst = 0;
    }
    for (;;)
    {
        screen_clear(COL_LIGHTGRAY, COL_BLACK);
        screen_print_centered(1, 1, SCREEN_COLS, COL_LIGHTRED, COL_BLACK, profile->title);
        screen_print_centered(2, 1, SCREEN_COLS, COL_WHITE, COL_BLACK, "Licence and notices");
        screen_draw_box(NOTICE_TOP - 1, 1, NOTICE_TOP + NOTICE_ROWS, SCREEN_COLS, COL_LIGHTRED, COL_BLACK);
        for (i = 0; i < NOTICE_ROWS && first + i < NOTICE_LINE_COUNT; i++)
        {
            strncpy(line, NOTICE_LINES[first + i], SCREEN_COLS - 4);
            line[SCREEN_COLS - 4] = '\0';
            screen_print_at(NOTICE_TOP + i, 3, COL_LIGHTGRAY, COL_BLACK, line);
        }
        screen_print_centered(NOTICE_TOP + NOTICE_ROWS + 1, 1, SCREEN_COLS, COL_DARKGRAY, COL_BLACK,
                               "UP/DOWN: scroll   ENTER: continue   ESC: quit");
        screen_print_centered(25, 1, SCREEN_COLS, COL_DARKGRAY, COL_BLACK,
                               "Installer created by Abigail / Abnormality Software (2026)");

        key = screen_read_key();
        if (key == 27)
        {
            return 0;
        }
        if (key == 13)
        {
            return 1;
        }
        if (key == KEY_UP && first > 0)
        {
            first--;
        }
        else if (key == KEY_DOWN && first < maxFirst)
        {
            first++;
        }
    }
}

int run_installer(int argc, char **argv, const GameProfile *profile)
{
    ProvisioningArgs args;
    int silent;

    screen_init();
    parse_provisioning_args(argc, argv, &args);

    /* A client-driven install (the retro client passes /INSTALLDIR= for
       an unattended install) skips the interactive form entirely - the
       full-screen chrome and progress bar still show, just no field
       editing, matching the original silent-if-provisioned contract. */
    silent = args.hasInstallDir;

    g_showGameOptions = profile->showGameOptions;
    if (!silent)
    {
        if (!show_notice(profile))
        {
            screen_shutdown();
            return 1; /* cancelled at the licence screen: nothing written */
        }
        run_form(profile, &args);
    }

    draw_chrome(profile);
    screen_print_centered(STATUS_ROW, 1, SCREEN_COLS, COL_WHITE, COL_BLACK, "Installing...");
    g_lastPercent = 999;
    g_lastProgressFile[0] = '\0';
    screen_draw_progress(PROGRESS_ROW, BOX_LEFT + 1, 50, 0, COL_LIGHTRED, COL_BLACK);

    if (!extract_archive(argv[0], args.installDir, progress_callback))
    {
        screen_print_centered(STATUS_ROW, 1, SCREEN_COLS, COL_LIGHTRED, COL_BLACK,
                               "Installation FAILED. Press any key to exit.        ");
        screen_read_key();
        screen_shutdown();
        return 1;
    }

    if (profile->configure)
    {
        screen_print_centered(STATUS_ROW, 1, SCREEN_COLS, COL_WHITE, COL_BLACK,
                               "Applying settings...                              ");
        profile->configure(args.installDir, &args);
    }

    /* Credit line, printed into the now-unused box interior (the form
       fields that lived here are gone once installation starts) rather
       than competing with STATUS_ROW/PROGRESS_ROW below the box, which
       are already both spoken for by the complete-message and the
       sound-card disclaimer right after this. */
    screen_print_centered(FIELD_ROW(2), BOX_LEFT + 1, BOX_RIGHT - 1, COL_DARKGRAY, COL_BLACK,
                           "Installer created by Abigail / Abnormality Software (2026)");

    /* This installer only sets the handful of config values it was
       actually asked to (player name / resolution / mouse invert) - it
       doesn't do the hardware autodetection a real DOS game's own
       SETUP.EXE does (sound card IRQ/DMA/port, joystick calibration,
       etc). Surfacing that here, right before the user goes to launch
       the game, is more useful than burying it in a README nobody
       reads - a wrong sound card guess is exactly the kind of thing
       that looks like a broken install otherwise. */
    screen_print_centered(STATUS_ROW, 1, SCREEN_COLS, COL_LIGHTGREEN, COL_BLACK,
                           "Installation complete!                             ");
    screen_print_centered(PROGRESS_ROW, 1, SCREEN_COLS, COL_YELLOW, COL_BLACK,
                           "For custom sound card/advanced setup, run the game's own SETUP. Any key exits.");
    if (!silent)
    {
        screen_read_key();   /* an unattended (/INSTALLDIR=) install must not wait for a key */
    }
    screen_shutdown();

    return 0;
}
