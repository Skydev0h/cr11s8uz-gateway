# Contributing

Contributions are welcome when they preserve the project's narrow hardware and
operational model.

## Before submitting

1. Keep all repository text in English.
2. Do not include original ORVIBO binaries or applications.
3. Replace unique IEEE addresses, network addresses, PAN identifiers, keys, and
   host paths with synthetic values.
4. Keep gateway mode explicit; joining or loading a converter must not silently
   rewrite a remote.
5. Preserve direct-mode recovery without a gateway.
6. Avoid adding Wi-Fi, BLE, MQTT, or a web server to the firmware without a
   separately justified design proposal.
7. Add tests for parser, state-machine, protocol, or converter changes.

Run before opening a pull request:

```sh
make test
```

Firmware changes should also include the relevant physical test evidence in the
pull request description. A successful build alone is not proof of radio,
commissioning, LED timing, watchdog, or sleepy-device behavior.

Do not regenerate or replace checked-in release binaries in an unrelated source
change. Release artifacts require the checklist in `docs/DEVELOPMENT.md`.
