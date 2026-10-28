/* Native save operations run between complete engine passes, never from an input callback. */
#include "file.h"
#include "gfx.h"
#include "inv.h"
#include "motion.h"
#include "object.h"
#include "player.h"
#include "sys.h"
#include "ui.h"
extern int uw2_tv_take_quick_action(void);
int uw2_tv_quick_save;
void uw2_tv_game_safe_point(void)
{
    int action = uw2_tv_take_quick_action();
    /* A button pressed at the title screen can remain queued until mainloop's
       first pass. The initial do_changes has not created the scroll yet. */
    if (!action || !in_game || scrnum != 0 || !scroll || NewPlayerX || !ThePlayer || ThePlayer->hp == 0) return;
    if (action == 1 && !check_save()) return;
    if (action == 2 && !check_rest()) return;
    mouse_hide();
    display_inventory_no_show = 1;
    uw2_tv_quick_save = action == 1;
    DoSaveRest(action == 2, 4); /* Slot IV remains accessible through the game's normal load menu. */
    uw2_tv_quick_save = 0;
    display_inventory_no_show = 0;
    if (action == 2) { RightButtonThing = 0; punt_fightmode(); }
    mouse_show();
}
