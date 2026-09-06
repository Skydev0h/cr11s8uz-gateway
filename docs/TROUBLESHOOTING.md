# Troubleshooting

## The gateway LED is completely dark

On ESP32-C6-DevKitC-1 v1.2, check the J5 jumper between `ESP_3V3` and the RGB LED
supply. Also verify a known-good USB data cable. The radio and firmware may be
running even when the RGB LED is unpowered, so inspect the USB-to-UART console
at 115200.

## Factory-new red pulses never become yellow

BOOT must be held for at least one second. Half-bright yellow appears while the
hold is being armed. If the button or GPIO mapping differs on another board,
rebuild with the correct `CR11_UI_BOOT_GPIO`.

## Yellow steering never joins

- Confirm Zigbee2MQTT permit-join is open.
- The supplied v0.9.4 image scans channels 11 through 26. Keep permit-join open
  long enough for the complete scan and subsequent interview.
- Keep the gateway near the coordinator for initial commissioning.
- Confirm no other process owns the coordinator.
- Check Zigbee2MQTT logs for admission or interview errors.

## The gateway joined but alternates yellow and green forever

The Zigbee network accepted the device, but the gateway has not received the
post-interview `ready` command. Verify:

- external JavaScript is enabled;
- `cr11_gateway_external.mjs` loaded without error;
- the device resolves as `Skydev0h / CR11Gateway` rather than unsupported;
- endpoint 21 was interviewed;
- Zigbee2MQTT 2.13.0 is in use.

Restarting Zigbee2MQTT should run the converter configure hook again. Do not
factory-reset the gateway merely because the UI loaded late.

## Green-to-orange warning pulses or a watchdog restart

The extension heartbeat has been absent for more than ten seconds. Check that
Zigbee2MQTT and its extension are running and that exactly one CR11Gateway is
joined. With zero or multiple candidates, the extension deliberately does not
guess which gateway to supervise.

After a watchdog reboot, green/off/orange pulses every three seconds mean the
lease is deliberately unarmed. A later heartbeat restores normal service and
prevents a reboot loop.

## The gateway briefly appears unsupported during startup

Zigbee2MQTT can print its initial device inventory before external converters
finish loading. Judge the final device definition and later logs, not the first
inventory line.

## Assignment mode does not become active

Hold the two right-lower `+/-` controls together for more than four seconds.
The remote should blink red, send Device Announce, and report Basic attribute
`0x00AA = 0`.

The mode cannot be enabled remotely. A cached frontend value is not sufficient;
the converter requires a fresh report seen by the current process. Wake the
remote near the coordinator and repeat the physical combination.

## Configure says that no gateway was found

Gateway discovery accepts exactly one joined, successfully interviewed device
with model `CR11-Bridge`, a recognized manufacturer, and endpoints 10, 11, 12,
13, and 21. Finish or repeat the gateway interview before trying again.

If no gateway exists, **Restore direct mode** remains available by design.

## Configure stops before 12/12

The remote was not awake long enough or a table response was lost. Leave its
red assignment mode active, inspect **Setup detail**, and run Configure again.
The operation starts with `clearBig`, so a complete retry is deterministic.

Do not use repeated random button presses as a wake strategy during a write.
Assignment mode is the supported awake window.

## Query says not found even though buttons work

This is a confirmed CR11 `v3.1.04` firmware defect. The Big-table query uses an
uninitialized lookup key and may fall through to a `QuerySmallResponse` with
status `0x8B`. It is not evidence that the Big table is empty. Use the write
acknowledgements and end-to-end actions.

## Upper buttons work, but lower buttons do not

Check these cases in order:

1. A full CR11 join cleared the action table. Configure gateway mode again.
2. The selected upper action has not resolved the gateway address yet. Perform
   one deliberate upper click, hold, and release, then retry the lower control.
3. The gateway rejoined with a different 16-bit network address and the CR11
   cache is stale. Rewrite all 12 records.
4. The CR11 is still in direct mode. **Last configured route** is local
   Zigbee2MQTT history, so confirm with actual actions after a restart.

The coordinator's `0x0000` address cannot serve as the stock lower-button target.
That is the firmware sentinel collision this gateway exists to avoid.

## One selector works and another does not

Each click, hold, and release tuple is a separate Big record with an independent
resolved-address cache. Complete the 12-action warm-up. If failure persists,
enter assignment mode and configure the whole plan again.

## A release appears only after the next hold

Very short or irregular stock hold/release gestures can expose the remote's own
state timing. The gateway also infers a Level Stop direction from the most recent
move on the same source and endpoint. Use a clear hold long enough for the first
movement command, then release. Current gateway mode was verified with rapid
normal gestures, but cannot invent a release the CR11 never transmitted.

## Identical repeated presses do not look like a state change

`action` is an event property. Zigbee2MQTT publishes each routed gateway
sequence, including identical consecutive actions, but a state-history UI may
render only value transitions. Use Home Assistant device triggers or subscribe
to the MQTT device topic when validating repeated presses.

No artificial empty action is injected. This matches normal Zigbee2MQTT input
device behavior and avoids doubling messages.

## Gateway raw events clutter recent activity

Set the gateway-specific **Publish gateway event** option to off. It defaults
off. Routing continues internally; only the diagnostic `cr11_gateway_event`
field is removed from outgoing state.

## RSSI or link quality looks like the gateway, not the remote

That is expected for relayed traffic. The packet RSSI is measured by the
gateway for the CR11-to-gateway hop. Zigbee2MQTT link quality on the gateway
entity describes the gateway-to-coordinator hop. Relaying cannot reconstruct a
coordinator measurement for a frame the coordinator did not receive directly.

## The installer refuses an existing file

This is a safety feature. Compare your installed file with the repository. If
the repository version should replace it, use `--replace`; the installer first
creates a timestamped mode-0600 backup beside the destination. It never edits
`configuration.yaml`.

## Native USB has no events

- Confirm the cable is connected to the ESP32-C6 native USB connector, not only
  USB-to-UART.
- Confirm the device path after reconnecting; prefer `/dev/serial/by-id`.
- Test a button that already works through the gateway.
- USB output includes only valid forwardable CR11 actions, not heartbeats or
  arbitrary Zigbee frames.
- Run the monitor without `--once` and inspect standard error for CRC or format
  rejection.

USB failure does not disable the Zigbee path.

## A factory-new gateway pulses green instead of red

A gateway that has never joined pulses **red** for 0.1 s once per second. If a
freshly flashed board pulses **green** at that same fast rhythm, the firmware
state is almost certainly correct and only the LED colour order is wrong: the
board carries an `RGB` addressable LED while the image was built for `GRB`.

Confirm it without any tooling. Yellow and blue use equal red and green
components, so they render identically in both orders:

1. Press and hold BOOT for less than a second. Half-bright **yellow** means the
   state machine is healthy and only red and green are exchanged.
2. Hold BOOT for one second and release. Network steering shows **yellow**
   pulsing at 0.5 s on, 0.5 s off, again in either colour order.

If both steps look yellow, flash the variant that matches the board:

```sh
python3 scripts/flash_firmware.py factory --board c6zero --port /dev/ttyACM0
```

If instead the BOOT hold produces no yellow at all, the problem is not the
colour order. Check the LED data GPIO and the BOOT GPIO against
**CR11 bridge configuration**; the defaults are GPIO 8 and GPIO 9.

Flashing the wrong variant is harmless. Nothing but the LED colour changes, and
reflashing the correct profile restores it.

## Sharing diagnostic logs

Normal firmware logs omit the gateway IEEE address and PAN identifiers. The
explicit `debug on` mode can include IEEE, short, PAN, extended-PAN, and source
addresses. Replace those identifiers with synthetic values before attaching a
debug log or packet capture to a public issue. Never publish network keys,
coordinator backups, or device databases.
