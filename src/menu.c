/*
 * The menu bar, built in code instead of loaded from a .RSC file.
 *
 * Positions are written in character cells, the way a resource editor
 * stores them; rsrc_obfix() converts them to pixels. A height like 0x0301
 * means "1 character + 3 pixels" (high byte is a pixel adjustment).
 *
 * The Desk menu must be: About, a separator, then six slots that the AES
 * fills in with the names of installed desk accessories.
 */
#include <stddef.h>
#include "menu.h"

static OBJECT tree[MENU_COUNT];

static char t_desk[] = " Desk ";
static char t_file[] = " File ";
static char s_about[] = "  About atari-chat...";
static char s_sep[]   = "---------------------";
static char s_acc[6][22] = {
    "  Desk Accessory 1   ", "  Desk Accessory 2   ", "  Desk Accessory 3   ",
    "  Desk Accessory 4   ", "  Desk Accessory 5   ", "  Desk Accessory 6   ",
};
static char s_quit[] = "  Quit     ^Q";

static void set(short i, short next, short head, short tail,
                unsigned short type, unsigned short state, char *str, long spec,
                short x, short y, short w, short h)
{
    OBJECT *o = &tree[i];

    o->ob_next = next;
    o->ob_head = head;
    o->ob_tail = tail;
    o->ob_type = type;
    o->ob_flags = 0;
    o->ob_state = state;
    if (str)
        o->ob_spec.free_string = str;
    else
        o->ob_spec.index = spec;
    o->ob_x = x;
    o->ob_y = y;
    o->ob_width = w;
    o->ob_height = h;
}

OBJECT *menu_build(void)
{
    GRECT screen;
    short i, bar_h;

    /*       idx            next            head           tail           type      state        str      spec        x  y  w   h */
    set(MENU_ROOT,     -1,             MENU_BAR,      MENU_SCREEN,   G_IBOX,   0,           NULL,    0L,         0, 0, 80, 25);
    set(MENU_BAR,      MENU_SCREEN,    MENU_ACTIVE,   MENU_ACTIVE,   G_BOX,    0,           NULL,    0x1100L,    0, 0, 80, 0x0201);
    set(MENU_ACTIVE,   MENU_BAR,       MENU_T_DESK,   MENU_T_FILE,   G_IBOX,   0,           NULL,    0L,         2, 0, 12, 0x0301);
    set(MENU_T_DESK,   MENU_T_FILE,    -1,            -1,            G_TITLE,  0,           t_desk,  0L,         0, 0, 6,  0x0301);
    set(MENU_T_FILE,   MENU_ACTIVE,    -1,            -1,            G_TITLE,  0,           t_file,  0L,         6, 0, 6,  0x0301);
    set(MENU_SCREEN,   MENU_ROOT,      MENU_DESK_BOX, MENU_FILE_BOX, G_IBOX,   0,           NULL,    0L,         0, 0x0301, 80, 19);
    set(MENU_DESK_BOX, MENU_FILE_BOX,  MENU_ABOUT,    MENU_ACC6,     G_BOX,    0,           NULL,    0xFF1100L,  2, 0, 21, 8);
    set(MENU_ABOUT,    MENU_DESK_SEP,  -1,            -1,            G_STRING, 0,           s_about, 0L,         0, 0, 21, 1);
    set(MENU_DESK_SEP, MENU_ACC1,      -1,            -1,            G_STRING, OS_DISABLED, s_sep,   0L,         0, 1, 21, 1);
    for (i = 0; i < 6; i++)
        set(MENU_ACC1 + i, (i < 5) ? MENU_ACC1 + i + 1 : MENU_DESK_BOX, -1, -1,
            G_STRING, 0, s_acc[i], 0L, 0, 2 + i, 21, 1);
    set(MENU_FILE_BOX, MENU_SCREEN,    MENU_QUIT,     MENU_QUIT,     G_BOX,    0,           NULL,    0xFF1100L,  8, 0, 13, 1);
    set(MENU_QUIT,     MENU_FILE_BOX,  -1,            -1,            G_STRING, 0,           s_quit,  0L,         0, 0, 13, 1);
    tree[MENU_QUIT].ob_flags = OF_LASTOB;

    for (i = 0; i < MENU_COUNT; i++)
        rsrc_obfix(tree, i);

    /* Stretch the bar and the root boxes to the real screen size. */
    wind_get_grect(0, WF_CURRXYWH, &screen);
    bar_h = tree[MENU_BAR].ob_height;
    tree[MENU_ROOT].ob_width = screen.g_w;
    tree[MENU_ROOT].ob_height = screen.g_h;
    tree[MENU_BAR].ob_width = screen.g_w;
    tree[MENU_SCREEN].ob_width = screen.g_w;
    tree[MENU_SCREEN].ob_height = screen.g_h - bar_h;

    return tree;
}
