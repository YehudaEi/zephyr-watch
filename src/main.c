/** Main application entry point for Zephyr-based smartwatch firmware.
 * Initializes display, Bluetooth, timers, and manages the core watch functionality including time tracking and UI updates.
 * Now includes notification testing functionality and proper BLE initialization.
 *
 * @license GNU v3
 * @maintainer electricalgorithm @ github
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "bluetooth/infrastructure.h"
#include "bluetooth/services/notifications.h"
#include "datetime/datetime.h"
#include "devicetwin/devicetwin.h"
#include "display/display.h"
#include "userinterface/userinterface.h"
#include "watchdog/watchdog.h"

// Define the logger.
LOG_MODULE_REGISTER(ZephyrWatch, LOG_LEVEL_INF);

#define SLEEP_UI_STABILIZE_MS 2000
#define SLEEP_MAIN_CORE_MS 20

// Setting for device's time zone.
int8_t utc_zone = +2;

/* ADD FUNCTION DECLARATION HERE */
int bluetooth_infrastructure_init(void);

static void notification_received_callback(const notification_t* notif)
{
    LOG_INF("New notification: %s from %s", notif->title, notif->app_name);
}

int main(void)
{
    int ret;

    // Set-up watchdog before all the subsystems.
    ret = enable_watchdog_subsystem();
    if (ret) {
        LOG_ERR("Watchdog subsystem couldn't be enabled. (RET: %d)", ret);
        return ret;
    }
    LOG_INF("Watchdog system is enabled.");

    /* ADD BLE INFRASTRUCTURE INIT HERE - Initialize BLE mutex early */
    ret = bluetooth_infrastructure_init();
    if (ret) {
        LOG_ERR("Bluetooth infrastructure couldn't be initialized. (RET: %d)", ret);
        return ret;
    }
    LOG_INF("Bluetooth infrastructure initialized.");

    // Create the device twin.
    device_twin_t* device_twin = create_device_twin_instance(0, utc_zone);
    if (!device_twin) {
        LOG_ERR("Cannot create device twin instance.");
        return 0;
    }
    LOG_INF("Device twin instance created successfully.");

    // Init the display subsystem.
    ret = enable_display_subsystem();
    if (ret) {
        LOG_ERR("Display subsystem couldn't enabled. (RET: %d)", ret);
        return ret;
    }
    LOG_INF("Display subsystem is enabled.");

    // Initialize the display device with initial user interface.
    user_interface_init();
    LOG_INF("User interface subsystem is enabled.");

    // Refresh the UI.
    user_interface_task_handler();
    LOG_INF("User interface is refreshed initially.");

    // Enable datetime subsystem.
    ret = enable_datetime_subsystem();
    if (ret) {
        LOG_ERR("Datetime subsystem couldn't enabled. (RET: %d)", ret);
        return ret;
    }
    LOG_INF("Datetime subsystem is enabled.");

    // Initialize the Bluetooth stack.
    // Give the system more time to stabilize before initializing Bluetooth.
    k_sleep(K_MSEC(SLEEP_UI_STABILIZE_MS));
    ret = enable_bluetooth_subsystem();
    if (ret) {
        LOG_ERR("Bluetooth subsystem couldn't enabled. (RET: %d)", ret);
        return ret;
    }
    LOG_INF("Bluetooth subsystem is enabled.");

    // Init notification service
    ret = notifications_init();
    if (ret) {
        LOG_ERR("Notifications service couldn't be initialized. (RET: %d)", ret);
        return ret;
    }

    ret = notifications_start();
    if (ret) {
        LOG_ERR("Notifications service couldn't be started. (RET: %d)", ret);
        return ret;
    }

    notifications_set_callback(notification_received_callback);
    LOG_INF("Notification service is enabled.");

    while (1) {
        user_interface_task_handler();
        k_sleep(K_MSEC(SLEEP_MAIN_CORE_MS));

        // Kick the watchdog.
        kick_watchdog();
    }
}
