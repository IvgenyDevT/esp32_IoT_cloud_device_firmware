/**
 * @file http_server.h
 * @brief Public interface for the embedded HTTP server module.
 *
 * ## Overview
 * This header defines the public API for the HTTP server responsible
 * for serving configuration web pages (HTML, CSS, JS) and handling
 * Wi-Fi credential POST requests.  
 * It provides initialization and startup functions used by the main
 * application to bring up the configuration web interface.
 *
 * ## Responsibilities
 *  - Mount and manage SPIFFS filesystem for web assets.
 *  - Start the embedded HTTP server.
 *  - Handle user requests for Wi-Fi configuration.
 *  - Provide integration with LCD and LED feedback mechanisms.
 *
 * ## Dependencies
 *  - lcd_driver.h  
 *  - nvs.h  
 *  - esp_http_server.h
 *
 * ## Example
 * @code
 *  init_spiffs();
 *  init_http_server(LCD, nvs_handle);
 *  start_webserver();
 * @endcode
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 */

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H


#include <lcd_driver.h>
#include <nvs.h>
#include "esp_http_server.h"




/* -------------------------------------------------------------------------- */
/*                            HTTP SERVER CONTROL API                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Start the embedded HTTP web server.
 *
 * Initializes and starts the HTTP server using the ESP-IDF `esp_http_server`
 * component. Registers handlers for all web assets and form submissions.
 *
 * @return httpd_handle_t Handle to the running HTTP server, or NULL on failure.
 */
httpd_handle_t start_webserver(void);




/* -------------------------------------------------------------------------- */
/*                              SPIFFS INITIALIZATION                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize and mount the SPIFFS filesystem.
 *
 * This function mounts the SPIFFS partition used for serving
 * static web files (`index.html`, `style.css`, `script.js`).
 * Logs memory statistics and handles mount errors.
 */
void init_spiffs(void);




/* -------------------------------------------------------------------------- */
/*                          HTTP SERVER INITIALIZATION                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the HTTP server with required dependencies.
 *
 * Stores the LCD and NVS handles used for user feedback and
 * Wi-Fi credential storage.  
 * Must be called before `start_webserver()`.
 *
 * @param LCD_con LCD context structure for displaying messages.
 * @param nvs     NVS handle for persistent Wi-Fi storage.
 */
void init_http_server(lcd_context_t LCD_con, nvs_handle_t nvs);



#endif /* HTTP_SERVER_H */