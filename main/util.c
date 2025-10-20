/**
 * @file util.c
 * @brief General-purpose utility functions for timing and safe string operations.
 *
 * ## Overview
 * This module provides lightweight helper utilities commonly used
 * across multiple project components — mainly timing wrappers,
 * safe string copy helpers, and basic character processing utilities.
 *
 * ## Features
 *  - Delay wrappers for milliseconds and microseconds.
 *  - Safe string copy (`s_strcpy`) with NULL protection.
 *  - Simple character replacement utility for URL decoding (`+ → space`).
 *
 * @note
 *  - `wait_ms()` and `wait_us()` are designed for use in FreeRTOS-based tasks.
 *  - All utilities are blocking and should not be used in ISR context.
 *
 * @dependencies
 *  - FreeRTOS (`vTaskDelay`, `pdMS_TO_TICKS`)
 *  - ESP-ROM (`esp_rom_delay_us`)
 *  - Standard C library (`string.h`)
 *
 * @author
 *  Ivgeny Tokarzhevsky
 *
 */

#include "util.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include <freertos/projdefs.h>



/* -------------------------------------------------------------------------- */
/*                                   Timing                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief Delay execution for a given number of milliseconds.
 *
 * Wrapper around FreeRTOS `vTaskDelay()` using millisecond granularity.
 *
 * @param ms Duration in milliseconds to delay.
 *
 * @note Non-blocking for the scheduler — yields CPU time to other tasks.
 */
void wait_ms(unsigned long long ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}



/**
 * @brief Busy-wait delay for a given number of microseconds.
 *
 * Wrapper around `esp_rom_delay_us()` (ROM-based delay function).
 *
 * @param us Duration in microseconds to delay.
 *
 * @warning This is a blocking delay — do not use for long intervals.
 */
void wait_us(unsigned long long us) {
    esp_rom_delay_us(us);
}



/* -------------------------------------------------------------------------- */
/*                             String processing                              */
/* -------------------------------------------------------------------------- */

/**
 * @brief Replace all '+' characters with spaces in a string.
 *
 * Commonly used to decode URL query parameters where '+' represents a space.
 *
 * @param str Pointer to a null-terminated string to modify in place.
 */
void replace_plus_with_space(char *str) {
    for (int i = 0; str[i]; i++) {
        if (str[i] == '+') {
            str[i] = ' ';
        }
    }
}



/**
 * @brief Safe version of `strlcpy()` that handles NULL inputs.
 *
 * If `src` is NULL, this function writes an empty string to `dst`.
 * If `dst` is NULL or has zero size, it performs no operation.
 *
 * @param[out] dst     Destination buffer.
 * @param[in]  dst_sz  Size of the destination buffer (in bytes).
 * @param[in]  src     Source string (can be NULL).
 */
void s_strcpy(char* dst, size_t dst_sz, const char* src) {
    if (!dst || dst_sz == 0)
        return;

    if (!src) {
        dst[0] = '\0';
        return;
    }

    strlcpy(dst, src, dst_sz);
}