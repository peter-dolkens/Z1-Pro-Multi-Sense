#include "light_driver.h"

#include <math.h>
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "led_strip.h"

#define LED_STRIP_GPIO            GPIO_NUM_3
#define LED_STRIP_LED_COUNT       1
#define LED_STRIP_RESOLUTION_HZ   10000000
#define DEFAULT_LEVEL             0xFE
#define DEFAULT_COLOR_X           0x616B
#define DEFAULT_COLOR_Y           0x607D

static const char *TAG = "light_driver";

static led_strip_handle_t s_led_strip = NULL;
static bool s_power = false;
static uint8_t s_level = DEFAULT_LEVEL;
static uint16_t s_color_x = DEFAULT_COLOR_X;
static uint16_t s_color_y = DEFAULT_COLOR_Y;

static inline float clampf(float value, float min, float max)
{
    if (value < min) {
        return min;
    }
    if (value > max) {
        return max;
    }
    return value;
}

static esp_err_t light_driver_apply_state(void)
{
    ESP_RETURN_ON_FALSE(s_led_strip, ESP_ERR_INVALID_STATE, TAG, "LED strip not initialized");

    if (!s_power || s_level == 0) {
        ESP_RETURN_ON_ERROR(led_strip_clear(s_led_strip), TAG, "clear LED strip failed");
        return led_strip_refresh(s_led_strip);
    }

    float level = (float)s_level / 255.0f;
    float x = (float)s_color_x / 65535.0f;
    float y = (float)s_color_y / 65535.0f;

    if (y < 0.0001f) {
        y = 0.0001f;
    }

    float z = 1.0f - x - y;
    if (z < 0.0f) {
        z = 0.0f;
    }

    float Y = level;
    float X = (Y / y) * x;
    float Z = (Y / y) * z;

    float r_lin = X * 3.2406f - Y * 1.5372f - Z * 0.4986f;
    float g_lin = -X * 0.9689f + Y * 1.8758f + Z * 0.0415f;
    float b_lin = X * 0.0557f - Y * 0.2040f + Z * 1.0570f;

    r_lin = fmaxf(0.0f, r_lin);
    g_lin = fmaxf(0.0f, g_lin);
    b_lin = fmaxf(0.0f, b_lin);

    float max_component = fmaxf(r_lin, fmaxf(g_lin, b_lin));
    if (max_component > 1.0f) {
        r_lin /= max_component;
        g_lin /= max_component;
        b_lin /= max_component;
    }

    if (r_lin <= 0.0031308f) {
        r_lin = 12.92f * r_lin;
    } else {
        r_lin = 1.055f * powf(r_lin, 1.0f / 2.4f) - 0.055f;
    }

    if (g_lin <= 0.0031308f) {
        g_lin = 12.92f * g_lin;
    } else {
        g_lin = 1.055f * powf(g_lin, 1.0f / 2.4f) - 0.055f;
    }

    if (b_lin <= 0.0031308f) {
        b_lin = 12.92f * b_lin;
    } else {
        b_lin = 1.055f * powf(b_lin, 1.0f / 2.4f) - 0.055f;
    }

    uint32_t red = (uint32_t)lroundf(clampf(r_lin, 0.0f, 1.0f) * 255.0f);
    uint32_t green = (uint32_t)lroundf(clampf(g_lin, 0.0f, 1.0f) * 255.0f);
    uint32_t blue = (uint32_t)lroundf(clampf(b_lin, 0.0f, 1.0f) * 255.0f);

    ESP_RETURN_ON_ERROR(led_strip_set_pixel(s_led_strip, 0, red, green, blue), TAG, "set pixel failed");
    return led_strip_refresh(s_led_strip);
}

esp_err_t light_driver_init(bool power_on)
{
    if (s_led_strip) {
        ESP_LOGW(TAG, "light driver already initialised");
        return ESP_OK;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = LED_STRIP_GPIO,
        .max_leds = LED_STRIP_LED_COUNT,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = LED_STRIP_RESOLUTION_HZ,
    };

    ESP_RETURN_ON_ERROR(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip), TAG, "create LED strip failed");

    s_power = power_on;
    s_level = DEFAULT_LEVEL;
    s_color_x = DEFAULT_COLOR_X;
    s_color_y = DEFAULT_COLOR_Y;

    if (!s_power) {
        ESP_RETURN_ON_ERROR(led_strip_clear(s_led_strip), TAG, "clear LED strip failed");
        return led_strip_refresh(s_led_strip);
    }

    return light_driver_apply_state();
}

esp_err_t light_driver_set_power(bool power_on)
{
    s_power = power_on;
    return light_driver_apply_state();
}

esp_err_t light_driver_set_level(uint8_t level)
{
    s_level = level;
    return light_driver_apply_state();
}

esp_err_t light_driver_set_color_xy(uint16_t current_x, uint16_t current_y)
{
    s_color_x = current_x;
    s_color_y = current_y;
    return light_driver_apply_state();
}
