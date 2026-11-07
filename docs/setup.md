# Setup

Tested with: ESP-IDF **v5.5.6**, Zigbee2MQTT **2.14.3** (Home Assistant add-on, converter library
`zigbee-herdsman-converters` 26.125.0), Home Assistant **2026.10**, Waveshare ESP32-C6-DEV-KIT-N8,
ETA PE-25 (2008).

## 1. Build and flash the firmware

Install [ESP-IDF v5.5.6](https://docs.espressif.com/projects/esp-idf/en/v5.5.6/esp32c6/get-started/) for the
ESP32-C6, then:

```sh
. ~/esp/esp-idf/export.sh
cd firmware
idf.py menuconfig        # optional, see below
idf.py build
idf.py -p /dev/ttyACM0 flash
```

Build options (*ETA bridge* menu):

| Option | Default | Meaning |
|---|---|---|
| UART pins | TX GPIO6, RX GPIO7 | wiring to the MAX3232 |
| ETA node number | 16 (0x10) | pellet boiler up to 30 kW; add the DIP-switch value if not 0 |
| Refresh interval | 30 s | how often the boiler reports |
| Zigbee primary channel | 11 | **set to your network's channel**; all channels are scanned anyway |
| Allow commands | yes | when off, nothing but the read-only poll is ever sent |
| Simulate | no | fake values, no boiler needed (for testing the HA side) |
| LED GPIO / colour order | 8 / RGB | Waveshare ESP32-C6 board |

Flashing: on the Waveshare board use the port of the **UART bridge (CH343)**. Flashing through the chip's native
USB port failed once with a broken pipe; the bridge port worked reliably. On boards without a UART bridge, use the
single USB port.

The serial console (115200 baud, same port) shows what the bridge is doing: `idf.py -p … monitor`, or
`tools/serial-log.py` to log to a file and survive unplug/replug.

## 2. Install the Zigbee2MQTT converter

Zigbee2MQTT does not know the device, so it needs the converter in `zigbee2mqtt/pe25_zb.mjs`:

1. Copy it to `external_converters/pe25_zb.mjs` inside the Zigbee2MQTT data directory
   (Home Assistant add-on: the folder that contains `configuration.yaml`, e.g. `/config/zigbee2mqtt/`).
2. **Restart Zigbee2MQTT.** Your other Zigbee devices are unreachable for ~15 seconds while it restarts. Choose a
   quiet moment if you also run safety devices (smoke detectors) through it.

## 3. Pair the bridge

1. In Zigbee2MQTT enable **Permit join**.
2. Power the board **near your coordinator**. A factory-new board logs
   `Factory new: searching for a network to join` and retries every few seconds.
3. Joining can take a **minute or two** of `No network joined (status 0x03)` retries before it succeeds. Be patient;
   watch for `Joined network: PAN …, channel …`.
4. Zigbee2MQTT interviews the device and shows it as **PE25-ZB** with 14 entities. Switch permit join off again.
5. Optionally rename it (e.g. *ETA Pelletkessel*). The entity IDs follow the name
   (`sensor.eta_pelletkessel_temperature_boiler` …).

To pair again from scratch: remove the device in Zigbee2MQTT and run `idf.py -p … erase-flash flash`.

## 4. Home Assistant dashboard

Dashboards use the entity prefix `eta_pelletkessel` (device name "ETA Pelletkessel"). Pick the language:
`homeassistant/dashboard.en.yaml` or `dashboard.de.yaml`.

**Option A: installer (needs Node 22+):**

```sh
cd homeassistant
npm install
export HA_URL=http://homeassistant.local:8123
export HA_TOKEN=<long-lived access token>     # Profile → Security
node install-dashboard.mjs dashboard.en.yaml --prefix my_device_name   # --prefix only if you renamed the device
```

It creates a sidebar dashboard at `/eta-pellets` (change with `--url-path`) and never touches other dashboards.

**Option B: by hand.** Create an empty dashboard, open it, ⋮ → *Edit dashboard* → ⋮ → *Raw configuration editor*,
paste the YAML. Replace the prefix if needed.

Adjust the pellet gauge (`max: 5.5`, tons) to the size of your silo.

## 5. Connect to the boiler

1. Do the [loopback test](hardware.md#loopback-test-do-this-before-connecting-the-boiler).
2. Plug the straight DB9 cable into the boiler's operating panel.
3. Within a second or two the log should show `boiler link UP`, the LED turns green and the entities fill with
   values. Compare them with the boiler display.
4. Enter your latest pellet delivery at the boiler controller; the pellet stock is a counter that the controller
   maintains (see [protocol.md](protocol.md#32-md--data)).

Nothing happens? See [troubleshooting.md](troubleshooting.md).
