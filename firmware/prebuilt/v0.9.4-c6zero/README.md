# CR11Gateway firmware v0.9.4 — ESP32-C6-Zero build

Board profile: `c6zero`. Identical to the default release except for the
addressable LED colour order, which is `RGB` here instead of the `GRB` used by
the ESP32-C6-DevKitC-1. Flashing the wrong variant does not damage anything; it
only exchanges red and green in every LED phase.

Flash it with the matching profile, which refuses a mismatched manifest:

```sh
python3 scripts/flash_firmware.py factory --board c6zero --port /dev/ttyACM0
```

## Validation status

This variant is built from the same sources and the same reproducible
toolchain as the default release, and CI compares it byte-for-byte. On real
hardware it has been confirmed to flash, boot, and drive the LED phases with
correct colours on a Waveshare ESP32-C6-Zero with 8 MB flash.

Zigbee commissioning, the Zigbee2MQTT interview, and the CR11 action matrix
remain validated only on the ESP32-C6-DevKitC-1 v1.2. Treat this variant as
supported for the LED and flashing path and unproven for the radio path until
that board completes the same checklist.

## Files

- `cr11s8uz-gateway-v0.9.4.factory.bin` is a merged image for a complete erase
  and first installation at offset `0x0`.
- `cr11s8uz-gateway-v0.9.4.app.bin` is the application-only image at offset
  `0x10000`; use it only with this project's existing partition layout.
- `bootloader.bin` and `partition-table.bin` are retained for audit and manual
  flashing.
- `manifest.json` records the board profile, LED colour order, build versions,
  reproducibility, offsets, commissioning settings, and binary hashes.
- `SHA256SUMS` covers all release artifacts except this explanatory file.
