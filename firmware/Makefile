HOST_CC ?= cc
HOST_CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -O2
PYTHON ?= python3

.PHONY: host-test clean-host

host-test:
	mkdir -p build-host
	$(HOST_CC) $(HOST_CFLAGS) -Imain main/cr11_event.c main/cr11_gateway_packet.c tests/test_cr11_event.c -o build-host/test_cr11_event
	./build-host/test_cr11_event
	$(HOST_CC) $(HOST_CFLAGS) -Imain main/cr11_ui_state.c tests/test_cr11_ui_state.c -o build-host/test_cr11_ui_state
	./build-host/test_cr11_ui_state
	$(HOST_CC) $(HOST_CFLAGS) -Imain main/cr11_usb_frame.c tests/test_cr11_usb_frame.c -o build-host/test_cr11_usb_frame
	./build-host/test_cr11_usb_frame
	PYTHONDONTWRITEBYTECODE=1 $(PYTHON) -m unittest tests/test_cr11_usb_monitor.py

clean-host:
	rm -f build-host/test_cr11_event build-host/test_cr11_ui_state build-host/test_cr11_usb_frame
