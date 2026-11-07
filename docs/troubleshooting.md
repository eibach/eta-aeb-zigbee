# Troubleshooting

Start with the **status LED** (see [hardware.md](hardware.md#status-led)) and the serial console log
(115200 baud). `tools/serial-log.py` writes it to a file and survives unplug/replug.

## The board does not join the Zigbee network

Log: `No network joined (status 0x03), retrying in 5000 ms` (status 0x03 = no joinable network found).

* **Is permit join on** in Zigbee2MQTT? The window closes by itself after a few minutes.
* **Be patient.** On the test system the first join needed roughly 1–2 minutes of retries (each attempt
  scans all channels) before it succeeded. Nothing was wrong; later boots rejoin within a second.
* **Distance.** Pair within a few metres of the coordinator first.
* **Channel.** Set *Zigbee primary channel* (menuconfig → *ETA bridge*) to your network's channel
  (Zigbee2MQTT → Settings → Advanced). A wrong value only slows the join down.
* **See what the radio hears.** `tools/scan154` is a small diagnostic firmware that hops over channels 11–26,
  sends beacon requests and prints every network it hears with PAN ID, signal strength and the *permit join*
  flag of each beacon. If your network does not show `permit_join=1` while permit join is on, the problem is the
  coordinator or the distance, not this firmware.

## Joined, but Zigbee2MQTT shows no entities / "unsupported"

The converter is missing or not loaded. Copy `zigbee2mqtt/pe25_zb.mjs` to `external_converters/`, **restart
Zigbee2MQTT**, and re-pair if the device was interviewed before (remove it, erase flash, pair again). Check the
Zigbee2MQTT log for errors while loading external converters.

## All entities are "unavailable"

The bridge is not reaching Zigbee2MQTT (Zigbee2MQTT marks a router offline after about 10 minutes of silence).

* **Power.** If it runs from a **USB power bank**, the bank has probably switched itself off (low load). Use a
  mains charger.
* Check the LED: no light means no power; red blinking means it cannot find the network (range).
* Add a mains-powered Zigbee router between the board and the coordinator.

## Entities are "unknown" (LED yellow)

The bridge is online but the boiler is not answering.

1. Look at the log: `sent MC poll … rx bytes so far: 0` means nothing arrives on the RX pin.
2. Do the **loopback test** ([hardware.md](hardware.md#loopback-test-do-this-before-connecting-the-boiler)).
   No echo: swap the two data wires on the level shifter, check 3.3 V, check the GPIO numbers.
3. Echo works but the boiler is silent: check that the cable is **straight-through**, that it is plugged into
   the DB9 on the **operating panel**, and that the controller is on.
4. A few random bytes (`00 ff …`) while you touch the wires are just noise from a floating line.
5. `bad frame (checksum)` / `(stop byte)` in the log: electrical noise, a wrong baud rate or a loose cable.

## Values look wrong

* **0 °C** on exactly zero: the sensor is not installed on your plant (the controller reports 0). Hide those
  entities in Home Assistant (or remove the rows from the dashboard).
* **Pellet stock negative / wrong:** it is a counter in the controller. Enter your latest pellet delivery at the
  controller.
* **Wrong values on another model:** the indices in `eta_points.c` are the AEL list of ETA's document (pellet
  boiler up to 30 kW). Other controller types have different lists, see
  [protocol.md](protocol.md#5-monitor-list-indices).

## Dashboard says "Boiler fault" after I used a command

Expected for now: the boiler announces every command with an `IM` frame (its "changed via SMS" notice), which the
firmware counts as a fault and holds for 30 minutes. It clears by itself. See [protocol.md](protocol.md#34-im--messages).
To see the real text, watch the serial log: it prints `boiler error: <text>` for every `IM` frame.

## Commands

* **Boiler on / off** work (verified). The boiler has no confirmation: the select only shows the last command and
  returns to `idle` after about two seconds. See the temperatures to know whether it is running.
* **Heating mode** commands (auto/day/night) showed the "changed via SMS" message on the boiler display but the
  mode in the boiler menu did not change on the test boiler. ETA says each heating circuit must have
  *SMS Erlauben = Ja*; unverified. These commands are therefore not part of the dashboard (the entity still
  exists in Home Assistant).
* There is **no summer / off mode** in the protocol.

## Flashing fails

* Use the **UART bridge port** of the board (on the Waveshare board a "USB Single Serial" CH343 device, usually
  `/dev/ttyACM0`). Flashing over the chip's native USB (`303a:1001`) failed once with a broken pipe.
* Stop anything else that holds the port (a serial monitor, `tools/serial-log.py`).

## The LED shows the wrong colours

The boot self-test shows red, green, blue. If you see green, red, blue the LED is wired in RGB order: set
`CONFIG_ETA_LED_ORDER_RGB=y` (and `=n` for the opposite case) in `firmware/sdkconfig.defaults`, rebuild, flash.
