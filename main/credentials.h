/**
 * @file credentials.h
 * @brief Defines all authentication and connection credentials used by the firmware.
 *
 * This header provides the static credentials required for both:
 *  - MQTT secure client connection (broker URI, username, and password).
 *  - Local Wi-Fi Access Point (AP) mode used for initial configuration or fallback.
 *
 * These constants are typically hardcoded during firmware development or testing,
 * but in a production environment they should be stored securely in NVS flash or
 * fetched dynamically from an encrypted configuration source.
 *
 * ------------------------------------------------------------
 *  Key Responsibilities:
 * ------------------------------------------------------------
 *  - Define MQTT broker connection parameters for `mqtt_manager.c`.
 *  - Define default SSID and password for setup Access Point (AP) mode.
 *  - Provide easy configuration for testing and local deployments.
 *
 * ------------------------------------------------------------
 *  Usage Example:
 * ------------------------------------------------------------
 *  ```c
 *  // Connect to MQTT broker using predefined credentials
 *  esp_mqtt_client_config_t mqtt_cfg = {
 *      .broker.address.uri = MQTT_BROKER_URI,
 *      .credentials.username = MQTT_USERNAME,
 *      .credentials.authentication.password = MQTT_PASSWORD,
 *  };
 *
 *  // Start AP mode with predefined SSID and password
 *  wfm_start_ap(&wfm, WIFI_AP_SSID, WIFI_AP_PASSWORD);
 *  ```
 *
 * ------------------------------------------------------------
 *  Security Note:
 * ------------------------------------------------------------
 *  - Avoid committing this file to public repositories if it contains
 *    real usernames or passwords.
 *  - For production firmware, consider storing credentials in secure NVS,
 *    or loading them dynamically over encrypted channels (e.g., HTTPS or TLS).
 */

#ifndef CREDENTIALS_H
#define CREDENTIALS_H


/* ------------------------------------------------------------
 *                   MQTT Broker Credentials
 * ------------------------------------------------------------ */

/**
 * @brief Secure MQTT broker endpoint (TLS/SSL).
 * Example: "mqtts://broker.example.com:8883"
 */
#define MQTT_BROKER_URI  "mqtts://2a7e41fb3049421ba6af414adaf4f849.s1.eu.hivemq.cloud:8883"

/**
 * @brief MQTT broker username for authentication.
 */
#define MQTT_USERNAME    "JekaDeGever"

/**
 * @brief MQTT broker password for authentication.
 */
#define MQTT_PASSWORD    "J1qwe321"


/* ------------------------------------------------------------
 *                   Wi-Fi Access Point (AP) Setup Mode
 * ------------------------------------------------------------ */

/**
 * @brief Default SSID used when the device starts in AP setup mode.
 */
#define WIFI_AP_SSID      "esp32_setup"

/**
 * @brief Default password for the setup Access Point.
 */
#define WIFI_AP_PASSWORD  "setup1234"


#endif // CREDENTIALS_H