/**
 * @file mqtt_callbacks.c
 * @brief MQTT event callback handlers for the IoT Dashboard device.
 *
 * ## Overview
 * This module implements all callback functions triggered by the MQTT Manager (`mqtt_manager.h`).
 * It provides real-time visual feedback on the LCD and LEDs based on MQTT connection
 * and message events, and automatically publishes device status upon successful connection.
 *
 * ## Responsibilities
 *  - Handle MQTT connection and disconnection states.
 *  - Display connection status on the LCD.
 *  - Reflect MQTT activity using LED indicators.
 *  - Publish “device connected” messages after successful connection.
 *
 * ## Dependencies
 *  - mqtt_manager.h  
 *  - lcd_driver.h  
 *  - leds_driver.h  
 *  - web_application.h  
 *  - esp_log.h  
 *
 * @note
 *  This module must be initialized with `init_mqtt_callbacks_handler()` before
 *  any callback is invoked, otherwise log errors will occur.
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 * @date
 *  Created on: 19/09/2025
 */

#include "MQTT_callbacks.h"
#include <esp_log.h>
#include <lcd_driver.h>
#include "hardware_layer.h"
#include "mqtt_manager.h"
#include "web_application.h"
#include "leds_driver.h"



/* -------------------------------------------------------------------------- */
/*                            STATIC MODULE VARIABLES                         */
/* -------------------------------------------------------------------------- */

/** @brief Log tag for MQTT callbacks. */
static const char* TAG = "MQTT_CALLBACKS";

/** @brief Local LCD context used for status display. */
static lcd_context_t LCD;

/** @brief Indicates whether the callback module was properly initialized. */
static bool initialized = false;

static  mqm_status_t prev_status = MQM_NONE;


/* -------------------------------------------------------------------------- */
/*                            MQTT CALLBACK FUNCTIONS                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Called whenever MQTT connection status changes.
 *
 * Displays the current MQTT client status on the LCD and
 * updates LED indicators accordingly.
 *
 * @param status        Human-readable connection status text.
 * @param client_status Enumerated status code from MQTT manager.
 */
void on_mqtt_status(const char *status, mqm_status_t client_status)
{
    if (!initialized) {
        ESP_LOGE(TAG, "MQTT callback not initialized");
        return;
    }

    if (prev_status != client_status)
        return;

    LCD_show_lines(0, status, LCD, true);

    switch (client_status) {
        case MQM_CONNECTING:
            led_blinking(YELLOW_LED, 0.4, false);
            break;

        case MQM_CONNECTED:
            led_on(YELLOW_LED, false);
            led_on(GREEN_LED, false);
            break;

        case MQM_DISCONNECTING:
            led_blinking(YELLOW_LED, 0.4, false);
            break;

        case MQM_DISCONNECTED:
            led_off(YELLOW_LED);
            break;

        case MQM_ERROR:
            led_on(RED_LED, false);
            led_off(YELLOW_LED);
            break;

        default:
            break;
    }
}




/**
 * @brief Called whenever a message is received from an MQTT topic.
 *
 * Logs the incoming message and topic.
 *
 * @param topic   Topic name of the incoming message.
 * @param payload Message content (UTF-8 string).
 */
void on_mqtt_message(const char *topic, const char *payload)
{
    if (!initialized) {
        ESP_LOGE(TAG, "MQTT message callback when not initialized");
        return;
    }

    ESP_LOGI("MQTT", "Topic='%s' Payload='%s'", topic, payload);
}




/**
 * @brief Publishes “device connected” message when MQTT client connects.
 *
 * Used as part of the MQTT Manager callbacks to inform the remote server
 * that this device is online.
 *
 * @param client Pointer to the active MQTT client instance.
 */
void publish_when_client_connected(mqm_t *client)
{
    if (!initialized) {
        ESP_LOGE(TAG, "MQTT publish callback when not initialized");
        return;
    }

    MQTT_PUBLISH_CHECK(client, TOPIC_OUT_DEVICE_CONNECTION, "device connected", 1, 0);
}




/* -------------------------------------------------------------------------- */
/*                           INITIALIZATION FUNCTION                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the MQTT callbacks handler.
 *
 * Stores the LCD context for later use and enables MQTT event visualization.
 *
 * @param LCD_context LCD context for display operations.
 */
void init_mqtt_callbacks_handler(lcd_context_t LCD_context)
{
    LCD = LCD_context;
    initialized = true;

    ESP_LOGI(TAG, "MQTT callback initialized");
}