# Third-party notices

The project source is MIT-licensed except where a file carries a different
SPDX identifier.

The prebuilt ESP32-C6 firmware contains code from these projects:

- ESP-IDF 5.5.4, Apache-2.0 and additional compatible component licenses:
  <https://github.com/espressif/esp-idf/tree/v5.5.4>
- Espressif ESP Zigbee Library 2.0.3, Apache-2.0:
  <https://github.com/espressif/esp-zigbee-sdk/tree/7eff0fbe19bcf2acd112ba0b5f080530efc49626/components/esp-zigbee-lib>
- Espressif LED Strip 3.0.3, Apache-2.0:
  <https://github.com/espressif/idf-extra-components/tree/7cd447361ca2f0a1c01aa3089e3031f6171b6c7e/led_strip>
- `firmware/main/alarm_timer.c` and `alarm_timer.h`, Espressif Systems,
  CC0-1.0. Their original SPDX notices are preserved.

The Zigbee2MQTT integration is loaded by, but does not redistribute,
Zigbee2MQTT or zigbee-herdsman-converters. Their upstream licenses remain in
effect.

No ORVIBO application package or proprietary CR11 firmware is included in
this repository or in the ESP32-C6 image.
