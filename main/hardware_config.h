/**
 * @file hardware_config.h
 * @brief Low-level hardware register mapping and bit definitions for ESP32-S2 GPIO and interrupt configuration.
 *
 * This header defines all physical memory-mapped register addresses, bit offsets, and configuration
 * constants required for direct access to GPIO, MUX, RTCIO, and interrupt controller registers.
 * It is intended for use in custom low-level drivers and embedded firmware code that
 * interfaces directly with hardware registers instead of relying on ESP-IDF drivers.
 *
 * ------------------------------------------------------------
 *  Key Responsibilities:
 * ------------------------------------------------------------
 *  - Define base memory offsets for GPIO, MUX, RTCIO, and interrupt registers.
 *  - Provide macros for register offsets (e.g., OUT, ENABLE, LEVEL).
 *  - Define interrupt configuration constants (trigger type, sync, enable bits).
 *  - Provide enumeration types for logic levels, drive strength, and interrupt type.
 *
 * ------------------------------------------------------------
 *  Usage Example:
 * ------------------------------------------------------------
 *  ```c
 *  // Configure GPIO5 as output and set it HIGH:
 *  REG_WRITE(GPIO_EN_W1TS_REG + GPIO_REG_OFFSET_ADDR, BIT_MASK(5));
 *  REG_WRITE(GPIO_OUT_W1TS_REG + GPIO_REG_OFFSET_ADDR, BIT_MASK(5));
 *
 *  // Configure interrupt on GPIO4 for rising edge:
 *  uint32_t reg = REG_READ(GPIO_PIN_REG(4) + GPIO_REG_OFFSET_ADDR);
 *  reg |= (RISING_EDGE << INTERRUPT_SYNC2_SHIFT);
 *  REG_WRITE(GPIO_PIN_REG(4) + GPIO_REG_OFFSET_ADDR, reg);
 *  ```
 */

#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H


/* ------------------------------------------------------------
 *                   General Configuration
 * ------------------------------------------------------------ */

/**
 * @brief Number of bits per GPIO word (used for register-level operations).
 */
#define WORD_BITS 8


/* ------------------------------------------------------------
 *                   Base Register Addresses
 * ------------------------------------------------------------ */

/**
 * @brief Base memory address for GPIO register block.
 */
#define GPIO_REG_OFFSET_ADDR   0x3F404000

/**
 * @brief Base memory address for GPIO multiplexer (function selection).
 */
#define GPIO_MUX_OFFSET_ADDR   0x3F409000

/**
 * @brief Base address for RTC IO registers.
 */
#define RTCIO_REG_OFFSET_ADDR  0x3F408400

/**
 * @brief Base address for GPIO SD (secure digital) peripheral.
 */
#define GPIOSD_REG_OFFSET_ADDR 0x60004F00

/**
 * @brief Base address for dedicated GPIO register block.
 */
#define GPIO_DEICATED_REG_OFFSET_ADDR 0x3F4CF000


/* ------------------------------------------------------------
 *                   GPIO Register Offsets
 * ------------------------------------------------------------ */

/**
 * @brief Macro to compute the address of a MUX register for a given GPIO.
 * @param n GPIO number.
 */
#define GPIO_MUX_REG(n)   (0x4 + (4 * (n)))

/** @brief Register controlling GPIO output value (bit mask per pin). */
#define GPIO_OUT_REG        0x04
/** @brief Register for setting single GPIO high. */
#define GPIO_OUT_W1TS_REG   0x08
/** @brief Register for clearing single GPIO low. */
#define GPIO_OUT_W1TC_REG   0x0C

/** @brief Enable GPIO as output. */
#define GPIO_EN_W1TS_REG    0x24
/** @brief Disable GPIO output. */
#define GPIO_EN_W1TC_REG    0x28
/** @brief Register for reading output enable state. */
#define GPIO_ENABLE_REG     0x20

/** @brief Register to read GPIO logic levels. */
#define GPIO_LEVEL_REG      0x3C


/* ------------------------------------------------------------
 *                   Interrupt Configuration
 * ------------------------------------------------------------ */

/**
 * @brief Per-pin interrupt configuration register.
 * @param n GPIO number.
 */
#define GPIO_PIN_REG(n)   (0x74 + (4 * (n)))

/** @brief Bit position for interrupt type (rising/falling). */
#define INTERRUPT_TYPE_SHIFT 7

/** @brief Synchronization configuration bits. */
#define INTERRUPT_SYNC2_SHIFT 0
#define INTERRUPT_SYNC1_SHIFT 3

/** @brief Interrupt synchronization modes. */
#define INTERRUPT_SYNC_DISABLED     0
#define INTERRUPT_SYNC_FALLING_EDGE 1
#define INTERRUPT_SYNC_RISING_EDGE  2

/** @brief Bit positions for interrupt enable control. */
#define INTERRUPT_ENABLE_SHIFT      13
#define INTERRUPT_NMI_ENABLE_SHIFT  14

/** @brief Registers to manually clear or set interrupt triggers. */
#define GPIO_INTERRUPT_W1TS_REG     0x48
#define GPIO_INTERRUPT_W1TC_REG     0x4C
#define GPIO_INTERRUPT_REG          0x0044


/* ------------------------------------------------------------
 *                   Interrupt Matrix (CPU mapping)
 * ------------------------------------------------------------ */

/** @brief Base address of the interrupt matrix block. */
#define INTERRUPT_MATRIX_BASE_ADDRESS       0x3F4C2000
/** @brief Offset for mapping GPIO interrupts to CPU. */
#define INTERRUPT_MATRIX_PRO_GPIO_MAP_REG   0x005C

/** @brief CPU interrupt number assignments. */
#define CPU_GPIO_INTERRUPT_NUM   4
#define CPU_UART0_INTERRUPT_NUM  5
#define CPU_UART1_INTERRUPT_NUM  6

/** @brief Bit mask generator macro (1 << x). */
#define BIT_MASK(x) (1UL << (x))


/* ------------------------------------------------------------
 *                   Enumerations
 * ------------------------------------------------------------ */

/**
 * @brief Defines GPIO drive current strength.
 */
typedef enum GPIO_strength {
    FIVE_MA,     /**< 5mA drive strength */
    TEN_MA,      /**< 10mA drive strength */
    TWENTY_MA,   /**< 20mA drive strength */
    FORT_MA      /**< 40mA drive strength */
} GPIO_strength;

/**
 * @brief Defines logic levels for GPIO signals.
 */
typedef enum level {
    LOW,   /**< Logic low (0V) */
    HIGH   /**< Logic high (3.3V) */
} level;

/**
 * @brief Defines available GPIO interrupt trigger types.
 */
typedef enum interrupt_type {
    DISABLE,       /**< No interrupt */
    RISING_EDGE,   /**< Trigger on rising edge */
    FALLING_EDGE,  /**< Trigger on falling edge */
    BOTH_EDGES,    /**< Trigger on both edges */
    LOW_LEVEL,     /**< Trigger when signal is low */
    HIGH_LEVEL     /**< Trigger when signal is high */
} interrupt_type;


#endif // HARDWARE_CONFIG_H