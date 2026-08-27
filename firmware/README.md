# ESP32-C6 CR11Gateway firmware

This ESP-IDF application turns an ESP32-C6-DevKitC-1 into the non-coordinator
target required to expose all eight ORVIBO CR11S8UZ controls.

See [the firmware design document](../docs/FIRMWARE.md) for endpoints, LED
states, watchdog behavior, flash layout, and output paths. See
[the installation guide](../docs/INSTALLATION.md) for the checksummed prebuilt
flashing flow.

## Build

```sh
source /path/to/esp-idf-v5.5.4/export.sh
idf.py set-target esp32c6
idf.py build
```

Defaults:

- ESP32-C6, 8 MB flash;
- Zigbee End Device;
- BDB Network Steering on Zigbee channels 11 through 26;
- BOOT GPIO 9;
- RGB GPIO 8;
- USB-to-UART console at 115200.

Change board pins or the primary commissioning channel mask under **CR11 bridge
configuration** in `idf.py menuconfig`.

## Host tests

```sh
make host-test
```

These tests compile the protocol, packet, USB frame, and UI state machines as
strict C11 host programs. The USB decoder also has Python tests.

## Development flash

```sh
idf.py -p /dev/serial/by-id/YOUR_ESP32_C6_UART flash monitor
```

Published users should prefer `scripts/flash_firmware.py`, which validates the
release manifest and checksums before opening hardware.
