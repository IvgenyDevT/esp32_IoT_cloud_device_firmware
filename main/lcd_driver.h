/**
 * @file lcd_driver.h
 * @brief Low-level LCD driver interface for HD44780-compatible displays.
 *
 * ## Overview
 * This header defines the public API and register definitions
 * for controlling a character LCD (e.g., 16x2) using 4-bit mode.
 * It provides initialization, cursor control, and text output functions.
 *
 * ## Responsibilities
 *  - Define LCD control commands and addressing macros
 *  - Expose LCD context structure and function prototypes
 *  - Support two-line, 5x8-dot character mode
 *
 * ## Dependencies
 *  - config.h
 *  - stdint.h
 *  - stdbool.h
 *
 * ## Example
 * @code
 *  lcd_context_t lcd = { .rs=4, .en=5, .d4=18, .d5=19, .d6=21, .d7=22 };
 *  LCD_initialize(lcd);
 *  LCD_show_lines(0, "Hello World!", lcd);
 * @endcode
 *
 * @note
 *  All functions are designed for 4-bit mode communication and require
 *  proper GPIO configuration before use.
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 */

#ifndef LCD_DRIVER_H
#define LCD_DRIVER_H
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <config.h>




/* -------------------------------------------------------------------------- */
/*                             LCD CONTEXT STRUCTURE                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief LCD context structure defining GPIO pins and display dimensions.
 */
typedef struct {
    int rs;      /**< Register select pin */
    int en;      /**< Enable pin */
    int d4;      /**< Data pin 4 */
    int d5;      /**< Data pin 5 */
    int d6;      /**< Data pin 6 */
    int d7;      /**< Data pin 7 */
    uint8_t cols; /**< Number of columns on display */
    uint8_t rows; /**< Number of rows on display */
} lcd_context_t;




/* -------------------------------------------------------------------------- */
/*                            ENUMS AND CONSTANTS                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief Register select mode.
 */
typedef enum RS {
    INSTRUCTION,  /**< Command register */
    DATA          /**< Data register */
} register_select;


/** Minimum LCD text display time in milliseconds */
#define MIN_LCD_SHOW_TIME 1500

/** LCD row DDRAM address offsets */
#define LCD_ROW_1_DDRAM_ADDR 0x00
#define LCD_ROW_2_DDRAM_ADDR 0x40




/* -------------------------------------------------------------------------- */
/*                            LCD COMMAND DEFINITIONS                         */
/* -------------------------------------------------------------------------- */

/* Clear and home commands */
#define LCD_CLEAR_DISPLAY       0x01
#define LCD_RETURN_HOME         0x02

/* Entry mode control */
#define LCD_ENTRY_MODE_SET      0x04
#define LCD_ENTRY_LEFT          0x03
#define LCD_ENTRY_SHIFT_INC     0x01

/* Display control */
#define LCD_DISPLAY_CONTROL     0x08
#define LCD_DISPLAY_ON          0x04
#define LCD_CURSOR_ON           0x02
#define LCD_BLINK_ON            0x01

/* Cursor / display shift */
#define LCD_CURSOR_SHIFT        0x10
#define LCD_DISPLAY_MOVE        0x08
#define LCD_MOVE_RIGHT          0x04
#define LCD_MOVE_LEFT           0x00

/* Function set */
#define LCD_FUNCTION_SET        0x20
#define LCD_4BIT_MODE           0x00
#define LCD_2LINE               0x08
#define LCD_5x8DOTS             0x00

/* Predefined LCD commands */
#define LCD_CMD_FUNCTION_SET    (LCD_FUNCTION_SET | LCD_4BIT_MODE | LCD_2LINE | LCD_5x8DOTS)
#define LCD_CMD_DISPLAY_ON      (LCD_DISPLAY_CONTROL | LCD_DISPLAY_ON)
#define LCD_CMD_DISPLAY_OFF     (LCD_DISPLAY_CONTROL)
#define LCD_CMD_CLEAR           LCD_CLEAR_DISPLAY
#define LCD_CMD_HOME            LCD_RETURN_HOME
#define LCD_CMD_ENTRY_MODE      (LCD_ENTRY_MODE_SET | LCD_ENTRY_LEFT)

/* Memory address commands */
#define LCD_SET_DDRAM_ADDRESS   0x80
#define LCD_SET_CGRAM_ADDRESS   0x40

/* Data bit manipulation */
#define MSB_HALF_BYTE(x) ((x) >> (WORD_BITS / 2))
#define LSB_HALF_BYTE(x) ((x) & 0x0F)




/* -------------------------------------------------------------------------- */
/*                            LCD DRIVER FUNCTIONS                            */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the LCD display in 4-bit mode.
 *
 * Configures all pins, sends initialization commands,
 * and prepares the display for text output.
 *
 * @param LCD_context LCD context with pin configuration.
 */
void LCD_initialize(lcd_context_t LCD_context);



/**
 * @brief Clear the LCD display and reset cursor position.
 *
 * @param LCD LCD context.
 */
void LCD_clear(lcd_context_t LCD);



/**
 * @brief Set the cursor to a specific column and row.
 *
 * @param col Column index (0–LCD_COLS-1).
 * @param row Row index (0–LCD_ROWS-1).
 * @param LCD LCD context.
 */
void LCD_set_cursor(uint8_t col, uint8_t row, lcd_context_t LCD);



/**
 * @brief Write a single character to the LCD.
 *
 * @param c   ASCII character to display.
 * @param LCD LCD context.
 */
void LCD_write_char(char c, lcd_context_t LCD);



/**
 * @brief Print a string to the LCD display.
 *
 * @param s   Null-terminated string.
 * @param LCD LCD context.
 */
void LCD_print(const char *s, lcd_context_t LCD);



/**
 * @brief Display a text string with automatic word wrapping across lines.
 *
 * Clears the screen and prints text starting from the given line offset.
 *
 * @param line_offset Starting row index (0 or 1).
 * @param string      String to display.
 * @param LCD         LCD context.
 * @param clear_screen_before
 */
void LCD_show_lines(unsigned short line_offset, const char *string, lcd_context_t LCD, bool clear_screen_before);



/**
 * @brief Retrieve the LCD context pointer (if used globally).
 *
 * @return const lcd_context_t* Pointer to the LCD context structure.
 */
const lcd_context_t *get_lcd_context(void);



#endif /* LCD_DRIVER_H */