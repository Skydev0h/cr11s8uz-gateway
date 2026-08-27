PYTHON ?= python3

.PHONY: check test test-python test-firmware test-z2m test-release

check:
	$(PYTHON) scripts/check_repository.py

test-python:
	$(PYTHON) -m unittest discover -s tests -v

test-firmware:
	$(MAKE) -C firmware host-test

test-z2m:
	$(PYTHON) scripts/test_z2m.py

test-release:
	$(PYTHON) scripts/flash_firmware.py factory --port /dev/ttyACM0 --dry-run

test: check test-python test-firmware test-z2m test-release
