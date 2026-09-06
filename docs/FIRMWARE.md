# CR11Gateway firmware

## Design goals

The ESP32-C6 firmware is intentionally narrow:

- native Zigbee only, with no Wi-Fi, BLE, IP stack, web server, or MQTT client;
- one physical BOOT button and one onboard RGB LED;
- one compact native USB event stream;
- an always-on Zigbee End Device role to avoid routing and parent side effects;
- bounded parsing, fixed-size packets, no dynamic JSON construction in the hot
  path unless debug logging is explicitly enabled;
- deterministic local recovery if Zigbee2MQTT stops supervising the gateway.

Its job is to impersonate only the standard target types the CR11 understands,
normalize received commands, and send one small event back to the coordinator.

## Zigbee endpoints

| Endpoint | CR11 selector | Device role |
|---:|---|---|
| 10 | left upper `●`, button 1 | Dimmable Light server |
| 11 | left middle `● ●`, button 3 | Dimmable Light server |
| 12 | right upper `■`, button 2 | Dimmable Light server |
| 13 | right middle `■ ■`, button 4 | Dimmable Light server |
| 20 | diagnostic compatibility path | Window Covering server |
| 21 | gateway control and event transport | vendor cluster `0xFC11` |

Four light endpoints are necessary because the destination endpoint is part of
the CR11 action record. They preserve a bijection between physical selectors
and targets without allocating a separate Zigbee device.

The gateway accepts standard On/Off and Level Control commands. Upper click,
hold, and release records use distinct command and payload combinations so the
firmware can recover all three states while preserving the lower rocker's
selected endpoint. Endpoint 20 remains for protocol diagnostics; normal
four-target setup uses endpoints 10 through 13.

## BOOT and RGB interface

### Factory-new

| State | LED |
|---|---|
| Waiting | red for 0.1 s every 1 s |
| BOOT held below 1 s | half-bright yellow |
| Network steering | yellow, 0.5 s on / 0.5 s off |
| Joined, waiting for ready | alternating yellow and green every 0.5 s |

A BOOT hold of one second starts steering. The button must be released before a
new BOOT action is accepted.

### Ready

| Event or state | LED |
|---|---|
| Healthy idle | green for 0.1 s every 5 s |
| Upper CR11 event | blue for 0.1 s |
| Lower CR11 event | pink for 0.1 s |
| Lease age 10 to 30 s | pulse every 2 s, green gradually changing to orange |
| Lease not armed after watchdog boot | green, off, orange pulses every 3 s |
| Restoring an existing network | green for 0.1 s every 2 s |

An event pulse restarts the idle heartbeat phase so a green pulse does not
immediately overwrite feedback.

### Destructive reset

Holding BOOT on a joined device acts like a visible spring-loaded confirmation:

| Hold time | LED |
|---:|---|
| 0 to 2 s | red, 0.5 s on / 0.5 s off |
| 2 to 4 s | red, 0.25 s on / 0.25 s off |
| 4 to 5 s | red, 0.1 s on / 0.1 s off |
| after 5 s | solid red while leaving or arming a forced wipe |

Release before five seconds to cancel. A graceful leave shows green for one
second. A timed-out leave shows purple for one second after the forced wipe is
durably armed. Both end in the terminal pattern: red 0.1 s, off 0.1 s, red
0.1 s, off 0.7 s. A power cycle is required.

The leave result never appears before at least one second of solid red. A normal
leave may take up to three seconds; after that the firmware records a deferred
wipe and restarts into the terminal state only after the write succeeds.

## Supervisor lease and watchdog

The Zigbee2MQTT extension sends a zero-payload `heartbeat` command to endpoint
21 every five seconds when exactly one gateway is present. The converter's
post-interview `ready` command also arms the lease.

- At 10 seconds without a heartbeat, the RGB warning begins.
- At approximately 30 seconds, a FreeRTOS task watchdog reset recovers the
  gateway and radio stack.
- A normal reboot of an already joined device arms its own initial lease.
- A watchdog reboot deliberately does not arm one, preventing a reboot loop
  while Zigbee2MQTT remains unavailable.
- The first later `ready` or `heartbeat` returns it to normal service.

This supervisor does not replace internal watchdog discipline. The task
watchdog is fed by the firmware while the external lease is healthy; Zigbee2MQTT
controls only whether that feed remains authorized.

Brownout detection is provided by ESP-IDF and resets the MCU when supply voltage
is unsafe. Zigbee membership is stored separately in NVS, so an ordinary power
or brownout restart attempts network restoration rather than factory-new
commissioning.

## Output paths

Every valid forwardable CR11 event is sent through two independent outputs:

1. Zigbee cluster `0xFC11`, command `0x00`, to coordinator endpoint 1;
2. native USB as one checksummed ASCII frame.

The Zigbee path is the supported Zigbee2MQTT integration. Native USB is a low
latency future integration point and a diagnostic escape hatch. A full queue or
disconnected USB host drops only the USB copy; it does not block Zigbee event
forwarding.

The USB-to-UART console is for ESP-IDF logs and explicit debug control. Normal
event handling does not build verbose JSON just for logging. Use `debug on` to
enable detailed receive and LED/state messages, and `debug off` to return to the
quiet hot path. `help` prints the available development commands.

## Flash layout

| Offset | Partition or image | Size |
|---:|---|---:|
| `0x0000` | second-stage bootloader | generated |
| `0x8000` | partition table | generated |
| `0x10000` | factory application | 1536 KiB partition |
| `0x190000` | `zb_storage` NVS | 32 KiB |
| after `zb_storage` | `zb_fct` | 1 KiB |

The full merged factory image contains the first three artifacts and is written
at `0x0` after a complete erase. Application-only upgrades write at `0x10000`
and preserve Zigbee state.

## Board configuration

Defaults target ESP32-C6-DevKitC-1 v1.2:

- BOOT: active-low GPIO 9;
- onboard addressable RGB data: GPIO 8;
- flash: 8 MB, DIO, 80 MHz;
- console: USB-to-UART at 115200;
- commissioning: BDB Network Steering on channels 11 through 26;
- role: receiver-on-while-idle End Device.

The default primary BDB mask contains every 2.4 GHz Zigbee channel. A custom
primary mask may be selected in `idf.py menuconfig`; its complement is used as
the secondary fallback set. Once joined, the gateway restores its saved network
and channel from NVS. Do not flash the supplied 8 MB image onto an unknown
ESP32-C6 layout without checking its flash size and pin wiring.

The current partition map ends at `0x198400`, below 2 MiB. A source build can
therefore be adapted to a 4 MB ESP32-C6 board by selecting the correct flash
size and validating that board's pins and layout. The published prebuilt image
remains qualified only for the listed 8 MB board.

## Board profiles

Addressable LEDs differ in byte order between boards, so the colour order is a
build option rather than a constant.

| Profile | Board | LED order | Prebuilt |
|---|---|---|---|
| `devkitc1` | ESP32-C6-DevKitC-1 v1.2, 8 MB | `GRB` | `firmware/prebuilt/v<version>/` |
| `c6zero` | Waveshare ESP32-C6-Zero, 8 MB | `RGB` | `firmware/prebuilt/v<version>-c6zero/` |

Both variants come from the same sources and the same reproducible toolchain,
and CI rebuilds and compares each one byte-for-byte. The manifest records
`board_profile` and `rgb_order`, and the flasher refuses a release that does not
match the profile it was asked for:

```sh
python3 scripts/flash_firmware.py factory --board c6zero --port /dev/ttyACM0
```

`devkitc1` remains the reference board: Zigbee commissioning, the Zigbee2MQTT
interview, and the full CR11 action matrix are validated only there.

To build a profile from source, apply its defaults fragment on top of
`sdkconfig.defaults` and keep its build directory separate:

```sh
cd firmware
idf.py -B build-c6zero \
  -D SDKCONFIG=sdkconfig.c6zero \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.c6zero.defaults" \
  build
```

The colour order is also reachable interactively in `idf.py menuconfig` under
**CR11 bridge configuration**. A board whose order is wrong exchanges red and
green in every phase; see the troubleshooting guide for the signature and the
check that separates it from a genuine state problem.
