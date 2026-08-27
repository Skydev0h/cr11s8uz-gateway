# Development and release guide

## Toolchain

The supported baseline is:

- ESP-IDF 5.5.4;
- target `esp32c6`;
- `esp-zigbee-lib 2.0.3`;
- `led_strip 3.0.3`;
- Zigbee2MQTT 2.13.0;
- esptool 5.3.1;
- Python 3.11 or newer;
- a C11 host compiler and GNU Make;
- Docker for exact Zigbee2MQTT runtime tests.

`firmware/dependencies.lock` pins the managed component graph. Do not casually
refresh it in a bug-fix change; review upstream release notes and rerun hardware
tests when the Zigbee stack changes.

## Build the firmware

Activate ESP-IDF 5.5.4, then:

```sh
cd firmware
idf.py set-target esp32c6
idf.py build
```

The default configuration targets ESP32-C6-DevKitC-1 v1.2 with 8 MB flash and
uses BDB Network Steering across all Zigbee channels 11 through 26. To change
the primary commissioning channel mask or board pins:

```sh
idf.py menuconfig
```

Open **CR11 bridge configuration**, change the primary channel mask or GPIO
values, save, and rebuild. Channels outside the primary mask become the BDB
secondary fallback set. Keep `CONFIG_ZB_ZED=y`; compilation intentionally
fails for a Router build. The public release builder requires the all-channel
mask and reproducible ESP-IDF output so a locally constrained or
non-reproducible diagnostic build cannot be published by mistake.

For source development, the normal ESP-IDF flash command is:

```sh
idf.py -p /dev/serial/by-id/YOUR_ESP32_C6_UART flash monitor
```

Use the repository flasher for published, checksummed artifacts.

## Run tests

All local checks:

```sh
make test
```

Individual suites:

```sh
python3 scripts/check_repository.py
python3 -m unittest discover -s tests -v
make -C firmware host-test
python3 scripts/test_z2m.py
```

The Zigbee2MQTT runner uses an exact image digest, mounts the checkout read-only,
disables networking, drops all Linux capabilities, and runs Node's test runner
inside the real application dependency tree.

The repository policy rejects content that is unsuitable for a portable public
source release, including local runtime state and unsafe filesystem entries.
All public documentation, source comments, test names, UI labels, and commit
messages must follow the project's language and portability conventions. Test
the language-policy detector with generated code points rather than literal
fixtures committed to the repository.

## Build a release directory

Install the pinned flashing dependency in a virtual environment:

```sh
python3 -m venv .venv
./.venv/bin/python -m pip install -r requirements-flash.txt
```

After `idf.py build`, create a new release in an unused path:

```sh
./.venv/bin/python scripts/build_release.py \
  --build-dir firmware/build \
  --output-dir dist/v0.9.3
```

The release tool:

1. reads the version from `firmware/CMakeLists.txt`;
2. validates required build artifacts;
3. uses esptool to merge bootloader, partition table, and application;
4. records target, flash settings, offsets, dependency versions, and binary
   hashes in `manifest.json`;
5. writes a strict `SHA256SUMS`;
6. atomically renames a temporary staging directory into place.

It refuses an existing output directory. This prevents an incomplete release
from silently mixing with older binaries.

Verify the intended release with:

```sh
sha256sum -c dist/v0.9.3/SHA256SUMS
./.venv/bin/python scripts/flash_firmware.py factory \
  --port /dev/ttyACM0 --dry-run
```

The final dry run validates the checked-in release directory, so copy the newly
reviewed files to `firmware/prebuilt/v0.9.3` before that command.

CI rebuilds the same source in a different workspace and compares every
generated release artifact byte-for-byte with the checked-in prebuilt files.
Run the same verification locally with:

```sh
./.venv/bin/python scripts/build_release.py \
  --build-dir firmware/build \
  --verify-current
```

## Release checklist

1. Update `PROJECT_VER` and the changelog. Firmware logs, the flasher, and the
   prebuilt path derive the version from `PROJECT_VER`.
2. Build from a clean ESP-IDF tree with the locked dependencies.
3. Run host and converter tests.
4. Flash the factory image to a clean supported board.
5. Join it, verify interview and lease recovery, and exercise all CR11 actions.
6. Test application-only upgrade from the previous release.
7. Test graceful and forced reset LED paths.
8. Generate the release directory, verify every checksum, and run the
   byte-for-byte current-release comparison.
9. Run the repository policy and a private-data scan.
10. Tag the exact reviewed commit. Do not regenerate binaries after tagging.

## Zigbee2MQTT compatibility work

External converter and extension APIs can change between releases. To qualify a
new Zigbee2MQTT version:

1. update the image tag and digest in `scripts/test_z2m.py`;
2. update the converter package version documented in the README;
3. run all Node tests inside the new image;
4. start a temporary Zigbee2MQTT instance with a copied database;
5. verify gateway interview, ready/heartbeat commands, assignment progress,
   repeated identical actions, and raw-event suppression;
6. complete physical tests before changing the supported baseline.

Never test a new converter against the only copy of a production Zigbee2MQTT
data directory.
