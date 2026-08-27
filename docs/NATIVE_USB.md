# Native USB event stream

CR11Gateway duplicates every valid forwardable event to the ESP32-C6 native
USB Serial/JTAG connector. This path avoids a Zigbee return trip and provides a
future integration point for a local service, while the supported Zigbee2MQTT
path continues to operate independently.

## Transport

- Interface: ESP32-C6 native USB Serial/JTAG CDC.
- Direction: gateway to host for event frames.
- Encoding: bounded ASCII lines.
- Queue: 32 fixed-size packets.
- Backpressure policy: non-blocking; a full queue or disconnected host drops
  only the USB copy.

The USB-to-UART connector is separate. It carries the ESP-IDF console, debug
logs, and development commands. Connecting both ports to the same host is
supported on ESP32-C6-DevKitC-1. Use normal USB cables and do not inject another
power source through header pins at the same time.

## Frame format

Each line is exactly:

```text
CR11:<64 uppercase hexadecimal characters>:<4 uppercase CRC characters>\n
```

The hexadecimal body is the 32-byte [gateway packet v1](PROTOCOL.md#gateway-packet-v1).
The CRC is CRC-16/CCITT-FALSE over those 32 binary bytes:

```text
width:   16
poly:    0x1021
init:    0xFFFF
refin:   false
refout:  false
xorout:  0x0000
```

The newline is not covered by the CRC. Receivers must reject incorrect length,
prefix, separators, lowercase or non-hex characters, unsupported packet
versions, invalid field ranges, reserved flag bits, and CRC mismatch.

## Included monitor

Find the native USB stable device path:

```sh
ls -l /dev/serial/by-id/
```

Decode validated frames to compact JSON:

```sh
python3 firmware/tools/cr11_usb_monitor.py \
  /dev/serial/by-id/YOUR_ESP32_C6_NATIVE_USB
```

Print one validated wire frame and exit:

```sh
python3 firmware/tools/cr11_usb_monitor.py \
  /dev/serial/by-id/YOUR_ESP32_C6_NATIVE_USB \
  --raw --once
```

Valid decoded output includes `button`, `action`, selector, endpoint, original
ZCL command, source address, gateway sequence, and
`rssi_scope="gateway_last_hop"`. Invalid frames are emitted to standard error
as compact JSON and are never passed through as events.

The monitor opens the device read-only, uses a maximum line buffer, restores
terminal attributes on exit, and never executes data received from USB.

## Direct service integration

A future `CR11GateService` can consume this stream and publish directly to MQTT
or another local event bus:

```text
CR11 -> Zigbee -> CR11Gateway -> native USB -> local service -> MQTT
```

Compared with the current path, this removes the gateway-to-coordinator Zigbee
message and can reduce latency and airtime. It also creates a separate identity
and lifecycle problem: the service must map the packet's source address back to
the correct CR11 entity, deduplicate by gateway sequence, reconnect safely, and
preserve normal Zigbee2MQTT action semantics.

The current Zigbee2MQTT extension does not open a serial port. Keeping serial
I/O outside the Zigbee2MQTT process avoids blocking its event loop and avoids
granting the main container extra device access. The wire format is stable so a
separate service can be added without changing the firmware or CR11 tables.

## Debug output is intentionally separate

The hot path always builds the fixed binary packet because both output paths
need it. It does not build a JSON log representation unless `debug on` has been
issued on the USB-to-UART console. LED and state transitions follow the same
rule. This keeps routine command handling independent of console baud rate and
host availability.
