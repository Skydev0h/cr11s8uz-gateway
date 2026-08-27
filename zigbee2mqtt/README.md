# Zigbee2MQTT integration

This directory contains three trusted external JavaScript modules for
Zigbee2MQTT 2.13.0:

| Source | Destination under the Z2M data directory | Purpose |
|---|---|---|
| `cr11s8uz_external.mjs` | `external_converters/` | CR11 protocol, actions, and setup UI |
| `cr11_gateway_external.mjs` | `external_converters/` | CR11Gateway identity and packet decoder |
| `cr11_gateway_router_extension.mjs` | `external_extensions/` | routes gateway events to the source CR11 and supervises the lease |

Use the guarded installer from the repository root:

```sh
python3 scripts/install_z2m.py --data-dir /path/to/zigbee2mqtt/data --dry-run
python3 scripts/install_z2m.py --data-dir /path/to/zigbee2mqtt/data
```

Then enable external JavaScript in `configuration.yaml` and restart:

```yaml
advanced:
  enable_external_js: true
```

No converter filename list is required. Zigbee2MQTT discovers files from the
two external directories.

## Runtime behavior

- The CR11 converter replaces the generic four-button definition for both known
  model IDs and retains normal direct actions.
- Gateway setup is explicit. Loading the converter never writes the remote.
- Configure requires a fresh physical assignment-mode report and exactly one
  complete gateway, then confirms one clear plus 12 record writes.
- Restore direct mode needs no gateway and confirms one table clear.
- The extension publishes routed actions on the original CR11 device entity.
- A gateway sequence is deduplicated, but consecutive packets with different
  sequence values are published even when their action strings match.
- Raw `cr11_gateway_event` state is hidden by default and can be enabled with a
  gateway-specific option without changing routing.
- One heartbeat every five seconds holds the firmware supervisor lease.

Advanced raw protocol commands remain reachable over MQTT for research but are
not exposed as normal frontend controls. Inputs are bounded and validated. The
human setup controls are the supported installation interface.

## Advanced soft rejoin

The extension exposes one deliberately awkward recovery request for a CR11 that
still exists in the database but has lost a durable association. Publish an
exact payload to the Zigbee2MQTT base topic:

```text
topic:   <base>/bridge/request/cr11_soft_rejoin
payload: {"id":"0x0123456789abcdef","confirm":"leave-and-rejoin"}
reply:   <base>/bridge/response/cr11_soft_rejoin
```

It accepts only a known CR11 IEEE, opens permit-join for 60 seconds, and sends a
standard leave-with-rejoin ZDO request. It rejects extra fields and concurrent
requests. This operation changes network state and temporarily opens the join
window; use it only from a trusted MQTT client. A complete stock join remains
the reliable fallback but clears both action tables.

External JavaScript executes in the main Zigbee2MQTT process. Review the files,
protect the data directory, and test version upgrades before production use.
