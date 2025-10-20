/**
 * @file interrupts.c
 * @brief GPIO interrupt handling module for button-based Wi-Fi reset logic.
 *
 * ## Overview
 * This module implements low-level interrupt handling logic for the
 * Wi-Fi reset button.
 * It detects both long presses (≥5 seconds) and triple short presses
 * via GPIO interrupt events.
 *
 * ## Responsibilities
 *  - Attach the GPIO interrupt service routine (ISR)
 *  - Detect button presses/releases
 *  - Distinguish between long press and triple-click patterns
 *  - Update global flags to trigger Wi-Fi reset or special actions
 *
 * ## Design Notes
 *  - ISR runs in IRAM (using `IRAM_ATTR`)
 *  - Timing is measured using `esp_timer_get_time()`
 *  - Interrupts are routed to CPU via Xtensa interrupt matrix
 *
 * ## Dependencies
 *  - hardware_layer.h
 *  - hardware_config.h
 *  - FreeRTOS
 *  - esp_timer.h
 *
 * ## Example
 * @code
 *  init_wifi_reset_button_GPIO(GPIO_NUM_0);
 *  enable_GPIO_interrupts(GPIO_NUM_0);
 * @endcode
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 */


#include "freertos/FreeRTOS.h"
#include "interrupts.h"

#include <esp_log.h>

#include "hardware_layer.h"
#include "hardware_config.h"

#include <stdint.h>
#include <esp_timer.h>
#include <inttypes.h>


static const char* TAG = "Interrupts_handler";


/* -------------------------------------------------------------------------- */
/*                            EXTERNAL VARIABLES                              */
/* -------------------------------------------------------------------------- */

/// Flag raised when a long-press (≥5s) is detected
extern volatile bool wifi_reset_pressed;

/// Flag raised when a triple short press is detected
extern bool wifi_triple_pressed;




/* -------------------------------------------------------------------------- */
/*                            STATIC VARIABLES                                */
/* -------------------------------------------------------------------------- */

/// GPIO number associated with Wi-Fi reset button interrupt
static uint32_t wifi_reset_pin = 0;




/* -------------------------------------------------------------------------- */
/*                            INTERRUPT HANDLER ISR                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief GPIO interrupt handler for Wi-Fi reset button.
 *
 * Detects short and long button presses, as well as triple-click patterns.
 *
 * ### Behavior:
 *  - Long press (≥5s): sets `wifi_reset_pressed = true`
 *  - Three short presses (within 3s): sets `wifi_triple_pressed = true`
 *
 * The ISR clears the interrupt flag, measures press durations using
 * `esp_timer_get_time()`, and updates global flags accordingly.
 *
 * @param arg Optional argument (unused).
 */
void IRAM_ATTR gpio_interrupt_handler(void *arg)
{
    static int last_level = 1;             /* Remember previous GPIO level */
    static int64_t press_start_time = 0;   /* Timestamp when press began */
    static int64_t last_release_time = 0;  /* Timestamp of last release */
    static int click_count = 0;            /* Number of rapid clicks counted */

    /* Read current interrupt status register */
    uint32_t status = read_register(GPIO_REG_OFFSET_ADDR + GPIO_INTERRUPT_REG);

    /* Check if our target GPIO triggered the interrupt */
    if (status & (1 << wifi_reset_pin))
    {
        /* Clear the interrupt flag for this pin */
        write_register(GPIO_REG_OFFSET_ADDR + GPIO_INTERRUPT_W1TC_REG,
                       1 << wifi_reset_pin);

        /* Read current GPIO logic level */
        int level = (int)((read_register(GPIO_REG_OFFSET_ADDR + GPIO_LEVEL_REG)
                          >> wifi_reset_pin) & 1);

        /* --- Button Pressed (falling edge) --- */
        if (level == 0 && last_level == 1)
        {
            press_start_time = esp_timer_get_time();  // store press start time
        }

        /* --- Button Released (rising edge) --- */
        else if (level == 1 && last_level == 0)
        {
            int64_t now = esp_timer_get_time();
            int64_t press_time_ms = (now - press_start_time) / 1000;  // convert µs → ms

            /* -------- Long Press Detection -------- */
            if (press_time_ms >= 5000)
            {
                wifi_reset_pressed = true;  // trigger long-press flag
                click_count = 0;            // reset click counter
            }

            /* -------- Short Press (triple click) -------- */
            else
            {
                // Within 3s since last release → count as consecutive click
                if (now - last_release_time <= 3000000)
                {
                    click_count++;
                }
                else
                {
                    // Too slow — start counting from 1 again
                    click_count = 1;
                }

                last_release_time = now;

                // Trigger triple-click event
                if (click_count >= 3)
                {
                    wifi_triple_pressed = true;
                    click_count = 0;
                }
            }
        }

        /* Update last level for edge tracking */
        last_level = level;
    }
}




/* -------------------------------------------------------------------------- */
/*                        INTERRUPT SETUP AND ENABLEMENT                      */
/* -------------------------------------------------------------------------- */

/**
 * @brief Enable GPIO interrupts and attach the ISR.
 *
 * Routes GPIO interrupts through the Xtensa interrupt matrix,
 * enables the interrupt at CPU level, and attaches the handler.
 *
 * @param interrupt_GPIO GPIO number to enable interrupts on.
 */
void enable_GPIO_interrupts(uint32_t interrupt_GPIO)
{
    /* Route GPIO interrupt source to CPU interrupt line */
    write_register(INTERRUPT_MATRIX_BASE_ADDRESS + INTERRUPT_MATRIX_PRO_GPIO_MAP_REG,
                   CPU_GPIO_INTERRUPT_NUM);

    /* Enable the CPU interrupt mask */
    xt_ints_on(1 << CPU_GPIO_INTERRUPT_NUM);

    /* Attach the handler to the GPIO interrupt vector */
    xt_set_interrupt_handler(CPU_GPIO_INTERRUPT_NUM, gpio_interrupt_handler, NULL);

    /* Store which pin is used for reference inside the ISR */
    wifi_reset_pin = interrupt_GPIO;

    ESP_LOGI(TAG,"interrupt GPIO enabled");

}