/**
 * @file leds_driver.c
 * @brief Production-grade LED control driver using FreeRTOS tasks and queues.
 *
 * ## Overview
 * This module implements a non-blocking LED driver supporting multiple LEDs
 * with independent on/off states and blinking patterns.  
 * It uses a dedicated FreeRTOS task (`LED_indicator_task`) that receives
 * LED control commands through a queue, enabling safe LED updates from
 * any context (including ISRs and other tasks).
 *
 * ## Responsibilities
 *  - Initialize all configured LEDs and GPIOs.
 *  - Maintain a shared queue for LED control commands.
 *  - Provide functions for turning LEDs on/off or blinking them.
 *  - Handle LED state updates in a dedicated task context.
 *
 * ## Dependencies
 *  - FreeRTOS (tasks and queues)
 *  - hardware_layer.h (GPIO configuration and output control)
 *  - hardware_config.h
 *  - util.h (for wait_ms)
 *
 * ## Example
 * @code
 *  all_leds_init(25, 26, 27);
 *  led_blinking(GREEN_LED, 0.5, true);
 *  led_on(RED_LED, false);
 * @endcode
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 * @date
 *  Created on: 19/09/2025
 */

#include "freertos/FreeRTOS.h"
#include "leds_driver.h"

#include <esp_log.h>
#include <FreeRTOSConfig.h>
#include <hardware_config.h>
#include <hardware_layer.h>
#include <portmacro.h>
#include <string.h>
#include <util.h>
#include <freertos/projdefs.h>
#include "esp_private/adc_share_hw_ctrl.h"
#include "freertos/task.h"
#include "freertos/queue.h"



/* -------------------------------------------------------------------------- */
/*                            STATIC MODULE VARIABLES                          */
/* -------------------------------------------------------------------------- */

/** @brief Table of LED indicators (state, GPIO, blink pattern). */
static LED_indicator LEDs_table[] = {
    {RED_LED,    -1, false, 0.0, 0},
    {GREEN_LED,  -1, false, 0.0, 0},
    {YELLOW_LED, -1, false, 0.0, 0}
};

/** @brief Application log tag for ESP logging. */
static const char* TAG = "LEDs driver";

/** @brief FreeRTOS queue used to send LED commands to the LED task. */
static QueueHandle_t LEDs_queue;

/** @brief Temporary structure for sending LED state commands. */
static LED_indicator LED_set;

/** @brief Flag indicating whether the driver has been initialized. */
static bool initialized = false;




/* -------------------------------------------------------------------------- */
/*                         INTERNAL HELPER FUNCTIONS                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Configure and initialize a single LED GPIO.
 *
 * @param GPIO The GPIO number to configure as output.
 */
static void single_led_GPIO_init(unsigned short GPIO)
{
    config_GPIO(GPIO, false, false, TWENTY_MA, false, false, DISABLE);
    set_output_direction(GPIO);
    set_output_level(GPIO, LOW);
}




/* -------------------------------------------------------------------------- */
/*                           PUBLIC DRIVER INTERFACE                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize all LEDs and start the LED handler task.
 *
 * @param green_led_pin GPIO number for the green LED.
 * @param red_led_pin   GPIO number for the red LED.
 * @param yellow_led_pin GPIO number for the yellow LED.
 */
void all_leds_init(int green_led_pin, int red_led_pin, int yellow_led_pin)
{
    LEDs_table[RED_LED].LED_pin = red_led_pin;
    LEDs_table[GREEN_LED].LED_pin = green_led_pin;
    LEDs_table[YELLOW_LED].LED_pin = yellow_led_pin;

    /* Create communication queue */
    LEDs_queue = xQueueCreate(10, sizeof(LED_indicator));
    ESP_LOGI(TAG, "LED queue created");

    /* Create LED handler task */
    xTaskCreate(LED_indicator_task, "LED Task", 2048, NULL, 5, NULL);
    ESP_LOGI(TAG, "LED Task started");

    /* Initialize all LED GPIOs */
    for (int i = 0; i < LEDS_AMOUNT; i++) {
        if (LEDs_table[i].LED_pin > -1 || true) {
            single_led_GPIO_init(LEDs_table[i].LED_pin);
        }
    }

    initialized = true;
    ESP_LOGI(TAG, "LED driver initialized");
}




/**
 * @brief Turn off all LEDs.
 */
void all_leds_off(void)
{
    if (!initialized) {
        ESP_LOGE(TAG, "Can't switch LED status, driver not initialized");
        return;
    }

    LED_set.blink_sec = 0;
    LED_set.times = 0;
    LED_set.on = false;

    for (int i = 0; i < LEDS_AMOUNT; i++) {
        LED_set.LED = LEDs_table[i].LED;
        xQueueSend(LEDs_queue, &LED_set, pdMS_TO_TICKS(500));
    }
}




/**
 * @brief Turn on a single LED.
 *
 * @param LED The LED to turn on.
 * @param turn_off_previous_leds If true, all other LEDs are turned off first.
 */
void led_on(LEDs LED, bool turn_off_previous_leds)
{
    if (!initialized) {
        ESP_LOGE(TAG, "Can't switch LED status, driver not initialized");
        return;
    }
printf("turnning led %s on",LED == RED_LED ? "red" : LED == GREEN_LED ? "green" : "yellow");
    if (turn_off_previous_leds) {
        all_leds_off();
    }

    LED_set.blink_sec = 0;
    LED_set.times = 0;
    LED_set.on = true;
    LED_set.LED = LED;
    xQueueSend(LEDs_queue, &LED_set, pdMS_TO_TICKS(500));
}




/**
 * @brief Turn off a single LED.
 *
 * @param LED The LED to turn off.
 */
void led_off(LEDs LED)
{
    if (!initialized) {
        ESP_LOGE(TAG, "Can't switch LED status, driver not initialized");
        return;
    }

    LED_set.blink_sec = 0;
    LED_set.times = 0;
    LED_set.on = false;
    LED_set.LED = LED;
    xQueueSend(LEDs_queue, &LED_set, pdMS_TO_TICKS(500));
}




/**
 * @brief Blink an LED for a limited number of times.
 *
 * @param LED LED to blink.
 * @param blink_sec Blink interval in seconds.
 * @param blink_times Number of blinks before stopping.
 * @param turn_off_previous_leds Whether to turn off other LEDs before blinking.
 */
void led_blinking_limited_times(LEDs LED, double blink_sec, int blink_times, bool turn_off_previous_leds)
{
    if (!initialized) {
        ESP_LOGE(TAG, "Can't switch LED status, driver not initialized");
        return;
    }

    if (turn_off_previous_leds) {
        all_leds_off();
    }

    LED_set.blink_sec = blink_sec;
    LED_set.times = blink_times;
    LED_set.on = true;
    LED_set.LED = LED;
    xQueueSend(LEDs_queue, &LED_set, pdMS_TO_TICKS(500));
}




/**
 * @brief Blink an LED continuously with a given interval.
 *
 * @param LED LED to blink.
 * @param blink_sec Blink interval in seconds.
 * @param turn_off_previous_leds Whether to turn off other LEDs before blinking.
 */
void led_blinking(LEDs LED, double blink_sec, bool turn_off_previous_leds)
{
    if (!initialized) {
        ESP_LOGE(TAG, "Can't switch LED status, driver not initialized");
        return;
    }

    if (turn_off_previous_leds) {
        all_leds_off();
    }

    LED_set.blink_sec = blink_sec;
    LED_set.times = 0;
    LED_set.on = true;
    LED_set.LED = LED;
    xQueueSend(LEDs_queue, &LED_set, pdMS_TO_TICKS(500));
}




/**
 * @brief FreeRTOS LED indicator task.
 *
 * Continuously listens for LED control commands and updates
 * LED states accordingly. Supports steady-state and blinking
 * (limited or continuous) operation.
 *
 * @param param Unused task parameter.
 */
void LED_indicator_task(void* param) {

    unsigned short index = 0;
    LED_indicator new_LEDs_state;
    LED_indicator current_LEDs_state;
    current_LEDs_state.blink_sec =0; current_LEDs_state.times =0; current_LEDs_state.on =0; current_LEDs_state.LED = RED_LED;
    bool first_queue_received = false;
    int blink_times = 0;
    bool blink_times_limited = false;

    while (1) {
        if (xQueueReceive(LEDs_queue, &new_LEDs_state,pdMS_TO_TICKS(100))) {

            /*update the state*/
            current_LEDs_state.blink_sec = new_LEDs_state.blink_sec;
            current_LEDs_state.times = new_LEDs_state.times;
            current_LEDs_state.on = new_LEDs_state.on;
            current_LEDs_state.LED = new_LEDs_state.LED;

            first_queue_received = true;
            if (current_LEDs_state.times != 0) {
                blink_times_limited = true;
                blink_times = 0;
            }

            /*find the table's index of the new LED*/
            for (index = 0; index < LEDS_AMOUNT; index++) {
                if (LEDs_table[index].LED == current_LEDs_state.LED) {
                    break;
                }
            }

        }
        if (!first_queue_received) {
            continue;
        }

        /*if the request is just to turn on\off the LED without blinking*/
        if (current_LEDs_state.blink_sec == 0) {

            if (current_LEDs_state.on != LEDs_table[index].on) {
                set_output_level(LEDs_table[index].LED_pin,current_LEDs_state.on);
                /*update the new status*/
                LEDs_table[index].on = current_LEDs_state.on;

            }
        }
        else {
            if (blink_times_limited  && (blink_times >= current_LEDs_state.times)) {
                set_output_level(current_LEDs_state.LED,LOW);
                continue;
            }
            set_output_level(LEDs_table[current_LEDs_state.LED].LED_pin,HIGH);
            wait_ms((unsigned long long)(current_LEDs_state.blink_sec*1000));

            set_output_level(LEDs_table[current_LEDs_state.LED].LED_pin,LOW);
            wait_ms((unsigned long long)(current_LEDs_state.blink_sec*1000));

            if (blink_times_limited) blink_times ++;
        }
        LEDs_table[index].blink_sec = current_LEDs_state.blink_sec;
        LEDs_table[index].times = current_LEDs_state.times;
        LEDs_table[index].on = current_LEDs_state.on;
        LEDs_table[index].LED = current_LEDs_state.LED;

    }
}