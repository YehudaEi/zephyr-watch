/** Notifications Screen Implementation.
 * Provides functionality to display all notifications in a list view.
 *
 * @license GNU v3
 * @maintainer electricalgorithm @ github
 */

#include "userinterface/screens/notifications/notifications.h"
#include "bluetooth/services/notifications.h"
#include "lvgl.h"
#include "userinterface/screens/home/home.h"
#include "userinterface/utils.h"
#include <string.h>
#include <zephyr/logging/log.h>

// Create a logger.
LOG_MODULE_REGISTER(ZephyrWatch_UI_Notifications, LOG_LEVEL_INF);

// Holds the notifications screen objects.
lv_obj_t* notifications_screen;
static lv_obj_t* previous_screen;
static lv_obj_t* label_title;
static lv_obj_t* notifications_list;
static lv_obj_t* no_notifications_label;

// Forward declarations for static functions
static void render_title_label(lv_obj_t* flex_element);
static void render_notifications_list(lv_obj_t* flex_element);
static void create_notification_item(lv_obj_t* parent, const notification_t* notif);
static void notification_item_event_handler(lv_event_t* event);
static const char* get_notification_icon(notification_type_t type);
static void clear_all_notifications_event(lv_event_t* event);

void notifications_screen_event(lv_event_t* event)
{
    lv_event_code_t event_code = lv_event_get_code(event);

    // Handle gesture events
    if (event_code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

        // Check for right-to-left gesture to return to home
        if (dir == LV_DIR_LEFT) {
            LOG_DBG("Left gesture detected: returning to home screen.");
            notifications_screen_unload();
        }
    }

    // Handle double click to return to home
    if (event_code == LV_EVENT_DOUBLE_CLICKED) {
        LOG_DBG("Double click detected: returning to home screen.");
        notifications_screen_unload();
    }
}

void notifications_screen_init()
{
    LOG_DBG("Initializing notifications screen");

    // Create the screen object which is the LV object with no parent.
    notifications_screen = create_screen();

    // Create main vertical layout container
    lv_obj_t* main_column = create_column(notifications_screen, 100, 100);
    lv_obj_set_style_pad_all(main_column, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_row(main_column, 5, LV_PART_MAIN);

    // Create rows for different sections
    lv_obj_t* title_row = create_row(main_column, 100, 15);
    lv_obj_t* content_row = create_row(main_column, 100, 85);

    // Render all components
    render_title_label(title_row);
    render_notifications_list(content_row);

    // Add event handler for gestures
    lv_obj_add_event_cb(notifications_screen, notifications_screen_event, LV_EVENT_ALL, NULL);
    LOG_DBG("Notifications screen initialized successfully.");
}

static void render_title_label(lv_obj_t* flex_element)
{
    label_title = lv_label_create(flex_element);
    lv_label_set_text(label_title, "Notifications");

    // Center the title
    lv_obj_set_width(label_title, LV_SIZE_CONTENT);
    lv_obj_set_height(label_title, LV_SIZE_CONTENT);
    lv_obj_set_align(label_title, LV_ALIGN_CENTER);

    // Style the title
    lv_obj_set_style_text_color(label_title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(label_title, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_center(label_title);
}

static void render_notifications_list(lv_obj_t* flex_element)
{
    // Create a scrollable container for notifications
    notifications_list = lv_obj_create(flex_element);
    lv_obj_set_size(notifications_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_layout(notifications_list, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(notifications_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(notifications_list, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Configure scrolling
    lv_obj_set_scroll_dir(notifications_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(notifications_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(notifications_list, LV_OBJ_FLAG_SCROLLABLE);

    // Style the container
    lv_obj_set_style_bg_opa(notifications_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(notifications_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(notifications_list, 5, LV_PART_MAIN);
    lv_obj_set_style_pad_row(notifications_list, 5, LV_PART_MAIN);

    // Initially populate the list
    notifications_screen_refresh();
}

static void create_notification_item(lv_obj_t* parent, const notification_t* notif)
{
    if (!notif || !parent)
        return;

    // Create notification item container
    lv_obj_t* item_container = lv_obj_create(parent);
    lv_obj_set_size(item_container, LV_PCT(95), 70);

    // Style the container
    lv_obj_set_style_radius(item_container, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(item_container, lv_color_hex(0x2E2E2E), LV_PART_MAIN);
    lv_obj_set_style_border_width(item_container, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(item_container, lv_color_hex(0x555555), LV_PART_MAIN);
    lv_obj_set_style_pad_all(item_container, 8, LV_PART_MAIN);

    // Create icon
    lv_obj_t* icon = lv_label_create(item_container);
    lv_label_set_text(icon, get_notification_icon(notif->type));
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, lv_color_hex(0x00AAFF), LV_PART_MAIN);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, 0, 0);

    // Create title label
    lv_obj_t* title_label = lv_label_create(item_container);
    lv_label_set_text(title_label, notif->title);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(title_label, lv_color_white(), LV_PART_MAIN);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title_label, 140);
    lv_obj_align_to(title_label, icon, LV_ALIGN_OUT_RIGHT_TOP, 8, -8);

    // Create app name label
    lv_obj_t* app_label = lv_label_create(item_container);
    lv_label_set_text(app_label, notif->app_name);
    lv_obj_set_style_text_font(app_label, &lv_font_montserrat_10, LV_PART_MAIN);
    lv_obj_set_style_text_color(app_label, lv_color_hex(0x888888), LV_PART_MAIN);
    lv_label_set_long_mode(app_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(app_label, 140);
    lv_obj_align_to(app_label, title_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    // Create text preview if available
    if (strlen(notif->text) > 0) {
        lv_obj_t* text_label = lv_label_create(item_container);
        lv_label_set_text(text_label, notif->text);
        lv_obj_set_style_text_font(text_label, &lv_font_montserrat_10, LV_PART_MAIN);
        lv_obj_set_style_text_color(text_label, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
        lv_label_set_long_mode(text_label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(text_label, 140);
        lv_obj_align_to(text_label, app_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    }

    // Store notification ID in user data and add click event
    lv_obj_set_user_data(item_container, (void*)(uintptr_t)notif->id);
    lv_obj_add_event_cb(item_container, notification_item_event_handler, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(item_container, LV_OBJ_FLAG_CLICKABLE);
}

static void notification_item_event_handler(lv_event_t* event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_CLICKED) {
        uint32_t notif_id = (uint32_t)(uintptr_t)lv_obj_get_user_data(lv_event_get_target(event));
        LOG_DBG("Notification item clicked: ID %u", notif_id);

        // Clear the notification
        notifications_clear_by_id(notif_id);

        // Refresh the display
        notifications_screen_refresh();
    }
}

static const char* get_notification_icon(notification_type_t type)
{
    switch (type) {
    case NOTIF_TYPE_CALL:
        return LV_SYMBOL_CALL;
    case NOTIF_TYPE_SMS:
        return LV_SYMBOL_ENVELOPE;
    case NOTIF_TYPE_EMAIL:
        return LV_SYMBOL_ENVELOPE;
    case NOTIF_TYPE_SOCIAL:
        return LV_SYMBOL_WIFI;
    case NOTIF_TYPE_CALENDAR:
        return LV_SYMBOL_LIST;
    default:
        return LV_SYMBOL_BELL;
    }
}

static void clear_all_notifications_event(lv_event_t* event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_CLICKED) {
        LOG_DBG("Clear all notifications button clicked");
        notifications_clear_all();
        notifications_screen_refresh();
    }
}

void notifications_screen_refresh()
{
    if (!notifications_list)
        return;

    // Clear existing items
    lv_obj_clean(notifications_list);

    int count = notifications_get_count();
    LOG_DBG("Refreshing notifications screen with %d notifications", count);

    if (count == 0) {
        // Show "no notifications" message
        no_notifications_label = lv_label_create(notifications_list);
        lv_label_set_text(no_notifications_label, "No notifications");
        lv_obj_set_style_text_color(no_notifications_label, lv_color_hex(0x888888), LV_PART_MAIN);
        lv_obj_set_style_text_font(no_notifications_label, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_center(no_notifications_label);
    } else {
        // Add clear all button
        lv_obj_t* clear_all_btn = lv_btn_create(notifications_list);
        lv_obj_set_width(clear_all_btn, LV_PCT(95));
        lv_obj_set_height(clear_all_btn, 35);
        lv_obj_set_style_radius(clear_all_btn, 8, LV_PART_MAIN);
        lv_obj_set_style_bg_color(clear_all_btn, lv_color_hex(0xFF4444), LV_PART_MAIN);

        lv_obj_t* clear_all_label = lv_label_create(clear_all_btn);
        lv_label_set_text(clear_all_label, "Clear All");
        lv_obj_set_style_text_color(clear_all_label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_font(clear_all_label, &lv_font_montserrat_12, LV_PART_MAIN);
        lv_obj_center(clear_all_label);

        lv_obj_add_event_cb(clear_all_btn, clear_all_notifications_event, LV_EVENT_CLICKED, NULL);

        // Add all notification items (newest first)
        for (int i = count - 1; i >= 0; i--) {
            const notification_t* notif = notifications_get_by_index(i);
            if (notif && notif->active) {
                create_notification_item(notifications_list, notif);
            }
        }
    }
}

void notifications_screen_load()
{
    // Save the previous screen to unload afterwards.
    previous_screen = lv_scr_act();

    if (!lv_obj_is_valid(notifications_screen)) {
        notifications_screen_init();
    }

    // Refresh the notifications before showing
    notifications_screen_refresh();

    // Load the notifications screen with animation.
    lv_screen_load_anim(notifications_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
}

void notifications_screen_unload()
{
    // Load the previous screen.
    lv_screen_load_anim(previous_screen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 300, 0, false);
}
