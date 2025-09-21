/** Bluetooth infrastructure for Zephyr-based devices.
 * Conservative ESP32-S3 version that avoids bt_disable() to prevent crashes
 *
 * @license: GNU v3
 * @maintainer: electricalgorithm @ github
 */

#include "userinterface/screens/blepairing/blepairing.h"
#include "watchdog/watchdog.h"
#include "zephyr/bluetooth/conn.h"
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

LOG_MODULE_REGISTER(ZephyrWatch_BLE, LOG_LEVEL_INF);

static bool advertising = false;
static bool bt_enabled = false;
static bool ble_services_active = false;
static struct k_work_delayable adv_work;
static struct k_mutex ble_mutex;

static const struct bt_data m_ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_CTS_VAL)), // Current Time Service
    BT_DATA_BYTES(BT_DATA_UUID128_ALL,
        0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12, // Custom Time Sync Service UUID
        0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12),
};

static const struct bt_data m_sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
    BT_DATA_BYTES(BT_DATA_MANUFACTURER_DATA,
        0x00, 0x00, // Company ID (use 0x0000 for testing)
        'T', 'S', // "TS" = Time Sync
        0x01), // Version 1
};

static void start_advertising_work(struct k_work* work)
{
    int err;

    if (advertising || !bt_enabled || !ble_services_active) {
        return;
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, m_ad, ARRAY_SIZE(m_ad), m_sd, ARRAY_SIZE(m_sd));
    if (err) {
        LOG_ERR("Advertising failed to start (err %d)", err);
        if (bt_enabled && ble_services_active) {
            k_work_schedule(&adv_work, K_SECONDS(5));
        }
        return;
    }

    advertising = true;
    LOG_INF("Advertising started successfully");
}

static void start_advertisement()
{
    if (bt_enabled && ble_services_active) {
        k_work_schedule(&adv_work, K_MSEC(100));
    }
}

static void stop_advertising()
{
    int err;

    k_work_cancel_delayable(&adv_work);

    if (!advertising) {
        return;
    }

    err = bt_le_adv_stop();
    if (err) {
        LOG_ERR("Advertising failed to stop (err %d)", err);
        return;
    }

    advertising = false;
    LOG_INF("Advertising stopped");
}

static void disconnect_all_connections()
{
    /* For ESP32, we'll let connections naturally timeout or disconnect */
    /* instead of forcing disconnection which can cause issues */
    k_sleep(K_MSEC(100));
    LOG_DBG("Connection cleanup completed");
}

static void process_connection(struct bt_conn* conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    if (!ble_services_active) {
        /* If services are disabled, reject the connection */
        if (!err) {
            bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
        }
        return;
    }

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err) {
        LOG_ERR("Connection failed to %s (err %u)", addr, err);
        if (ble_services_active) {
            start_advertisement();
        }
        return;
    }

    LOG_INF("Connected to %s", addr);
    stop_advertising();
}

static void process_disconnection(struct bt_conn* conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Disconnected from %s (reason 0x%02x)", addr, reason);

    if (bt_enabled && ble_services_active) {
        start_advertisement();
    }
}

static bool le_param_req(struct bt_conn* conn, struct bt_le_conn_param* param)
{
    if (!ble_services_active) {
        return false;
    }

    LOG_INF("Connection parameter request: interval %u-%u, latency %u, timeout %u",
        param->interval_min, param->interval_max, param->latency, param->timeout);

    return true;
}

static void le_param_updated(struct bt_conn* conn, uint16_t interval,
    uint16_t latency, uint16_t timeout)
{
    LOG_INF("Connection parameters updated: interval %u, latency %u, timeout %u",
        interval, latency, timeout);
}

BT_CONN_CB_DEFINE(connection_callbacks) = {
    .connected = process_connection,
    .disconnected = process_disconnection,
    .recycled = start_advertisement,
    .le_param_req = le_param_req,
    .le_param_updated = le_param_updated,
};

char* passkey_to_string(const unsigned int passkey)
{
    static char passkey_str[7];
    snprintf(passkey_str, sizeof(passkey_str), "%06u", passkey);
    return passkey_str;
}

static void process_passkey_display(struct bt_conn* conn, unsigned int passkey)
{
    char addr[BT_ADDR_LE_STR_LEN] = { 0 };

    if (!ble_services_active) {
        return;
    }

    blepairing_screen_init();
    blepairing_screen_set_pin(passkey_to_string(passkey));
    blepairing_screen_load();
    LOG_DBG("Displaying passkey on the screen.");

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_DBG("Passkey for %s: %06u", addr, passkey);
}

static void process_auth_cancel(struct bt_conn* conn)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_DBG("Pairing cancelled: %s", addr);
    blepairing_screen_unload();
}

static void process_pairing_complete(struct bt_conn* conn, bool bonded)
{
    LOG_DBG("Pairing complete. Bonded: %s", bonded ? "OK" : "FAILURE");
    blepairing_screen_unload();
}

static void process_pairing_failed(struct bt_conn* conn, enum bt_security_err reason)
{
    LOG_DBG("Pairing failed. Reason: 0x%02x", reason);
    if (ble_services_active) {
        bt_conn_disconnect(conn, BT_HCI_ERR_AUTH_FAIL);
    }
    blepairing_screen_unload();
}

static struct bt_conn_auth_info_cb auth_info_callbacks = {
    .pairing_complete = process_pairing_complete,
    .pairing_failed = process_pairing_failed,
};

static struct bt_conn_auth_cb auth_callbacks = {
    .passkey_display = process_passkey_display,
    .passkey_entry = NULL,
    .cancel = process_auth_cancel,
};

static void bluetooth_infrastructure_init_mutex(void)
{
    k_mutex_init(&ble_mutex);
    LOG_DBG("BLE mutex initialized");
}

int enable_bluetooth_subsystem()
{
    int err;

    k_mutex_lock(&ble_mutex, K_FOREVER);

    if (bt_enabled && ble_services_active) {
        LOG_WRN("Bluetooth already enabled");
        k_mutex_unlock(&ble_mutex);
        return 0;
    }

    kick_watchdog();

    /* Only initialize BT stack once */
    if (!bt_enabled) {
        k_work_init_delayable(&adv_work, start_advertising_work);

        err = bt_enable(NULL);
        if (err) {
            LOG_ERR("Bluetooth init failed (err %d).", err);
            k_mutex_unlock(&ble_mutex);
            return err;
        }
        LOG_DBG("Bluetooth initialized.");

        kick_watchdog();
        bt_enabled = true;

        err = bt_unpair(BT_ID_DEFAULT, BT_ADDR_LE_ANY);
        if (err) {
            LOG_ERR("Unpairing failed (err %d).", err);
        }
        LOG_DBG("Unpairing successful.");

        if (IS_ENABLED(CONFIG_SETTINGS)) {
            settings_load();
        }

        kick_watchdog();

        err = bt_conn_auth_cb_register(&auth_callbacks);
        if (err) {
            LOG_ERR("Failed to register authentication callbacks (err %d).", err);
            k_mutex_unlock(&ble_mutex);
            return err;
        }
        LOG_DBG("Authentication callback registered successfully.");

        err = bt_conn_auth_info_cb_register(&auth_info_callbacks);
        if (err) {
            LOG_ERR("Failed to register authentication information callbacks (err %d).", err);
            bt_conn_auth_cb_register(NULL);
            k_mutex_unlock(&ble_mutex);
            return err;
        }
        LOG_DBG("Authentication information callback registered successfully.");
    }

    /* Enable BLE services */
    ble_services_active = true;
    start_advertisement();

    k_mutex_unlock(&ble_mutex);
    LOG_INF("Bluetooth services enabled");
    return 0;
}

int disable_bluetooth_subsystem()
{
    k_mutex_lock(&ble_mutex, K_FOREVER);

    if (!ble_services_active) {
        LOG_WRN("Bluetooth services already disabled");
        k_mutex_unlock(&ble_mutex);
        return 0;
    }

    LOG_INF("Disabling Bluetooth services...");
    kick_watchdog();

    /* Disable services but keep BT stack running */
    ble_services_active = false;

    /* Stop advertising */
    stop_advertising();
    kick_watchdog();

    /* Disconnect connections gracefully */
    disconnect_all_connections();
    kick_watchdog();

    /*
     * NOTE: We do NOT call bt_disable() here as it causes crashes on ESP32-S3
     * Instead, we just stop all BLE services while keeping the stack alive
     * This prevents the interrupt controller assertion error
     */

    k_mutex_unlock(&ble_mutex);
    LOG_INF("Bluetooth services disabled (stack remains active)");
    return 0;
}

/* Function to check if BLE services are active */
bool is_bluetooth_services_active(void)
{
    return ble_services_active;
}

int bluetooth_infrastructure_init(void)
{
    bluetooth_infrastructure_init_mutex();
    return 0;
}
