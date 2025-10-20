/**
* @file WiFi_callbacks.h
 * @brief Callback interface for Wi-Fi status and scan events.
 *
 * ## Overview
 * This module defines the callback functions that the Wi-Fi Manager (`wfm_t`)
 * uses to notify the application layer about changes in Wi-Fi state and
 * network scan results.
 *
 * ## Responsibilities
 *  - Display Wi-Fi connection status messages on the LCD.
 *  - Control LEDs based on current connection state.
 *  - Log JSON-formatted scan results to the console.
 *
 * @dependencies
 *  - `WiFi_manager.h`
 *  - `lcd_driver.h`
 *
 * @date
 *  Created on: 12/10/2025
 * @author
 *  Ivgeny Tokarzhevsky
 */

#ifndef WIFI_CALLBACKS_H
#define WIFI_CALLBACKS_H

#include <lcd_driver.h>
#include <WiFi_manager.h>

#ifdef __cplusplus
extern "C" {
#endif


    /* -------------------------------------------------------------------------- */
    /*                             Wi-Fi Callback API                             */
    /* -------------------------------------------------------------------------- */

    /**
     * @brief Handle Wi-Fi connection status updates.
     *
     * Called by the Wi-Fi manager when connection state changes.
     * Displays the message on the LCD and updates LEDs accordingly.
     *
     * @param msg          Human-readable status message.
     * @param wifi_status  Current Wi-Fi connection status enum.
     */
    void on_wifi_status(const char *msg, wifi_status_t wifi_status);

    /**
     * @brief Handle Wi-Fi scan results (JSON format).
     *
     * Called when a Wi-Fi scan is completed. Logs the JSON result string.
     *
     * @param result_list  JSON-formatted string with SSIDs and RSSI values.
     */
    void on_wifi_scan_json(const char *result_list);

    /**
     * @brief Initialize the Wi-Fi callback handler.
     *
     * Must be called once before other callback functions are used.
     *
     * @param LCD_context  LCD context structure for displaying messages.
     */
    void init_wifi_callbacks_handler(lcd_context_t LCD_context);


#ifdef __cplusplus
}
#endif

#endif /* WIFI_CALLBACKS_H */