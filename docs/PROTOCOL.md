# ORVIBO CR11S8UZ protocol notes

## Scope and evidence

This document describes the protocol implemented by the examined CR11S8UZ
firmware `v3.1.04_20160520`. It was reconstructed from a complete CC2530 firmware
dump, cross-checked against the ORVIBO HomeMate application and independent
ORVIBO converter behavior, then verified with live radio traffic in an isolated
Nabu Casa ZBT-2 network.

The original firmware image is not distributed. It contains unique IEEE, PAN,
network key, and frame-counter state. The original application and private
captures are also excluded from this repository.

## Device descriptor

```text
node type: End Device
endpoint: 1
profile: 0x0104 Home Automation
device ID: 0x0006
advertised input clusters:  [0x0000]
advertised output clusters: [0x0000, 0x0017]
firmware/date: v3.1.04_20160520
```

Observed model identifiers:

```text
51725b7bcba945c8a595b325127461e9
3c4e4fc81ed442efaf69353effcdfc5f
```

The proprietary action-table cluster is `0x0017`. Its command payloads use raw
byte buffers.

## Physical controls

| Z2M action | Position | CR11 key number |
|---|---|---:|
| `button_1` | left upper `●` | 3 |
| `button_2` | right upper `■` | 11 |
| `button_3` | left middle `● ●` | 7 |
| `button_4` | right middle `■ ■` | 15 |
| `button_5` | left lower `+` | 4 |
| `button_6` | left lower `-` | 8 |
| `button_7` | right lower `+` | 12 |
| `button_8` | right lower `-` | 16 |

The upper controls are not merely four generic buttons. Each upper action looks
up a stored record, executes its ZCL command, and selects the target used by the
lower rocker in that column. The lower rockers then emit standard Level Control
or Window Covering commands directly to the selected target.

## Assignment mode

Hold right-lower `+` and `-` together for more than four seconds. The remote:

- emits a Device Announce;
- reports Basic attribute `0x00AA = 0`;
- blinks red and stays awake for action-table commands.

Press either right-lower control to exit. The remote reports `0x00AA = 1`.

Attribute `0x00AA` is a read-only `uint8`. Live Write Attributes attempts
returned ZCL `READ_ONLY`; no proprietary remote enable/disable command was found.
Configuration therefore requires a deliberate physical action and a fresh
report observed by the current Zigbee2MQTT process.

## Cluster 0x0017 commands

| Client command | ID | Response ID | Purpose |
|---|---:|---:|---|
| `addBig` | `0x00` | `0x00` | add a full target action |
| `query` | `0x01` | `0x01` or `0x05` | diagnostic lookup |
| `deleteBig` | `0x02` | `0x02` | remove one full action |
| `clearBig` | `0x03` | `0x03` | clear all full actions |
| `addSmall` | `0x04` | `0x04` | add a six-byte opaque action |
| `clearSmall` | `0x05` | `0x06` | clear the Small table |
| `clearAll` | `0x06` | `0x07` | clear both tables |

Response `0x08` is a key event. Common status values are `0x00` success,
`0x10` memory error, `0x87` invalid value, `0x89` insufficient space, and
`0x8B` not found.

## Big action record

The public payload is variable length:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | key number |
| 1 | 1 | key bank, normally 0 |
| 2 | 1 | key action: click `0x00`, hold `0x02`, release `0x03` |
| 3 | 8 | target IEEE in little-endian wire order |
| 11 | 1 | target endpoint |
| 12 | 2 | profile ID, little-endian |
| 14 | 2 | cluster ID, little-endian |
| 16 | 1 | action length: command ID plus payload bytes |
| 17 | 1 | ZCL command ID |
| 18 | 0 to 7 | ZCL command payload |

The firmware's internal record is 27 bytes. It adds a two-byte cached target
network address after the maximum public action area. New records are zeroed,
so the cache begins as `0x0000`.

## Why the coordinator cannot be the target

The target is stored by full IEEE address. Before executing a record, the CR11
resolves that IEEE to a 16-bit Zigbee network address and caches the result.
The examined firmware considers both `0x0000` and `0xFFFF` unresolved.

`0x0000` is also the coordinator's valid Zigbee network address. Targeting the
coordinator therefore creates a sentinel collision:

1. CR11 sends an IEEE-to-network address request.
2. The coordinator correctly answers `0x0000`.
3. CR11 stores or observes zero and still treats it as unresolved.
4. The next action repeats resolution instead of sending the lower command.

This is why a non-coordinator endpoint is necessary even though all radio
traffic ultimately reaches the coordinator. CR11Gateway joins with a nonzero
network address and supplies the standard server clusters expected by the
remote.

The cache is per Big record. Click, hold, and release for one selector resolve
independently. No application path was found that invalidates the cache after a
failed send or gateway address change. Rewriting the plan clears the caches and
forces fresh resolution.

## The 12-record gateway plan

Gateway mode clears the Big table, then writes three records for each upper
key. The target endpoints are `10, 11, 12, 13` for selectors `1, 3, 2, 4`
respectively.

| Upper state | Cluster | Command | Payload | Meaning at gateway |
|---|---:|---:|---|---|
| click | On/Off `0x0006` | Toggle `0x02` | empty | upper click |
| hold | Level `0x0008` | Move to Level `0x00` | `A5 00 00` | upper hold marker |
| release | Level `0x0008` | Stop with On/Off `0x04` | `5A 00 00` | upper release marker |

The deliberately unusual `A5` and `5A` marker payloads distinguish upper
state without consuming more endpoints. They are accepted by the target-side
decoder and keep each lower rocker attached to the same physical selector.

Lower rocker traffic is produced by stock CR11 behavior:

- Level Step becomes a lower click;
- Level Move becomes lower hold;
- Level Stop becomes release, with direction inferred from the preceding move;
- the compatibility Window Covering path maps open, close, stop, and percentage
  commands to the corresponding right-side actions.

## Full stock join

The commissioning sequence is:

1. press both lower `+` controls, buttons 5 and 7;
2. after three red flashes press upper buttons `1, 2, 3, 4`;
3. fast blinking indicates network search.

Static analysis shows that the final handler explicitly clears the Small table,
clears the Big table, and only then starts commissioning. A full stock join must
therefore be followed by gateway configuration. Entering assignment mode alone
does not clear either table.

## Broken Big-table query in v3.1.04

Command `0x01` cannot authoritatively read the Big table on the examined
firmware. Its Big lookup passes an uninitialized three-byte key, while the Small
fallback uses and echoes the actual request key. A working, physically executed
12-record table can consequently return a plausible `QuerySmallResponse` with
status `0x8B`.

The integration keeps query support for research but marks every result with
`big_table_authoritative=false`. Installation correctness is established by:

1. the acknowledged `clearBig` response;
2. 12 individually acknowledged `addBig` responses;
3. end-to-end button behavior.

## Gateway packet v1

Endpoint 21 sends command `0x00` of cluster `0xFC11` with a fixed 32-byte
payload. All multi-byte integers are little-endian.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 1 | packet version, 1 |
| 1 | 1 | declared length, 32 |
| 2 | 1 | action: unknown, click, hold, release |
| 3 | 1 | normalized event kind |
| 4 | 1 | direction: none, up, down |
| 5 | 1 | flags |
| 6 | 1 | selected upper button, 0 to 4 |
| 7 | 1 | physical button, 0 to 8 |
| 8 | 1 | source mode: unknown, short, extended, group |
| 9 | 1 | source endpoint |
| 10 | 1 | gateway target endpoint |
| 11 | 1 | ZCL transaction sequence number |
| 12 | 2 | profile ID |
| 14 | 2 | cluster ID |
| 16 | 1 | command ID |
| 17 | 1 | signed RSSI of the CR11-to-gateway last hop |
| 18 | 2 | optional command value |
| 20 | 8 | source address |
| 28 | 4 | monotonically increasing gateway sequence |

Flag bits are `has_value=0x01`, `with_on_off=0x02`,
`direction_inferred=0x04`, and `forwardable=0x08`. Reserved bits must be zero.
The Zigbee2MQTT converter validates length, version, ranges, flags, and semantic
consistency before the extension routes an event.

Endpoint 21 also accepts two zero-payload client commands:

- `0x01 ready`, sent after Zigbee2MQTT configures the gateway;
- `0x02 heartbeat`, sent every five seconds by the extension.

Both arm or renew the firmware supervisor lease.

## Remaining evidence boundaries

Live traffic proved the coordinator sentinel collision, all eight button paths,
all upper and lower click/hold/release mappings, four independent selectors,
assignment-mode reports, table write acknowledgements, and operation with the
gateway as an always-on End Device.

Firmware analysis proved the internal table layout, zero initialization,
resolver sentinels, cache lifetime, full-join table clearing, and the Big-query
defect.

One behavior remains a static prediction rather than a deliberately forced
hardware test: a gateway rejoin that changes its 16-bit address can leave the
CR11's cached entries stale until the plan is rewritten.
