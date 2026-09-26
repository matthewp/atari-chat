#ifndef MENU_H
#define MENU_H

#include <gem.h>

/* Object indices in the menu tree. */
enum {
    MENU_ROOT,
    MENU_BAR,
    MENU_ACTIVE,
    MENU_T_DESK,
    MENU_T_FILE,
    MENU_SCREEN,
    MENU_DESK_BOX,
    MENU_ABOUT,
    MENU_DESK_SEP,
    MENU_ACC1, MENU_ACC2, MENU_ACC3, MENU_ACC4, MENU_ACC5, MENU_ACC6,
    MENU_FILE_BOX,
    MENU_QUIT,
    MENU_COUNT
};

/* Build the menu tree in memory. Call after appl_init(). */
OBJECT *menu_build(void);

#endif
