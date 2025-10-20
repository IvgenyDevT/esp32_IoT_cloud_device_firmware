/**
 * @file nvs_memory.h
 * @brief Persistent storage manager for Wi-Fi credentials using ESP-IDF NVS.
 *
 * ## Overview
 * This module abstracts ESP-IDF’s Non-Volatile Storage (NVS) API
 * to provide a simple and reliable interface for storing Wi-Fi credentials.
 *
 * It allows:
 *  - Initializing the NVS partition and opening a namespace (folder).
 *  - Saving, updating, and removing Wi-Fi credentials (SSID & password).
 *  - Loading saved credential lists at runtime.
 *
 * ## Features
 *  - Uses binary blob storage for multiple credentials.
 *  - Automatically handles NVS version mismatch or full partition.
 *  - Simple interface designed for embedded applications.
 *
 * @note
 *  This module depends on `WiFi_manager.h` for `wfm_cred_list_t`
 *  and related definitions.
 *
 * @dependencies
 *  - `nvs_flash.h`
 *  - `nvs.h`
 *  - `esp_err.h`
 *  - `WiFi_manager.h`
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 */

#ifndef NVS_MEMORY_H
#define NVS_MEMORY_H

#include "nvs.h"
#include "WiFi_manager.h"   /**< For wfm_cred_list_t definition */


/* -------------------------------------------------------------------------- */
/*                                   Defines                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief Key name used to store the Wi-Fi credential list inside NVS.
 */
#define WIFI_LIST_KEY "wifi_list"



/* -------------------------------------------------------------------------- */
/*                               Initialization                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the NVS partition and open a folder (namespace).
 *
 * If the NVS is not formatted or version-mismatched, it will automatically
 * erase and reinitialize the flash partition.
 *
 * @param[out] nvs_handler Pointer to an `nvs_handle_t` that will receive the handle.
 * @param[in]  folder_name Name of the NVS namespace to open.
 *
 * @return
 *  - ESP_OK on success  
 *  - ESP_FAIL or other ESP_ERR_NVS_* error code on failure.
 */
esp_err_t init_NVS_memory(nvs_handle_t *nvs_handler, const char *folder_name);



/**
 * @brief Open a specific NVS folder (namespace).
 *
 * @param[in] folder_name Name of the namespace to open.
 */
void NVS_open_folder(const char* folder_name);



/* -------------------------------------------------------------------------- */
/*                             Read / Write APIs                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Save or update a Wi-Fi credential in NVS.
 *
 * If the SSID already exists, the password is updated.
 * Otherwise, the new credential is added to the list.
 *
 * @param[in] ssid         Null-terminated SSID string.
 * @param[in] password     Null-terminated password string.
 * @param[in] nvs_handler  Open NVS handle.
 */
void add_wifi_creds_to_NVS_memory(const char* ssid, const char* password, nvs_handle_t nvs_handler);



/**
 * @brief Retrieve all saved Wi-Fi credentials from NVS.
 *
 * @param[out] out         Pointer to an allocated `wfm_cred_list_t` structure.
 * @param[in]  nvs_handler Open NVS handle.
 *
 * @return
 *  - ESP_OK if credentials were loaded successfully.  
 *  - ESP_FAIL if no data found or read failed.
 */
esp_err_t get_wifi_creds_from_NVS_memory(wfm_cred_list_t *out, nvs_handle_t nvs_handler);



/* -------------------------------------------------------------------------- */
/*                                 Deletion                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Remove a Wi-Fi credential by SSID.
 *
 * Searches the stored list for the given SSID and removes it if found.
 *
 * @param[in] ssid         Null-terminated SSID string to remove.
 * @param[in] nvs_handler  Open NVS handle.
 */
void remove_wifi_creds_from_NVS_memory(const char* ssid, nvs_handle_t nvs_handler);



#endif /* NVS_MEMORY_H */