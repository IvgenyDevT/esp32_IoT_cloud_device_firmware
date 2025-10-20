/**
 * @file wifi_manager.c
 * @brief Implementation of a production-grade Wi-Fi Manager for ESP-IDF.
 *
 * This module provides a complete event-driven Wi-Fi control layer that wraps the
 * ESP-IDF Wi-Fi driver. It offers reliable STA/AP switching, deterministic
 * synchronization, safe memory handling, and optional JSON reporting of scan results.
 *
 * Key design goals:
 *  - Single opaque context (no global state leakage)
 *  - Clean lifecycle (init → connect → disconnect/AP → deinit)
 *  - Thread-safe synchronization with FreeRTOS EventGroups
 *  - Strong separation between Wi-Fi driver logic and application-level effects
 *
 * Example usage:
 * @code
 * wfm_init(&wfm, &saved_creds, NULL, &wifi_cbs);
 * wfm_first_connect(&wfm);
 * wfm_start_ap(&wfm, "MyAP", "12345678");
 * @endcode
 *
 * Note:
 *  - The application must call esp_event_loop_create_default() once before wfm_init().
 *  - All user-visible actions (LCD, MQTT, LEDs) should be implemented in callbacks.
 *
 * @author
 * Ivgeny Tokarzhevsky
 */

#include "wifi_manager.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_check.h"
#include "esp_log.h"
#include "cJSON.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "util.h"
#include "nvs_flash.h"
#include <stdbool.h>



/* -------------------------------------------------------------------------- */
/*                         Static function declarations                       */
/* -------------------------------------------------------------------------- */

/** Emits a user-facing status message via callback and log output. */
static void print_status(const wfm_t* wfm,
                         const char* msg,
                         wifi_status_t connection_status,
                         bool update_device);

/** Converts the AP list into JSON format and sends it via callback. */
static void convert_AP_list_to_JSON(const wfm_t* wfm,
                                    const wfm_scan_list_t* list);



/** Ensures a default STA interface exists. */
static esp_err_t is_sta_netif_created(wfm_t* wfm);

/** Destroys the STA interface if it exists. */
static esp_err_t destroy_sta_netif(wfm_t* wfm);

/** Ensures a default AP interface exists. */
static esp_err_t is_ap_netif_created(wfm_t* wfm);

/** Destroys the AP interface if it exists. */
static esp_err_t destroy_ap_netif(wfm_t* wfm);

static void wfm_reconnect_task(void* arg);

static esp_err_t wfm_stop_sta(wfm_t* wfm);



static esp_err_t wfm_disconnect_sta(wfm_t* wfm);



/* -------------------------------------------------------------------------- */
/*                               Module constants                             */
/* -------------------------------------------------------------------------- */

#define TAG "WFM"

/** Default runtime configuration used if the user passes NULL cfg. */
static const wfm_config_t WFM_DEFAULT_CFG = {
    .sta_listen_interval     = STA_LISTEN_INTERVAL,
    .wifi_connect_timeout_ms = WIFI_CONNECT_TIMEOUT_MS,
    .wifi_stop_timeout_ms    = WIFI_STOP_TIMEOUT_MS,
    .scan_active_min_ms      = WIFI_SCAN_TIME_MIN,
    .scan_active_max_ms      = WIFI_SCAN_TIME_max,
    .scan_channel            = WIFI_SCAN_CHANNEL,
    .allow_hidden            = WIFI_SCAN_SHOW_HIDDEN,
    .max_reconnect_attempts  = MAX_RECONNECT_ATTEMPTS,
};






/* -------------------------------------------------------------------------- */
/*                                Event handler                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Central event handler for Wi-Fi and IP events.
 *
 * Handles asynchronous events from the ESP-IDF Wi-Fi and TCP/IP stack.
 * Updates the Wi-Fi Manager internal state and triggers callbacks when needed.
 *
 * Handled events:
 *  - WIFI_EVENT_STA_START
 *  - WIFI_EVENT_STA_DISCONNECTED
 *  - WIFI_EVENT_STA_STOP
 *  - WIFI_EVENT_SCAN_DONE
 *  - IP_EVENT_STA_GOT_IP
 */
static void wifi_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    wfm_t* wfm = (wfm_t*)arg;

    /* Handle Wi-Fi related events */
    if (base == WIFI_EVENT) {

        /* STA started */
        if (id == WIFI_EVENT_STA_START) {
            xEventGroupSetBits(wfm->eg, WFM_BIT_STARTED);
            wfm->started = true;

            if (wfm->connect_on_start) {
                print_status(wfm, "Wi-Fi started, connecting...", WIFI_CONNECTING, true);
                ESP_ERROR_CHECK(esp_wifi_connect());
            }
            return;
        }

        /* STA disconnected */
        if (id == WIFI_EVENT_STA_DISCONNECTED) {
            const wifi_event_sta_disconnected_t* d = (const wifi_event_sta_disconnected_t*)data;
            wfm->connected = false;
            wfm->last_disc = WFM_DISC_OTHER;

            if (d) {
                switch (d->reason) {
                    case WIFI_REASON_AUTH_FAIL:
                    case WIFI_REASON_AUTH_EXPIRE:
                    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
                    case WIFI_REASON_ASSOC_EXPIRE:
                        wfm->last_disc = WFM_DISC_WRONG_PASSWORD; break;
                    case WIFI_REASON_NO_AP_FOUND:
                        wfm->last_disc = WFM_DISC_NO_AP; break;
                    default:
                        wfm->last_disc = WFM_DISC_OTHER; break;
                }
            }

            xEventGroupClearBits(wfm->eg, WFM_BIT_CONNECTED);
            xEventGroupSetBits(wfm->eg, WFM_BIT_FAIL);
            print_status(wfm, "Wi-Fi disconnected", WIFI_DISCONNECTED, true);

            if (wfm->auto_reconnect && !wfm->manual_stop) {
                print_status(wfm, "Auto-reconnect enabled, creating task...", WIFI_NONE, false);
                xTaskCreate(wfm_reconnect_task, "wfm_reconnect_task", 4096, wfm, 5, NULL);
            }
            return;
        }

        /* STA stopped */
        if (id == WIFI_EVENT_STA_STOP) {
            xEventGroupSetBits(wfm->eg, WFM_BIT_STOPPED);
            wfm->started = false;
            return;
        }

        /* Scan finished */
        if (id == WIFI_EVENT_SCAN_DONE) {
            xEventGroupSetBits(wfm->eg, WFM_BIT_SCANDONE);
            return;
        }
    }

    /* Handle IP events */
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t* ev = (const ip_event_got_ip_t*)data;
        wifi_config_t conf = {0};
        (void)esp_wifi_get_config(WIFI_IF_STA, &conf);

        s_strcpy(wfm->info.ssid, sizeof(wfm->info.ssid), (const char*)conf.sta.ssid);
        s_strcpy(wfm->info.pass, sizeof(wfm->info.pass), (const char*)conf.sta.password);

        if (ev) snprintf(wfm->info.ip, sizeof(wfm->info.ip), IPSTR, IP2STR(&ev->ip_info.ip));

        uint8_t mac[6] = {0};
        (void)esp_wifi_get_mac(WIFI_IF_STA, mac);
        snprintf(wfm->info.mac, sizeof(wfm->info.mac),
                 "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK)
            snprintf(wfm->info.rssi, sizeof(wfm->info.rssi), "%d", ap.rssi);
        else {
            s_strcpy(wfm->info.rssi, sizeof(wfm->info.rssi), "N/A");
            print_status(wfm, "Wi-Fi unstable connection", WIFI_CONNECTED, true);
        }

        xEventGroupSetBits(wfm->eg, WFM_BIT_CONNECTED);
        xEventGroupClearBits(wfm->eg, WFM_BIT_FAIL);
        wfm->connected = true;

        print_status(wfm, "Wi-Fi connected", WIFI_CONNECTED, true);
        return;
    }

    printf("Unhandled Wi-Fi event: %ld\n", id);
}






/* ========================================================================== */
/*                         Public API — Initialization                        */
/* ========================================================================== */

/**
 * @brief Initializes the Wi-Fi Manager context and registers event handlers.
 *
 * This function prepares the Wi-Fi Manager for use. It clears the given context,
 * applies the configuration (or default values), copies any saved credentials,
 * and registers the required event handlers for Wi-Fi and IP events.
 *
 * The function does not start the Wi-Fi driver; it only sets up the framework.
 *
 * @param wfm   Pointer to the Wi-Fi Manager context structure.
 * @param saved Optional pointer to a list of saved credentials.
 * @param cfg   Optional pointer to a runtime configuration structure.
 * @param cbs   Optional pointer to callback handlers (status and scan).
 *
 * @return ESP_OK on success, or an appropriate ESP-IDF error code.
 */
esp_err_t wfm_init(wfm_t* wfm,
                   const wfm_cred_list_t* saved,
                   const wfm_config_t* cfg,
                   const wfm_callbacks_t* cbs)
{
    if (!wfm) return ESP_ERR_INVALID_ARG;

    /* Reset context to a known state */
    memset(wfm, 0, sizeof(*wfm));

    /* Apply configuration or defaults */
    wfm->cfg = cfg ? *cfg : WFM_DEFAULT_CFG;

    /* Copy callback handlers if provided */
    if (cbs) wfm->cbs = *cbs;

    /* Copy saved credentials, ensuring bounds */
    if (saved) {
        wfm->saved = *saved;
        if (wfm->saved.count > WFM_MAX_CREDS)
            wfm->saved.count = WFM_MAX_CREDS;
    }

    /* Initialize network interface subsystem */
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "esp_netif_init");

    /* Create an EventGroup for synchronization between Wi-Fi states */
    wfm->eg = xEventGroupCreate();
    if (!wfm->eg) return ESP_ERR_NO_MEM;

    /* Register event handlers for Wi-Fi and IP events */
    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            wifi_event_handler, wfm, &wfm->evt_wifi),
        TAG, "register_wifi_event");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            wifi_event_handler, wfm, &wfm->evt_ip),
        TAG, "register_ip_event");

    wfm->mode = WFM_MODE_NONE;
    print_status(wfm, "Wi-Fi manager initialized", WIFI_NONE, true);
    ESP_LOGI(TAG, "Wi-Fi manager initialized");
    return ESP_OK;
}






/* -------------------------------------------------------------------------- */
/*                         Public API — Deinitialization                      */
/* -------------------------------------------------------------------------- */



/**
 * @brief Deinitializes the Wi-Fi Manager and releases all resources.
 *
 * Stops any running Wi-Fi interfaces, unregisters event handlers, and frees
 * synchronization objects. The context is cleared so it can be reused safely.
 *
 * @param wfm Pointer to the Wi-Fi Manager context.
 */
void wfm_deinit(wfm_t* wfm)
{
    if (!wfm) return;

    /* Stop Wi-Fi gracefully */
    esp_wifi_stop();
    (void)xEventGroupWaitBits(wfm->eg, WFM_BIT_STOPPED, pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
    esp_wifi_deinit();

    /* Destroy network interfaces */
    destroy_ap_netif(wfm);
    destroy_sta_netif(wfm);

    /* Unregister event handlers */
    if (wfm->evt_wifi)
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wfm->evt_wifi);
    if (wfm->evt_ip)
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, wfm->evt_ip);

    /* Delete EventGroup */
    if (wfm->eg)
        vEventGroupDelete(wfm->eg);

    memset(wfm, 0, sizeof(*wfm));
}





/* -------------------------------------------------------------------------- */
/*                          Public API — Wi-Fi Scanning                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Performs a synchronous Wi-Fi scan and stores results internally.
 *
 * The function blocks until the scan is finished and then builds an internal
 * list of unique SSIDs with their strongest RSSI values. It also triggers
 * the scan JSON callback if one is registered.
 *
 * @param wfm Pointer to the Wi-Fi Manager context.
 * @return ESP_OK if successful, otherwise an error code.
 */
esp_err_t wfm_scan_sync(wfm_t* wfm)
{
    if (!wfm) return ESP_ERR_INVALID_ARG;

    /* Ensure driver is started in STA mode for scanning */
    if (!wfm->started) {
        ESP_RETURN_ON_ERROR(is_sta_netif_created(wfm), TAG, "create_sta_if");

        wifi_init_config_t icfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_RETURN_ON_ERROR(esp_wifi_init(&icfg), TAG, "wifi_init");

        ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set_sta_mode");

        wfm->connect_on_start = false;
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi_start");

        (void)xEventGroupWaitBits(wfm->eg, WFM_BIT_STARTED,
                                  pdTRUE, pdFALSE, pdMS_TO_TICKS(3000));
    }

    /* Prepare scan configuration */
    wifi_scan_config_t sc = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = wfm->cfg.scan_channel,
        .show_hidden = wfm->cfg.allow_hidden,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time   = {
            .active = {
                .min = wfm->cfg.scan_active_min_ms,
                .max = wfm->cfg.scan_active_max_ms
            }
        },
    };

    /* Perform the blocking scan */
    ESP_RETURN_ON_ERROR(esp_wifi_scan_start(&sc, true), TAG, "scan_start");

    /* Get the number of APs found */
    uint16_t num = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_scan_get_ap_num(&num), TAG, "get_ap_num");

    if (num == 0) {
        wfm->scan.count = 0;
        convert_AP_list_to_JSON(wfm, &wfm->scan);
        return ESP_OK;
    }

    /* Allocate buffer for raw AP records */
    wifi_ap_record_t* rec = calloc(num, sizeof(*rec));
    if (!rec) return ESP_ERR_NO_MEM;

    esp_err_t r = esp_wifi_scan_get_ap_records(&num, rec);
    if (r != ESP_OK) { free(rec); return r; }

    /* Build unique SSID list */
    wfm->scan.count = 0;
    for (uint16_t i = 0; i < num && wfm->scan.count < WFM_SCAN_MAX; ++i) {
        const char* ssid = (const char*)rec[i].ssid;
        if (!ssid || ssid[0] == '\0') continue;

        int found = -1;
        for (uint8_t k = 0; k < wfm->scan.count; ++k) {
            if (strcmp(wfm->scan.aps[k].ssid, ssid) == 0) {
                found = k;
                break;
            }
        }

        if (found < 0) {
            s_strcpy(wfm->scan.aps[wfm->scan.count].ssid,
                     sizeof(wfm->scan.aps[wfm->scan.count].ssid),
                     ssid);
            wfm->scan.aps[wfm->scan.count].rssi = rec[i].rssi;
            wfm->scan.count++;
        } else if (rec[i].rssi > wfm->scan.aps[found].rssi) {
            wfm->scan.aps[found].rssi = rec[i].rssi;
        }
    }

    free(rec);
    convert_AP_list_to_JSON(wfm, &wfm->scan);
    return ESP_OK;
}






/* -------------------------------------------------------------------------- */
/*                     Public API — Access Point (AP) Mode                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Starts Wi-Fi in Access Point mode with the given SSID and password.
 *
 * Stops any running STA interface, creates a new AP interface, and starts the
 * Wi-Fi driver in AP mode. The resulting access point allows devices to connect
 * directly to this ESP device.
 *
 * @param wfm   Pointer to the Wi-Fi Manager context.
 * @param ssid  SSID name for the AP.
 * @param pass  Password for the AP (optional, can be empty for open mode).
 *
 * @return ESP_OK on success, or an appropriate error code.
 */
esp_err_t wfm_start_ap(wfm_t* wfm, const char* ssid, const char* pass)
{
    if (!wfm || !ssid) return ESP_ERR_INVALID_ARG;

    /* Stop and clean up STA mode */
    (void)esp_wifi_stop();
    (void)xEventGroupWaitBits(wfm->eg, WFM_BIT_STOPPED,
                              pdTRUE, pdFALSE, pdMS_TO_TICKS(1000));
    (void)esp_wifi_deinit();
    destroy_sta_netif(wfm);

    /* Create AP netif */
    ESP_RETURN_ON_ERROR(is_ap_netif_created(wfm), TAG, "create_ap_if");

    /* Initialize driver in AP mode */
    wifi_init_config_t icfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&icfg), TAG, "wifi_init_ap");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_AP), TAG, "set_ap_mode");

    /* Configure AP parameters */
    wifi_config_t ac = {0};
    s_strcpy((char*)ac.ap.ssid, sizeof(ac.ap.ssid), ssid);
    s_strcpy((char*)ac.ap.password, sizeof(ac.ap.password), pass ? pass : "");
    ac.ap.ssid_len       = strlen((const char*)ac.ap.ssid);
    ac.ap.channel        = WIFI_AP_CHANNELS;
    ac.ap.max_connection = WIFI_AP_MAX_CONNECTIONS;
    ac.ap.authmode       = (pass && pass[0]) ? WIFI_AUTH_WPA_WPA2_PSK : WIFI_AUTH_OPEN;

    /* Apply configuration and start the AP */
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_AP, &ac), TAG, "set_ap_config");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start_ap");

    wfm->mode = WFM_MODE_AP;
    print_status(wfm, "Wi-Fi setup server started", WIFI_NONE, true);
    return ESP_OK;
}






/**
 * @brief Stops Wi-Fi AP mode and cleans up the driver.
 *
 * @param wfm Pointer to the Wi-Fi Manager context.
 * @return ESP_OK on success, otherwise an error code.
 */
esp_err_t wfm_stop_ap(wfm_t* wfm)
{
    if (!wfm) return ESP_ERR_INVALID_ARG;
    if (!wfm->started) return ESP_OK;
    if (wfm->mode != WFM_MODE_AP) return ESP_OK;

    ESP_ERROR_CHECK(esp_wifi_stop());
    (void)xEventGroupWaitBits(wfm->eg, WFM_BIT_STOPPED,
                              pdTRUE, pdFALSE, pdMS_TO_TICKS(2000));
    ESP_ERROR_CHECK(esp_wifi_deinit());

    destroy_ap_netif(wfm);
    wfm->mode = WFM_MODE_NONE;

    print_status(wfm, "Wi-Fi setup server stopped", WIFI_NONE, true);
    return ESP_OK;
}






/* ========================================================================== */
/*                     Public API — Connection and Switching                  */
/* ========================================================================== */

/**
 * @brief Attempts to connect to one of the saved Wi-Fi networks.
 *
 * This function performs a full scan of available networks, compares the results
 * with the saved credential list, and attempts to connect to each known SSID in
 * order until a connection is established.
 *
 * It blocks until either a connection succeeds or all credentials fail.
 *
 * @param wfm Pointer to the Wi-Fi Manager context.
 * @return ESP_OK if connected successfully, ESP_FAIL if no network could be joined.
 */
esp_err_t wfm_first_connect(wfm_t* wfm)
{
    if (!wfm) return ESP_ERR_INVALID_ARG;
    if (wfm->saved.count == 0) return ESP_FAIL;

    /* Always perform a fresh scan before connecting */
    esp_err_t err = wfm_scan_sync(wfm);
    if (err != ESP_OK) return err;

    /* Stop previous STA session (if any) */
    ESP_RETURN_ON_ERROR(wfm_stop_sta(wfm), TAG, "stop_sta");

    wifi_config_t wc = {0};
    wc.sta.threshold.authmode = STA_AUTH_MODE;
    wc.sta.pmf_cfg.capable    = PMF_CAPABLE;
    wc.sta.pmf_cfg.required   = PMF_REQUIRED;
    wc.sta.scan_method        = STA_SCAN_METHOD;
    wc.sta.sort_method        = WIFI_CONNECT_AP_BY_SIGNAL;
    wc.sta.listen_interval    = wfm->cfg.sta_listen_interval;

    /* Iterate through saved credentials */
    for (uint8_t i = 0; i < wfm->saved.count; ++i) {

        /* Skip networks not found in the last scan */
        if (!is_ssid_available(wfm, wfm->saved.creds[i].ssid)) continue;

        s_strcpy((char*)wc.sta.ssid, sizeof(wc.sta.ssid), wfm->saved.creds[i].ssid);
        s_strcpy((char*)wc.sta.password, sizeof(wc.sta.password), wfm->saved.creds[i].pass);

        ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wc), TAG, "set_config");

        /* Trigger connection */
        wfm->connect_on_start = true;
        wfm->auto_reconnect = false;
        ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start_sta");

        /* Wait for connection or failure */
        EventBits_t bits = xEventGroupWaitBits(
            wfm->eg,
            WFM_BIT_CONNECTED | WFM_BIT_FAIL,
            pdTRUE,
            pdFALSE,
            pdMS_TO_TICKS(wfm->cfg.wifi_connect_timeout_ms)
        );

        if (bits & WFM_BIT_CONNECTED) {
            wfm->auto_reconnect = true;
            wfm->mode = WFM_MODE_STA;
            return ESP_OK;
        }

        /* Failed: stop and try next credential */
        ESP_ERROR_CHECK(esp_wifi_stop());
        (void)xEventGroupWaitBits(wfm->eg, WFM_BIT_STOPPED, pdTRUE, pdFALSE, pdMS_TO_TICKS(3000));
    }

    /* All credentials exhausted */
    return ESP_FAIL;
}





/* -------------------------------------------------------------------------- */
/*                Public API — Full Driver Stop and Cleanup                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Fully stops and deinitializes the Wi-Fi driver.
 *
 * This ensures that all Wi-Fi activity is halted and the driver is fully deinitialized.
 * Used mainly when switching between STA and AP modes or before deep sleep.
 *
 * @param wfm Pointer to the Wi-Fi Manager context.
 * @return ESP_OK on success.
 */
esp_err_t wfm_full_driver_stop(wfm_t* wfm)
{
    if (!wfm) return ESP_ERR_INVALID_ARG;

    wfm->manual_stop = true;
    (void)esp_wifi_disconnect();
    ESP_ERROR_CHECK(esp_wifi_stop());

    (void)xEventGroupWaitBits(wfm->eg, WFM_BIT_STOPPED, pdTRUE, pdFALSE, pdMS_TO_TICKS(3000));
    ESP_ERROR_CHECK(esp_wifi_deinit());

    wfm->manual_stop = false;
    xEventGroupClearBits(wfm->eg, WFM_BIT_CONNECTED | WFM_BIT_FAIL | WFM_BIT_STARTED);
    wfm->mode = WFM_MODE_NONE;

    print_status(wfm, "Wi-Fi full driver stopped", WIFI_DISCONNECTED, true);
    return ESP_OK;
}






/* -------------------------------------------------------------------------- */
/*                   Public API — Change Wi-Fi Network                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Switches to a new Wi-Fi network atomically.
 *
 * This function disconnects from the current network, applies a new SSID and password,
 * and attempts to connect to the new network. If the connection fails, the manager
 * automatically reverts to the previous network.
 *
 * This ensures the driver is never left in a half-connected state.
 *
 * @param wfm        Wi-Fi Manager context.
 * @param new_ssid   Target SSID to connect to.
 * @param new_pass   Target password.
 * @param out_reason Optional pointer to receive the last disconnect reason.
 *
 * @return ESP_OK if switched successfully, ESP_FAIL if failed and reverted.
 */
esp_err_t wfm_change_network(wfm_t* wfm,
                             const char* new_ssid,
                             const char* new_pass,
                             wfm_disc_reason_e* out_reason)
{
    if (!wfm || !new_ssid || !new_pass)
        return ESP_ERR_INVALID_ARG;

    wfm->auto_reconnect = false;

    /* Backup previous credentials */
    char prev_ssid[WFM_SSID_MAX + 1] = {0};
    char prev_pass[WFM_PASS_MAX + 1] = {0};
    s_strcpy(prev_ssid, sizeof(prev_ssid), wfm->info.ssid);
    s_strcpy(prev_pass, sizeof(prev_pass), wfm->info.pass);

    wifi_config_t wc = {0};
    ESP_RETURN_ON_ERROR(esp_wifi_get_config(WIFI_IF_STA, &wc), TAG, "get_config");

    /* Disconnect current link */
    esp_wifi_disconnect();
    xEventGroupClearBits(wfm->eg, WFM_BIT_CONNECTED | WFM_BIT_FAIL);

    /* Apply new credentials */
    s_strcpy((char*)wc.sta.ssid, sizeof(wc.sta.ssid), new_ssid);
    s_strcpy((char*)wc.sta.password, sizeof(wc.sta.password), new_pass);
    ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wc), TAG, "set_config");

    ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "connect");

    /* Wait for connection result */
    EventBits_t bits = xEventGroupWaitBits(
        wfm->eg,
        WFM_BIT_CONNECTED | WFM_BIT_FAIL,
        pdTRUE, pdFALSE,
        pdMS_TO_TICKS(wfm->cfg.wifi_connect_timeout_ms)
    );

    if (bits & WFM_BIT_CONNECTED) {
        if (out_reason) *out_reason = WFM_DISC_NONE;
        print_status(wfm, "Switched to new Wi-Fi successfully", WIFI_NONE, false);
        wfm->auto_reconnect = true;
        return ESP_OK;
    }

    /* Failed — revert to previous network */
    s_strcpy((char*)wc.sta.ssid, sizeof(wc.sta.ssid), prev_ssid);
    s_strcpy((char*)wc.sta.password, sizeof(wc.sta.password), prev_pass);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_connect());

    (void)xEventGroupWaitBits(wfm->eg,
                              WFM_BIT_CONNECTED | WFM_BIT_FAIL,
                              pdTRUE, pdFALSE,
                              pdMS_TO_TICKS(wfm->cfg.wifi_connect_timeout_ms));

    if (out_reason) *out_reason = wfm->last_disc;

    if (!wfm->connected)
        wfm_reconnect(wfm);

    wfm->auto_reconnect = true;
    return ESP_FAIL;
}






/* -------------------------------------------------------------------------- */
/*                   Public API — Connection State Helpers                    */
/* -------------------------------------------------------------------------- */


/**
 * @brief Checks if the device is currently connected to a Wi-Fi network.
 */
bool wfm_is_connected(const wfm_t* wfm)
{
    if (!wfm) return false;
    return wfm->connected ? true : false;
}





/**
 * @brief Checks if a specific SSID is present in the last scan results.
 */
bool is_ssid_available(const wfm_t* wfm, const char* ssid)
{
    if (!wfm || !ssid) return false;

    for (uint8_t i = 0; i < wfm->scan.count; ++i) {
        if (strcmp(wfm->scan.aps[i].ssid, ssid) == 0)
            return true;
    }
    return false;
}






/* ========================================================================== */
/*                        Automatic Reconnect and Helpers                     */
/* ========================================================================== */

/**
 * @brief Attempts to reconnect automatically using known credentials.
 *
 * This function is called after an unexpected disconnection. It scans for
 * available networks and tries to reconnect to any of the saved SSIDs in
 * priority order. The process repeats up to `MAX_RECONNECT_ATTEMPTS` times.
 *
 * If all attempts fail, the function exits gracefully, leaving the driver
 * ready for future manual reconnection attempts.
 *
 * @param wfm Pointer to the Wi-Fi Manager context.
 * @return ESP_OK if reconnection succeeded, ESP_FAIL otherwise.
 */
esp_err_t wfm_reconnect(wfm_t* wfm)
{
    if (!wfm) return ESP_ERR_INVALID_ARG;

    wfm->auto_reconnect = false;
    print_status(wfm, "Auto-reconnect in progress", WIFI_NONE, true);

    for (int attempt = 0; attempt < MAX_RECONNECT_ATTEMPTS; attempt++) {
        print_status(wfm, "Attempting to reconnect...", WIFI_NONE, true);

        esp_err_t err = wfm_scan_sync(wfm);
        if (err != ESP_OK) {
            print_status(wfm, "Wi-Fi scan failed during reconnect", WIFI_NONE, true);
            return err;
        }

        wifi_config_t wc = {0};
        ESP_RETURN_ON_ERROR(esp_wifi_get_config(WIFI_IF_STA, &wc), TAG, "get_cfg");

        for (int i = -1; i < wfm->saved.count; ++i) {
            const char* ssid;
            const char* pass;

            if (i == -1) {
                /* First try reconnecting to the previously used network */
                ssid = wfm->info.ssid;
                pass = wfm->info.pass;
            } else {
                ssid = wfm->saved.creds[i].ssid;
                pass = wfm->saved.creds[i].pass;
            }

            if (!is_ssid_available(wfm, ssid)) continue;

            s_strcpy((char*)wc.sta.ssid, sizeof(wc.sta.ssid), ssid);
            s_strcpy((char*)wc.sta.password, sizeof(wc.sta.password), pass);

            ESP_RETURN_ON_ERROR(esp_wifi_set_config(WIFI_IF_STA, &wc), TAG, "reconnect_set_cfg");
            xEventGroupClearBits(wfm->eg, WFM_BIT_CONNECTED | WFM_BIT_FAIL);

            ESP_RETURN_ON_ERROR(esp_wifi_connect(), TAG, "reconnect_wifi_connect");

            EventBits_t bits = xEventGroupWaitBits(
                wfm->eg,
                WFM_BIT_CONNECTED | WFM_BIT_FAIL,
                pdTRUE, pdFALSE,
                pdMS_TO_TICKS(wfm->cfg.wifi_connect_timeout_ms)
            );

            if (bits & WFM_BIT_CONNECTED) {
                wfm->mode = WFM_MODE_STA;
                wfm->auto_reconnect = true;
                print_status(wfm, "Auto-reconnect succeeded", WIFI_NONE, false);
                return ESP_OK;
            } else {
                print_status(wfm, "Failed to reconnect", WIFI_NONE, false);
            }
        }
    }

    print_status(wfm, "Auto-reconnect failed after all attempts", WIFI_NONE, true);
    wfm->auto_reconnect = true;
    return ESP_FAIL;
}




/* -------------------------------------------------------------------------- */
/*                          Station Disconnect / Stop                         */
/* -------------------------------------------------------------------------- */

/**
 * @brief Gracefully disconnects from the current Wi-Fi network.
 *
 * This helper function issues a `esp_wifi_disconnect()` and waits for
 * the disconnection event to complete, using the internal EventGroup.
 * It does **not** destroy the STA interface.
 *
 * @param wfm Pointer to an initialized Wi-Fi Manager context.
 * @return ESP_OK on success, or ESP_FAIL if timeout or invalid state.
 */
static esp_err_t wfm_disconnect_sta(wfm_t* wfm)
{
    if (!wfm || wfm->mode != WFM_MODE_STA)
        return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Disconnecting STA...");

    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_disconnect() returned %s", esp_err_to_name(err));
        return err;
    }

    /* Wait for disconnect event or timeout */
    EventBits_t bits = xEventGroupWaitBits(
        wfm->eg,
        WFM_BIT_FAIL | WFM_BIT_STOPPED,
        pdTRUE, pdFALSE,
        pdMS_TO_TICKS(WIFI_DISCONNECT_TIMEOUT_MS)
    );

    if (bits & (WFM_BIT_FAIL | WFM_BIT_STOPPED)) {
        ESP_LOGI(TAG, "STA disconnected");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Timeout waiting for STA disconnect");
    return ESP_FAIL;
}

/**
 * @brief Stops the Wi-Fi Station mode completely.
 *
 * Ensures the STA driver is stopped, disconnecting any active connections
 * and waiting for confirmation from the event loop. This function does not
 * delete the netif, allowing quick restart if needed.
 *
 * @param wfm Pointer to Wi-Fi Manager context.
 * @return ESP_OK on success, or ESP_FAIL on error.
 */
static esp_err_t wfm_stop_sta(wfm_t* wfm)
{
    if (!wfm)
        return ESP_ERR_INVALID_ARG;

    ESP_LOGI(TAG, "Stopping STA mode...");
    wfm_disconnect_sta(wfm);

    esp_err_t err = esp_wifi_stop();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_stop() returned %s", esp_err_to_name(err));
        return err;
    }

    EventBits_t bits = xEventGroupWaitBits(
        wfm->eg,
        WFM_BIT_STOPPED,
        pdTRUE, pdFALSE,
        pdMS_TO_TICKS(WIFI_STOP_TIMEOUT_MS)
    );

    if (bits & WFM_BIT_STOPPED) {
        ESP_LOGI(TAG, "STA stopped successfully");
        return ESP_OK;
    }

    ESP_LOGW(TAG, "Timeout waiting for STA stop");
    return ESP_FAIL;
}




/* -------------------------------------------------------------------------- */
/*                     Asynchronous Auto-Reconnect Task                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Background FreeRTOS task that performs auto-reconnect asynchronously.
 *
 * This avoids blocking the ESP event loop and ensures Wi-Fi events continue
 * to flow while the reconnect logic runs in the background.
 *
 * @param arg Pointer to the Wi-Fi Manager context.
 */
static void wfm_reconnect_task(void* arg){
    wfm_t* wfm = (wfm_t*)arg;
    if (!wfm) {
        vTaskDelete(NULL);
        return;
    }

    print_status(wfm, "Auto-reconnect task started", WIFI_NONE, false);
    vTaskDelay(pdMS_TO_TICKS(500));

    esp_err_t res = wfm_reconnect(wfm);
    if (res == ESP_OK)
        print_status(wfm, "Auto-reconnect success", WIFI_NONE, false);
    else
        print_status(wfm, "Auto-reconnect failed", WIFI_NONE, false);

    vTaskDelete(NULL);
}




/* -------------------------------------------------------------------------- */
/*                        Internal Logging and JSON Utils                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief Emits a user-facing status message through the callback and log output.
 *
 * @param wfm             Pointer to the Wi-Fi Manager context.
 * @param msg             Text message to log.
 * @param connection_status  Enum describing the current connection state.
 * @param update_device   Whether to notify the user callback (e.g., LCD/LED).
 */
static void print_status(const wfm_t* wfm,
                         const char* msg,
                         wifi_status_t connection_status,
                         bool update_device)
{
    if (wfm->cbs.on_status && update_device)
        wfm->cbs.on_status(msg, connection_status);

    ESP_LOGI(TAG, "%s", msg);
}




/**
 * @brief Converts the access point list to JSON and sends it to the callback.
 *
 * Example output:
 * @code
 * [
 *   {"ssid":"MyHomeWiFi","rssi":-42},
 *   {"ssid":"GuestNet","rssi":-67}
 * ]
 * @endcode
 */
static void convert_AP_list_to_JSON(const wfm_t* wfm, const wfm_scan_list_t* list)
{
    if (!wfm->cbs.on_scan_json) return;

    cJSON* root = cJSON_CreateArray();
    if (!root) return;

    for (uint8_t i = 0; i < list->count; ++i) {
        cJSON* o = cJSON_CreateObject();
        if (!o) continue;
        cJSON_AddStringToObject(o, "ssid", list->aps[i].ssid);
        cJSON_AddNumberToObject(o, "rssi", list->aps[i].rssi);
        cJSON_AddItemToArray(root, o);
    }

    char* json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        wfm->cbs.on_scan_json(json_str);
        cJSON_free(json_str);
    }

    cJSON_Delete(root);
}





/* -------------------------------------------------------------------------- */
/*                     Internal Netif Creation and Cleanup                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Ensures that a default STA network interface exists.
 */
static esp_err_t is_sta_netif_created(wfm_t* wfm)
{
    if (wfm->sta_netif) return ESP_OK;

    wfm->sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (!wfm->sta_netif)
        wfm->sta_netif = esp_netif_create_default_wifi_sta();

    return wfm->sta_netif ? ESP_OK : ESP_FAIL;
}




/**
 * @brief Destroys the STA network interface if it exists.
 */
static esp_err_t destroy_sta_netif(wfm_t* wfm)
{
    if (wfm->sta_netif) {
        esp_netif_destroy(wfm->sta_netif);
        wfm->sta_netif = NULL;
    }
    return ESP_OK;
}





/**
 * @brief Ensures that a default AP network interface exists.
 */
static esp_err_t is_ap_netif_created(wfm_t* wfm)
{
    if (wfm->ap_netif) return ESP_OK;

    wfm->ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (!wfm->ap_netif)
        wfm->ap_netif = esp_netif_create_default_wifi_ap();

    return wfm->ap_netif ? ESP_OK : ESP_FAIL;
}





/**
 * @brief Destroys the AP network interface if it exists.
 */
static esp_err_t destroy_ap_netif(wfm_t* wfm)
{
    if (wfm->ap_netif) {
        esp_netif_destroy(wfm->ap_netif);
        wfm->ap_netif = NULL;
    }
    return ESP_OK;
}