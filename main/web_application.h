/**
 * @file web_application.h
 * @brief Central application control layer for MQTT-based IoT device logic.
 *
 * ## Overview
 * This module connects the Wi-Fi and MQTT subsystems with the user interface.
 * It implements all core application logic such as:
 *  - OTA updates via HTTPS
 *  - LED control through MQTT commands
 *  - Wi-Fi network switching
 *  - Device information and diagnostic reporting
 *  - LCD text display and feedback
 *
 * ## Responsibilities
 *  - Handle incoming MQTT topic commands.
 *  - Manage communication between managers (`wfm_t`, `mqm_t`).
 *  - Publish system updates and device status messages.
 *
 * @note
 *  Must be initialized via `init_web_app()` before use.
 *  Requires an active Wi-Fi and MQTT connection.
 *
 * @dependencies
 *  - `mqtt_manager.h`
 *  - `WiFi_manager.h`
 *  - `lcd_driver.h`
 *  - `nvs_memory.h`
 *
 * @date
 *  Created on: 19/09/2025
 * @author
 *  Ivgeny Tokarzhevsky
 */

#ifndef WEB_APPLICATION_H
#define WEB_APPLICATION_H

#include <lcd_driver.h>
#include <mqtt_manager.h>
#include <nvs.h>
#include <WiFi_manager.h>

#include "esp_err.h"
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif


/* -------------------------------------------------------------------------- */
/*                              MQTT Macros                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Safe publish macro that logs success/failure of MQTT transmission.
 *
 * Wraps `mqm_publish_ex()` and prints the topic, message, and error string.
 *
 * Example:
 * ```c
 * MQTT_PUBLISH_CHECK(mqm, "topic/out", "hello", 1, 0);
 * ```
 */
#define MQTT_PUBLISH_CHECK(mqm, topic, msg, qos, retain)                           \
    do {                                                                           \
        esp_err_t __err = mqm_publish_ex((mqm), (topic), (msg), (qos), (retain));  \
        if (__err != ESP_OK) {                                                     \
            ESP_LOGE("MQTT", "Publish failed! topic='%s' msg='%s' err=%s",         \
                     (topic), (msg), esp_err_to_name(__err));                      \
        } else {                                                                   \
            ESP_LOGI("MQTT", "Publish OK: topic='%s' msg='%s'", (topic), (msg));   \
        }                                                                          \
    } while (0)



/* -------------------------------------------------------------------------- */
/*                              MQTT Topics                                   */
/* -------------------------------------------------------------------------- */

#define TOPIC_OUT_NEW_WIFI_CONNECT_STATUS  "wifi_connection_status"
#define TOPIC_IN_CONNECT_NEW_WIFI          "connect_new_wifi"

#define TOPIC_IN_SCAN_WIFI_NETS            "scan_wifi_nets"
#define TOPIC_OUT_SCAN_WIFI_RESULT         "scan_wifi_result"

#define TOPIC_IN_LCD_DISPLAY               "LCD_display"

#define TOPIC_IN_OTA_UPDATE                "OTA_update"
#define TOPIC_OUT_OTA_UPDATE               "OTA_update_progress"

#define TOPIC_IN_DEVICE_CONNECTION         "Get_device_connection_status"
#define TOPIC_OUT_DEVICE_CONNECTION        "device_connection_status"

#define TOPIC_OUT_WIFI_CRED_LIST           "wifi_cred_list"

#define TOPIC_IN_LEDS_TOGGLE               "leds_toggle"



/* -------------------------------------------------------------------------- */
/*                              Public API                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Display text on the LCD (via MQTT or local trigger).
 * @param text The text to display (UTF-8 string; may be NULL → empty).
 */
void LCD_display_text(const char* text);

/**
 * @brief Handle LED toggle commands received via MQTT.
 * @param command String command (e.g. "red led on", "green led off").
 */
void leds_toggle_handler(const char* command);

/**
 * @brief Perform synchronous Wi-Fi scan and publish results as JSON.
 *
 * Publishes results to `TOPIC_OUT_SCAN_WIFI_RESULT`.
 *
 * @param payload (unused) Reserved for future use; may be NULL.
 */
void scan_wifi_networks(const char* payload);

/**
 * @brief Handle incoming "change Wi-Fi network" command.
 *
 * Spawns a background task that switches networks gracefully, updates NVS,
 * and reports results via MQTT (`TOPIC_OUT_NEW_WIFI_CONNECT_STATUS`).
 *
 * @param payload Formatted as "ssid|password".
 */
void change_wifi_network_handler(const char* payload);

/**
 * @brief Handle "device connection test" MQTT command.
 *
 * Publishes device name, firmware version, Wi-Fi info, and stored credentials.
 *
 * @param payload (unused) Reserved for future use; may be NULL.
 */
void device_connection_test(const char* payload);

/**
 * @brief Start OTA firmware update via HTTPS.
 *
 * Publishes progress and result to `TOPIC_OUT_OTA_UPDATE`.
 *
 * @param download_path Full OTA URL (e.g., "https://server/fw.bin").
 */
void OTA_update(const char* download_path);

/**
 * @brief Initialize the web application layer.
 *
 * Connects the web application to the active Wi-Fi and MQTT contexts.
 *
 * @param wifi_manager  Pointer to initialized Wi-Fi manager context.
 * @param mqtt_client   Pointer to initialized MQTT manager context.
 * @param LCD_context   LCD context for UI feedback.
 * @param nvs_memory    Handle to NVS storage for Wi-Fi credentials.
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if any parameter is NULL.
 */
esp_err_t init_web_app(wfm_t* wifi_manager,
                       mqm_t* mqtt_client,
                       lcd_context_t LCD_context,
                       nvs_handle_t nvs_memory);



#ifdef __cplusplus
}
#endif

#endif /* WEB_APPLICATION_H */