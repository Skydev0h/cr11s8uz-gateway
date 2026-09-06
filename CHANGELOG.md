# Changelog

All notable project changes are documented here.

Versions 0.1.0 through 0.9.1 were internal laboratory builds rather than
public releases. Their history is reconstructed from preserved firmware
backups and physical captures made between 2026-08-24 and 2026-08-26.

## Unreleased

- Preserve the canonical `CR11S8UZ` model identity in the external converter
  so Zigbee2MQTT retains the standard device image while exposing all eight
  buttons and gateway configuration controls.
- Retry only definite `NWK_NO_ROUTE` failures during an awake CR11 action-table
  install with bounded backoff, and preserve the transport-cause tail of long
  setup errors instead of truncating the useful part.

## 0.9.4 - 2026-09-06

- Make the addressable LED colour order selectable, defaulting to the tested
  board's `GRB`. Boards carrying an `RGB` part, such as the Waveshare
  ESP32-C6-Zero, exchanged red and green and showed a factory-new device as a
  green pulse instead of a red one.
- Record the colour order in the release manifest and make both release
  construction and flashing reject an image built for a different board.
- Document the failure signature and the yellow BOOT-hold check that separates
  a wrong colour order from a wrong state.
- Add board profiles. `devkitc1` remains the reference release; `c6zero`
  publishes the same sources built for the Waveshare ESP32-C6-Zero. The release
  builder, the flasher, and CI all work per profile, and CI rebuilds and
  compares both prebuilt directories byte-for-byte.
- Add `firmware/sdkconfig.c6zero.defaults` so the variant is a committed recipe
  rather than a documented sequence of flags.
- Confirm on a Waveshare ESP32-C6-Zero with 8 MB flash that the `GRB` image
  boots and shows the exchanged red and green signature, and that the `c6zero`
  build restores correct colours. Zigbee commissioning and the CR11 action
  matrix remain validated only on the ESP32-C6-DevKitC-1 v1.2.

## 0.9.3 - 2026-08-27

- Replace the channel-25-only join configuration with standard BDB Network
  Steering across all Zigbee channels 11 through 26. Existing joined gateways
  continue restoring their saved network and channel from persistent storage.
- Record the commissioning mode, complete channel set, and channel mask in the
  release manifest schema 2.
- Make both release construction and flashing reject an accidentally
  single-channel public build.
- Add checksummed v0.9.3 factory and application-only images.
- Make firmware binaries reproducible across build paths and have CI rebuild
  and compare the complete checked-in release byte-for-byte.
- Derive runtime and flasher versions from `PROJECT_VER`, scan binary artifacts
  for private build markers, and harden failed soft-rejoin and factory-flash
  recovery paths.
- Physically validate application-only upgrade, graceful local leave,
  factory-new all-channel join to a channel-25 network, complete interview,
  ready transition, and supervisor heartbeat recovery on the supported board.

## 0.9.2 - 2026-08-27

- Add the recoverable `restoring` UI phase for a saved Zigbee network that is
  temporarily unavailable. Continue BDB retries, show a distinct yellow/orange
  indication, and keep the BOOT reset escape available.
- Publish the complete ESP32-C6 source and checksummed factory/application
  images for the first public release.
- Add Zigbee2MQTT human setup controls with live 12-record progress, explicit
  direct-mode restore, plain/contextual actions, and raw-event suppression.
- Document the proprietary action table, coordinator address sentinel collision,
  full-join table clear, and v3.1.04 Big-query defect.
- Add guarded install, flash, release, policy, host, and exact-runtime test tools.

## 0.9.1 (internal)

- Change the Basic-cluster manufacturer identity from the original development
  value `SkyDev` to the GitHub-aligned `Skydev0h`.
- Accept both manufacturer values in gateway discovery so existing Zigbee2MQTT
  interview caches can migrate without a rejoin or Zigbee storage erase.
- Confirm the new identity through a physical re-interview while preserving the
  gateway IEEE address, network address, End Device role, and all endpoints.

## 0.9.0 (internal)

- Add the coordinator/Zigbee2MQTT supervisor lease and five-second heartbeat on
  endpoint 21.
- Indicate heartbeat loss with a green-to-orange warning from 10 to 30 seconds,
  then request one Task WDT recovery reboot.
- Prevent reboot loops after watchdog recovery, locally arm ordinary joined
  boots, and allow a late heartbeat to cancel a pending watchdog reset.

## 0.8.0 (internal)

- Add the event-only native USB stream: one fixed 32-byte gateway packet encoded
  as uppercase hexadecimal with CRC-16/CCITT-FALSE.
- Move USB output behind a fixed queue and dedicated task so a slow host cannot
  block the Zigbee receive callback.
- Make verbose UART receive, forwarding, and LED JSON explicitly opt-in with
  `debug on`; keep UART quiet by default.
- Add the strict host-side USB decoder and framing tests.

## 0.7.1 (internal)

- Label event RSSI explicitly as `gateway_last_hop` in UART JSON and
  Zigbee2MQTT output so it is not mistaken for an end-to-end CR11 metric.
- Preserve version 1 of the 32-byte event packet.

## 0.7.0 (internal)

- Change the gateway from a Zigbee Router to an always-on End Device with its
  receiver enabled while idle.
- Add child-timeout and parent-refresh behavior suitable for an always-powered
  receiver.
- Prevent the gateway from parenting the CR11 or routing unrelated traffic,
  avoiding orphaned remotes after a gateway reset or replacement.

## 0.6.0 (internal)

- Add the deterministic GPIO9 BOOT and GPIO8 RGB commissioning/reset UI.
- Add join, interview, ready heartbeat, upper/lower event feedback, accelerating
  destructive-reset warning, graceful/forced leave results, and the terminal
  power-cycle state.
- Defer forced Zigbee NVS erasure until the next cold boot so storage is never
  erased under the running stack.
- Add UART commands that drive and observe the same UI state machine for
  repeatable timing tests.

## 0.5.0 (internal)

- Expand CR11 gateway configuration from four click records to a 12-record plan
  with separate click, hold, and release records for every upper selector.
- Add exact Level Control marker commands for upper hold/release events while
  retaining the selected lower-rocker context.
- Keep gateway event packet version 1 compatible with existing consumers.

## 0.4.0 (internal)

- Add endpoint 21, vendor cluster `0xFC11`, and the stable 32-byte versioned
  gateway event packet.
- Forward normalized events from the ESP32-C6 back to coordinator endpoint 1
  while retaining UART observations.
- Complete the first physical end-to-end path from CR11 through the gateway and
  Zigbee2MQTT to a `button_1_click` event on the original remote entity.

## 0.3.0 (internal)

- Add four independent Dimmable Light targets on endpoints 10 through 13, one
  for each upper selector; move the Window Covering diagnostic target to 20.
- Introduce the `four-target` plan so all four selector contexts remain distinct
  without creating multiple physical Zigbee devices.
- Validate the full 28-command physical matrix without duplicate or mixed
  targets.

## 0.2.0 (internal)

- Split the two columns across Dimmable Light endpoints 10 and 12.
- Track Move/Stop direction by source and destination endpoint so simultaneous
  target contexts cannot overwrite one another.
- Confirm independent left/right Level click, hold, and release paths on the
  physical remote.

## 0.1.0 (internal)

- Replace the board's experimental firmware with the first working ESP32-C6
  Zigbee target and raw ZCL receiver.
- Expose initial Dimmable Light and Window Covering endpoints for the two CR11
  columns.
- Physically prove that a stock CR11 can resolve a nonzero target address and
  emit all eight controls without replacing the remote firmware.
