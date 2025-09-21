/** Enhanced Current Time Service with Android compatibility
 * This service provides both standard CTS and a simplified time sync for Android devices.
 *
 * @license: GNU v3
 * @maintainer: electricalgorithm @ github
 */

#include <string.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "current_time_service.h"
#include "datetime/datetime.h"
#include "devicetwin/devicetwin.h"

LOG_MODULE_REGISTER(ZephyrWatch_BLE_CTS_Enhanced, LOG_LEVEL_INF);

/* Custom Time Sync Service UUID (for Android compatibility) */
#define CUSTOM_TIME_SYNC_SERVICE_UUID \
    BT_UUID_128_ENCODE(0x12345678, 0x1234, 0x5678, 0x1234, 0x56789abcdef0)

#define CUSTOM_TIME_SYNC_CHAR_UUID \
    BT_UUID_128_ENCODE(0x87654321, 0x4321, 0x8765, 0x4321, 0x0fedcba987654)

static const struct bt_uuid_128 custom_time_svc_uuid = BT_UUID_INIT_128(CUSTOM_TIME_SYNC_SERVICE_UUID);
static const struct bt_uuid_128 custom_time_char_uuid = BT_UUID_INIT_128(CUSTOM_TIME_SYNC_CHAR_UUID);

/* Current Time Service Write Callback (existing - with relaxed permissions) */
static ssize_t m_time_write_callback(
    struct bt_conn* conn,
    const struct bt_gatt_attr* attr,
    const void* buf,
    uint16_t len,
    uint16_t offset,
    uint8_t flags)
{

    // Check if we received exactly 4 bytes for UNIX timestamp
    if (len != 4 || offset != 0) {
        LOG_ERR("Invalid write length or offset. Expected 4 bytes at offset 0, got %d bytes at offset %d", len, offset);
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    // Extract the UNIX timestamp from the buffer (assuming little-endian)
    uint32_t unix_timestamp = sys_le32_to_cpu(*(uint32_t*)buf);
    LOG_DBG("Received UNIX timestamp: %u", unix_timestamp);

    // Get the device twin instance to get the UTC zone
    device_twin_t* device_twin = get_device_twin_instance();
    if (device_twin == NULL) {
        LOG_ERR("Failed to get device twin instance.");
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }
    device_twin->unix_time = unix_timestamp;
    trigger_ui_update();

    // Convert UNIX timestamp to local time using the device's UTC zone to print.
    datetime_t local_time = unix_to_localtime(unix_timestamp, device_twin->utc_zone);
    LOG_INF("Current time updated to local time: %04d-%02d-%02d %02d:%02d:%02d (UTC%+d)",
        local_time.year, local_time.month, local_time.day,
        local_time.hour, local_time.minute, local_time.second,
        device_twin->utc_zone);

    return len;
}

/* Custom Time Sync Write Callback (simplified for Android) */
static ssize_t custom_time_write_callback(
    struct bt_conn* conn,
    const struct bt_gatt_attr* attr,
    const void* buf,
    uint16_t len,
    uint16_t offset,
    uint8_t flags)
{

    LOG_DBG("Custom time sync write received, length: %d", len);

    // Support multiple formats for Android compatibility
    if (len == 4 && offset == 0) {
        // 4-byte UNIX timestamp (same as CTS)
        uint32_t unix_timestamp = sys_le32_to_cpu(*(uint32_t*)buf);
        LOG_INF("Custom time sync: UNIX timestamp %u", unix_timestamp);

        device_twin_t* device_twin = get_device_twin_instance();
        if (device_twin != NULL) {
            device_twin->unix_time = unix_timestamp;
            trigger_ui_update();

            datetime_t local_time = unix_to_localtime(unix_timestamp, device_twin->utc_zone);
            LOG_INF("Time updated via custom service: %04d-%02d-%02d %02d:%02d:%02d",
                local_time.year, local_time.month, local_time.day,
                local_time.hour, local_time.minute, local_time.second);
        }
        return len;
    } else if (len == 8 && offset == 0) {
        // 8-byte timestamp (supports milliseconds or 64-bit timestamps)
        uint64_t timestamp_ms = sys_le64_to_cpu(*(uint64_t*)buf);
        uint32_t unix_timestamp = (uint32_t)(timestamp_ms / 1000); // Convert ms to seconds
        LOG_INF("Custom time sync: 64-bit timestamp %llu ms -> %u s", timestamp_ms, unix_timestamp);

        device_twin_t* device_twin = get_device_twin_instance();
        if (device_twin != NULL) {
            device_twin->unix_time = unix_timestamp;
            trigger_ui_update();
        }
        return len;
    } else if (len >= 10 && offset == 0) {
        // Text format: "YYYY-MM-DD HH:MM:SS" or similar
        char time_str[32] = { 0 };
        size_t copy_len = MIN(len, sizeof(time_str) - 1);
        memcpy(time_str, buf, copy_len);
        LOG_INF("Custom time sync: text format '%s'", time_str);

        // For now, just log it - you could implement text parsing here
        LOG_WRN("Text time format not yet implemented");
        return len;
    }

    LOG_ERR("Invalid custom time sync format: len=%d, offset=%d", len, offset);
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
}

/* Time Read Callback (to allow reading current time) */
static ssize_t time_read_callback(
    struct bt_conn* conn,
    const struct bt_gatt_attr* attr,
    void* buf,
    uint16_t len,
    uint16_t offset)
{

    device_twin_t* device_twin = get_device_twin_instance();
    if (device_twin == NULL) {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    uint32_t current_time = device_twin->unix_time;
    LOG_DBG("Time read request, returning: %u", current_time);

    return bt_gatt_attr_read(conn, attr, buf, len, offset, &current_time, sizeof(current_time));
}

/* Dummy data for GATT characteristics */
static uint8_t dummy_data[8] = { 0 };

/* Enhanced Current Time Service with both standard CTS and custom service */
BT_GATT_SERVICE_DEFINE(enhanced_cts,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_CTS),

    /* Standard CTS characteristic with relaxed permissions */
    BT_GATT_CHARACTERISTIC(
        BT_UUID_CTS_CURRENT_TIME,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, // Removed encryption requirement
        time_read_callback, m_time_write_callback, dummy_data),
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

/* Custom Time Sync Service for Android compatibility */
BT_GATT_SERVICE_DEFINE(custom_time_sync_svc,
    BT_GATT_PRIMARY_SERVICE(&custom_time_svc_uuid),

    /* Custom time sync characteristic with minimal permissions */
    BT_GATT_CHARACTERISTIC(
        &custom_time_char_uuid.uuid,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ | BT_GATT_PERM_WRITE, // No encryption required
        time_read_callback, custom_time_write_callback, dummy_data),
    BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );
