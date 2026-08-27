# Security policy

## Supported versions

The latest tagged release and the current `main` branch receive security fixes.
External Zigbee2MQTT code is supported only on the versions listed in the main
README unless a newer baseline has completed the compatibility checklist.

## Reporting a vulnerability

Prefer a private GitHub security advisory for this repository. If private
reporting is unavailable, contact the maintainer through the public profile
before disclosing exploit details. Do not attach Zigbee network keys,
coordinator backups, device databases, original firmware dumps, or captures
containing private identifiers to a public issue.

Include:

- affected commit or release;
- hardware and Zigbee2MQTT versions;
- reproduction steps with synthetic identifiers;
- impact and any known preconditions;
- a proposed patch if available.

## Security boundaries

- External converters and extensions are trusted code executing inside the
  Zigbee2MQTT process. They are not a sandbox boundary.
- Zigbee link security and coordinator admission remain the responsibility of
  the user's network.
- Native USB is a local physical interface. Its parser validates bounded frames
  but does not authenticate their origin.
- The firmware is not a safety controller. Do not use button events as the sole
  interlock for hazardous machinery.
- The included helpers do not manage credentials or edit the Zigbee2MQTT
  configuration. Runtime secrets must remain outside this repository.
- MQTT clients allowed to publish to
  `<base>/bridge/request/cr11_soft_rejoin` can make a known CR11 leave and
  rejoin and can temporarily open coordinator admission. Restrict write access
  to the Zigbee2MQTT base topic even though the request requires an exact
  confirmation value.

## Defensive behavior

The release flasher verifies a strict checksum manifest and confirms full-chip
erase. The installer refuses unsafe targets and preserves conflicting files only
after explicit replacement. Subprocesses use argument arrays without a shell.
CI dependencies and the Zigbee2MQTT test image are pinned to reviewed commits or
digests. A separate repository policy check guards the public release boundary.
