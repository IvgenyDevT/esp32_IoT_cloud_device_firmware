/**
 * @file hardware_layer.c
 * @brief Hardware Abstraction Layer (HAL) for GPIO configuration and register access.
 *
 * ## Overview
 * This module provides low-level access to the hardware registers controlling
 * GPIO functionality on the ESP32/ESP32-S2 family.
 * It includes utility functions to configure pin direction, output levels,
 * electrical drive strength, pull-up/down resistors, and interrupt behavior.
 *
 * The goal of this layer is to encapsulate all raw register operations behind
 * simple and reusable functions, ensuring consistency and reducing hardware coupling
 * across different modules.
 *
 * ## Responsibilities
 *  - Configure GPIO pins as input/output.
 *  - Write to and read from hardware registers safely.
 *  - Configure pull-ups, drive strength, and interrupts.
 *  - Initialize the Wi-Fi reset button input pin.
 *
 * ## Dependencies
 *  - hardware_layer.h
 *  - hardware_config.h
 *  - <inttypes.h>, <stdio.h>
 *
 * ## Example
 * @code
 *  config_GPIO(GPIO_NUM_5, true, false, TEN_MA, false, false, DISABLE);
 *  set_output_direction(GPIO_NUM_5);
 *  set_output_level(GPIO_NUM_5, HIGH);
 * @endcode
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 */


#include "hardware_layer.h"

#include <esp_log.h>

#include "hardware_config.h"

#include <inttypes.h>
#include <stdio.h>


static const char* TAG = "Hardware_layer";

/* -------------------------------------------------------------------------- */
/*                           GPIO DIRECTION CONTROL                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Configure a GPIO pin as output.
 *
 * Writes to the GPIO enable register (W1TS) to set the specified pin
 * as an output. This function should be called after configuring the pin's mode.
 *
 * @param pin_num GPIO pin number to configure as output.
 */
void set_output_direction(unsigned short pin_num)
{
    /* Set GPIO as output */
    write_register(GPIO_REG_OFFSET_ADDR + GPIO_EN_W1TS_REG, BIT_MASK(pin_num));
}




/**
 * @brief Configure a GPIO pin as input.
 *
 * Writes to the GPIO enable clear register (W1TC) to disable output
 * mode on the specified pin, effectively setting it as input.
 *
 * @param pin_num GPIO pin number to configure as input.
 */
void set_input_direction(unsigned short pin_num)
{
    /* Set GPIO as input */
    write_register(GPIO_REG_OFFSET_ADDR + GPIO_EN_W1TC_REG, BIT_MASK(pin_num));
}




/* -------------------------------------------------------------------------- */
/*                            GPIO OUTPUT LEVEL API                           */
/* -------------------------------------------------------------------------- */

/**
 * @brief Set logic level (HIGH/LOW) on a GPIO pin.
 *
 * This function changes the output state of a GPIO pin.
 * It writes to either W1TS (set) or W1TC (clear) register depending on the desired level.
 *
 * @param pin_num GPIO number to modify.
 * @param level   Logic level: `LOW` or `HIGH`.
 */
void set_output_level(unsigned short pin_num, level level)
{
    /* Set GPIO output level */
    if (level == LOW)
    {
        write_register(GPIO_REG_OFFSET_ADDR + GPIO_OUT_W1TC_REG, BIT_MASK(pin_num));
    }
    else
    {
        write_register(GPIO_REG_OFFSET_ADDR + GPIO_OUT_W1TS_REG, BIT_MASK(pin_num));
    }
}




/* -------------------------------------------------------------------------- */
/*                         GPIO CONFIGURATION FUNCTION                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Configure electrical and interrupt parameters of a GPIO pin.
 *
 * This function programs various configuration registers associated with
 * a specific GPIO, including:
 *  - Pull-up / pull-down enable
 *  - Drive strength
 *  - Interrupt enable, NMI, and type
 *  - Synchronization settings
 *
 * @param GPIO             GPIO number to configure.
 * @param pull_up_en       Enable internal pull-up resistor.
 * @param pull_down_en     Enable internal pull-down resistor.
 * @param current          Drive strength (e.g., FIVE_MA, TEN_MA, TWENTY_MA).
 * @param Interrupt_en     Enable standard interrupt.
 * @param NMI_interrupt    Enable Non-Maskable Interrupt (NMI).
 * @param interrupt_type   Interrupt trigger type (rising/falling/both edges).
 */
void config_GPIO(uint32_t GPIO,
                 bool pull_up_en,
                 bool pull_down_en,
                 GPIO_strength current,
                 bool Interrupt_en,
                 bool NMI_interrupt,
                 interrupt_type interrupt_type)
{
    uint32_t reg_val = 0;

    /* Configure pull resistors and drive strength */
    reg_val |= (pull_down_en << 2);  /* Pull-down enable bit */
    reg_val |= (pull_up_en << 3);    /* Pull-up enable bit */
    reg_val |= (current << 10);      /* Drive strength bits */
    reg_val |= (1 << 12);            /* MUX select to GPIO function */

    // write_register(GPIO_MUX_OFFSET_ADDR + GPIO_MUX_REG(GPIO), reg_val);

    reg_val = 0;

    /* Configure interrupt behavior */
    reg_val |= (Interrupt_en  << INTERRUPT_ENABLE_SHIFT);      /* Enable interrupt */
    reg_val |= (NMI_interrupt << INTERRUPT_NMI_ENABLE_SHIFT);  /* Enable NMI */
    reg_val |= (interrupt_type << INTERRUPT_TYPE_SHIFT);       /* Set interrupt type */

    /* Configure synchronization stages */
    reg_val |= INTERRUPT_SYNC_FALLING_EDGE;
    reg_val |= (INTERRUPT_SYNC_FALLING_EDGE << INTERRUPT_SYNC2_SHIFT);

    /* Commit configuration to the hardware register */
    write_register(GPIO_REG_OFFSET_ADDR + GPIO_PIN_REG(GPIO), reg_val);
}




/* -------------------------------------------------------------------------- */
/*                     WIFI RESET BUTTON GPIO INITIALIZATION                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize GPIO pin used as the Wi-Fi reset button.
 *
 * This function sets up a dedicated GPIO pin to act as an input for the
 * Wi-Fi reset button. It enables both-edge interrupts to detect button press
 * and release, and clears any pending interrupt flags.
 *
 * @param interupt_GPIO GPIO number used for Wi-Fi reset button input.
 */
void init_wifi_reset_button_GPIO(uint32_t interupt_GPIO)
{
    /* Configure the button GPIO */
    config_GPIO(interupt_GPIO, true, false, FIVE_MA, true, true, BOTH_EDGES);

    /* Set pin as input */
    set_input_direction(interupt_GPIO);

    /* Clear interrupt flag */
    write_register(GPIO_REG_OFFSET_ADDR + GPIO_INTERRUPT_W1TC_REG, 1 << interupt_GPIO);

    ESP_LOGI(TAG,"wifi reset button initialized");
}




/* -------------------------------------------------------------------------- */
/*                           REGISTER ACCESS UTILITIES                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Write a 32-bit value to a hardware register.
 *
 * @param address Register address.
 * @param val     32-bit value to write.
 */
void write_register(unsigned int address, uint32_t val)
{
    *(volatile uint32_t *)(address) = val;
}




/**
 * @brief Print a register's address and current value.
 *
 * Useful for debugging or verifying register configurations at runtime.
 *
 * @param addr Address of the hardware register to read.
 */
inline void print_register(uint32_t addr)
{
    uint32_t val = read_register(addr);
    printf("REG[0x%08" PRIX32 "] = 0x%08" PRIX32 "\n", addr, val);
}




/**
 * @brief Read and return the value of a 32-bit hardware register.
 *
 * @param addr Register address.
 * @return uint32_t The current value stored at that register address.
 */
inline uint32_t read_register(uint32_t addr)
{
    return *(volatile uint32_t *)addr;
}