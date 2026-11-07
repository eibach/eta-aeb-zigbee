# Development

## Layout

```
firmware/                 ESP-IDF project (ESP32-C6)
  main/main.c             Zigbee router, endpoints, attribute updates, command handling
  main/eta_link.c         UART link: polling, frame parsing, link supervision, simulator
  main/status_led.c       status LED
  components/eta_proto/   portable C, no platform dependencies, host-testable
    eta_proto.c           frame encoding / streaming parser / MD + IM decoding
    eta_points.c          which values are exposed, scaling, command mapping
  test/test_eta_proto.c   host tests
zigbee2mqtt/pe25_zb.mjs   external converter (+ test.mjs)
homeassistant/            dashboards (de/en) and installer
tools/scan154/            diagnostic 802.15.4 channel / beacon scanner (separate firmware)
tools/serial-log.py       serial logger that survives unplug/replug
docs/                     protocol, hardware, setup, troubleshooting
```

## Tests

```sh
make test            # host tests + converter tests + dashboard parse check
make host-test       # only the C protocol library (gcc, with ASan/UBSan)
```

CI (`.github/workflows/ci.yml`) runs the same three checks plus a firmware build in the ESP-IDF container.
The firmware CI job has not been run yet; the same build was verified locally with ESP-IDF v5.5.6.

The protocol library is plain C99 so it can be tested without hardware. `test_real_capture` replays a poll and a
reply captured on a real PE-25 (the poll we build is byte-identical to the one the boiler answered, the reply
decodes to the values that were on its display); the other tests use synthetic frames for edge cases.

## Simulator

To test the Zigbee and Home Assistant side without a boiler, build with *Simulate* enabled. The firmware then
produces plausible fake values every 10 s, never touches the UART, and only logs the commands it would send:

```sh
cd firmware
echo "CONFIG_ETA_SIMULATE=y" > sdkconfig.sim
idf.py -B build_sim -DSDKCONFIG=sdkconfig.sim.cfg \
       -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.sim" build
```

**Never leave a simulator image on a board that is meant to report real values.**

## Zigbee device layout

One Zigbee router device, one standard Home Automation profile endpoint per value:

| Endpoint | Cluster | Meaning |
|---:|---|---|
| 10–19 | Temperature Measurement | boiler, flue gas, buffer top, buffer bottom, hot water tank, hot water tank bottom, outside, flow direct, flow mixer 0, external heat (ZCL unit: °C × 100) |
| 20 | Analog Input | pellet stock in tons (float) |
| 21 | Multistate Output | heating mode: 1 reset / 2 auto / 3 day / 4 night → `IH` |
| 22 | Multistate Output | boiler command: 1 idle / 2 on / 3 off → `IH`, resets to 1. State 4 (charge hot water) is implemented in the firmware but not offered by the converter, because the test plant has no hot water tank |
| 23 | Binary Input | boiler fault flag |

Endpoint numbers are shared between `components/eta_proto/include/eta_points.h`, `main/main.c` and
`zigbee2mqtt/pe25_zb.mjs`; keep them in sync.

## Design decisions

* **Selects, not switches, for commands.** The boiler never reports whether it is running, so an on/off switch
  would show a state nobody knows. A multistate output that returns to `idle` is honest about being a one-shot
  command. The dashboard uses two confirm buttons on top of it.
* **Unknown values are explicit sentinels.** While the boiler is silent, temperatures are set to the ZCL
  "invalid" value `0x8000` and the pellet stock to `−1000`; the converter turns both into `null`, so Home
  Assistant shows "unknown" instead of stale or absurd numbers. (`NaN` would not work: Zigbee does not report
  a change to NaN.)
* **Pellet stock may be negative** (a controller counter), so the sentinel lies far outside the possible range
  of raw/100 (−327.68…327.67).
* **Fault flag held for 30 minutes**, because the protocol has no "error cleared" message.
* **Link supervision.** Poll every 10 s until the boiler answers, then every 5 minutes as keep-alive; the link
  counts as down after 3 refresh periods without data.
* **Join-only router.** The device never forms a network; it only joins an existing one.

## Adding or changing values

1. Add the index to the `eta_ael_index` enum in `eta_proto.h` and a row to the table in `eta_points.c`
   (endpoint, kind, name). Max 20 values (protocol limit for one `MC`). Update `test_eta_proto.c` if needed.
2. Add the endpoint in `zigbee2mqtt/pe25_zb.mjs` (`temps` list) and run `make z2m-test`.
3. Add it to the dashboards if wanted.
4. Re-pair the device (endpoint changes need a new interview): remove it in Zigbee2MQTT, `erase-flash`, flash, pair.

For another ETA controller type, change `ETA_NODE` (menuconfig) and the index table. Please document verified
values in `docs/protocol.md`.

## Diagnostic tools

* `tools/scan154`: flash this *instead of* the main firmware to see which Zigbee networks the radio hears
  (channels 11–26, PAN IDs, signal, permit-join flag, beacon payload fields). Build like the main firmware.
* `tools/serial-log.py <port> <logfile> <pidfile>`: appends the serial output to a file, reconnects after an
  unplug, resets the board once at start. Stop it with `kill $(cat <pidfile>)`.
