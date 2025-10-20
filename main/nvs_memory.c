/**
 * @file nvs_memory.c
 * @brief NVS (Non-Volatile Storage) Manager for Wi-Fi credentials.
 *
 * ## Overview
 * This module provides persistent storage for Wi-Fi credentials (SSID and password)
 * using ESP-IDF’s NVS (Non-Volatile Storage) API.
 * It supports initialization, retrieval, addition, update, and removal
 * of saved Wi-Fi networks.
 *
 * ## Features
 *  - Automatic NVS re-initialization on version mismatch.
 *  - Safe blob-based storage of credential list.
 *  - Simple add/update/remove operations.
 *  - Memory-safe handling using bounded string operations.
 *
 * ## Dependencies
 *  - `nvs_flash.h` / `nvs.h`
 *  - `esp_log.h`
 *  - `util.h` for helper macros like `RETURN_IF_ERROR()`
 *  - `wifi_manager.h` for `wfm_cred_list_t` definition
 *
 * @note
 *  - All APIs are thread-safe when called from a single application task.
 *  - Wi-Fi credentials are stored under key `"wifi_list"`.
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 * @date
 *  Created on: 19/09/2025
 */

#include "nvs_memory.h"

#include <nvs_flash.h>

#include "esp_log.h"
#include <string.h>
#include "util.h"



/* -------------------------------------------------------------------------- */
/*                               Static globals                               */
/* -------------------------------------------------------------------------- */

static const char* TAG = "NVS_MEM";



/* -------------------------------------------------------------------------- */
/*                          Initialization / Setup                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the NVS memory subsystem and open a storage folder.
 *
 * If NVS is uninitialized or found with an incompatible version,
 * it will automatically erase and reinitialize the flash partition.
 *
 * @param[out] nvs_handler Pointer to an NVS handle (output parameter).
 * @param[in]  folder_name  Name of the NVS namespace (folder) to open.
 *
 * @return
 *  - ESP_OK on success
 *  - ESP_FAIL or ESP_ERR_NVS_xxx error on failure
 */
esp_err_t init_NVS_memory(nvs_handle_t *nvs_handler, const char *folder_name)
{
    esp_err_t err = nvs_flash_init();

    /* Handle case of missing or mismatched NVS pages */
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    RETURN_IF_ERROR(err);
    RETURN_IF_ERROR(nvs_open(folder_name, NVS_READWRITE, nvs_handler));

    ESP_LOGI(TAG, "NVS folder successfully opened");
    return ESP_OK;
}



/* -------------------------------------------------------------------------- */
/*                             Read stored data                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Retrieve the saved Wi-Fi credentials list from NVS.
 *
 * @param[out] out         Pointer to a `wfm_cred_list_t` structure that will receive the data.
 * @param[in]  nvs_handler Open NVS handle obtained from `init_NVS_memory()`.
 *
 * @return
 *  - ESP_OK if Wi-Fi credentials were loaded successfully
 *  - ESP_FAIL if no credentials exist or read failed
 */
esp_err_t get_wifi_creds_from_NVS_memory(wfm_cred_list_t *out, const nvs_handle_t nvs_handler)
{
    if (!out) {
        ESP_LOGE(TAG, "Output parameter is NULL");
        return ESP_FAIL;
    }

    memset(out, 0, sizeof(*out));

    size_t sz = sizeof(*out);
    esp_err_t err = nvs_get_blob(nvs_handler, WIFI_LIST_KEY, out, &sz);

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No Wi-Fi credentials stored yet");
        return ESP_OK;
    }
    else if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_blob failed (%s)", esp_err_to_name(err));
        memset(out, 0, sizeof(*out));
        return ESP_FAIL;
    }

    return ESP_OK;
}



/* -------------------------------------------------------------------------- */
/*                         Add / Update stored data                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Add or update a Wi-Fi credential entry in NVS.
 *
 * If the SSID already exists, its password will be updated.
 * Otherwise, a new entry will be appended to the list.
 *
 * @param[in] ssid        Null-terminated SSID string.
 * @param[in] password    Null-terminated password string.
 * @param[in] nvs_handler Open NVS handle.
 */
void add_wifi_creds_to_NVS_memory(const char* ssid, const char* password, const nvs_handle_t nvs_handler)
{
    if (!ssid || !*ssid)
        return;

    wfm_cred_list_t list = {0};
    get_wifi_creds_from_NVS_memory(&list, nvs_handler);

    /* Check if SSID already exists → update password */
    for (uint8_t i = 0; i < list.count; ++i) {
        if (strcmp(list.creds[i].ssid, ssid) == 0) {
            strlcpy(list.creds[i].pass, password, sizeof(list.creds[i].pass));
            goto save;
        }
    }

    /* Add new entry if capacity allows */
    if (list.count < WFM_MAX_CREDS) {
        strlcpy(list.creds[list.count].ssid, ssid, sizeof(list.creds[list.count].ssid));
        strlcpy(list.creds[list.count].pass, password, sizeof(list.creds[list.count].pass));
        list.count++;
    }

save:
    ESP_ERROR_CHECK(nvs_set_blob(nvs_handler, WIFI_LIST_KEY, &list, sizeof(list)));
    ESP_ERROR_CHECK(nvs_commit(nvs_handler));
    ESP_LOGI(TAG, "Wi-Fi credentials updated in NVS");
}



/* -------------------------------------------------------------------------- */
/*                          Remove specific credential                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Remove a specific Wi-Fi credential from NVS by SSID.
 *
 * Searches the stored list for the given SSID and removes it,
 * shifting entries down to fill the gap.
 *
 * @param[in] ssid        Null-terminated SSID string to remove.
 * @param[in] nvs_handler Open NVS handle.
 */
void remove_wifi_creds_from_NVS_memory(const char* ssid, const nvs_handle_t nvs_handler)
{
    if (!ssid || !*ssid)
        return;

    wfm_cred_list_t list = {0};
    get_wifi_creds_from_NVS_memory(&list, nvs_handler);

    for (uint8_t i = 0; i < list.count; ++i) {
        if (strcmp(list.creds[i].ssid, ssid) == 0) {
            /* Replace with last entry, clear the tail */
            list.creds[i] = list.creds[list.count - 1];
            memset(&list.creds[list.count - 1], 0, sizeof(list.creds[0]));
            list.count--;
            break;
        }
    }

    ESP_ERROR_CHECK(nvs_set_blob(nvs_handler, WIFI_LIST_KEY, &list, sizeof(list)));
    ESP_ERROR_CHECK(nvs_commit(nvs_handler));
    ESP_LOGI(TAG, "Removed SSID '%s' from NVS", ssid);
}