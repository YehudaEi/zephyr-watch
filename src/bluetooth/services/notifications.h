/**
 * @file notifications.h
 * @brief Smartwatch notifications handler (Updated with test functions)
 */

#ifndef NOTIFICATIONS_H
#define NOTIFICATIONS_H

#include <lvgl.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum notification text length */
#define NOTIFICATION_MAX_TITLE_LEN 64
#define NOTIFICATION_MAX_TEXT_LEN 256
#define NOTIFICATION_MAX_APP_LEN 32
#define NOTIFICATION_QUEUE_SIZE 10

/* Notification types */
typedef enum {
    NOTIF_TYPE_CALL = 0,
    NOTIF_TYPE_SMS,
    NOTIF_TYPE_EMAIL,
    NOTIF_TYPE_SOCIAL,
    NOTIF_TYPE_CALENDAR,
    NOTIF_TYPE_OTHER,
    NOTIF_TYPE_MAX
} notification_type_t;

/* Notification structure */
typedef struct {
    uint32_t id;
    notification_type_t type;
    char app_name[NOTIFICATION_MAX_APP_LEN];
    char title[NOTIFICATION_MAX_TITLE_LEN];
    char text[NOTIFICATION_MAX_TEXT_LEN];
    uint64_t timestamp;
    bool active;
} notification_t;

/* Notification callback function type */
typedef void (*notification_callback_t)(const notification_t* notif);

/**
 * @brief Initialize notification service
 * @return 0 on success, negative errno on failure
 */
int notifications_init(void);

/**
 * @brief Start notification service
 * @return 0 on success, negative errno on failure
 */
int notifications_start(void);

/**
 * @brief Stop notification service
 * @return 0 on success, negative errno on failure
 */
int notifications_stop(void);

/**
 * @brief Register notification callback
 * @param callback Function to call when notification received
 */
void notifications_set_callback(notification_callback_t callback);

/**
 * @brief Get active notifications count
 * @return Number of active notifications
 */
int notifications_get_count(void);

/**
 * @brief Get notification by index
 * @param index Notification index (0-based)
 * @return Pointer to notification or NULL if not found
 */
const notification_t* notifications_get_by_index(int index);

/**
 * @brief Clear all notifications
 */
void notifications_clear_all(void);

/**
 * @brief Clear specific notification
 * @param id Notification ID to clear
 */
void notifications_clear_by_id(uint32_t id);

/**
 * @brief Show notification popup on LVGL screen
 * @param notif Notification to display
 */
void notifications_show_popup(const notification_t* notif);

/**
 * @brief Create notification list screen
 * @param parent Parent LVGL object
 * @return Created list object
 */
lv_obj_t* notifications_create_list_screen(lv_obj_t* parent);

/**
 * @brief Update notification list display
 */
void notifications_update_list_display(void);

/**
 * @brief Add a test notification (for development/testing)
 * @param type Notification type
 * @param app_name Application name
 * @param title Notification title
 * @param text Notification text
 * @return 0 on success, negative on failure
 */
int notifications_add_test_notification(notification_type_t type, const char* app_name,
    const char* title, const char* text);

/**
 * @brief Add some sample test notifications (for development/testing)
 */
void notifications_add_sample_notifications(void);

/**
 * @brief Start ANCS discovery for iOS devices
 * @param conn Bluetooth connection
 * @return 0 on success, negative errno on failure
 */
int notifications_start_ancs_discovery(struct bt_conn* conn);

#ifdef __cplusplus
}
#endif

#endif /* NOTIFICATIONS_H */
