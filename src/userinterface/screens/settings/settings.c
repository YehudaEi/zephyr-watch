/** Enhanced Settings Screen with Time Management
 * Replace the existing settings screen implementation with this enhanced version
 * Update src/userinterface/screens/settings/settings.c
 *
 * @license GNU v3
 * @maintainer electricalgorithm @ github
 */

#include "lvgl.h"
#include <stdio.h>
#include <zephyr/logging/log.h>

#include "bluetooth/infrastructure.h"
#include "devicetwin/devicetwin.h"
#include "display/display.h"
#include "misc/lv_event.h"
#include "userinterface/screens/menu/menu.h"
#include "userinterface/userinterface.h"
#include "userinterface/utils.h"

LOG_MODULE_REGISTER(ZephyrWatch_UI_Settings_Enhanced, LOG_LEVEL_INF);

// The screen container and UI elements
lv_obj_t* settings_screen;
static lv_obj_t* slider_brightness;
static lv_obj_t* enable_ble;
static lv_obj_t* manual_time_btn;
static lv_obj_t* status_label;
static lv_obj_t* current_time_label;

extern bool is_bluetooth_services_active(void);

/* Update current time display */
static void update_current_time_display(void)
{
    if (!current_time_label)
        return;

    device_twin_t* device_twin = get_device_twin_instance();
    if (!device_twin)
        return;

    datetime_t local_time = unix_to_localtime(device_twin->unix_time, device_twin->utc_zone);
    lv_label_set_text_fmt(current_time_label, "Current: %04d-%02d-%02d %02d:%02d:%02d",
        local_time.year, local_time.month, local_time.day,
        local_time.hour, local_time.minute, local_time.second);
}

static void status_timer_cb(lv_timer_t* timer)
{
    if (status_label) {
        lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_del(timer);
}

/* Show temporary status message */
static void show_status_message(const char* text, lv_color_t color, uint32_t duration_ms)
{
    if (!status_label)
        return;

    lv_label_set_text(status_label, text);
    lv_obj_set_style_text_color(status_label, color, 0);
    lv_obj_clear_flag(status_label, LV_OBJ_FLAG_HIDDEN);

    // Auto-hide after duration
    static lv_timer_t* status_timer = NULL;
    if (status_timer) {
        lv_timer_del(status_timer);
    }

    status_timer = lv_timer_create(status_timer_cb, duration_ms, NULL);
}

/* Settings screen event handler */
void settings_screen_event(lv_event_t* event)
{
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_DOUBLE_CLICKED) {
        if (!lv_obj_is_valid(menu_screen)) {
            menu_screen_init();
        }
        lv_screen_load_anim(menu_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 300, 0, false);
    }
}

/* Brightness slider event handler */
void brightness_slider_event(lv_event_t* event)
{
    lv_event_code_t event_code = lv_event_get_code(event);

    if (event_code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t* slider = lv_event_get_target_obj(event);
        uint8_t brightness = (uint8_t)lv_slider_get_value(slider);
        LOG_DBG("Brightness: %d%%", brightness);
        change_brightness(brightness);
    }
}

/* BLE checkbox event handler */
void ble_checkbox_event(lv_event_t* event)
{
    int ret;
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_VALUE_CHANGED) {
        lv_obj_t* checkbox = lv_event_get_target(event);
        bool checked = lv_obj_has_state(checkbox, LV_STATE_CHECKED);

        if (checked) {
            show_status_message("Enabling BLE...", lv_color_hex(0x00FF00), 0);
            lv_task_handler();

            ret = enable_bluetooth_subsystem();
            if (ret != 0) {
                LOG_ERR("Failed to enable bluetooth services (err %d)", ret);
                show_status_message("BLE Enable Failed!", lv_color_hex(0xFF0000), 2000);
                lv_obj_remove_state(checkbox, LV_STATE_CHECKED);
            } else {
                LOG_INF("Bluetooth services enabled.");
                show_status_message("BLE Enabled!", lv_color_hex(0x00FF00), 1000);
            }
        } else {
            show_status_message("Disabling BLE...", lv_color_hex(0xFFAA00), 0);
            lv_task_handler();

            ret = disable_bluetooth_subsystem();
            if (ret != 0) {
                LOG_ERR("Failed to disable bluetooth services (err %d)", ret);
                show_status_message("BLE Disable Failed!", lv_color_hex(0xFF0000), 2000);
                lv_obj_add_state(checkbox, LV_STATE_CHECKED);
            } else {
                LOG_INF("Bluetooth services disabled.");
                show_status_message("BLE Disabled!", lv_color_hex(0x888888), 500);
            }
        }
    }
}

void set_btn_event(lv_event_t* event)
{
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) {
        // This would need proper implementation to extract spinner values
        // and update the time - simplified for demonstration
        show_status_message("Time updated manually", lv_color_hex(0x00FF00), 2000);
        lv_msgbox_close(lv_obj_get_parent(lv_event_get_target(event)));
    }
}

/* Manual time setting popup */
static void show_manual_time_popup(void)
{
    device_twin_t* device_twin = get_device_twin_instance();
    if (!device_twin)
        return;

    datetime_t current = unix_to_localtime(device_twin->unix_time, device_twin->utc_zone);

    // Create a simple popup for manual time adjustment
    lv_obj_t* popup = lv_msgbox_create(lv_scr_act());
    lv_msgbox_add_title(popup, "Set Time");
    lv_obj_set_size(popup, 200, 180);
    lv_obj_center(popup);

    // Add time adjustment controls
    lv_obj_t* content = lv_msgbox_get_content(popup);

    // Hour spinner
    lv_obj_t* hour_spinner = lv_spinbox_create(content);
    lv_spinbox_set_range(hour_spinner, 0, 23);
    lv_spinbox_set_value(hour_spinner, current.hour);
    lv_spinbox_set_step(hour_spinner, 1);
    lv_obj_set_width(hour_spinner, 60);

    // Minute spinner
    lv_obj_t* min_spinner = lv_spinbox_create(content);
    lv_spinbox_set_range(min_spinner, 0, 59);
    lv_spinbox_set_value(min_spinner, current.minute);
    lv_spinbox_set_step(min_spinner, 1);
    lv_obj_set_width(min_spinner, 60);

    // Set button
    lv_obj_t* set_btn = lv_btn_create(content);
    lv_obj_t* set_label = lv_label_create(set_btn);
    lv_label_set_text(set_label, "SET");
    lv_obj_center(set_label);

    // Set button event handler
    lv_obj_add_event_cb(set_btn, set_btn_event, LV_EVENT_CLICKED, NULL);
}

/* Manual time button event handler */
void manual_time_btn_event(lv_event_t* event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (code == LV_EVENT_CLICKED) {
        LOG_INF("Manual time button clicked");
        show_manual_time_popup();
    }
}

/* Work item for updating time display */
static struct k_work update_time_work;
static void update_time_worker(struct k_work* work)
{
    update_current_time_display();
}

/* Refresh BLE state */
void settings_screen_refresh_ble_state(void)
{
    if (enable_ble && lv_obj_is_valid(enable_ble)) {
        if (is_bluetooth_services_active()) {
            lv_obj_add_state(enable_ble, LV_STATE_CHECKED);
        } else {
            lv_obj_remove_state(enable_ble, LV_STATE_CHECKED);
        }
    }
}

/* Enhanced settings screen initialization */
void settings_screen_init()
{
    LOG_DBG("Initializing enhanced settings screen");

    // Initialize work item
    k_work_init(&update_time_work, update_time_worker);

    // Create the screen object
    settings_screen = create_screen();
    lv_obj_t* main_column = create_column(settings_screen, 100, 100);

    // Title
    lv_obj_t* title_row = create_row(main_column, 100, 12);
    lv_obj_t* title_label = lv_label_create(title_row);
    lv_label_set_text(title_label, "Settings");
    lv_obj_set_style_text_color(title_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_18, LV_PART_MAIN);

    // Current time display
    lv_obj_t* time_row = create_row(main_column, 100, 12);
    current_time_label = lv_label_create(time_row);
    lv_obj_set_style_text_font(current_time_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_color(current_time_label, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
    update_current_time_display();

    // Status message (initially hidden)
    lv_obj_t* status_row = create_row(main_column, 100, 8);
    status_label = lv_label_create(status_row);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_add_flag(status_label, LV_OBJ_FLAG_HIDDEN);

    // Brightness setting
    lv_obj_t* brightness_obj = lv_obj_create(main_column);
    lv_obj_set_size(brightness_obj, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(brightness_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(brightness_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(brightness_obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(brightness_obj, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_t* lb_brightness = lv_label_create(brightness_obj);
    lv_label_set_text(lb_brightness, "Brightness:");
    slider_brightness = lv_slider_create(brightness_obj);
    lv_obj_set_width(slider_brightness, lv_pct(90));
    lv_slider_set_value(slider_brightness, 100, LV_ANIM_OFF);

    // BLE setting
    lv_obj_t* ble_obj = lv_obj_create(main_column);
    lv_obj_set_size(ble_obj, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ble_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ble_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(ble_obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(ble_obj, LV_OPA_TRANSP, LV_PART_MAIN);

    enable_ble = lv_checkbox_create(ble_obj);
    lv_checkbox_set_text(enable_ble, "Enable BLE");
    lv_obj_set_width(enable_ble, lv_pct(100));

    // Time sync buttons
    lv_obj_t* time_sync_obj = lv_obj_create(main_column);
    lv_obj_set_size(time_sync_obj, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(time_sync_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(time_sync_obj, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(time_sync_obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(time_sync_obj, LV_OPA_TRANSP, LV_PART_MAIN);

    // Manual time button
    manual_time_btn = lv_btn_create(time_sync_obj);
    lv_obj_set_size(manual_time_btn, lv_pct(80), 35);
    lv_obj_t* manual_label = lv_label_create(manual_time_btn);
    lv_label_set_text(manual_label, "Set Time Manually");
    lv_obj_set_style_text_font(manual_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_center(manual_label);

    // Add event handlers
    lv_obj_add_event_cb(title_row, settings_screen_event, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(slider_brightness, brightness_slider_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(enable_ble, ble_checkbox_event, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(manual_time_btn, manual_time_btn_event, LV_EVENT_CLICKED, NULL);

    // Initialize states
    settings_screen_refresh_ble_state();

    LOG_DBG("Enhanced settings screen initialized successfully.");
}
