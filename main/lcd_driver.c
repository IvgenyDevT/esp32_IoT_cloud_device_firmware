/**
 * @file lcd_driver.c
 * @brief Low-level driver for controlling HD44780-compatible LCD display in 4-bit mode.
 *
 * ## Overview
 * This module implements a hardware-level driver for a parallel 16x2 (or similar)
 * LCD screen using the HD44780 protocol over GPIO.  
 * It supports 4-bit communication mode and provides functions for initialization,
 * clearing, cursor control, and text display.
 *
 * ## Responsibilities
 *  - Initialize LCD hardware pins (RS, EN, D4–D7)
 *  - Send 4-bit and 8-bit commands or data
 *  - Manage cursor position and text display
 *  - Provide simple text rendering with word wrapping
 *
 * ## Design Notes
 *  - Uses GPIO operations via `hardware_layer.c`
 *  - Timing requirements met with `wait_ms()` and `wait_us()` delays
 *  - Supports up to two display lines (`LCD_ROWS`)
 *
 * ## Dependencies
 *  - hardware_layer.h
 *  - hardware_config.h
 *  - util.h
 *  - esp_log.h
 *  - FreeRTOS
 *
 * ## Example
 * @code
 *  lcd_context_t lcd = { .rs=4, .en=5, .d4=18, .d5=19, .d6=21, .d7=22 };
 *  LCD_initialize(lcd);
 *  LCD_show_lines(0, "Hello World!", lcd);
 * @endcode
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 */


#include "lcd_driver.h"
#include <hardware_layer.h>
#include <util.h>
#include "main.h"
#include "hardware_config.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"


/***/
static const char* TAG = "LCD_driver";

/* -------------------------------------------------------------------------- */
/*                           INTERNAL HELPER FUNCTIONS                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Toggle the LCD enable (EN) pin to latch data.
 *
 * This function pulses the EN pin, which tells the LCD to
 * latch the data present on its data lines.
 *
 * @param en_pin GPIO number for EN pin.
 */
static void refresh_LCD(int en_pin)
{
    gpio_set_level(en_pin, 0);
    wait_us(1);
    gpio_set_level(en_pin, 1);
    wait_us(1);
    gpio_set_level(en_pin, 0);
    wait_us(100);
}




/**
 * @brief Send 4 bits (half byte) of data to the LCD.
 *
 * Maps each bit of the 4-bit value to the LCD’s data pins (D4–D7)
 * and then toggles the EN pin to transmit.
 *
 * @param value 4-bit value to send.
 * @param LCD   LCD context structure with pin assignments.
 */
static void write_4_bits_LCD(uint8_t value, lcd_context_t LCD)
{
    /* Prepare bits */
    int bit_0 = (value >> 0) & 0x01;
    int bit_1 = (value >> 1) & 0x01;
    int bit_2 = (value >> 2) & 0x01;
    int bit_3 = (value >> 3) & 0x01;

    /* Set GPIO levels for data pins */
    gpio_set_level(LCD.d4, bit_0);
    gpio_set_level(LCD.d5, bit_1);
    gpio_set_level(LCD.d6, bit_2);
    gpio_set_level(LCD.d7, bit_3);

    /* Latch data into LCD */
    refresh_LCD(LCD.en);
}




/**
 * @brief Send a full 8-bit command or data byte to the LCD.
 *
 * Splits the byte into two 4-bit halves and transmits them sequentially.
 *
 * @param value 8-bit value to send.
 * @param mode  Register select mode (INSTRUCTION or DATA).
 * @param LCD   LCD context.
 */
static void write_8_bits_LCD(uint8_t value, register_select mode, lcd_context_t LCD)
{
    gpio_set_level(LCD.rs, mode);
    write_4_bits_LCD(MSB_HALF_BYTE(value), LCD);
    write_4_bits_LCD(LSB_HALF_BYTE(value), LCD);
}




/* -------------------------------------------------------------------------- */
/*                           LCD INITIALIZATION                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Initialize the LCD display and configure GPIO pins.
 *
 * Performs all required LCD setup sequences according to HD44780
 * initialization timing for 4-bit mode.
 *
 * @param LCD LCD context containing pin assignments.
 */
void LCD_initialize(lcd_context_t LCD)
{
    /* Configure pins as GPIO outputs */
    config_GPIO(LCD.rs, false, false, FIVE_MA, false, false, DISABLE);
    config_GPIO(LCD.en, false, false, FIVE_MA, false, false, DISABLE);
    config_GPIO(LCD.d4, false, false, FIVE_MA, false, false, DISABLE);
    config_GPIO(LCD.d5, false, false, FIVE_MA, false, false, DISABLE);
    config_GPIO(LCD.d6, false, false, FIVE_MA, false, false, DISABLE);
    config_GPIO(LCD.d7, false, false, FIVE_MA, false, false, DISABLE);

    set_output_direction(LCD.rs);
    set_output_direction(LCD.en);
    set_output_direction(LCD.d4);
    set_output_direction(LCD.d5);
    set_output_direction(LCD.d6);
    set_output_direction(LCD.d7);

    /* Set all outputs LOW initially */
    set_output_level(LCD.rs, LOW);
    set_output_level(LCD.en, LOW);
    set_output_level(LCD.d4, LOW);
    set_output_level(LCD.d5, LOW);
    set_output_level(LCD.d6, LOW);
    set_output_level(LCD.d7, LOW);

    wait_ms(50);

    /* --- Initialization sequence (4-bit mode entry) --- */
    write_4_bits_LCD(0x03, LCD);
    wait_ms(5);
    write_4_bits_LCD(0x03, LCD);
    wait_us(150);
    write_4_bits_LCD(0x03, LCD);

    /* Set 4-bit mode */
    write_4_bits_LCD(0x02, LCD);

    /* Send LCD initialization commands */
    write_8_bits_LCD(LCD_CMD_FUNCTION_SET, INSTRUCTION, LCD);
    write_8_bits_LCD(LCD_CMD_DISPLAY_ON, INSTRUCTION, LCD);
    write_8_bits_LCD(LCD_CMD_CLEAR, INSTRUCTION, LCD);
    write_8_bits_LCD(LCD_CMD_ENTRY_MODE, INSTRUCTION, LCD);

    LCD_set_cursor(0, 0, LCD);

    wait_us(100);

    ESP_LOGI(TAG, "LCD initialized");
}




/* -------------------------------------------------------------------------- */
/*                              BASIC COMMANDS                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Clear the entire LCD display.
 *
 * Sends the `0x01` clear command and waits for LCD to reset.
 *
 * @param LCD LCD context.
 */
void LCD_clear(lcd_context_t LCD)
{
    write_8_bits_LCD(0x01, INSTRUCTION, LCD);
    wait_ms(200);
}




/**
 * @brief Set cursor position on LCD screen.
 *
 * Moves the DDRAM cursor to the specified row and column.
 *
 * @param col Column number (0–LCD_COLS-1).
 * @param row Row number (0–LCD_ROWS-1).
 * @param LCD LCD context.
 */
void LCD_set_cursor(uint8_t col, uint8_t row, lcd_context_t LCD)
{
    static const int row_offsets[] = { LCD_ROW_1_DDRAM_ADDR, LCD_ROW_2_DDRAM_ADDR };
    write_8_bits_LCD(LCD_SET_DDRAM_ADDRESS | (col + row_offsets[row]), INSTRUCTION, LCD);
}




/**
 * @brief Write a single ASCII character to the LCD.
 *
 * @param c   Character to write.
 * @param LCD LCD context.
 */
void LCD_write_char(char c, lcd_context_t LCD)
{
    write_8_bits_LCD((uint8_t)c, DATA, LCD);
}




/**
 * @brief Print a full string to the LCD.
 *
 * Writes each character sequentially, inserting a small delay
 * between writes for display stability.
 *
 * @param string String to print.
 * @param LCD    LCD context.
 */
void LCD_print(const char *string, lcd_context_t LCD)
{
    while (*string)
    {
        wait_ms(15);
        LCD_write_char(*string++, LCD);
    }
}




/* -------------------------------------------------------------------------- */
/*                            TEXT DISPLAY UTILITIES                          */
/* -------------------------------------------------------------------------- */

/**
 * @brief Display a multi-word text string with line wrapping.
 *
 * Splits the input string into words and displays them across
 * the LCD’s available rows and columns, wrapping automatically.
 *  
 * @param line_offset Starting LCD row (0 or 1).
 * @param string      String to display.
 * @param LCD         LCD context.
 * @param clear_screen_before
 */
void LCD_show_lines(unsigned short line_offset, const char *string, lcd_context_t LCD, bool clear_screen_before)
{
    unsigned short col = 0;
    unsigned short row = line_offset;
    char *string_cpy = NULL;
    char *word = NULL;

    /* Duplicate input string for tokenization */
    string_cpy = malloc(strlen(string) + 1);
    if (string_cpy == NULL)
    {
        return;
    }
    strcpy(string_cpy, string);

    /* Clear screen before displaying if required */
    if (clear_screen_before) {
    LCD_clear(LCD);
}

    LCD_set_cursor(0, row, LCD);

    /* Word-by-word rendering */
    word = strtok(string_cpy, " ");
    while (word != NULL)
    {
        /* Wrap line if text exceeds column width */
        if (strlen(word) + col > LCD_COLS)
        {
            if (row < LCD_ROWS)
            {
                row++;
                LCD_set_cursor(0, row, LCD);
            }
            col = 0;
        }

        LCD_print(word, LCD);
        LCD_print(" ", LCD);
        col += strlen(word) + 1;

        word = strtok(NULL, " ");
    }

    free(string_cpy);

    wait_ms(MIN_LCD_SHOW_TIME);
}