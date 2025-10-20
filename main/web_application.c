/**
 * @file web_application.c
 * @brief Main application logic for MQTT-based remote control, Wi-Fi management, and OTA.
 *
 * ## Overview
 * This module acts as the core "web application layer" for the IoT device.
 * It connects the Wi-Fi and MQTT managers, handles remote commands,
 * controls the LCD and LEDs, and manages OTA firmware updates.
 *
 * ## Responsibilities
 *  - Handle incoming MQTT commands (LCD updates, LED control, Wi-Fi switching, OTA).
 *  - Bridge between MQTT topics and hardware/UI components.
 *  - Manage device connection diagnostics and status publishing.
 *  - Integrate with NVS memory for credential persistence.
 *
 * @note
 *  This file depends heavily on the following project modules:
 *  - `wifi_manager.h`
 *  - `mqtt_manager.h`
 *  - `lcd_driver.h`
 *  - `leds_driver.h`
 *  - `nvs_memory.h`
 *  - `util.h`
 *  - `config.h`
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 */

#include "web_application.h"

/* -------------------------------------------------------------------------- */
/*                           ESP-IDF / Standard C                             */
/* -------------------------------------------------------------------------- */
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "esp_https_ota.h"

/* -------------------------------------------------------------------------- */
/*                               Project headers                              */
/* -------------------------------------------------------------------------- */
#include "WiFi_manager.h"
#include "mqtt_manager.h"
#include "nvs_memory.h"
#include "leds_driver.h"
#include "lcd_driver.h"
#include "config.h"
#include "util.h"
#include "cJSON.h"



/* -------------------------------------------------------------------------- */
/*                              Global Contexts                               */
/* -------------------------------------------------------------------------- */

/** @brief Shared Wi-Fi manager pointer (set during init). */
static wfm_t* wfm = NULL;

/** @brief Shared MQTT manager pointer (set during init). */
static mqm_t* mqm = NULL;

/** @brief LCD context structure used for UI updates. */
static lcd_context_t LCD;

/** @brief NVS handler for Wi-Fi credential storage. */
static nvs_handle_t nvs_memory_handler;

/** @brief Application log tag. */
static const char* TAG = "Web_App";

/** @brief True if the web application has been initialized. */
static bool app_initialized = false;



/* -------------------------------------------------------------------------- */
/*                             MQTT Publish Helper                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Publish a non-retained QoS=1 MQTT message with safety checks.
 *
 * This macro wrapper uses `MQTT_PUBLISH_CHECK()` defined in the MQTT manager.
 *
 * @param topic MQTT topic to publish to.
 * @param msg   Payload string.
 */
static inline void publish_q1(const char* topic, const char* msg) {
    MQTT_PUBLISH_CHECK(mqm, topic, msg, 1, 0);
}



/* -------------------------------------------------------------------------- */
/*                               LED Commands                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Table-based command mapping for LED control via MQTT.
 */
typedef void (*void_fn)(void);
typedef struct {
    const char* command;
    void_fn     handler;
} leds_cmd_t;

/* Local LED command handlers */
static void red_led_on(void)    { led_on(RED_LED,    false); }
static void red_led_off(void)   { led_off(RED_LED);          }
static void yellow_led_on(void) { led_on(YELLOW_LED, false); }
static void yellow_led_off(void){ led_off(YELLOW_LED);       }
static void green_led_on(void)  { led_on(GREEN_LED,  false); }
static void green_led_off(void) { led_off(GREEN_LED);        }

/** @brief Command lookup table for LED actions. */
static const leds_cmd_t s_leds_table[] = {
    { "red led on",     red_led_on     },
    { "red led off",    red_led_off    },
    { "yellow led on",  yellow_led_on  },
    { "yellow led off", yellow_led_off },
    { "green led on",   green_led_on   },
    { "green led off",  green_led_off  },
};



/* -------------------------------------------------------------------------- */
/*                              MQTT Handlers                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Display text remotely via MQTT.
 *
 * @param text String to display on the LCD.
 */
void LCD_display_text(const char* text) {
    if (!text) text = "";
    LCD_show_lines(0, text, LCD, true);
}



/**
 * @brief Handle LED toggle commands received via MQTT.
 *
 * @param command Command string (e.g. "red led on").
 */
void leds_toggle_handler(const char* command) {
    if (!command) return;

    for (size_t i = 0; i < sizeof(s_leds_table) / sizeof(s_leds_table[0]); ++i) {
        if (strcmp(s_leds_table[i].command, command) == 0) {
            s_leds_table[i].handler();
            return;
        }
    }
    ESP_LOGW(TAG, "Unknown LED command: %s", command);
}



/**
 * @brief Scan available Wi-Fi networks, build JSON, and publish the result.
 *
 * Publishes results to `TOPIC_OUT_SCAN_WIFI_RESULT`.
 */
void scan_wifi_networks(const char* /*payload*/) {
    if (wfm_scan_sync(wfm) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi scan failed");
        publish_q1(TOPIC_OUT_SCAN_WIFI_RESULT, "[]");
        return;
    }

    cJSON* arr = cJSON_CreateArray();
    if (!arr) {
        publish_q1(TOPIC_OUT_SCAN_WIFI_RESULT, "[]");
        return;
    }

    for (uint8_t i = 0; i < wfm->scan.count; ++i) {
        cJSON* o = cJSON_CreateObject();
        if (!o) continue;
        cJSON_AddStringToObject(o, "ssid", wfm->scan.aps[i].ssid);
        cJSON_AddNumberToObject(o, "rssi", wfm->scan.aps[i].rssi);
        cJSON_AddItemToArray(arr, o);
    }

    char* js = cJSON_PrintUnformatted(arr);
    if (js) {
        publish_q1(TOPIC_OUT_SCAN_WIFI_RESULT, js);
        cJSON_free(js);
    }
    cJSON_Delete(arr);

    LCD_show_lines(0, "Wi-Fi scan done", LCD, true);
}



/* -------------------------------------------------------------------------- */
/*                        Wi-Fi Network Switching (MQTT)                      */
/* -------------------------------------------------------------------------- */

/** @brief Helper argument passed to the Wi-Fi change task. */
typedef struct {
    char* heap_str; /**< Duplicated "SSID|PASSWORD" payload */
} change_wifi_arg_t;



/**
 * @brief FreeRTOS task that performs a Wi-Fi network switch via MQTT command.
 */
static void change_wifi_network_task(void* param)
{
    change_wifi_arg_t* change_wifi_arg = (change_wifi_arg_t*)param;
    char* payload = change_wifi_arg ? change_wifi_arg->heap_str : NULL;

    char* ssid = NULL;
    char* pass = NULL;

    /* Parse "SSID|PASSWORD" format */
    if (payload) {
        char* p = strchr(payload, '|');
        if (p) {
            *p = '\0';
            ssid = payload;
            pass = p + 1;
        }
    }

    if (!ssid || !*ssid || !pass) {
        ESP_LOGE(TAG, "Invalid change Wi-Fi payload");
        publish_q1(TOPIC_OUT_NEW_WIFI_CONNECT_STATUS, "invalid payload");
        goto cleanup;
    }

    LCD_show_lines(0, "Switching Wi-Fi…", LCD, true);
    led_blinking(GREEN_LED, 0.3, true);

    wfm_disc_reason_e reason = WFM_DISC_NONE;
    esp_err_t new_wifi_connected = wfm_change_network(wfm, ssid, pass, &reason);

    /* Connection succeeded — wait for MQTT reconnect */
    if (new_wifi_connected == ESP_OK && wfm_is_connected(wfm)) {
        int wait_sec = 0;
        while (wait_sec < 60 && !mqm_is_connected(mqm)) {
            wait_ms(1000);
            wait_sec++;
        }

        if (mqm_is_connected(mqm)) {
            publish_q1(TOPIC_OUT_NEW_WIFI_CONNECT_STATUS, "new wifi connected");
            add_wifi_creds_to_NVS_memory(ssid, pass, nvs_memory_handler);
            LCD_show_lines(0, "Wi-Fi switched OK", LCD, true);
            led_on(GREEN_LED, true);
        } else {
            ESP_LOGW(TAG, "Wi-Fi connected, MQTT didn't reconnect");
        }
    }
    /* Connection failed — revert and notify */
    else {
        if (wfm_is_connected(wfm)) {
            int wait_sec = 0;
            while (wait_sec < 60 && !mqm_is_connected(mqm)) {
                wait_ms(1000);
                wait_sec++;
            }

            if (mqm_is_connected(mqm)) {
                ESP_LOGW(TAG, "MQTT reconnected, new wifi connection fail");
                led_on(YELLOW_LED, true);
            } else {
                ESP_LOGW(TAG, "Wi-Fi reverted, MQTT not reconnected");
            }

            switch (reason) {
                case WFM_DISC_WRONG_PASSWORD:
                    publish_q1(TOPIC_OUT_NEW_WIFI_CONNECT_STATUS,
                               "new wifi not connected - wrong password");
                    remove_wifi_creds_from_NVS_memory(ssid, nvs_memory_handler);
                    break;
                case WFM_DISC_NO_AP:
                    publish_q1(TOPIC_OUT_NEW_WIFI_CONNECT_STATUS,
                               "new wifi not connected - ssid not found");
                    break;
                default:
                    publish_q1(TOPIC_OUT_NEW_WIFI_CONNECT_STATUS,
                               "new wifi not connected - other reason");
                    break;
            }
        } else {
            LCD_show_lines(0, "Wi-Fi reconnection failed", LCD, true);
            led_on(RED_LED, true);
        }
    }

cleanup:
    if (change_wifi_arg) {
        if (change_wifi_arg->heap_str) free(change_wifi_arg->heap_str);
        free(change_wifi_arg);
    }
    vTaskDelete(NULL);
}



/**
 * @brief Entry point for changing Wi-Fi via MQTT command.
 *
 * Spawns `change_wifi_network_task()` in a new FreeRTOS task.
 */
void change_wifi_network_handler(const char* payload) {
    if (!payload) {
        ESP_LOGE(TAG, "change_wifi_network_handler: NULL payload");
        return;
    }

    change_wifi_arg_t* arg = (change_wifi_arg_t*)calloc(1, sizeof(*arg));
    if (!arg) return;
    arg->heap_str = strdup(payload);
    if (!arg->heap_str) { free(arg); return; }

    xTaskCreate(change_wifi_network_task,
                "change_wifi_network_task",
                4096, arg, 5, NULL);
}



/* -------------------------------------------------------------------------- */
/*                         Device Connection Test (MQTT)                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Respond to a device connection test MQTT command.
 *
 * Publishes device name, firmware, SSID, IP, MAC, RSSI, and saved networks.
 */
void device_connection_test(const char* /*payload*/) {
    char dev_name[64] = "Device Name: " DEV_NAME;
    char prog_ver[64] = "Firmware: "    PROG_VERSION;

    char ssid[64] = "WiFi SSID: ";
    strlcpy(ssid + strlen(ssid), wfm->info.ssid, sizeof(ssid) - strlen(ssid));

    char ip[64] = "IP Address: ";
    strlcpy(ip + strlen(ip), wfm->info.ip, sizeof(ip) - strlen(ip));

    char mac[64] = "MAC Address: ";
    strlcpy(mac + strlen(mac), wfm->info.mac, sizeof(mac) - strlen(mac));

    char rssi[64] = "RSSI: ";
    strlcpy(rssi + strlen(rssi), wfm->info.rssi, sizeof(rssi) - strlen(rssi));

    MQTT_PUBLISH_CHECK(mqm, TOPIC_OUT_DEVICE_CONNECTION, dev_name, 1, 0);
    MQTT_PUBLISH_CHECK(mqm, TOPIC_OUT_DEVICE_CONNECTION, prog_ver, 1, 0);
    MQTT_PUBLISH_CHECK(mqm, TOPIC_OUT_DEVICE_CONNECTION, ssid,     1, 0);
    MQTT_PUBLISH_CHECK(mqm, TOPIC_OUT_DEVICE_CONNECTION, ip,       1, 0);
    MQTT_PUBLISH_CHECK(mqm, TOPIC_OUT_DEVICE_CONNECTION, mac,      1, 0);
    MQTT_PUBLISH_CHECK(mqm, TOPIC_OUT_DEVICE_CONNECTION, rssi,     1, 0);

    /* Include saved Wi-Fi credentials */
    wfm_cred_list_t list = {0};
    get_wifi_creds_from_NVS_memory(&list, nvs_memory_handler);

    for (uint8_t i = 0; i < list.count; ++i) {
        char out[128];
        snprintf(out, sizeof(out), "ssid:%s pass:%s",
                 list.creds[i].ssid, list.creds[i].pass);
        MQTT_PUBLISH_CHECK(mqm, TOPIC_OUT_WIFI_CRED_LIST, out, 1, 0);
    }
}



/* -------------------------------------------------------------------------- */
/*                              OTA Management                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Perform HTTPS OTA update from a given URL.
 */
static void perform_ota(const char *ota_url) {
    if (!ota_url || !*ota_url) {
        ESP_LOGE(TAG, "OTA: empty URL");
        publish_q1(TOPIC_OUT_OTA_UPDATE, "invalid url");
        return;
    }

    esp_http_client_config_t http_cfg = {
        .url = ota_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };

    esp_https_ota_handle_t h = NULL;
    if (esp_https_ota_begin(&ota_cfg, &h) != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed");
        publish_q1(TOPIC_OUT_OTA_UPDATE, "Begin failed");
        return;
    }

    publish_q1(TOPIC_OUT_OTA_UPDATE, "Download started");
//todo clear before progress, check why yellow only on connected mqtt, whuke yellow blink on no creds start ser
    LCD_show_lines(0,"",LCD,true);
    int last_bucket = -1;
    while (1) {
        esp_err_t e = esp_https_ota_perform(h);
        if (e == ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            int total = esp_https_ota_get_image_size(h);
            int read  = esp_https_ota_get_image_len_read(h);
            if (total > 0) {
                int pct = (read * 100) / total;
                if (pct / 5 > last_bucket / 5) {
                    last_bucket = pct;
                    char msg[48];
                    snprintf(msg, sizeof(msg), "Progress: %d%%", pct);
                    publish_q1(TOPIC_OUT_OTA_UPDATE, msg);
                    LCD_show_lines(0,msg,LCD, false);
                }
            }
        } else {
            break;
        }
    }

    if (esp_https_ota_finish(h) == ESP_OK) {
        publish_q1(TOPIC_OUT_OTA_UPDATE, "OTA successful, restarting...");
        LCD_show_lines(0,"new version installed",LCD, true);
        for (int i =0; i <5;i++) {
            led_on(GREEN_LED, false);
            led_on(RED_LED, false);
            led_on(YELLOW_LED, false);
            wait_ms(350);
            //all_leds_off();
            led_off(GREEN_LED);
            led_off(RED_LED);
            led_off(YELLOW_LED);
        }
        wait_ms(1000);
        esp_restart();
    } else {
        publish_q1(TOPIC_OUT_OTA_UPDATE, "OTA version updated failed");
        LCD_show_lines(0,"new version installed",LCD, true);
        led_on(RED_LED,true);
    }
}



/**
 * @brief MQTT handler for performing OTA update.
 *
 * @param download_path HTTPS URL of the new firmware binary.
 */
void OTA_update(const char* download_path) {
    LCD_show_lines(0, "Starting OTA update", LCD, true);
    perform_ota(download_path);
}



/* -------------------------------------------------------------------------- */
/*                                Initialization                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the web application layer.
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
                       nvs_handle_t nvs_memory)
{
    if (!wifi_manager || !mqtt_client || !nvs_memory) {
        ESP_LOGE(TAG, "Null param in initialize attempt");
        return ESP_ERR_INVALID_ARG;
    }

    wfm = wifi_manager;
    mqm = mqtt_client;
    LCD = LCD_context;
    nvs_memory_handler = nvs_memory;

    app_initialized = true;
    return ESP_OK;
}