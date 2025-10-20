#pragma once
/**
 * @file mqtt_manager.h
 * @brief Production-grade MQTT Manager for ESP-IDF (ESP32 / ESP32-S2).
 *
 * ## Overview
 * The MQTT Manager module provides a high-level abstraction layer
 * for managing an ESP-IDF MQTT client (`esp-mqtt`).
 *
 * ### Design Goals
 *  - Single opaque context (`mqm_t`), no global variables.
 *  - Clean lifecycle: `init → start → publish → stop → deinit`.
 *  - Strong synchronization via FreeRTOS EventGroup.
 *  - Safe string handling and bounded waits.
 *  - Optional callbacks for status updates and inbound messages.
 *
 * ### Typical Usage
 * ```c
 * mqm_t mqm;
 * const mqm_config_t cfg = { .uri = "mqtts://broker.local", ... };
 * const mqm_callbacks_t cbs = { .on_status = on_mqtt_status, .on_message = on_mqtt_message };
 * mqm_init(&mqm, &cfg, &cbs, mqtt_topics, topic_count);
 * mqm_start(&mqm, 15000);
 * mqm_publish_ex(&mqm, "topic/test", "hello", 1, 0);
 * ```
 *
 * @note
 *  All API calls must be invoked from task context (not ISR).
 *  Strings used in `mqm_config_t` must remain valid during client lifetime.
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 */

#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H


#include <stdint.h>
#include "esp_err.h"
#include "esp_event.h"
#include "mqtt_client.h"
#include "freertos/event_groups.h"



/* -------------------------------------------------------------------------- */
/*                                 Constants                                  */
/* -------------------------------------------------------------------------- */

#define MQM_MAX_TOPIC   128    /**< Maximum topic string length */
#define MQM_MAX_PAYLOAD 256    /**< Maximum payload string length */


/* -------------------------------------------------------------------------- */
/*                           Root CA certificate symbols                      */
/* -------------------------------------------------------------------------- */

extern const uint8_t root_ca_pem_start[] asm("_binary_root_ca_pem_start");
extern const uint8_t root_ca_pem_end[]   asm("_binary_root_ca_pem_end");



/* -------------------------------------------------------------------------- */
/*                               Event bits                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Internal EventGroup bits representing MQTT client state.
 */
typedef enum {
    MQM_BIT_CONNECTED = BIT0,  /**< Set when MQTT connection established */
    MQM_BIT_FAIL      = BIT1,  /**< Set when connection fails or disconnect occurs */
    MQM_BIT_STOPPED   = BIT2,  /**< Reserved: Set when client stopped */
} mqm_bits_e;



/* -------------------------------------------------------------------------- */
/*                             Topic dispatch table                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Prototype for per-topic message handler.
 *
 * Each handler receives a null-terminated payload string (may be empty).
 */
typedef void (*mqm_topic_handler_t)(const char* payload);

/**
 * @brief Mapping entry for topic → handler.
 *
 * Defines an association between an MQTT topic string and
 * a callback that processes incoming messages for that topic.
 */
typedef struct {
    const char*         topic;    /**< Subscribed MQTT topic string */
    mqm_topic_handler_t handler;  /**< Handler function for this topic */
} mqm_topic_entry_t;



/* -------------------------------------------------------------------------- */
/*                               Configuration                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Configuration parameters for MQTT broker and session.
 *
 * All string fields (`uri`, `username`, `password`) must remain valid
 * throughout the client lifetime or be dynamically allocated.
 */
typedef struct {
    const char* uri;                     /**< Broker URI, e.g. "mqtts://host:8883" */
    const char* username;                /**< Optional MQTT username */
    const char* password;                /**< Optional MQTT password */
    bool        keep_alive_enable;       /**< Enable TCP keepalive mechanism */
    int         keepalive_sec;           /**< Session keepalive interval (default: 20s) */
    int         keep_alive_idle;         /**< TCP keepalive idle time */
    int         keep_alive_interval;     /**< TCP keepalive interval */
    int         keep_alive_count;        /**< TCP keepalive retry count */
    bool        clean_session;           /**< false = persistent session */
    bool        disable_auto_reconnect;  /**< true = disable automatic reconnect */
    int         reconnect_timeout_ms;    /**< Reconnect delay in milliseconds */
    char*       last_will_msg;           /**< Message published on unexpected disconnect */
    char*       last_will_topic;         /**< Topic for last will message */
    int         last_will_qos;           /**< QoS for last will */
    bool        last_will_retain;        /**< Retain flag for last will */
    int         msg_retransmit_timeout;  /**< Message retransmit timeout (QoS1 PUBACK window) */
} mqm_config_t;



/* -------------------------------------------------------------------------- */
/*                                Status enum                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief Enumeration of MQTT client states used for callbacks.
 */
typedef enum {
    MQM_CONNECTING,     /**< Attempting to connect to the broker */
    MQM_CONNECTED,      /**< Successfully connected */
    MQM_DISCONNECTING,  /**< Disconnect in progress */
    MQM_DISCONNECTED,   /**< Disconnected cleanly or lost connection */
    MQM_ERROR,          /**< General or connection error */
    MQM_NONE            /**< Idle / uninitialized */
} mqm_status_t;



/* -------------------------------------------------------------------------- */
/*                                 Callbacks                                  */
/* -------------------------------------------------------------------------- */

typedef struct mqm_t mqm_t; /**< Forward declaration for callback prototypes */

/**
 * @brief Optional application-level MQTT event callbacks.
 */
typedef struct {
    /**
     * @brief Called on significant MQTT state changes.
     *
     * Can be used to update user interface elements (LCD, LEDs, etc.)
     * or print log messages.
     *
     * @param text           Human-readable message string.
     * @param client_status  Current MQTT client status code.
     */
    void (*on_status)(const char* text, mqm_status_t client_status);

    /**
     * @brief Called on every received message before topic dispatch.
     *
     * @param topic   Null-terminated topic string.
     * @param payload Null-terminated message payload.
     */
    void (*on_message)(const char* topic, const char* payload);

    /**
     * @brief Called immediately after successful client connection.
     *
     * Typically used to publish a “device connected” or status update message.
     *
     * @param client Pointer to the active MQTT manager instance.
     */
    void (*publish_when_client_connected)(mqm_t* client);

} mqm_callbacks_t;



/* -------------------------------------------------------------------------- */
/*                                   Context                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief MQTT Manager context structure.
 *
 * Stores all runtime information required to manage a single MQTT client instance.
 * The structure should be allocated statically or globally by the application.
 */
struct mqm_t {
    esp_mqtt_client_handle_t client;     /**< Underlying ESP-IDF MQTT client handle */
    EventGroupHandle_t       eg;         /**< FreeRTOS EventGroup for synchronization */
    bool                     connected;  /**< True if currently connected to broker */
    bool                     started;    /**< True if client started */
    bool                     initialized;/**< True if initialized successfully */

    mqm_config_t             cfg;        /**< Client configuration */
    mqm_callbacks_t          cbs;        /**< Callback table for events */

    const mqm_topic_entry_t* table;      /**< Topic dispatch table */
    size_t                   table_len;  /**< Number of entries in topic table */
};



/* -------------------------------------------------------------------------- */
/*                                   API                                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the MQTT Manager and create the client instance.
 *
 * @param mqm       Pointer to zeroed context structure.
 * @param cfg       MQTT broker configuration (copied internally).
 * @param cbs       Optional callbacks (copied internally, may be NULL).
 * @param table     Topic→handler mapping table (copied as pointers).
 * @param table_len Number of entries in the topic table.
 *
 * @return
 *  - ESP_OK on success
 *  - ESP_ERR_INVALID_ARG on bad input
 *  - ESP_ERR_NO_MEM on allocation failure
 */
esp_err_t mqm_init(mqm_t* mqm,
                   const mqm_config_t* cfg,
                   const mqm_callbacks_t* cbs,
                   const mqm_topic_entry_t* table,
                   size_t table_len);



/**
 * @brief Start the MQTT client and wait for broker connection.
 *
 * @param mqm         Pointer to initialized MQTT Manager context.
 * @param timeout_ms  Maximum time (in ms) to wait for connection (default: 15000).
 * @return ESP_OK if connected successfully, ESP_FAIL if timeout or failure.
 */
esp_err_t mqm_start(mqm_t* mqm, uint32_t timeout_ms);



/**
 * @brief Stop the MQTT client gracefully.
 *
 * @param mqm         Pointer to initialized MQTT Manager context.
 * @param timeout_ms  Timeout for stop operation (default: 3000 ms).
 * @return ESP_OK on successful stop, ESP_FAIL on timeout or error.
 */
esp_err_t mqm_stop(mqm_t* mqm, uint32_t timeout_ms);



/**
 * @brief Destroy the MQTT client and free all resources.
 *
 * Safe to call even if the client was not started or already deinitialized.
 *
 * @param mqm Pointer to MQTT Manager context.
 */
void mqm_deinit(mqm_t* mqm);



/**
 * @brief Publish a message to the specified topic.
 *
 * @param mqm     Pointer to MQTT Manager context.
 * @param topic   Null-terminated topic string.
 * @param msg     Null-terminated payload string.
 * @param qos     Quality of Service level (0, 1, or 2).
 * @param retain  Retain flag (0 = false, 1 = true).
 *
 * @return ESP_OK on success, ESP_FAIL or ESP_ERR_INVALID_STATE on failure.
 */
esp_err_t mqm_publish_ex(mqm_t* mqm, const char* topic, const char* msg, int qos, int retain);



/**
 * @brief Check whether the MQTT client is currently connected.
 *
 * @param mqm Pointer to MQTT Manager context.
 * @return true if connected, false otherwise.
 */
bool mqm_is_connected(const mqm_t* mqm);



/**
 * @brief Return the current MQTT Manager instance (if used globally).
 *
 * @return Copy of current `mqm_t` context.
 */
mqm_t get_mqtt_client(void);

#endif //MQTT_MANAGER_H