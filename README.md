# Sensy-One Z1 Pro Multi-Sense — Zigbee LED Firmware

This repository now contains a fresh ESP-IDF firmware for the Sensy-One Z1 Pro Multi-Sense hardware configured with the ESP32-C6. The firmware replaces the previous ESPHome Wi-Fi build and focuses exclusively on exposing the on-board status LED over Zigbee using the standard Home Automation on/off light cluster.

## Features

- **ESP32-C6 Zigbee stack** using the native IEEE 802.15.4 radio and Espressif's Zigbee Home Automation libraries.
- **Single HA on/off endpoint** (endpoint 10) that maps directly to the rear WS2812 status LED.
- **Simple GPIO driver** that toggles the LED between on/off states in response to Zigbee attribute writes.
- **Factory-new behaviour** with network steering enabled so the node can be commissioned from any compliant Zigbee coordinator (e.g., Home Assistant ZHA, Zigbee2MQTT).

## Hardware assumptions

- The build targets the **ESP32-C6** MCU revision of the board.
- The on-board WS2812 LED is driven on **GPIO3**. If your revision differs, adjust `LED_GPIO` in `firmware/zigbee_led/main/zigbee_led.c`.

## Repository layout

```
firmware/
  zigbee_led/
    CMakeLists.txt          # ESP-IDF project entry point
    partitions.csv          # Custom partition table with Zigbee storage slots
    sdkconfig.defaults      # Default SDK configuration targeting ESP32-C6 + Zigbee router role
    main/
      CMakeLists.txt
      zigbee_led.c          # Application source code implementing the Zigbee LED endpoint
assets/
  config/                   # Plotly card + zone editor (legacy assets retained for reference)
  images/                   # Product imagery
  step/                     # Mechanical CAD exports
```

## Building the firmware

1. Install the [ESP-IDF 5.2 (or newer) toolchain](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/get-started/index.html) with the `esp32c6` target enabled.
2. Export the environment variables: `source $IDF_PATH/export.sh`.
3. Configure, build, and flash:

```bash
cd firmware/zigbee_led
idf.py set-target esp32c6   # optional if IDF_TARGET not already set
idf.py build
idf.py -p /dev/ttyUSBx flash monitor
```

The provided `sdkconfig.defaults` already selects the Zigbee stack (`CONFIG_ZB_ENABLED=y`, `CONFIG_ZB_ZCZR=y`) and loads the `partitions.csv` table that reserves dedicated storage for Zigbee persistence.

## Commissioning and usage

1. Power the device and put your Zigbee coordinator into pairing/permit-join mode.
2. On first boot the node starts in factory-new mode, runs the Zigbee Base Device Behaviour (BDB) initialization, and automatically begins network steering.
3. Once joined, the device exposes a single on/off light entity. Toggle it from your coordinator UI or via automation to switch the LED on or off.
4. To factory-reset, clear the NVS partition (e.g., hold BOOT at reset and erase, or flash with `idf.py erase_flash`). On the next boot the node will again present itself as factory-new and ready to commission.

## Customising

- **Channel mask**: adjust `ZIGBEE_PRIMARY_CHANNEL_MASK` in `zigbee_led.c` to limit the channels used during steering.
- **LED polarity/pin**: change `LED_GPIO`, `LED_ON_LEVEL`, and `LED_OFF_LEVEL` to match your hardware.
- **Endpoint metadata**: extend the basic cluster information or add identify behaviours by modifying the Zigbee endpoint creation block in `zigbee_led.c`.

## Licensing

All firmware code in `firmware/zigbee_led` is provided under the Apache 2.0 license unless otherwise noted.
