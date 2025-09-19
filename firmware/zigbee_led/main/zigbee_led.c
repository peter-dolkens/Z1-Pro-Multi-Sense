/*
 * Zigbee firmware for the Z1-Pro Multi-Sense using the ESP32-C6.
 *
 * The application exposes a single Home Automation on/off light endpoint
 * that maps the Zigbee on/off cluster to the on-board status LED.
 */

#include <stdbool.h>
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"

static const char *TAG = "ZB_LED";

/**
 * Zigbee network configuration
 */
#define INSTALLCODE_POLICY_ENABLE       false
#define ZIGBEE_LED_ENDPOINT             10
#define ZIGBEE_PRIMARY_CHANNEL_MASK     ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK

/**
 * On-board LED wiring
 */
#define LED_GPIO                        GPIO_NUM_3
#define LED_ON_LEVEL                    1
#define LED_OFF_LEVEL                   0

static bool s_led_initialized = false;

static void led_driver_set(bool on)
{
    gpio_set_level(LED_GPIO, on ? LED_ON_LEVEL : LED_OFF_LEVEL);
}

static esp_err_t led_driver_init(bool on)
{
    if (s_led_initialized) {
        led_driver_set(on);
        return ESP_OK;
    }

    const gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&io_conf), TAG, "Failed to configure LED GPIO");
    led_driver_set(on);
    s_led_initialized = true;
    ESP_LOGI(TAG, "LED driver initialised (GPIO %d, active level %d)", LED_GPIO, LED_ON_LEVEL);
    return ESP_OK;
}

static esp_err_t deferred_driver_init(void)
{
    static bool is_inited = false;
    if (!is_inited) {
        ESP_RETURN_ON_ERROR(led_driver_init(false), TAG, "Failed to init LED driver");
        is_inited = true;
    }
    return ESP_OK;
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_RETURN_ON_FALSE(esp_zb_bdb_start_top_level_commissioning(mode_mask) == ESP_OK, , TAG,
                        "Failed to start Zigbee commissioning");
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *sig_ptr = signal_struct->p_app_signal;
    esp_err_t status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *sig_ptr;

    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Initialising Zigbee stack");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (status == ESP_OK) {
            if (deferred_driver_init() != ESP_OK) {
                ESP_LOGE(TAG, "Deferred LED init failed");
                return;
            }
            ESP_LOGI(TAG, "Device boot mode: %s", esp_zb_bdb_is_factory_new() ? "factory-new" : "networked");
            if (esp_zb_bdb_is_factory_new()) {
                ESP_LOGI(TAG, "Starting network steering");
                esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            }
        } else {
            ESP_LOGW(TAG, "Signal %s failed: %s, retrying", esp_zb_zdo_signal_to_string(sig_type),
                     esp_err_to_name(status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                   ESP_ZB_BDB_MODE_INITIALIZATION, 1000);
        }
        break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (status == ESP_OK) {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG, "Joined network: PAN 0x%04hx Channel %d", esp_zb_get_pan_id(),
                     esp_zb_get_current_channel());
            ESP_LOGI(TAG, "Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x", extended_pan_id[7],
                     extended_pan_id[6], extended_pan_id[5], extended_pan_id[4], extended_pan_id[3],
                     extended_pan_id[2], extended_pan_id[1], extended_pan_id[0]);
        } else {
            ESP_LOGW(TAG, "Network steering failed: %s", esp_err_to_name(status));
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb,
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
    default:
        ESP_LOGI(TAG, "Zigbee signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(status));
        break;
    }
}

static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message)
{
    ESP_RETURN_ON_FALSE(message, ESP_ERR_INVALID_ARG, TAG, "Empty attribute message");
    ESP_RETURN_ON_FALSE(message->info.status == ESP_ZB_ZCL_STATUS_SUCCESS, ESP_ERR_INVALID_ARG, TAG,
                        "Attribute status error: %d", message->info.status);

    ESP_LOGD(TAG, "Attribute write: ep %d cluster 0x%04x attr 0x%04x size %d", message->info.dst_endpoint,
             message->info.cluster, message->attribute.id, message->attribute.data.size);

    if (message->info.dst_endpoint == ZIGBEE_LED_ENDPOINT &&
        message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF &&
        message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID &&
        message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
        bool led_on = message->attribute.data.value ? (*(bool *)message->attribute.data.value) : false;
        ESP_LOGI(TAG, "LED -> %s", led_on ? "ON" : "OFF");
        led_driver_set(led_on);
    }
    return ESP_OK;
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message)
{
    switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
        return zb_attribute_handler((const esp_zb_zcl_set_attr_value_message_t *)message);
    default:
        ESP_LOGW(TAG, "Unhandled Zigbee action callback 0x%x", callback_id);
        return ESP_OK;
    }
}

static void zigbee_task(void *pv_parameters)
{
    esp_zb_cfg_t zb_cfg = {
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,
        .install_code_policy = INSTALLCODE_POLICY_ENABLE,
        .nwk_cfg = {
            .zczr_cfg = {
                .max_children = 0,
            },
        },
    };

    ESP_ERROR_CHECK(esp_zb_init(&zb_cfg));

    esp_zb_on_off_light_cfg_t light_cfg = ESP_ZB_DEFAULT_ON_OFF_LIGHT_CONFIG();
    esp_zb_ep_list_t *led_endpoint = esp_zb_on_off_light_ep_create(ZIGBEE_LED_ENDPOINT, &light_cfg);
    if (!led_endpoint) {
        ESP_LOGE(TAG, "Failed to create LED endpoint");
        vTaskDelete(NULL);
        return;
    }
    ESP_ERROR_CHECK(esp_zb_device_register(led_endpoint));
    ESP_ERROR_CHECK(esp_zb_core_action_handler_register(zb_action_handler));

    esp_zb_set_primary_network_channel_set(ZIGBEE_PRIMARY_CHANNEL_MASK);

    ESP_ERROR_CHECK(esp_zb_start(false));
    esp_zb_stack_main_loop();
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }

    esp_zb_platform_config_t platform_config = {
        .radio_config = {
            .radio_mode = ZB_RADIO_MODE_NATIVE,
        },
        .host_config = {
            .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE,
        },
    };
    ESP_ERROR_CHECK(esp_zb_platform_config(&platform_config));

    xTaskCreate(zigbee_task, "zigbee_main", 4096, NULL, 5, NULL);
}
