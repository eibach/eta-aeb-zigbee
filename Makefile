# Host-side checks (no ESP32 needed). The firmware itself is built with ESP-IDF: see docs/development.md
.PHONY: test host-test z2m-test dashboard-check

test: host-test z2m-test dashboard-check

host-test:
	gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address,undefined \
	    -Ifirmware/components/eta_proto/include \
	    firmware/components/eta_proto/eta_proto.c firmware/components/eta_proto/eta_points.c \
	    firmware/test/test_eta_proto.c -lm -o /tmp/eta_proto_test
	/tmp/eta_proto_test

z2m-test:
	cd zigbee2mqtt && npm ci --no-audit --no-fund && npm test

dashboard-check:
	cd homeassistant && npm ci --no-audit --no-fund
	cd homeassistant && node install-dashboard.mjs dashboard.de.yaml --dry-run > /dev/null
	cd homeassistant && node install-dashboard.mjs dashboard.en.yaml --dry-run > /dev/null
	@echo "dashboards parse OK"
