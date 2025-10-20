/**
 * @file http_server.c
 * @brief Embedded HTTP server module for Wi-Fi configuration via web interface.
 *
 * ## Overview
 * This module implements a lightweight web server using ESP-IDF’s `esp_http_server`
 * to serve static files (HTML, CSS, JS) from the SPIFFS filesystem, and process
 * POST requests for setting Wi-Fi credentials.
 * It provides a user-friendly configuration page for entering SSID and password.
 *
 * ## Responsibilities
 *  - Serve web UI files from SPIFFS (`index.html`, `style.css`, `script.js`)
 *  - Handle POST request for setting new Wi-Fi credentials
 *  - Interface with NVS to save Wi-Fi data
 *  - Display feedback on LCD and control LEDs for user feedback
 *
 * ## Dependencies
 *  - esp_http_server.h
 *  - esp_spiffs.h
 *  - lcd_driver.h
 *  - leds_driver.h
 *  - nvs_memory.h
 *  - util.h
 *  - WiFi_manager.h
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


#include "http_server.h"
#include <esp_log.h>
#include <lcd_driver.h>
#include <leds_driver.h>
#include <nvs_memory.h>
#include <util.h>
#include "esp_http_server.h"
#include "WiFi_manager.h"
#include "esp_spiffs.h"




/* -------------------------------------------------------------------------- */
/*                              STATIC VARIABLES                              */
/* -------------------------------------------------------------------------- */

static const char *TAG_SPIFFS = "SPIFFS";          ///< Tag for SPIFFS logging
static const char *TAG = "http server";            ///< Tag for general HTTP server logs

static bool initialized = false;                   ///< Indicates whether init_http_server() was called

static lcd_context_t LCD;                          ///< LCD context used for displaying messages
static nvs_handle_t nvs_memory;                    ///< Handle for NVS memory storage




/* -------------------------------------------------------------------------- */
/*                        STATIC FUNCTION DECLARATIONS                        */
/* -------------------------------------------------------------------------- */

/// Handler for serving the main index.html page
static esp_err_t index_get_handler(httpd_req_t *req);

/// Handler for serving CSS file
static esp_err_t css_get_handler(httpd_req_t *req);

/// Handler for serving JavaScript file
static esp_err_t js_get_handler(httpd_req_t *req);

/// Handler for processing Wi-Fi credential POST requests
static esp_err_t set_post_handler(httpd_req_t *req);




/* -------------------------------------------------------------------------- */
/*                           HTTP GET FILE HANDLERS                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Serve the main HTML page (`index.html`) from SPIFFS.
 *
 * @param req HTTP request structure.
 * @return ESP_OK on success, ESP_FAIL if file not found.
 */
static esp_err_t index_get_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/index.html", "r");
    if (!f)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "index.html not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/html");

    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        httpd_resp_send_chunk(req, buf, n);
    }

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}




/**
 * @brief Serve the CSS file (`style.css`) from SPIFFS.
 *
 * @param req HTTP request structure.
 * @return ESP_OK on success, ESP_FAIL if file not found.
 */
static esp_err_t css_get_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/style.css", "r");
    if (!f)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "style.css not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "text/css");

    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        httpd_resp_send_chunk(req, buf, n);
    }

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}




/**
 * @brief Serve the JavaScript file (`script.js`) from SPIFFS.
 *
 * @param req HTTP request structure.
 * @return ESP_OK on success, ESP_FAIL if file not found.
 */
static esp_err_t js_get_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/script.js", "r");
    if (!f)
    {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "script.js not found");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/javascript");

    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    {
        httpd_resp_send_chunk(req, buf, n);
    }

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}




/* -------------------------------------------------------------------------- */
/*                            HTTP POST HANDLER                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Process HTTP POST request from the web form.
 *
 * The POST request contains SSID and password data in URL-encoded format.
 * The credentials are parsed, saved to NVS, and then the device reboots
 * after user feedback is shown on the LCD and LEDs.
 *
 * @param req HTTP request handle.
 * @return ESP_OK if processed successfully, ESP_FAIL on error.
 */
static esp_err_t set_post_handler(httpd_req_t *req)
{
    char buf[128];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0)
    {
        httpd_resp_send_500(req);
        led_on(RED_LED, true);
        LCD_show_lines(0, "Server ERROR", LCD, true);
        return ESP_FAIL;
    }

    buf[ret] = '\0';

    char ssid[64] = {0};
    char pass[64] = {0};

    /* Parse SSID and password from POST data */
    sscanf(buf, "ssid=%63[^&]&pass=%63s", ssid, pass);

    ESP_LOGI(TAG, "Received SSID=%s PASS=%s", ssid, pass);

    replace_plus_with_space(ssid);
    replace_plus_with_space(pass);

    add_wifi_creds_to_NVS_memory(ssid, pass, nvs_memory);

    httpd_resp_sendstr(req, "Saved! Rebooting...");

    wait_ms(1000);

    /* User feedback */
    led_blinking_limited_times(GREEN_LED, 0.3, 5, true);
    LCD_show_lines(0, "New WIFI set successfully !", LCD, true);
    LCD_show_lines(0, "Rebooting", LCD, true);

    esp_restart();
}




/* -------------------------------------------------------------------------- */
/*                           HTTP SERVER CONTROL                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Start the HTTP server and register all URI handlers.
 *
 * @return httpd_handle_t Server handle if successful, NULL otherwise.
 */
httpd_handle_t start_webserver(void)
{
    if (!initialized)
    {
        ESP_LOGE(TAG, "setup http server not initialized");
        return NULL;
    }

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    if (httpd_start(&server, &config) == ESP_OK)
    {
        /* Register handlers for web assets */
        httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = index_get_handler };
        httpd_register_uri_handler(server, &root);

        httpd_uri_t css = { .uri = "/style.css", .method = HTTP_GET, .handler = css_get_handler };
        httpd_register_uri_handler(server, &css);

        httpd_uri_t js = { .uri = "/script.js", .method = HTTP_GET, .handler = js_get_handler };
        httpd_register_uri_handler(server, &js);

        httpd_uri_t set = { .uri = "/set", .method = HTTP_POST, .handler = set_post_handler };
        httpd_register_uri_handler(server, &set);

        ESP_LOGI(TAG, "HTTP Server started");
    }

    return server;
}




/* -------------------------------------------------------------------------- */
/*                             SPIFFS INITIALIZATION                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize SPIFFS filesystem for serving static web files.
 *
 * This function mounts the SPIFFS partition and logs memory usage.
 */
void init_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG_SPIFFS, "Failed to mount SPIFFS (%s)", esp_err_to_name(ret));
        return;
    }

    size_t total = 0, used = 0;
    esp_spiffs_info(conf.partition_label, &total, &used);

    ESP_LOGI(TAG_SPIFFS, "Mounted. Total: %u, Used: %u", (unsigned)total, (unsigned)used);
}




/* -------------------------------------------------------------------------- */
/*                          HTTP SERVER INITIALIZATION                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize HTTP server dependencies.
 *
 * Stores LCD and NVS handles for later use in handlers,
 * and sets the module as ready for server startup.
 *
 * @param LCD_con LCD context handle.
 * @param nvs     NVS handle for persistent Wi-Fi storage.
 */
void init_http_server(lcd_context_t LCD_con, nvs_handle_t nvs)
{
    nvs_memory = nvs;
    LCD = LCD_con;
    initialized = true;

    ESP_LOGI(TAG, "HTTP server handler initialized");
}