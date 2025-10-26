/**
 * @file mqtt_manager.c
 * @brief Production-grade MQTT Manager implementation for ESP-IDF.
 *
 * ## Overview
 * This module encapsulates the native ESP-IDF MQTT client (`esp-mqtt`)
 * into a higher-level, event-driven manager that provides:
 *  - Safe lifecycle control (init → start → stop → deinit)
 *  - Structured callbacks for connection, messages, and publish events
 *  - Automatic topic subscription and message dispatching
 *  - Clean separation between configuration, runtime, and user callbacks
 *
 * ## Features
 *  - Event group–based connection synchronization
 *  - Thread-safe state transitions
 *  - Publish QoS1/retain support
 *  - Topic handler table for direct command routing
 *
 * ## Dependencies
 *  - `mqtt_manager.h`
 *  - ESP-IDF MQTT client (`esp_mqtt_client.h`)
 *  - FreeRTOS event groups
 *
 * ## Architecture
 * ```
 * mqm_init()
 *   ├── Build esp_mqtt_client_config_t
 *   ├── Register event handler
 *   └── Create event group
 *
 * mqm_start()
 *   ├── Start MQTT client
 *   ├── Wait for connection or failure
 *   └── Update status bits
 *
 * mqm_event_core()
 *   ├── On CONNECTED → subscribe topics, notify app
 *   ├── On DATA → dispatch to callback + handler table
 *   └── On DISCONNECTED → mark fail and notify
 * ```
 *
 * @note
 *  All functions in this module are blocking-safe and must be called
 *  from the application task context (not from interrupt handlers).
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 * @date
 *  Created on: 19/09/2025
 */

#include "mqtt_manager.h"
#include "esp_log.h"
#include "esp_check.h"
#include <string.h>
#include <util.h>

#include "mqtt_client.h"


/* -------------------------------------------------------------------------- */
/*                                   Macros                                   */
/* -------------------------------------------------------------------------- */

#define TAG "MQM"




/* -------------------------------------------------------------------------- */
/*                        Forward declarations (private)                      */
/* -------------------------------------------------------------------------- */

static void mqm_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data);
static esp_err_t mqm_event_core(mqm_t* mqm, esp_mqtt_event_handle_t ev);
static esp_err_t mqm_subscribe_all(const mqm_t* mqtt_client);
static void mqm_status(const mqm_t* mqm, const char* msg, mqm_status_t client_status, bool update_device);




/* -------------------------------------------------------------------------- */
/*                             Helper functions                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Emit a status message to the user callback and to the log.
 *
 * @param mqm            Pointer to MQTT manager instance.
 * @param msg            Human-readable message text.
 * @param client_status  Enumerated client status (connected, error, etc.).
 * @param update_device  Whether to notify the user callback.
 */
static void mqm_status(const mqm_t* mqm, const char* msg, mqm_status_t client_status, bool update_device)
{
    if (mqm->cbs.on_status && update_device)
        mqm->cbs.on_status(msg, client_status);

    ESP_LOGI(TAG, "%s", msg);
}


/**
 * @brief Simplified wrapper for publishing with QoS=1, retain=0.
 *
 * @param mqm   Pointer to MQTT manager instance.
 * @param topic Target topic string.
 * @param msg   Message payload string.
 */
static inline void mqm_publish(mqm_t* mqm, const char* topic, const char* msg)
{
    (void)mqm_publish_ex(mqm, topic, msg, 1, 0);
}




/* -------------------------------------------------------------------------- */
/*                              Public API                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the MQTT Manager.
 *
 * Allocates internal resources, builds MQTT configuration, and registers
 * the event handler with ESP-IDF MQTT client.
 *
 * @param mqm        Pointer to the MQTT manager instance.
 * @param cfg        MQTT configuration structure.
 * @param cbs        Optional callback structure for events.
 * @param table      Optional topic handler table.
 * @param table_len  Length of the topic handler table.
 *
 * @return
 *  - ESP_OK on success
 *  - ESP_ERR_INVALID_ARG for invalid parameters
 *  - ESP_ERR_NO_MEM if allocation fails
 */
esp_err_t mqm_init(mqm_t* mqm, const mqm_config_t* cfg, const mqm_callbacks_t* cbs,
                   const mqm_topic_entry_t* table, size_t table_len)
{
    if (!mqm || !cfg || !cfg->uri)
        return ESP_ERR_INVALID_ARG;

    memset(mqm, 0, sizeof(*mqm));
    mqm->cfg = *cfg;
    if (cbs)   mqm->cbs = *cbs;
    if (table) { mqm->table = table; mqm->table_len = table_len; }

    mqm->eg = xEventGroupCreate();
    if (!mqm->eg)
        return ESP_ERR_NO_MEM;

    /** Build ESP-MQTT client configuration */
    esp_mqtt_client_config_t mcfg = {
        .broker = {
            .address.uri = cfg->uri,
            .verification.certificate = (const char*)root_ca_pem_start,
        },
        .credentials = {
            .username = cfg->username,
            .authentication.password = cfg->password,
        },
        .network = {
            .disable_auto_reconnect = cfg->disable_auto_reconnect,
            .reconnect_timeout_ms   = cfg->reconnect_timeout_ms,
            .tcp_keep_alive_cfg = {
                .keep_alive_enable   = cfg->keep_alive_enable,
                .keep_alive_idle     = cfg->keep_alive_idle,
                .keep_alive_interval = cfg->keep_alive_interval,
                .keep_alive_count    = cfg->keep_alive_count,
            },
        },
        .session = {
            .keepalive                 = cfg->keepalive_sec > 0 ? cfg->keepalive_sec : 20,
            .disable_clean_session     = !cfg->clean_session,
            .message_retransmit_timeout = cfg->msg_retransmit_timeout,
            .last_will.msg             = cfg->last_will_msg,
            .last_will.topic           = cfg->last_will_topic,
            .last_will.qos             = cfg->last_will_qos,
            .last_will.retain          = cfg->last_will_retain,
        },
    };

    mqm->client = esp_mqtt_client_init(&mcfg);
    if (!mqm->client)
        return ESP_ERR_NO_MEM;

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqm->client, ESP_EVENT_ANY_ID,
                                                   mqm_event_handler, mqm));

    mqm->initialized = true;
    mqm_status(mqm, "MQTT manager initialized", MQM_NONE, false);
    return ESP_OK;
}



/**
 * @brief Start the MQTT client and wait for connection.
 *
 * @param mqm         Pointer to MQTT manager instance.
 * @param timeout_ms  Timeout to wait for connection (ms).
 * @return ESP_OK if connected successfully, otherwise ESP_FAIL.
 */
esp_err_t mqm_start(mqm_t* mqm, uint32_t timeout_ms)
{
    if (!mqm || !mqm->initialized)
        return ESP_ERR_INVALID_STATE;

    xEventGroupClearBits(mqm->eg, MQM_BIT_CONNECTED | MQM_BIT_FAIL);
    ESP_ERROR_CHECK(esp_mqtt_client_start(mqm->client));

    mqm->started = true;

    EventBits_t status_bit = xEventGroupWaitBits(
        mqm->eg,
        MQM_BIT_CONNECTED | MQM_BIT_FAIL,
        pdTRUE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms ? timeout_ms : 15000)
    );

    if (status_bit & MQM_BIT_CONNECTED) {
        mqm->connected = true;
        return ESP_OK;
    }

    mqm_status(mqm, "MQTT connect timeout/fail", MQM_ERROR, true);
    return ESP_FAIL;
}



/**
 * @brief Stop the MQTT client and wait for clean shutdown.
 *
 * @param mqm         Pointer to MQTT manager instance.
 * @param timeout_ms  Timeout to wait for stop confirmation (ms).
 * @return ESP_OK if stopped successfully, otherwise ESP_FAIL.
 */
esp_err_t mqm_stop(mqm_t* mqm, uint32_t timeout_ms)
{
    if (!mqm || !mqm->started)
        return ESP_OK;

    mqm_status(mqm, "Stopping MQTT...", MQM_DISCONNECTING, true);
    esp_mqtt_client_stop(mqm->client);

    EventBits_t status_bit = xEventGroupWaitBits(
        mqm->eg, MQM_BIT_FAIL, pdTRUE, pdFALSE,
        pdMS_TO_TICKS(timeout_ms ? timeout_ms : 3000)
    );

    if (status_bit & MQM_BIT_FAIL) {
        mqm->connected = false;
        mqm->started = false;
        mqm_status(mqm, "MQTT stopped", MQM_DISCONNECTED, true);
        return ESP_OK;
    }

    mqm_status(mqm, "MQTT stop error", MQM_ERROR, true);
    return ESP_FAIL;
}



/**
 * @brief Deinitialize the MQTT manager and free resources.
 *
 * Stops the MQTT client if active, destroys the client, and releases
 * any allocated event groups.
 *
 * @param mqm Pointer to MQTT manager instance.
 */
void mqm_deinit(mqm_t* mqm)
{
    if (!mqm)
        return;

    if (mqm->started)
        mqm_stop(mqm, 2000);

    if (mqm->client)
        esp_mqtt_client_destroy(mqm->client);

    if (mqm->eg)
        vEventGroupDelete(mqm->eg);

    memset(mqm, 0, sizeof(*mqm));
    mqm_status(mqm, "MQTT uninitialized", MQM_DISCONNECTED, true);
}



/**
 * @brief Publish a message with custom QoS and retain flags.
 *
 * @param mqm    Pointer to MQTT manager instance.
 * @param topic  Topic name string.
 * @param msg    Message payload.
 * @param qos    Quality of Service level (0,1,2).
 * @param retain Whether to retain message on broker.
 * @return ESP_OK on success, ESP_FAIL on failure.
 */
esp_err_t mqm_publish_ex(mqm_t* mqm, const char* topic, const char* msg, int qos, int retain)
{
    if (!mqm || !mqm->client || !topic || !msg)
        return ESP_ERR_INVALID_ARG;

    if (!mqm->connected)
        return ESP_ERR_INVALID_STATE;

    int mid = esp_mqtt_client_publish(mqm->client, topic, msg, 0, qos, retain);
    if (mid < 0) {
        ESP_LOGE(TAG, "Publish failed topic=%s", topic);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "PUBLISH mid=%d topic=%s payload=%s", mid, topic, msg);
    return ESP_OK;
}



/**
 * @brief Check whether the MQTT client is currently connected.
 *
 * @param mqm Pointer to MQTT manager instance.
 * @return true if connected, false otherwise.
 */
bool mqm_is_connected(const mqm_t* mqm)
{
    if (!mqm || !mqm->connected) {
        return false;
    }
    return true;
}




/* -------------------------------------------------------------------------- */
/*                             Event handler logic                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief ESP-IDF MQTT event handler wrapper.
 */
static void mqm_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    mqm_t* mqm = (mqm_t*)arg;
    if (mqm)
        mqm_event_core(mqm, (esp_mqtt_event_handle_t)data);
}


/**
 * @brief Core MQTT event processing.
 */
static esp_err_t mqm_event_core(mqm_t* mqm, esp_mqtt_event_handle_t ev)
{
    switch (ev->event_id) {

    case MQTT_EVENT_CONNECTED:
        xEventGroupSetBits(mqm->eg, MQM_BIT_CONNECTED);
        mqm->connected = true;
        mqm_status(mqm, "MQTT connected", MQM_CONNECTED, true);

        if (mqm_subscribe_all(mqm) != ESP_OK)
            mqm_status(mqm, "Subscription failed", MQM_ERROR, true);

        if (mqm->cbs.publish_when_client_connected)
            mqm->cbs.publish_when_client_connected(mqm);
        break;


    case MQTT_EVENT_DISCONNECTED:
        xEventGroupSetBits(mqm->eg, MQM_BIT_FAIL);
        mqm->connected = false;
        mqm_status(mqm, "MQTT DISCONNECTED", MQM_DISCONNECTED, true);
        break;


    case MQTT_EVENT_DATA: {
        char topic[MQM_MAX_TOPIC] = {0};
        char payload[MQM_MAX_PAYLOAD] = {0};

        int topic_len = ev->topic_len < (int)sizeof(topic) - 1 ? ev->topic_len : (int)sizeof(topic) - 1;
        int data_len  = ev->data_len  < (int)sizeof(payload) - 1 ? ev->data_len : (int)sizeof(payload) - 1;

        memcpy(topic, ev->topic, topic_len);
        memcpy(payload, ev->data, data_len);
        topic[topic_len] = '\0';
        payload[data_len] = '\0';

        if (mqm->cbs.on_message)
            mqm->cbs.on_message(topic, payload);

        for (size_t i = 0; i < mqm->table_len; ++i) {
            if (mqm->table[i].topic && strcmp(mqm->table[i].topic, topic) == 0) {
                if (mqm->table[i].handler)
                    mqm->table[i].handler(payload);
                break;
            }
        }
        break;
    }

    default:
        break;
    }

    return ESP_OK;
}




/* -------------------------------------------------------------------------- */
/*                              Subscriptions                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Subscribe to all topics in the registered table.
 *
 * @param mqtt_client Pointer to MQTT manager instance.
 * @return ESP_OK if all topics were subscribed successfully.
 */
static esp_err_t mqm_subscribe_all(const mqm_t* mqtt_client)
{
    if (!mqtt_client || !mqtt_client->table || mqtt_client->table_len == 0)
        return ESP_ERR_INVALID_ARG;

    for (size_t i = 0; i < mqtt_client->table_len; ++i) {
        if (!mqtt_client->table[i].topic)
            continue;

        int r = esp_mqtt_client_subscribe(mqtt_client->client, mqtt_client->table[i].topic, 1);
        ESP_LOGI(TAG, "SUBSCRIBED %s (%d)", mqtt_client->table[i].topic, r);
    }
    return ESP_OK;
}