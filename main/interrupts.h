/**
 * @file interrupts.h
 * @brief Interface for GPIO interrupt handling and setup.
 *
 * ## Overview
 * This header defines the public API for the interrupt control module.
 * It exposes functions for attaching the GPIO interrupt service routine (ISR)
 * and enabling CPU-level GPIO interrupt routing.
 *
 * The module is designed primarily to detect and respond to button presses,
 * including long-press and triple-click patterns used for Wi-Fi reset or
 * other user-defined actions.
 *
 * ## Responsibilities
 *  - Provide ISR definition for GPIO events.
 *  - Enable and configure GPIO interrupts through the Xtensa interrupt matrix.
 *  - Offer clean, hardware-abstracted interface to upper layers.
 *
 * ## Dependencies
 *  - esp_attr.h
 *  - hardware_layer.h (used internally in the .c file)
 *
 * ## Example
 * @code
 *  init_wifi_reset_button_GPIO(GPIO_NUM_0);
 *  enable_GPIO_interrupts(GPIO_NUM_0);
 * @endcode
 *
 * @note
 *  The ISR (`gpio_interrupt_handler`) is placed in IRAM for fast execution.
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 * @date
 *  Created on: 19/09/2025
 */

#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <esp_attr.h>
#include <stdint.h>




/* -------------------------------------------------------------------------- */
/*                            GPIO INTERRUPT HANDLER                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief GPIO interrupt service routine (ISR).
 *
 * Handles button press events and identifies patterns such as
 * long-press or triple-click.
 * This function must be placed in IRAM and registered via
 * `xt_set_interrupt_handler()`.
 *
 * @param arg Optional argument passed during ISR registration (unused).
 */
void IRAM_ATTR gpio_interrupt_handler(void *arg);




/* -------------------------------------------------------------------------- */
/*                        INTERRUPT CONFIGURATION FUNCTION                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Enable GPIO interrupts and attach the ISR.
 *
 * Configures the Xtensa interrupt matrix to route GPIO interrupts
 * to the CPU, enables the interrupt line, and assigns the ISR.
 *
 * @param interrupt_GPIO GPIO number to enable interrupts on.
 */
void enable_GPIO_interrupts(uint32_t interrupt_GPIO);



#endif /* INTERRUPTS_H */