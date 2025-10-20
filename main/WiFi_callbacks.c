/**
 * @file WiFi_callbacks.c
 * @brief Implementation of Wi-Fi callback handlers for connection and scan events.
 *
 * ## Overview
 * This module defines the Wi-Fi callback functions used by the Wi-Fi Manager (`wfm_t`):
 *  - Display connection status and logs on the LCD.
 *  - Control LED indicators according to Wi-Fi state.
 *  - Print scan results in JSON format to the console.
 *
 * ## Responsibilities
 *  - Provide visual feedback (LCD + LEDs) for connection states.
 *  - Safely log and display messages from the Wi-Fi Manager.
 *  - Initialize callback environment with the given LCD context.
 *
 * @dependencies
 *  - `WiFi_manager.h`
 *  - `leds_driver.h`
 *  - `lcd_driver.h`
 *
 * @author
 *  Ivgeny Tokarzhevsky
 */

#include "WiFi_callbacks.h"
#include "WiFi_manager.h"
#include <esp_log.h>
#include "leds_driver.h"
#include "lcd_driver.h"


/* -------------------------------------------------------------------------- */
/*                               Local Context                                */
/* -------------------------------------------------------------------------- */

/** @brief Module log tag */
static const char* TAG = "WIFI_CALLBACKS";

/** @brief LCD context used for displaying messages */
static lcd_context_t LCD;

/** @brief Indicates whether the callback handler has been initialized */
static bool initialized = false;



/* -------------------------------------------------------------------------- */
/*                              Callback Handlers                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Handle Wi-Fi connection status updates.
 *
 * Displays a status message on the LCD and updates LEDs accordingly.
 *
 * @param msg          Human-readable status message.
 * @param wifi_status  Current Wi-Fi connection state.
 */
void on_wifi_status(const char *msg, wifi_status_t wifi_status) {

    if (!initialized) {
        ESP_LOGE(TAG, "Wi-Fi callback called before initialization");
        return;
    }

    /* Display message on LCD */
    LCD_show_lines(0, msg, LCD, true);

    /* Update LEDs according to Wi-Fi state */
    switch (wifi_status) {

        case WIFI_CONNECTING:
            led_blinking(GREEN_LED, 0.4, true);
            break;

        case WIFI_CONNECTED:
            led_on(GREEN_LED, false);
            break;

        case WIFI_DISCONNECTING:
            led_blinking(GREEN_LED, 0.4, false);
            break;

        case WIFI_DISCONNECTED:
            led_off(GREEN_LED);
            break;

        case WIFI_ERROR:
            led_on(RED_LED, true);
            break;

        default:
            break;
    }
}



/**
 * @brief Handle Wi-Fi scan completion event.
 *
 * Prints the scan results (JSON-formatted string) to the log.
 *
 * @param result_list JSON string containing scanned SSIDs and RSSI values.
 */
void on_wifi_scan_json(const char *result_list) {

    if (!initialized) {
        ESP_LOGE(TAG, "Wi-Fi callback called before initialization");
        return;
    }

    ESP_LOGI("WiFi", "Scan results: %s", result_list);
}



/**
 * @brief Initialize the Wi-Fi callback handler.
 *
 * Must be called before any callback is executed.
 *
 * @param LCD_context LCD context structure used for display.
 */
void init_wifi_callbacks_handler(lcd_context_t LCD_context) {
    LCD = LCD_context;
    initialized = true;

    ESP_LOGI(TAG, "Wi-Fi callback initialized");
}