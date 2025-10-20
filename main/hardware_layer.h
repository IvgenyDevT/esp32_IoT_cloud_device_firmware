/**
 * @file hardware_layer.h
 * @brief Public interface for the hardware abstraction layer (HAL).
 *
 * ## Overview
 * This header file exposes a clean, high-level interface for low-level
 * hardware control functions implemented in `hardware_layer.c`.
 *
 * The Hardware Layer provides:
 *  - GPIO configuration and direction control
 *  - Setting logic levels (HIGH/LOW)
 *  - Register read/write access
 *  - Wi-Fi reset button initialization
 *
 * All functions here abstract raw register access, offering a safe and
 * structured API for upper-layer modules.
 *
 * ## Usage Example
 * @code
 *  set_output_direction(GPIO_NUM_5);
 *  set_output_level(GPIO_NUM_5, HIGH);
 *  print_register(GPIO_OUT_REG);
 * @endcode
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 */

#ifndef HARDWARE_LAYER_H
#define HARDWARE_LAYER_H

#include <stdint.h>
#include <hardware_config.h>





/* -------------------------------------------------------------------------- */
/*                              GPIO DIRECTION API                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Configure a GPIO pin as output.
 *
 * @param pin_num GPIO number to configure as output.
 */
void set_output_direction(unsigned short pin_num);



/**
 * @brief Configure a GPIO pin as input.
 *
 * @param pin_num GPIO number to configure as input.
 */
void set_input_direction(unsigned short pin_num);




/* -------------------------------------------------------------------------- */
/*                           GPIO OUTPUT LEVEL CONTROL                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Set a GPIO pin output level (HIGH or LOW).
 *
 * Writes to hardware registers to set or clear the GPIO output bit.
 *
 * @param pin_num GPIO pin number.
 * @param level   Output level (`LOW` or `HIGH`).
 */
void set_output_level(unsigned short pin_num, level level);




/* -------------------------------------------------------------------------- */
/*                          GPIO CONFIGURATION FUNCTION                       */
/* -------------------------------------------------------------------------- */

/**
 * @brief Configure a GPIO pin’s pull resistors, drive strength, and interrupt.
 *
 * This function controls electrical and interrupt characteristics of a GPIO.
 *
 * @param GPIO            GPIO number.
 * @param pull_up_en      Enable internal pull-up resistor.
 * @param pull_down_en    Enable internal pull-down resistor.
 * @param current         Drive strength (e.g., FIVE_MA, TEN_MA, TWENTY_MA).
 * @param Interrupt_en    Enable standard interrupt.
 * @param NMI_interrupt   Enable Non-Maskable Interrupt (NMI).
 * @param interrupt_type  Type of interrupt trigger (rising/falling/both edges).
 */
void config_GPIO(uint32_t GPIO,
                 bool pull_up_en,
                 bool pull_down_en,
                 GPIO_strength current,
                 bool Interrupt_en,
                 bool NMI_interrupt,
                 interrupt_type interrupt_type);




/* -------------------------------------------------------------------------- */
/*                       WIFI RESET BUTTON INITIALIZATION                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize GPIO pin used for Wi-Fi reset button.
 *
 * Configures the given pin as input with both-edge interrupt detection.
 *
 * @param interupt_GPIO GPIO number for the Wi-Fi reset button.
 */
void init_wifi_reset_button_GPIO(uint32_t interupt_GPIO);




/* -------------------------------------------------------------------------- */
/*                           REGISTER ACCESS UTILITIES                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Write a 32-bit value to a memory-mapped hardware register.
 *
 * @param address Register address.
 * @param val     32-bit value to write.
 */
void write_register(unsigned int address, uint32_t val);



/**
 * @brief Print a register’s address and current value to stdout.
 *
 * @param addr Address of the register to read and print.
 */
void print_register(uint32_t addr);



/**
 * @brief Read the 32-bit value from a memory-mapped hardware register.
 *
 * @param addr Register address.
 * @return uint32_t The value stored at the given address.
 */
uint32_t read_register(uint32_t addr);



#endif /* HARDWARE_LAYER_H */