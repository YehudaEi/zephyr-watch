/**
 * @file notifications.c
 * @brief Smartwatch notifications implementation
 */

#include "notifications.h"
#include <stdio.h>
#include <string.h>
#include <zephyr/bluetooth/att.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(notifications, LOG_LEVEL_DBG);

/* ANCS UUIDs for iOS notifications */
#define BT_UUID_ANCS_NOTIFICATION_SOURCE_VAL \
    BT_UUID_128_ENCODE(0x9FBF120D, 0x6301, 0x42D9, 0x8C58, 0x25E699A21DBD)

static const struct bt_uuid_128 ancs_notif_src_uuid = BT_UUID_INIT_128(BT_UUID_ANCS_NOTIFICATION_SOURCE_VAL);

/* Custom notification service UUID for Android */
#define CUSTOM_NOTIF_SERVICE_UUID \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x1234, 0x1234, 0x123456789ABC)

#define CUSTOM_NOTIF_CHAR_UUID \
    BT_UUID_128_ENCODE(0x87654321, 0x4321, 0x4321, 0x4321, 0xCBA987654321)

static const struct bt_uuid_128 custom_notif_svc_uuid = BT_UUID_INIT_128(CUSTOM_NOTIF_SERVICE_UUID);
static const struct bt_uuid_128 custom_notif_char_uuid = BT_UUID_INIT_128(CUSTOM_NOTIF_CHAR_UUID);

/* Static variables */
static notification_t notifications[NOTIFICATION_QUEUE_SIZE];
static int notification_count = 0;
static uint32_t next_notification_id = 1;
static notification_callback_t notification_callback = NULL;
static lv_obj_t* notification_list = NULL;
static lv_obj_t* notification_popup = NULL;

/* LVGL styles */
static lv_style_t popup_style;
static lv_style_t list_style;

/* Forward declarations */
static uint8_t ancs_notification_source_notify(struct bt_conn* conn,
    struct bt_gatt_subscribe_params* params,
    const void* data, uint16_t length);
static uint8_t custom_notification_notify(struct bt_conn* conn,
    struct bt_gatt_subscribe_params* params,
    const void* data, uint16_t length);
static void add_notification(notification_type_t type, const char* app_name,
    const char* title, const char* text);
static const char* get_notification_icon(notification_type_t type);
static void popup_timer_callback(lv_timer_t* timer);
static void notification_item_clicked(lv_event_t* e);
static uint8_t discover_ancs_callback(struct bt_conn* conn,
    const struct bt_gatt_attr* attr,
    struct bt_gatt_discover_params* params);

/* GATT discovery parameters */
static struct bt_gatt_discover_params discover_params;

/* GATT subscription parameters */
static struct bt_gatt_subscribe_params ancs_notif_params = {
    .notify = ancs_notification_source_notify,
    .value_handle = 0,
    .ccc_handle = 0,
};

static struct bt_gatt_subscribe_params custom_notif_params __maybe_unused = {
    .notify = custom_notification_notify,
    .value_handle = 0,
    .ccc_handle = 0,
};

/* ANCS notification source callback */
static uint8_t ancs_notification_source_notify(struct bt_conn* conn,
    struct bt_gatt_subscribe_params* params,
    const void* data, uint16_t length)
{
    if (!data || length < 8) {
        return BT_GATT_ITER_CONTINUE;
    }

    const uint8_t* payload = (const uint8_t*)data;
    uint8_t event_id = payload[0];
    uint8_t category_id = payload[2];
    uint32_t notification_uid = sys_get_le32(&payload[4]);

    LOG_DBG("ANCS notification: event=%d, category=%d, uid=%d",
        event_id, category_id, notification_uid);

    /* Map ANCS categories to our types */
    notification_type_t type = NOTIF_TYPE_OTHER;
    switch (category_id) {
    case 1:
        type = NOTIF_TYPE_CALL;
        break;
    case 2:
        type = NOTIF_TYPE_SMS;
        break;
    case 3:
        type = NOTIF_TYPE_EMAIL;
        break;
    case 4:
    case 5:
    case 6:
        type = NOTIF_TYPE_SOCIAL;
        break;
    case 7:
    case 8:
        type = NOTIF_TYPE_CALENDAR;
        break;
    default:
        type = NOTIF_TYPE_OTHER;
        break;
    }

    /* For demo purposes, add a sample notification */
    if (event_id == 0) { /* Added */
        add_notification(type, "iOS App", "New Notification",
            "You have a new notification from your iOS device");
    } else if (event_id == 2) { /* Removed */
        notifications_clear_by_id(notification_uid);
    }

    return BT_GATT_ITER_CONTINUE;
}

/* Custom notification callback for Android */
static uint8_t custom_notification_notify(struct bt_conn* conn,
    struct bt_gatt_subscribe_params* params,
    const void* data, uint16_t length)
{
    if (!data || length < 4) {
        return BT_GATT_ITER_CONTINUE;
    }

    const uint8_t* payload = (const uint8_t*)data;
    uint8_t type = payload[0];
    uint8_t app_len = payload[1];
    uint8_t title_len = payload[2];
    uint8_t text_len = payload[3];

    if (length < 4 + app_len + title_len + text_len) {
        return BT_GATT_ITER_CONTINUE;
    }

    char app_name[NOTIFICATION_MAX_APP_LEN] = { 0 };
    char title[NOTIFICATION_MAX_TITLE_LEN] = { 0 };
    char text[NOTIFICATION_MAX_TEXT_LEN] = { 0 };

    int offset = 4;
    strncpy(app_name, (const char*)&payload[offset],
        MIN(app_len, NOTIFICATION_MAX_APP_LEN - 1));
    offset += app_len;

    strncpy(title, (const char*)&payload[offset],
        MIN(title_len, NOTIFICATION_MAX_TITLE_LEN - 1));
    offset += title_len;

    strncpy(text, (const char*)&payload[offset],
        MIN(text_len, NOTIFICATION_MAX_TEXT_LEN - 1));

    LOG_DBG("Custom notification: app=%s, title=%s", app_name, title);

    add_notification((notification_type_t)type, app_name, title, text);

    return BT_GATT_ITER_CONTINUE;
}

/* Add notification to queue */
static void add_notification(notification_type_t type, const char* app_name,
    const char* title, const char* text)
{
    if (notification_count >= NOTIFICATION_QUEUE_SIZE) {
        /* Remove oldest notification */
        memmove(&notifications[0], &notifications[1],
            (NOTIFICATION_QUEUE_SIZE - 1) * sizeof(notification_t));
        notification_count--;
    }

    notification_t* notif = &notifications[notification_count];
    notif->id = next_notification_id++;
    notif->type = type;
    notif->timestamp = k_uptime_get();
    notif->active = true;

    strncpy(notif->app_name, app_name ? app_name : "Unknown",
        NOTIFICATION_MAX_APP_LEN - 1);
    strncpy(notif->title, title ? title : "Notification",
        NOTIFICATION_MAX_TITLE_LEN - 1);
    strncpy(notif->text, text ? text : "",
        NOTIFICATION_MAX_TEXT_LEN - 1);

    notification_count++;

    LOG_INF("Added notification: %s - %s", notif->title, notif->text);

    /* Show popup */
    notifications_show_popup(notif);

    /* Call callback if registered */
    if (notification_callback) {
        notification_callback(notif);
    }

    /* Update list display */
    notifications_update_list_display();
}

/* Get notification type icon */
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

/* Popup timer callback */
static void popup_timer_callback(lv_timer_t* timer)
{
    if (notification_popup) {
        lv_obj_del(notification_popup);
        notification_popup = NULL;
    }
    lv_timer_del(timer);
}

/* Notification item click callback */
static void notification_item_clicked(lv_event_t* e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        uint32_t id = (uint32_t)(uintptr_t)lv_obj_get_user_data(lv_event_get_target(e));
        notifications_clear_by_id(id);
    }
}

/* GATT discovery callback */
static uint8_t discover_ancs_callback(struct bt_conn* conn,
    const struct bt_gatt_attr* attr,
    struct bt_gatt_discover_params* params)
{
    if (!attr) {
        LOG_DBG("Discovery complete");
        return BT_GATT_ITER_STOP;
    }

    LOG_DBG("Found characteristic handle %u", attr->handle);

    /* For simplicity, assume the first characteristic found is the notification source */
    if (ancs_notif_params.value_handle == 0) {
        ancs_notif_params.value_handle = attr->handle;
        ancs_notif_params.ccc_handle = attr->handle + 1; /* Assume CCC is next handle */

        /* Subscribe to notifications */
        int err = bt_gatt_subscribe(conn, &ancs_notif_params);
        if (err) {
            LOG_ERR("Subscribe failed (err %d)", err);
        } else {
            LOG_INF("Subscribed to ANCS notifications");
        }
    }

    return BT_GATT_ITER_CONTINUE;
}

/* Custom notification service */
static ssize_t write_custom_notif(struct bt_conn* conn,
    const struct bt_gatt_attr* attr,
    const void* buf, uint16_t len, uint16_t offset,
    uint8_t flags)
{
    LOG_DBG("Custom notification write: len=%d", len);
    custom_notification_notify(conn, &custom_notif_params, buf, len);
    return len;
}

/* GATT service definition */
BT_GATT_SERVICE_DEFINE(custom_notif_svc,
    BT_GATT_PRIMARY_SERVICE(&custom_notif_svc_uuid),
    BT_GATT_CHARACTERISTIC(&custom_notif_char_uuid.uuid,
        BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_WRITE,
        NULL, write_custom_notif, NULL),
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

/* Initialize notification service */
int notifications_init(void)
{
    memset(notifications, 0, sizeof(notifications));
    notification_count = 0;
    next_notification_id = 1;

    /* Initialize LVGL styles */
    lv_style_init(&popup_style);
    lv_style_set_bg_color(&popup_style, lv_color_hex(0x333333));
    lv_style_set_bg_opa(&popup_style, LV_OPA_90);
    lv_style_set_border_width(&popup_style, 2);
    lv_style_set_border_color(&popup_style, lv_color_hex(0x0080FF));
    lv_style_set_radius(&popup_style, 10);
    lv_style_set_pad_all(&popup_style, 10);

    lv_style_init(&list_style);
    lv_style_set_bg_color(&list_style, lv_color_hex(0x1a1a1a));
    lv_style_set_text_color(&list_style, lv_color_white());

    LOG_INF("Notifications service initialized");
    return 0;
}

/* Start notification service */
int notifications_start(void)
{
    LOG_INF("Starting notification service");

    /* Note: In a real implementation, you would start GATT discovery here
     * when connected to a device. For now, we'll rely on the custom GATT service
     * for receiving notifications from Android devices.
     */

    return 0;
}

/* Start ANCS discovery */
int notifications_start_ancs_discovery(struct bt_conn* conn)
{
    if (!conn) {
        return -EINVAL;
    }

    discover_params.uuid = &ancs_notif_src_uuid.uuid;
    discover_params.func = discover_ancs_callback;
    discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

    int err = bt_gatt_discover(conn, &discover_params);
    if (err) {
        LOG_ERR("ANCS discovery failed (err %d)", err);
        return err;
    }

    LOG_INF("Started ANCS discovery");
    return 0;
}

/* Stop notification service */
int notifications_stop(void)
{
    LOG_INF("Stopping notification service");
    return 0;
}

/* Set notification callback */
void notifications_set_callback(notification_callback_t callback)
{
    notification_callback = callback;
}

/* Get active notifications count */
int notifications_get_count(void)
{
    return notification_count;
}

/* Get notification by index */
const notification_t* notifications_get_by_index(int index)
{
    if (index < 0 || index >= notification_count) {
        return NULL;
    }
    return &notifications[index];
}

/* Clear all notifications */
void notifications_clear_all(void)
{
    notification_count = 0;
    memset(notifications, 0, sizeof(notifications));
    notifications_update_list_display();
    LOG_INF("All notifications cleared");
}

/* Clear specific notification */
void notifications_clear_by_id(uint32_t id)
{
    for (int i = 0; i < notification_count; i++) {
        if (notifications[i].id == id) {
            memmove(&notifications[i], &notifications[i + 1],
                (notification_count - i - 1) * sizeof(notification_t));
            notification_count--;
            notifications_update_list_display();
            LOG_INF("Notification %d cleared", id);
            break;
        }
    }
}

/* Show notification popup */
void notifications_show_popup(const notification_t* notif)
{
    if (!notif)
        return;

    lv_obj_t* screen = lv_scr_act();

    /* Remove existing popup */
    if (notification_popup) {
        lv_obj_del(notification_popup);
    }

    /* Create popup container */
    notification_popup = lv_obj_create(screen);
    lv_obj_add_style(notification_popup, &popup_style, 0);
    lv_obj_set_size(notification_popup, 220, 100);
    lv_obj_align(notification_popup, LV_ALIGN_CENTER, 0, 10);

    /* Icon */
    lv_obj_t* icon = lv_label_create(notification_popup);
    lv_label_set_text(icon, get_notification_icon(notif->type));
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_16, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);

    /* App name */
    lv_obj_t* app_label = lv_label_create(notification_popup);
    lv_label_set_text(app_label, notif->app_name);
    lv_obj_set_style_text_font(app_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(app_label, lv_color_hex(0x888888), 0);
    lv_obj_align_to(app_label, icon, LV_ALIGN_OUT_RIGHT_TOP, 5, 0);

    /* Title */
    lv_obj_t* title_label = lv_label_create(notification_popup);
    lv_label_set_text(title_label, notif->title);
    lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_width(title_label, 170);
    lv_obj_align_to(title_label, app_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);

    /* Text preview */
    if (strlen(notif->text) > 0) {
        lv_obj_t* text_label = lv_label_create(notification_popup);
        lv_label_set_text(text_label, notif->text);
        lv_obj_set_style_text_font(text_label, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(text_label, lv_color_hex(0xCCCCCC), 0);
        lv_label_set_long_mode(text_label, LV_LABEL_LONG_DOT);
        lv_obj_set_width(text_label, 170);
        lv_obj_align_to(text_label, title_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 2);
    }

    /* Auto-hide popup after 3 seconds */
    lv_timer_create(popup_timer_callback, 3000, NULL);
}

/* Create notification list screen */
lv_obj_t* notifications_create_list_screen(lv_obj_t* parent)
{
    notification_list = lv_list_create(parent);
    lv_obj_add_style(notification_list, &list_style, 0);
    lv_obj_set_size(notification_list, LV_HOR_RES, LV_VER_RES);

    notifications_update_list_display();
    return notification_list;
}

/* Update notification list display */
void notifications_update_list_display(void)
{
    if (!notification_list)
        return;

    /* Clear existing items */
    lv_obj_clean(notification_list);

    if (notification_count == 0) {
        lv_obj_t* btn = lv_list_add_btn(notification_list, LV_SYMBOL_BELL, "No notifications");
        lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        return;
    }

    /* Add notification items */
    for (int i = notification_count - 1; i >= 0; i--) {
        const notification_t* notif = &notifications[i];

        char btn_text[128];
        snprintf(btn_text, sizeof(btn_text), "%s\n%s",
            notif->title, notif->app_name);

        lv_obj_t* btn = lv_list_add_btn(notification_list,
            get_notification_icon(notif->type),
            btn_text);

        /* Store notification ID in user data */
        lv_obj_set_user_data(btn, (void*)(uintptr_t)notif->id);

        /* Add click event */
        lv_obj_add_event_cb(btn, notification_item_clicked, LV_EVENT_CLICKED, NULL);
    }
}

/* Add a test notification (for development/testing) */
int notifications_add_test_notification(notification_type_t type, const char* app_name,
    const char* title, const char* text)
{
    if (!app_name || !title) {
        LOG_ERR("Invalid parameters for test notification");
        return -EINVAL;
    }

    add_notification(type, app_name, title, text ? text : "");
    LOG_DBG("Test notification added: %s - %s", title, app_name);
    return 0;
}
