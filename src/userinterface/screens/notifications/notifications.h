/** Notifications Screen Interface.
 * Provides functionality to display all notifications in a list view.
 *
 * @license GNU v3
 * @maintainer electricalgorithm @ github
 */

#ifndef _UI_SCREENS_NOTIFICATIONS_H
#define _UI_SCREENS_NOTIFICATIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* The screen object to be used in the userinterface. */
extern lv_obj_t* notifications_screen;

/* The init implementation for the notifications screen. */
void notifications_screen_init();

/** Load the notifications screen.
 * @return void
 */
void notifications_screen_load();

/** Unload the notifications screen.
 * @return void
 */
void notifications_screen_unload();

/** Event handler for notifications screen gestures.
 * @param event The event object containing gesture details.
 * @return void
 */
void notifications_screen_event(lv_event_t* event);

/** Refresh the notifications display.
 * @return void
 */
void notifications_screen_refresh();

#ifdef __cplusplus
} // extern "C"
#endif

#endif
