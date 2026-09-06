# Installation and operation

This guide starts with an unconfigured ESP32-C6 and ends with eight CR11S8UZ
buttons producing actions on one Zigbee2MQTT device entity.

## 1. Check the prerequisites

You need:

- an ORVIBO CR11S8UZ already joined to the target Zigbee network, or ready to
  join it;
- one ESP32-C6-DevKitC-1 v1.2 with 8 MB flash;
- a USB data cable connected to the board's USB-to-UART port;
- Zigbee2MQTT 2.13.0 and access to its persistent data directory;
- Python 3.11 or newer for the helper scripts.

The native ESP32-C6 USB connector is optional. It carries the compact event
stream described in [NATIVE_USB.md](NATIVE_USB.md). Both USB connectors may be
attached to one computer. Do not simultaneously inject power through the
development board headers.

On ESP32-C6-DevKitC-1 v1.2, install the J5 jumper between `ESP_3V3` and the RGB
LED supply if you want visual status. A missing jumper does not disable Zigbee,
but the onboard addressable LED remains dark.

## 2. Flash the gateway

Clone the repository and create an isolated Python environment:

```sh
git clone https://github.com/Skydev0h/cr11s8uz-gateway.git
cd cr11s8uz-gateway
python3 -m venv .venv
./.venv/bin/python -m pip install -r requirements-flash.txt
```

Find the stable path for the USB-to-UART connector:

```sh
ls -l /dev/serial/by-id/
```

Preview the destructive factory operation without touching hardware:

```sh
./.venv/bin/python scripts/flash_firmware.py factory \
  --port /dev/serial/by-id/YOUR_ESP32_C6_UART \
  --dry-run
```

Then flash it:

```sh
./.venv/bin/python scripts/flash_firmware.py factory \
  --port /dev/serial/by-id/YOUR_ESP32_C6_UART
```

Boards other than the reference ESP32-C6-DevKitC-1 may need a different
addressable-LED byte order. Add `--board c6zero` for the Waveshare
ESP32-C6-Zero; the flasher refuses a release that does not match the profile you
asked for. See [the firmware guide](FIRMWARE.md) for the list of profiles.

Factory mode validates all SHA-256 checksums, asks you to type `ERASE`, erases
the entire board, and writes one merged image at offset `0x0`. If automatic
reset fails, hold BOOT, tap RESET, release BOOT, and retry. A lower baud rate is
available for marginal cables:

```sh
./.venv/bin/python scripts/flash_firmware.py factory \
  --port /dev/serial/by-id/YOUR_ESP32_C6_UART \
  --baud 115200
```

For a board already running this project, application-only upgrade mode writes
the app at `0x10000` and preserves the Zigbee storage partitions:

```sh
./.venv/bin/python scripts/flash_firmware.py upgrade \
  --port /dev/serial/by-id/YOUR_ESP32_C6_UART
```

Do not use upgrade mode to convert an unrelated firmware layout.

## 3. Install the Zigbee2MQTT integration

Back up the Zigbee2MQTT data directory, then stop Zigbee2MQTT. The correct host
path is the directory that contains its active `configuration.yaml`, database,
and coordinator backup. For a container deployment, use the host side of the
data volume, not `/app/data` inside a stopped container.

Preview and install:

```sh
python3 scripts/install_z2m.py --data-dir /path/to/zigbee2mqtt/data --dry-run
python3 scripts/install_z2m.py --data-dir /path/to/zigbee2mqtt/data
```

The installer manages exactly these destinations:

```text
external_converters/cr11s8uz_external.mjs
external_converters/cr11_gateway_external.mjs
external_extensions/cr11_gateway_router_extension.mjs
```

It refuses a symlink, a non-file destination, or an existing different file.
If a previous project version is present, inspect the diff and explicitly
request a timestamped backup and replacement:

```sh
python3 scripts/install_z2m.py --data-dir /path/to/zigbee2mqtt/data --replace
```

The installer does not modify configuration. Add or merge this setting:

```yaml
advanced:
  enable_external_js: true
```

Restart Zigbee2MQTT. Verify that both external converters and the external
extension load without an exception. You can later check that installed files
still match the checkout:

```sh
python3 scripts/install_z2m.py --data-dir /path/to/zigbee2mqtt/data --check
```

Because external JavaScript runs with Zigbee2MQTT's privileges, protect the
data directory and install only reviewed code.

## 4. Join the CR11Gateway

Open permit-join in Zigbee2MQTT. On a factory-new gateway the LED emits a short
red pulse once per second.

Hold the board's BOOT button for at least one second:

1. Half-bright yellow while BOOT is held means the request is being armed.
2. Yellow blinking at 0.5 seconds on and 0.5 seconds off means network steering.
3. Alternating yellow and green means the device has joined and Zigbee2MQTT is
   interviewing/configuring it.
4. A short green pulse every five seconds means the gateway is ready and its
   Zigbee2MQTT supervisor lease is healthy.

Network steering scans Zigbee channels 11 through 26 automatically. Leave
permit-join open until the scan and interview finish. After a successful join,
the selected network and channel are restored from persistent storage on normal
reboots and application-only upgrades.

The device should appear as vendor `Skydev0h`, model `CR11Gateway`, Zigbee model
`CR11-Bridge`. Close permit-join after commissioning.

The gateway is an always-on End Device. Its topology icon may therefore differ
from a mains-powered Router even though it is permanently powered and listening.

## 5. Join or wake the CR11S8UZ

If the CR11 is already joined to the same Zigbee network, do not reset it. Press
one upper button to wake it and allow Zigbee2MQTT to apply the external
converter. Existing remotes can be upgraded in place.

For a complete stock join:

1. Open permit-join.
2. Press both lower `+` controls together, buttons 5 and 7.
3. After three red flashes, press the upper controls in this exact order:
   left upper `●`, right upper `■`, left middle `● ●`, right middle `■ ■`. This
   is buttons `1, 2, 3, 4` and internal keys `K3, K11, K7, K15`.
4. Fast blinking means the remote is searching. Keep it awake if interview
   needs another upper press.

The remote should be identified as `CR11S8UZ` by ORVIBO. The canonical model
identity lets the Zigbee2MQTT frontend reuse its standard device image. A full
stock join clears both proprietary action tables, so gateway mode must be
configured afterward.

## 6. Route all buttons through the gateway

Open the CR11 device and select **Exposes**.

1. Hold the two right-lower `+` and `-` controls together for more than four
   seconds.
2. Wait for the remote to blink red and for **Assignment mode** to become
   `active`. **Setup state** becomes `ready`.
3. Click **Configure** under **Route through CR11Gateway**.
4. Watch **Setup progress** move from `0/12` through `12/12`.
5. Wait for **Setup state** `gateway_ready` and **Last configured route**
   `gateway`.
6. Press either right-lower control once to leave assignment mode. The remote
   reports `idle`.

The converter deliberately refuses to start from cached state. It must observe
a fresh `active` report in the current Zigbee2MQTT process, because an asleep
remote cannot reliably receive all 13 operations: one clear plus 12 writes.

The first use of each upper click, hold, and release record can take longer
while the remote resolves the gateway IEEE address. As a warm-up, perform one
click and one deliberate hold/release on each upper selector, waiting briefly
between selectors. Then test the lower controls.

Blue gateway flashes represent upper actions. Pink flashes represent lower
actions. All actions should appear on the original CR11 entity, not as separate
button entities on the gateway.

## 7. Choose action naming

The CR11 device-specific setting **CR11 gateway action mode** has two values:

- `plain` emits names such as `button_5_click`;
- `selected` emits contextual lower names such as
  `button_5_click_selected_3`.

Upper buttons always use `button_1` through `button_4`. The setting is local to
Zigbee2MQTT and does not rewrite the remote.

Home Assistant learns action choices as they are observed. Exercise each action
once before building automations. Identical consecutive actions are still
published as separate events; use device triggers or MQTT event messages rather
than a state-change-only comparison.

## 8. Return to direct mode

Direct mode does not require a working gateway:

1. Enter CR11 assignment mode with the two right-lower controls.
2. Click **Restore** under **Restore direct mode**.
3. Wait for `1/1` and `direct_ready`.
4. Exit assignment mode with either right-lower control.

Buttons 1 through 4 then report directly to the coordinator using stock
behavior. Buttons 5 through 8 become inactive because they no longer have a
selected non-coordinator target.

## 9. Remove or reset the gateway

While the gateway is joined, hold BOOT for five seconds. Red flashing accelerates
after two and four seconds; release early to cancel. At five seconds the device
requests a normal Zigbee leave and shows solid red while it waits:

- a successful leave produces one second of green;
- a three-second timeout arms a forced Zigbee-storage wipe and produces one
  second of purple.

Both results end in a double-red terminal blink. Power-cycle the board before
it accepts BOOT again. This intentional terminal state prevents an accidental
immediate rejoin after a destructive operation.

Resetting the gateway does not alter CR11 records. If the same gateway rejoins
with its preserved IEEE address but a different 16-bit network address, enter
assignment mode and configure the 12 records again to refresh all caches.

## 10. Uninstall cleanly

Before removing the integration, restore direct mode on every configured CR11
while Zigbee2MQTT and the gateway are still available. This prevents remotes
from continuing to target an absent gateway.

Then:

1. Reset or remove the CR11Gateway from the Zigbee network.
2. Stop Zigbee2MQTT.
3. Remove the three files listed in section 3 from its data directory, retaining
   any timestamped backups you still need.
4. Remove `advanced.enable_external_js` only when no other external converter or
   extension requires it.
5. Restart Zigbee2MQTT and verify the stock/direct CR11 actions.

Uninstalling files does not rewrite a sleeping remote. Always perform the direct
restore first.
