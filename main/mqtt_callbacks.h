/**
 * @file MQTT_callbacks.h
 * @brief MQTT event callback declarations for the IoT Dashboard device.
 *
 * ## Overview
 * This header defines all MQTT callback functions used by the
 * `mqtt_manager` module to notify the application about
 * connection status changes, incoming messages, and client connection events.
 *
 * The callbacks update the LCD display and LED indicators to provide
 * real-time feedback to the user and automatically publish a
 * "device connected" message upon successful MQTT connection.
 *
 * ## Responsibilities
 *  - Handle MQTT client connection/disconnection states.
 *  - Display status text on the LCD screen.
 *  - Blink or turn on LEDs to visualize MQTT state.
 *  - Send "device connected" message automatically.
 *
 * ## Dependencies
 *  - esp_log.h  
 *  - lcd_driver.h  
 *  - mqtt_manager.h
 *
 * ## Example
 * @code
 *  lcd_context_t LCD;
 *  init_mqtt_callbacks_handler(LCD);
 *
 *  mqm_callbacks_t cbs = {
 *      .on_status  = on_mqtt_status,
 *      .on_message = on_mqtt_message,
 *      .publish_when_client_connected = publish_when_client_connected
 *  };
 * @endcode
 *
 * @note
 *  Must call `init_mqtt_callbacks_handler()` before using any MQTT callback.
 *  Otherwise, LED and LCD updates will not be functional.
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 */

#ifndef MQTT_CALLBACKS_H
#define MQTT_CALLBACKS_H

#include <esp_log.h>
#include <lcd_driver.h>
#include <mqtt_manager.h>




/* -------------------------------------------------------------------------- */
/*                           MQTT CALLBACK FUNCTIONS                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Handle MQTT connection status changes.
 *
 * Called automatically by the MQTT manager whenever the client’s connection
 * state changes (connecting, connected, disconnecting, disconnected, error).
 * Updates the LCD and LEDs accordingly.
 *
 * @param status        Human-readable connection status string.
 * @param client_status Enumerated MQTT status value.
 */
void on_mqtt_status(const char *status, mqm_status_t client_status);



/**
 * @brief Handle incoming MQTT messages.
 *
 * Logs the received topic and payload.  
 * Can be expanded to handle dynamic device actions based on incoming commands.
 *
 * @param topic   Topic name of the incoming MQTT message.
 * @param payload UTF-8 string message payload.
 */
void on_mqtt_message(const char *topic, const char *payload);



/**
 * @brief Publish a "device connected" message when MQTT client connects.
 *
 * Used as a callback to notify remote services or dashboards
 * that this device is online.
 *
 * @param client Pointer to the active MQTT client instance.
 */
void publish_when_client_connected(mqm_t *client);



/* -------------------------------------------------------------------------- */
/*                          MODULE INITIALIZATION API                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the MQTT callback handler.
 *
 * Saves the LCD context used to display MQTT connection information.
 * Must be called before using any other MQTT callback.
 *
 * @param LCD_context LCD context used for display output.
 */
void init_mqtt_callbacks_handler(lcd_context_t LCD_context);



#endif /* MQTT_CALLBACKS_H */