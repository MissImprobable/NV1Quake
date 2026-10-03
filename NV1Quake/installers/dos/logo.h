#ifndef DOS_LOGO_H
#define DOS_LOGO_H

/* Fragged LAN Manager skull-and-crossbones logo (source: fragged.png),
   converted to a CP437 shaded-block ASCII grid for direct printing in
   text mode (see screen_draw_logo). Generated at 36x14 - the aspect
   ratio a real DOS 8x16 text cell needs to reproduce the source image's
   roughly-square proportions is cols:rows ~= 2:1, which 36:14 is close
   enough to unsquashed for. Colour comes from the caller's current text
   attribute (screen_draw_logo takes fg/bg), not from the characters
   themselves - the shading (space/light/medium/dark/full block) encodes
   how "solid" each cell should look, matching how the original red
   skull sits on a transparent background. */

#define LOGO_WIDTH 36
#define LOGO_HEIGHT 14

static const char *g_logo[LOGO_HEIGHT] = {
    "            \xB0\xB0\xB0\xB0\xB0\xB0\xB0\xB0\xB0\xB0\xB0\xB0            ",
    "        \xB0\xB0\xB0\xB0    \xB0\xB0\xB0\xB0    \xB0\xB0\xB0\xB0        ",
    "      \xB0\xB0\xB0   \xB0\xB1\xB2\xB2\xDB\xDB\xDB\xDB\xB2\xB2\xB1\xB0   \xB0\xB1\xB0      ",
    "    \xB0\xB1    \xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2\xB2\xDB\xDB\xDB\xDB\xDB\xDB\xB2    \xB1\xB0    ",
    "   \xB1\xB0    \xB2\xDB\xB0    \xB1\xDB\xDB\xB1    \xB1\xDB\xB1    \xB1\xB0   ",
    "  \xB0\xB1     \xB2\xB0    \xB0\xB2\xB0\xB0\xB2     \xB0\xB2     \xB2   ",
    "  \xB2     \xB1\xDB\xB2\xB1\xB1\xB1\xB2\xDB    \xDB\xB2\xB1\xB1\xB1\xB2\xDB\xB1    \xB0\xB1 \xB0",
    "  \xB1      \xB1\xB1  \xB0\xDB\xDB\xDB\xDB\xDB\xB2\xDB\xB2   \xB1\xB1     \xB0\xB1 \xB0",
    "   \xB2      \xB0\xB0            \xB0       \xB2   ",
    "    \xB1    \xB0\xB1\xB0\xB0\xB0\xB0     \xB0\xB0\xB0\xB0\xB1\xB1     \xB1    ",
    "     \xB0\xB0   \xB0\xB1\xB0\xB0\xB1\xB1\xB0\xB0 \xB0\xB0\xB1\xB0\xB0\xB0    \xB0\xB0     ",
    "       \xB0\xB0\xB0\xB0\xB0\xB0          \xB0\xB0 \xB0\xB0\xB0       ",
    "          \xB0 \xB0\xB0\xB0\xB0    \xB0\xB0\xB0             ",
    "                                    ",
};

#endif
