idf_component_register(SRCS "zigbee_led.c"
                      INCLUDE_DIRS "."
                      REQUIRES esp-zigbee-lib
                      PRIV_REQUIRES nvs_flash)
