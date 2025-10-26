/**
 * @file main.c
 * @brief Main application entry point for the ESP32 IoT Dashboard Device.
 *
 * ## Overview
 * This file implements the main startup sequence for the ESP32-based IoT Dashboard device.
 * It initializes all subsystems (Wi-Fi, MQTT, HTTP, LCD, LEDs, NVS) and handles
 * runtime events such as Wi-Fi reset or AP mode switching.
 *
 * ## Responsibilities
 *  - Initialize and configure all hardware and software modules.
 *  - Attempt Wi-Fi STA connection using saved credentials.
 *  - If no credentials are available, start HTTP configuration server in AP mode.
 *  - Establish MQTT connection after successful Wi-Fi link.
 *  - Handle runtime button events (reset, triple press) in the main loop.
 *
 * ## Architecture
 * ```
 * app_main()
 *   ├── Initialize system + peripherals
 *   ├── Wi-Fi Manager (wfm_t)
 *   │     ├── Scan + connect or start AP
 *   ├── MQTT Manager (mqm_t)
 *   │     ├── Subscribe to topics
 *   │     ├── Publish device status
 *   └── Main loop (handles button events, watchdog, etc.)
 * ```
 *
 * ## Dependencies
 *  - Wi-Fi Manager (`wifi_manager.h`)
 *  - MQTT Manager (`mqtt_manager.h`)
 *  - LED Driver (`leds_driver.h`)
 *  - LCD Driver (`lcd_driver.h`)
 *  - NVS Memory (`nvs_memory.h`)
 *  - HTTP Server (`http_server.h`)
 *  - Interrupts (`interrupts.h`)
 *
 * @note
 *  All hardware initialization is performed before connecting to Wi-Fi.
 *  If any initialization step fails, the system enters a failure state and displays an error on the LCD.
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 * @date
 *  Created on: 19/09/2025
 */


#include <credentials.h>
#include "hardware_layer.h"
#include <interrupts.h>
#include <main.h>
#include <util.h>

#include "mqtt_manager.h"
#include "http_server.h"
#include "wifi_manager.h"

#include "leds_driver.h"
#include "lcd_driver.h"
#include "nvs_memory.h"
#include "MQTT_callbacks.h"
#include "WiFi_callbacks.h"
#include "web_application.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"




/* -------------------------------------------------------------------------- */
/*                           GLOBAL / STATIC VARIABLES                        */
/* -------------------------------------------------------------------------- */

/** @brief Application log tag for ESP logging. */
static const char* TAG = "MAIN";

/** @brief Global Wi-Fi and MQTT context instances. */
static wfm_t wfm;
static mqm_t mqm;

/** @brief NVS handle for persistent storage. */
static nvs_handle_t nvs_handler;

/** @brief LCD context for display operations. */
static lcd_context_t LCD_context;

/** @brief Flags updated from ISR (reset button, triple-press). */
volatile bool wifi_reset_pressed  = false;
volatile bool wifi_triple_pressed = false;




/* -------------------------------------------------------------------------- */
/*                           MAIN APPLICATION ENTRY                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Main entry point for the ESP-IDF application.
 *
 * Performs full initialization of all subsystems and enters
 * the main runtime loop, which handles Wi-Fi reset and AP switch events.
 */
void app_main(void)
{
    bool init_success = true;

    /* === System initialization === */
    ESP_LOGI(TAG, "Initializing system...");

    /* Initialize NVS flash */
    END_IF_ERROR(nvs_flash_init(), "NVS flash init");

    /* Initialize network interfaces */
    END_IF_ERROR(esp_netif_init(), "esp_netif init");

    /* Create default event loop */
    END_IF_ERROR(esp_event_loop_create_default(), "event loop init");

    /* Initialize NVS storage wrapper */
    END_IF_ERROR(init_NVS_memory(&nvs_handler, NVS_STORAGE_FOLDER), "NVS memory init");

    /* Initialize LED driver */
    all_leds_init(GREEN_LED_PIN, RED_LED_PIN, YELLOW_LED_PIN);

    /* Configure LCD pinout and dimensions */
    LCD_context.rs   = LCD_PIN_RS;
    LCD_context.en   = LCD_PIN_EN;
    LCD_context.d4   = LCD_PIN_D4;
    LCD_context.d5   = LCD_PIN_D5;
    LCD_context.d6   = LCD_PIN_D6;
    LCD_context.d7   = LCD_PIN_D7;
    LCD_context.cols = LCD_COLS;
    LCD_context.rows = LCD_ROWS;

    /* Initialize LCD display */
    LCD_initialize(LCD_context);

    /* Display firmware version */
    char ver_msg[32] = "Program version ";
    strcat(ver_msg, PROG_VERSION);
    LCD_show_lines(0, ver_msg, LCD_context, true);

    /* Configure GPIO interrupts and button */
    enable_GPIO_interrupts(WIFI_RESET_PIN);
    init_wifi_reset_button_GPIO(WIFI_RESET_PIN);

    /* Initialize SPIFFS filesystem for web assets */
    init_spiffs();

    /* Initialize callbacks */
    init_mqtt_callbacks_handler(LCD_context);
    init_wifi_callbacks_handler(LCD_context);

    /* Initialize HTTP server (for AP configuration mode) */
    init_http_server(LCD_context, nvs_handler);


    /* === Load stored Wi-Fi credentials === */
    wfm_cred_list_t saved_creds;
    END_IF_ERROR(get_wifi_creds_from_NVS_memory(&saved_creds, nvs_handler), "get Wi-Fi credentials");


    /* === Set up Wi-Fi callbacks === */
    const wfm_callbacks_t wifi_cbs = {
        .on_scan_json = on_wifi_scan_json,
        .on_status    = on_wifi_status,
    };



    /* === Initialize Wi-Fi Manager === */
    if (wfm_init(&wfm, &saved_creds, NULL, &wifi_cbs) != ESP_OK) {
        LCD_show_lines(0, "Wi-Fi init failed!", LCD_context, true);
        ESP_LOGE(TAG, "Wi-Fi manager initialization failed");
        init_success = false;
        goto initialize_failure;
    }



    /* === Connection handling === */
    if (saved_creds.count == 0) {
        /* Case 1: No credentials → start AP mode */
        LCD_show_lines(0, "No Wi-Fi credentials", LCD_context, true);
        LCD_show_lines(0, "Starting AP setup mode...", LCD_context, true);
        led_blinking(GREEN_LED,1,true);

        END_IF_ERROR(wfm_start_ap(&wfm, WIFI_AP_SSID, WIFI_AP_PASSWORD), "Wi-Fi AP");

        LCD_show_lines(0, "Starting HTTP server...", LCD_context, true);

        if (start_webserver() == NULL) {
            init_success = false;
            goto initialize_failure;
        }
        LCD_show_lines(0, "connect to AP, insert wifi info", LCD_context, true);
    }
    else {

        /* Case 2: Try connecting as STA */
        if (wfm_first_connect(&wfm) == ESP_OK) {


            /* === MQTT Setup === */
            const mqm_callbacks_t mqtt_cbs = {
                .on_status  = on_mqtt_status,
                .on_message = on_mqtt_message,
                .publish_when_client_connected = publish_when_client_connected
            };

            /* Define MQTT topic handlers */
            const mqm_topic_entry_t mqtt_topics[] = {
                { TOPIC_IN_OTA_UPDATE,        OTA_update },
                { TOPIC_IN_LCD_DISPLAY,       LCD_display_text },
                { TOPIC_IN_SCAN_WIFI_NETS,    scan_wifi_networks },
                { TOPIC_IN_DEVICE_CONNECTION, device_connection_test },
                { TOPIC_IN_LEDS_TOGGLE,       leds_toggle_handler },
                { TOPIC_IN_CONNECT_NEW_WIFI,  change_wifi_network_handler },
            };

            /* Configure MQTT client parameters */
            const mqm_config_t mqtt_cfg = {
                .uri                    = MQTT_BROKER_URI,
                .username               = MQTT_USERNAME,
                .password               = MQTT_PASSWORD,
                .msg_retransmit_timeout = 3000,
                .keep_alive_enable      = true,
                .keepalive_sec          = 20,
                .keep_alive_interval    = 8,
                .keep_alive_count       = 2,
                .keep_alive_idle        = 5,
                .clean_session          = false,
                .disable_auto_reconnect = false,
                .reconnect_timeout_ms   = 4000,
                .last_will_msg          = "status changed",
                .last_will_topic        = TOPIC_OUT_DEVICE_CONNECTION,
                .last_will_qos          = 1,
                .last_will_retain       = true,
            };


            /* === Initialize and start MQTT === */
            if (mqm_init(&mqm, &mqtt_cfg, &mqtt_cbs, mqtt_topics,
                         sizeof(mqtt_topics) / sizeof(mqtt_topics[0])) == ESP_OK
                && mqm_start(&mqm, 15000) == ESP_OK)
            {
                if (init_web_app(&wfm, &mqm, LCD_context, nvs_handler) != ESP_OK) {
                    ESP_LOGE(TAG, "Web application handler initialization failed!");
                    init_success = false;
                    goto initialize_failure;
                }


            }
            else {
                LCD_show_lines(0, "MQTT connect failed!", LCD_context, true);
                init_success = false;
                goto initialize_failure;
            }
        }
        else {
            /*Wi-Fi not connected because no available wifi found on scan*/
            if (wfm.scan.count == 0) {
                LCD_show_lines(0, "no available Wi-Fi found", LCD_context, true);
            }
            else {
                /*Wi-Fi not connected because of an error*/
                LCD_show_lines(0, "Wi-Fi connection error", LCD_context, true);
            }






        }
    }



initialize_failure:
    if (!init_success) {
        ESP_LOGE(TAG, "Application initialization failed!");
        led_on(RED_LED, true);
    }
    else {
        ESP_LOGI(TAG, "Entering main loop...");
        wait_ms(3000);
        LCD_show_lines(0, "Online", LCD_context, true);
    }


    /* === Main loop === */
    while (1) {
        if (init_success) {
            /* Handle Wi-Fi reset button */
            if (wifi_reset_pressed) {
                wifi_reset_pressed = false;
                led_blinking_limited_times(RED_LED, 0.5, 5, true);
                LCD_show_lines(0, "Reset button pressed!", LCD_context, true);
                LCD_show_lines(0, "Erasing NVS...", LCD_context, true);
                nvs_flash_erase();
                LCD_show_lines(0, "Restarting...", LCD_context, true);
                LCD_clear(LCD_context);
                esp_restart();
            }

            /* Handle triple-press → switch to AP mode */
            else if (wifi_triple_pressed) {
                wifi_triple_pressed = false;
                LCD_show_lines(0, "Switching to AP mode...", LCD_context, true);
                wfm_full_driver_stop(&wfm);
                vTaskDelay(pdMS_TO_TICKS(1000));
                wfm_start_ap(&wfm, WIFI_AP_SSID, WIFI_AP_PASSWORD);
                LCD_show_lines(0, "HTTP server starting...", LCD_context, true);
                start_webserver();
                LCD_show_lines(0, "connect to AP, insert wifi info", LCD_context, true);
                led_blinking(GREEN_LED,1,true);

            }
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}