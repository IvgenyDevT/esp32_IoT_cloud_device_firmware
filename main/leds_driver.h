/**
 * @file leds_driver.h
 * @brief Multi-LED driver interface for FreeRTOS-based embedded systems.
 *
 * ## Overview
 * This header defines the data structures and public API used to control
 * multiple LEDs (red, green, yellow) with on/off and blinking behavior.
 * The implementation uses a background FreeRTOS task and queue to handle
 * LED updates safely and asynchronously.
 *
 * ## Responsibilities
 *  - Initialize GPIOs for all LEDs.
 *  - Provide functions to turn LEDs on/off or make them blink.
 *  - Manage a dedicated LED task for concurrent LED control.
 *
 * ## Usage Notes
 * - To indicate that a specific LED is **not connected**, pass a **negative GPIO number**
 *   (e.g., `-1`) to `all_leds_init()`. That LED will be ignored during initialization.
 *
 * ## Example
 * @code
 *  all_leds_init(25, 26, -1);     // Yellow LED not connected
 *  led_blinking(GREEN_LED, 0.5, true);
 *  led_on(RED_LED, false);
 * @endcode
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 */

#ifndef LEDS_DRIVER_H
#define LEDS_DRIVER_H

#include <stdbool.h>




/* -------------------------------------------------------------------------- */
/*                                DEFINITIONS                                 */
/* -------------------------------------------------------------------------- */

/** Total number of LEDs supported by the driver */
#define LEDS_AMOUNT 3

/** FreeRTOS LED task name */
#define LED_TASK_NAME "LED Task"




/* -------------------------------------------------------------------------- */
/*                                 ENUM TYPES                                 */
/* -------------------------------------------------------------------------- */

/**
 * @brief LED identifiers.
 */
typedef enum LEDs {
    RED_LED,     /**< Red LED identifier */
    GREEN_LED,   /**< Green LED identifier */
    YELLOW_LED   /**< Yellow LED identifier */
} LEDs;




/* -------------------------------------------------------------------------- */
/*                                DATA STRUCTURES                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief LED indicator structure representing one LED and its current state.
 */
typedef struct LED_indicator {
    LEDs LED;          /**< LED identifier */
    int LED_pin;       /**< Physical GPIO pin number (use -1 if not connected) */
    bool on;           /**< Current ON/OFF state */
    double blink_sec;  /**< Blink interval in seconds (0 for static state) */
    int times;         /**< Number of blinks (0 = continuous) */
} LED_indicator;




/* -------------------------------------------------------------------------- */
/*                               PUBLIC FUNCTIONS                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize all LEDs and start the LED control task.
 *
 * Each LED must be assigned a GPIO number.
 * To skip an unused LED, pass a negative number (e.g., `-1`).
 *
 * @param green_led_pin  GPIO number for the green LED.
 * @param red_led_pin    GPIO number for the red LED.
 * @param yellow_led_pin GPIO number for the yellow LED.
 */
void all_leds_init(int green_led_pin, int red_led_pin, int yellow_led_pin);



/**
 * @brief Turn off all LEDs immediately.
 */
void all_leds_off(void);



/**
 * @brief Turn on a specific LED.
 *
 * @param LED The LED to turn on.
 * @param turn_off_previous_leds Whether to turn off other LEDs before turning this one on.
 */
void led_on(LEDs LED, bool turn_off_previous_leds);



/**
 * @brief Turn off a specific LED.
 *
 * @param LED The LED to turn off.
 */
void led_off(LEDs LED);



/**
 * @brief Blink an LED a limited number of times.
 *
 * @param LED LED to blink.
 * @param blink_sec Blink interval in seconds.
 * @param blink_times Number of times to blink.
 * @param turn_off_previous_leds Whether to turn off other LEDs first.
 */
void led_blinking_limited_times(LEDs LED, double blink_sec, int blink_times, bool turn_off_previous_leds);



/**
 * @brief Blink an LED continuously.
 *
 * @param LED LED to blink.
 * @param blink_sec Blink interval in seconds.
 * @param turn_off_previous_leds Whether to turn off other LEDs first.
 */
void led_blinking(LEDs LED, double blink_sec, bool turn_off_previous_leds);



/**
 * @brief Task that handles LED updates.
 *
 * This task runs in the background and processes LED control commands
 * received through the FreeRTOS queue.
 *
 * @param param Unused task parameter.
 */
void LED_indicator_task(void *param);



#endif /* LEDS_DRIVER_H */