/**
 * @file config.h
 * @brief Global hardware and firmware configuration for the ESP32-S2 project.
 *
 * This file defines compile-time constants for hardware pin mappings,
 * LCD configuration, LED control pins, Wi-Fi reset button, NVS storage,
 * and firmware metadata such as version and device name.
 *
 * The configuration values here are project-specific and should be adapted
 * to match the target board wiring or custom hardware revision.
 *
 * ## Overview
 * - Contains all key pin assignments and peripheral configuration.
 * - Used by LCD, LED, Wi-Fi, and system initialization modules.
 * - Keeps hardware dependencies centralized and easy to modify.
 *
 * ## Typical usage
 * ```c
 * #include "config.h"
 *
 * LCD_init(LCD_PIN_RS, LCD_PIN_EN, LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7);
 * led_init(GREEN_LED_PIN, YELLOW_LED_PIN, RED_LED_PIN);
 * enable_GPIO_interrupts(WIFI_RESET_PIN);
 * ```
 */

#ifndef CONFIG_H
#define CONFIG_H

/* -------------------------------------------------------------------------- */
/*                           Firmware Information                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Current firmware version.
 *
 * This string is published through MQTT and displayed on the LCD
 * during device connection testing.
 */
#define PROG_VERSION "1.3.2"

/**
 * @brief Device identifier used in logs, MQTT messages, and UI.
 */
#define DEV_NAME "ESP32-s2 Ivgeny-DevKit"

/* -------------------------------------------------------------------------- */
/*                              LCD Display Pins                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief LCD interface pin assignments (4-bit parallel mode).
 *
 * The display is a standard HD44780-compatible LCD connected
 * directly to GPIO pins. Adjust these according to your wiring.
 */
#define LCD_PIN_RS  18   /**< Register Select pin */
#define LCD_PIN_EN  16   /**< Enable pin */
#define LCD_PIN_D4   9   /**< Data line D4 */
#define LCD_PIN_D5  11   /**< Data line D5 */
#define LCD_PIN_D6  12   /**< Data line D6 */
#define LCD_PIN_D7  14   /**< Data line D7 */

/**
 * @brief LCD geometry configuration.
 */
#define LCD_COLS 16      /**< Number of character columns */
#define LCD_ROWS 2       /**< Number of display rows */

/* -------------------------------------------------------------------------- */
/*                               LED Indicators                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief LED GPIO pins for system status indication.
 *
 * The LEDs are typically used as follows:
 *  - Green: Wi-Fi connection status
 *  - Yellow: MQTT connection status
 *  - Red: Error indicator
 */
#define GREEN_LED_PIN   1
#define YELLOW_LED_PIN  3
#define RED_LED_PIN     5

/* -------------------------------------------------------------------------- */
/*                             Wi-Fi Reset Button                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief GPIO pin connected to the Wi-Fi reset button.
 *
 * This button allows clearing saved Wi-Fi credentials and restarting
 * the device in Access Point (AP) configuration mode.
 */
#define WIFI_RESET_PIN 0

/* -------------------------------------------------------------------------- */
/*                        Non-Volatile Storage (NVS)                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief NVS partition name used for storing Wi-Fi credentials and settings.
 */
#define NVS_STORAGE_FOLDER "storage"

#endif /* CONFIG_H */