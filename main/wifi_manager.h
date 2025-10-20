#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

/**
 * @file wifi_manager.h
 * @brief High-level Wi-Fi Manager for ESP-IDF (ESP32 / ESP32-S2)
 *
 * This module defines the API, configuration structures, and callbacks
 * for managing Wi-Fi connections in both Station (STA) and Access Point (AP) modes.
 *
 * ## Features
 * - Deterministic, event-driven state machine.
 * - Automatic reconnect and scan management.
 * - Integrated support for persistent credentials.
 * - Safe string handling with bounded buffers.
 * - Synchronous and asynchronous operations with clear return codes.
 *
 * ## Typical usage:
 * ```c
 * wfm_t wfm;
 * wfm_cred_list_t creds;
 *
 * get_wifi_creds_from_NVS_memory(&creds, nvs_handler);
 * wfm_init(&wfm, &creds, NULL, &wifi_cbs);
 *
 * if (wfm_first_connect(&wfm) != ESP_OK)
 *     wfm_start_ap(&wfm, "MyDevice_AP", "12345678");
 * ```
 */

/* -------------------------------------------------------------------------- */
/*                         CONSTANTS AND LIMITS                               */
/* -------------------------------------------------------------------------- */

#define WFM_MAX_CREDS               15
#define WFM_SSID_MAX                32
#define WFM_PASS_MAX                64
#define WFM_SCAN_MAX                32

#define STA_LISTEN_INTERVAL         3
#define WIFI_CONNECT_TIMEOUT_MS     30000
#define WIFI_STOP_TIMEOUT_MS        10000
#define MAX_RECONNECT_ATTEMPTS      5
#define WIFI_DISCONNECT_TIMEOUT_MS  5000

#define WIFI_AP_CHANNELS            6
#define WIFI_AP_MAX_CONNECTIONS     4

#define PMF_CAPABLE                 true
#define PMF_REQUIRED                false
#define STA_AUTH_MODE               WIFI_AUTH_WPA2_PSK




/* -------------------------------------------------------------------------- */
/*                             ENUMERATIONS                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Connection status codes used in callbacks.
 */
typedef enum {
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_DISCONNECTING,
    WIFI_DISCONNECTED,
    WIFI_ERROR,
    WIFI_NONE
} wifi_status_t;



/**
 * @brief Internal event bits used for synchronization.
 */
typedef enum {
    WFM_BIT_STARTED    = BIT0,
    WFM_BIT_CONNECTED  = BIT1,
    WFM_BIT_FAIL       = BIT2,
    WFM_BIT_STOPPED    = BIT3,
    WFM_BIT_SCANDONE   = BIT4,
} wfm_bits_e;



/**
 * @brief Disconnection reasons reported by the Wi-Fi Manager.
 */
typedef enum {
    WFM_DISC_NONE = 0,
    WFM_DISC_WRONG_PASSWORD,
    WFM_DISC_NO_AP,
    WFM_DISC_OTHER,
} wfm_disc_reason_e;



/**
 * @brief Operating mode of the Wi-Fi Manager.
 */
typedef enum {
    WFM_MODE_NONE = 0,
    WFM_MODE_STA,
    WFM_MODE_AP
} wfm_mode_e;




/* -------------------------------------------------------------------------- */
/*                        STRUCTURE DEFINITIONS                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Structure holding a single Wi-Fi credential pair.
 */
typedef struct {
    char ssid[WFM_SSID_MAX + 1];  /**< SSID string (null-terminated). */
    char pass[WFM_PASS_MAX + 1];  /**< Password string (null-terminated). */
} wfm_cred_t;



/**
 * @brief List of Wi-Fi credentials stored in memory.
 */
typedef struct {
    wfm_cred_t creds[WFM_MAX_CREDS]; /**< Array of credential entries. */
    uint8_t    count;                /**< Number of valid entries in the list. */
} wfm_cred_list_t;




/**
 * @brief Scan result entry for a single access point.
 */
typedef struct {
    char   ssid[WFM_SSID_MAX + 1]; /**< Access point SSID. */
    int8_t rssi;                   /**< Signal strength (RSSI in dBm). */
} wfm_scan_ap_t;




/**
 * @brief List of unique access points found during scan.
 */
typedef struct {
    wfm_scan_ap_t aps[WFM_SCAN_MAX]; /**< Unique SSIDs with strongest RSSI. */
    uint8_t       count;             /**< Number of valid entries. */
} wfm_scan_list_t;



/**
 * @brief Snapshot of current connection information.
 */
typedef struct {
    char ssid[WFM_SSID_MAX + 1]; /**< Connected SSID. */
    char pass[WFM_PASS_MAX + 1]; /**< Connected password. */
    char ip[16];                  /**< Current IP address as string. */
    char mac[18];                 /**< MAC address string (e.g. "AA:BB:CC:DD:EE:FF"). */
    char rssi[8];                 /**< RSSI as string. */
} wfm_conn_info_t;




/**
 * @brief Optional callback set for user interaction.
 */
typedef struct {
    /**
     * @brief Called when Wi-Fi scan results are available in JSON format.
     * @param json_null_terminated JSON string representing available networks.
     */
    void (*on_scan_json)(const char* json_null_terminated);

    /**
     * @brief Called on Wi-Fi status changes (connecting, connected, error, etc.).
     * @param text Descriptive message for the current status.
     * @param wifi_status Current Wi-Fi connection state.
     */
    void (*on_status)(const char* text, wifi_status_t wifi_status);
} wfm_callbacks_t;




/**
 * @brief Configuration for Wi-Fi Manager behavior and timing.
 */
typedef struct {
    uint16_t sta_listen_interval;       /**< Beacon listen interval in STA mode. */
    uint32_t wifi_connect_timeout_ms;   /**< Timeout for connecting to a network. */
    uint32_t wifi_stop_timeout_ms;      /**< Timeout for stopping the driver. */
    uint32_t scan_active_min_ms;        /**< Minimum scan dwell time per channel. */
    uint32_t scan_active_max_ms;        /**< Maximum scan dwell time per channel. */
    uint8_t  scan_channel;              /**< Channel number to scan (0 = all). */
    bool     allow_hidden;              /**< Whether to include hidden SSIDs. */
    uint8_t  max_reconnect_attempts;    /**< Maximum reconnect attempts on failure. */
} wfm_config_t;



/**
 * @brief Main Wi-Fi Manager context structure.
 *
 * All state information is stored here.
 * Applications should allocate one static instance and pass it
 * to all Wi-Fi manager API calls.
 */
typedef struct {
    wfm_mode_e          mode;          /**< Current operating mode (STA/AP). */
    wfm_disc_reason_e   last_disc;     /**< Last disconnect reason. */
    wfm_conn_info_t     info;          /**< Connection info snapshot. */
    wfm_scan_list_t     scan;          /**< Last scan results. */
    wfm_cred_list_t     saved;         /**< Saved credentials list. */

    bool                connected;     /**< Current connection status. */
    bool                started;       /**< Whether the driver is started. */

    EventGroupHandle_t  eg;            /**< Event group for synchronization. */
    esp_netif_t*        sta_netif;     /**< STA interface handle. */
    esp_netif_t*        ap_netif;      /**< AP interface handle. */

    esp_event_handler_instance_t evt_wifi; /**< Wi-Fi event handler. */
    esp_event_handler_instance_t evt_ip;   /**< IP event handler. */

    TaskHandle_t        reconnect_task; /**< Reconnect task handle. */

    bool                auto_reconnect;    /**< Automatic reconnect flag. */
    bool                connect_on_start;  /**< Connect immediately after start. */
    bool                manual_stop;       /**< Indicates user-requested stop. */

    wfm_config_t        cfg;           /**< Configuration settings. */
    wfm_callbacks_t     cbs;           /**< Callback set for notifications. */
} wfm_t;


/*- - - - -  WIFI scan config - - - -*/
#define WIFI_SCAN_SSID 0
#define WIFI_SCAN_BSSID 0
#define WIFI_SCAN_CHANNEL 0
#define WIFI_SCAN_SHOW_HIDDEN false
#define WIFI_SCAN_TYPE WIFI_SCAN_TYPE_ACTIVE

#define PMF_CAPABLE true

#define PMF_REQUIRED false

#define STA_AUTH_MODE WIFI_AUTH_WPA2_PSK
#define STA_SORT_METHOD WIFI_CONNECT_AP_BY_SIGNAL
#define STA_SCAN_METHOD WIFI_ALL_CHANNEL_SCAN

#define WIFI_SCAN_TIME_MIN 50
#define WIFI_SCAN_TIME_max 120

#define WIFI_AP_CHANNELS 6
#define WIFI_AP_MAX_CONNECTIONS 4

/* -------------------------------------------------------------------------- */
/*                             PUBLIC API                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the Wi-Fi Manager module.
 *
 * This function prepares all internal structures, registers event handlers,
 * and initializes `esp_netif`. It must be called before any other Wi-Fi API.
 *
 * @param wfm   Pointer to an allocated `wfm_t` context (should be zeroed).
 * @param saved Optional pointer to stored Wi-Fi credentials (copied internally).
 * @param cfg   Optional configuration overrides (NULL = defaults used).
 * @param cbs   Optional callbacks for status and scan updates (copied internally).
 *
 * @return ESP_OK on success,
 *         ESP_ERR_INVALID_ARG if `wfm` is NULL,
 *         or ESP_FAIL on internal initialization error.
 */
esp_err_t wfm_init(wfm_t* wfm,const wfm_cred_list_t* saved,const wfm_config_t* cfg,const wfm_callbacks_t* cbs);




/**
 * @brief Deinitialize and clean up Wi-Fi Manager resources.
 *
 * This function stops the Wi-Fi driver, unregisters event handlers,
 * and clears the internal state of the context.
 *
 * @param wfm Pointer to an initialized `wfm_t` context.
 */
void wfm_deinit(wfm_t* wfm);




/**
 * @brief Perform a synchronous Wi-Fi network scan.
 *
 * Populates the internal scan list (`wfm->scan`) with unique SSIDs and
 * strongest RSSI values. Blocks until the scan is complete.
 *
 * @param wfm Pointer to initialized Wi-Fi Manager context.
 * @return ESP_OK on success, or an appropriate error code on failure.
 */
esp_err_t wfm_scan_sync(wfm_t* wfm);




/**
 * @brief Attempt connection using saved credentials.
 *
 * Performs a scan and tries connecting to each saved SSID in order until
 * one succeeds or all fail.
 *
 * @param wfm Pointer to Wi-Fi Manager context.
 * @return ESP_OK if connection established, ESP_FAIL otherwise.
 */
esp_err_t wfm_first_connect(wfm_t* wfm);




/**
 * @brief Fully stop and deinitialize the Wi-Fi driver.
 *
 * Used when switching between STA and AP modes or before restarting the system.
 *
 * @param wfm Pointer to Wi-Fi Manager context.
 * @return ESP_OK always (best effort stop).
 */
esp_err_t wfm_full_driver_stop(wfm_t* wfm);




/**
 * @brief Start a Wi-Fi access point (SoftAP mode).
 *
 * This will stop any existing STA session and create a new AP network.
 *
 * @param wfm  Pointer to Wi-Fi Manager context.
 * @param ssid SSID for the SoftAP.
 * @param pass Password for the SoftAP (empty string for open network).
 *
 * @return ESP_OK on success, or ESP_FAIL on initialization failure.
 */
esp_err_t wfm_start_ap(wfm_t* wfm, const char* ssid, const char* pass);




/**
 * @brief Stop the currently running SoftAP.
 *
 * Deinitializes the Wi-Fi driver and destroys the AP network interface.
 *
 * @param wfm Pointer to Wi-Fi Manager context.
 * @return ESP_OK on success, ESP_FAIL otherwise.
 */
esp_err_t wfm_stop_ap(wfm_t* wfm);




/**
 * @brief Switch to a new Wi-Fi network dynamically.
 *
 * Disconnects from the current network, applies new credentials,
 * and reconnects. If the new connection fails, the previous network
 * is automatically restored.
 *
 * @param wfm        Pointer to Wi-Fi Manager context.
 * @param new_ssid   Target SSID to connect to.
 * @param new_pass   Target password.
 * @param out_reason Optional pointer to receive last disconnect reason.
 *
 * @return ESP_OK if switched successfully,
 *         ESP_FAIL if failed and reverted,
 *         or ESP_ERR_INVALID_ARG on invalid parameters.
 */
esp_err_t wfm_change_network(wfm_t* wfm,const char* new_ssid,const char* new_pass, wfm_disc_reason_e* out_reason);



/**
 * @brief Check if currently connected to a Wi-Fi network.
 *
 * @param wfm Pointer to Wi-Fi Manager context.
 * @return true if connected, false otherwise.
 */
bool wfm_is_connected(const wfm_t* wfm);




/**
 * @brief Check if a given SSID is available from the last scan.
 *
 * @param wfm  Pointer to Wi-Fi Manager context.
 * @param ssid Target SSID to look for.
 * @return true if found, false otherwise.
 */
bool is_ssid_available(const wfm_t* wfm, const char* ssid);



/**
 * @brief Attempt automatic reconnection to known networks.
 *
 * This function performs multiple scans and attempts to reconnect
 * using saved credentials. It is typically triggered internally
 * when connection loss occurs.
 *
 * @param wfm Pointer to Wi-Fi Manager context.
 * @return ESP_OK if reconnection succeeds, ESP_FAIL otherwise.
 */
esp_err_t wfm_reconnect(wfm_t* wfm);