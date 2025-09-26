#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LIGHT_DEFAULT_OFF false
#define LIGHT_DEFAULT_ON  true

esp_err_t light_driver_init(bool power_on);
esp_err_t light_driver_set_power(bool power_on);
esp_err_t light_driver_set_level(uint8_t level);
esp_err_t light_driver_set_color_xy(uint16_t current_x, uint16_t current_y);

#ifdef __cplusplus
}
#endif
