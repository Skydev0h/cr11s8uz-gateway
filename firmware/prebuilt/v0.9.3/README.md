# CR11Gateway firmware v0.9.3

Tested target: ESP32-C6-DevKitC-1 v1.2 with 8 MB flash. Factory-new devices
use BDB Network Steering across Zigbee channels 11 through 26.

- `cr11s8uz-gateway-v0.9.3.factory.bin` is a merged image for a complete erase
  and first installation at offset `0x0`.
- `cr11s8uz-gateway-v0.9.3.app.bin` is the application-only image at offset
  `0x10000`; use it only with this project's existing partition layout.
- `bootloader.bin` and `partition-table.bin` are retained for audit and manual
  flashing.
- `manifest.json` records build versions, reproducibility, offsets,
  commissioning settings, and binary hashes.
- `SHA256SUMS` covers all release artifacts except this explanatory file.

Use `scripts/flash_firmware.py` from the repository root. It validates the
release before flashing and requires explicit confirmation for a factory erase.
