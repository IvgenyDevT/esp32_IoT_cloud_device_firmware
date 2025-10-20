

#ifndef MAIN_H
#define MAIN_H
#include <esp_attr.h>
#include "config.h"
#include <stdio.h>




/**
 * @brief Macro: If a function returns an ESP-IDF error (not ESP_OK),
 * log the failure with the provided function name, mark initialization as failed,
 * and jump to `initialize_failure`.
 */
#define END_IF_ERROR(expr, expr_name)                        \
do {                                                     \
esp_err_t __err = (expr);                            \
if (__err != ESP_OK) {                               \
ESP_LOGE(TAG, "%s initialize failed (err=0x%x)", \
(expr_name), __err);                    \
init_success = false;                            \
goto initialize_failure;                         \
}                                                    \
} while (0)

/**
 * @brief Macro: If a function returns false,
 * log the failure with the provided function name, mark initialization as failed,
 * and jump to `initialize_failure`.
 */
#define END_IF_FALSE(expr, expr_name)                        \
do {                                                     \
if (!(expr)) {                                       \
ESP_LOGE(TAG, "%s initialize failed (returned false)", \
(expr_name));                           \
init_success = false;                            \
goto initialize_failure;                         \
}                                                    \
} while (0)


#endif //MAIN_H

