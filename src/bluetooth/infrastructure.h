/** Bluetooth Subsystem for ZephyrWatch
 * This interface manages Bluetooth functionality, including advertising and connection handling.
 * Updated with initialization function for mutex setup.
 *
 * @license: GNU v3
 * @maintainer: electricalgorithm @ github
 */

#ifndef BLUETOOTH_INFRASTRUCTURE_H_
#define BLUETOOTH_INFRASTRUCTURE_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

int bluetooth_infrastructure_init(void);

int enable_bluetooth_subsystem();
int disable_bluetooth_subsystem();

/* Function to check if BLE services are active (for UI feedback) */
bool is_bluetooth_services_active(void);

#ifdef __cplusplus
}
#endif

#endif /* BLUETOOTH_INFRASTRUCTURE_H_ */
