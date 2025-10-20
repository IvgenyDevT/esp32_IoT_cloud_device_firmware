/**
 * @file util.h
 * @brief General-purpose helper utilities and macros for ESP-IDF projects.
 *
 * ## Overview
 * This header provides:
 *  - Common error-handling and validation macros for cleaner ESP-IDF code.
 *  - Delay wrappers for millisecond and microsecond timing.
 *  - Safe string manipulation helpers.
 *  - Simple text processing (e.g. URL decoding replacements).
 *
 * ## Design Goals
 *  - Improve code readability by abstracting repetitive patterns.
 *  - Provide thread-safe, lightweight utilities suitable for FreeRTOS.
 *  - Remain independent of application logic.
 *
 * @note
 *  - This header requires `esp_log.h` and `esp_err.h` to be included
 *    in any file that uses the logging/error macros.
 *  - None of the delay functions should be used inside ISRs.
 *
 * @dependencies
 *  - FreeRTOS (`vTaskDelay`, `pdMS_TO_TICKS`)
 *  - ESP-ROM (`esp_rom_delay_us`)
 *  - Standard C library (`string.h`)
 *
 * @author
 *  Ivgeny Tokarzhevsky
 */

#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdint.h>


/* -------------------------------------------------------------------------- */
/*                              Error Handling                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief Evaluate an expression that returns an `esp_err_t`, log on failure, and return the error.
 *
 * Example usage:
 * ```c
 * RETURN_IF_ERROR(nvs_flash_init());
 * ```
 *
 * @param _expr Expression to evaluate (must return `esp_err_t`).
 *
 * @note Requires a `TAG` string defined in the calling file for logging.
 */
#define RETURN_IF_ERROR(_expr) do {                                    \
    esp_err_t __error = (_expr);                                       \
    if (__error != ESP_OK) {                                           \
        ESP_LOGE(TAG, #_expr " failed: %s", esp_err_to_name(__error)); \
        return __error;                                                \
    }                                                                  \
} while(0)



/**
 * @brief Validate a condition, log a message, and return an error if false.
 *
 * Example usage:
 * ```c
 * RETURN_IF_FALSE(pointer != NULL, ESP_FAIL, "Pointer is NULL");
 * ```
 *
 * @param _cond Boolean condition to check.
 * @param _err  Error code to return if condition is false.
 * @param _msg  Message to log on failure.
 */
#define RETURN_IF_FALSE(_cond, _err, _msg) do {     \
    if (!(_cond)) {                                 \
        ESP_LOGE(TAG, "%s", _msg);                  \
        return (_err);                              \
    }                                               \
} while(0)



/* -------------------------------------------------------------------------- */
/*                                  Timing                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief Delay execution for a given number of milliseconds (non-blocking for scheduler).
 *
 * Wrapper around `vTaskDelay(pdMS_TO_TICKS(ms))`.
 *
 * @param ms Duration in milliseconds.
 */
void wait_ms(unsigned long long ms);



/**
 * @brief Delay execution for a given number of microseconds (busy wait).
 *
 * Wrapper around `esp_rom_delay_us(us)`.
 *
 * @param us Duration in microseconds.
 *
 * @warning Blocking function — should not be used for long delays.
 */
void wait_us(unsigned long long us);



/* -------------------------------------------------------------------------- */
/*                             String Utilities                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief Replace all '+' characters in a string with spaces.
 *
 * Useful for URL decoding or simple text normalization.
 *
 * @param str Pointer to the null-terminated string to modify in place.
 */
void replace_plus_with_space(char *str);



/**
 * @brief Safe version of `strlcpy()` that checks for NULL inputs.
 *
 * If `src` is NULL, writes an empty string to `dst`.
 * If `dst` is NULL or `dst_sz` is zero, does nothing.
 *
 * @param[out] dst     Destination buffer.
 * @param[in]  dst_sz  Size of the destination buffer.
 * @param[in]  src     Source string (can be NULL).
 */
void s_strcpy(char* dst, size_t dst_sz, const char* src);



#endif /* UTIL_H */