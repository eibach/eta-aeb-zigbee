# Hardware

## Parts

| Part | Notes |
|---|---|
| **ESP32-C6 board** | Any ESP32-C6 (it needs the 802.15.4 radio, so **not** a plain ESP32, ESP32-S3 or ESP8266/D1 mini). Developed and tested on the **Waveshare ESP32-C6-DEV-KIT-N8**. |
| **MAX3232 module** with DB9 | Must work at **3.3 V**. A plain MAX232 needs 5 V and can damage the ESP32 inputs. Choose one with a female DB9 (wired like a PC's COM port). |
| **DB9 cable, straight-through** | Not a null-modem cable. Female on the level-shifter side, male on the boiler side, as in ETA's manual. |
| **Mains USB charger (5 V)** | Powers the ESP32. See [Power](#power). |

## Wiring

```
 ESP32-C6                MAX3232 module                    ETA PE-25
┌──────────┐            ┌───────────────┐   straight     ┌───────────┐
│ GPIO6 TX ├───────────►│ MCU-side TX in│   DB9 cable    │ operating │
│ GPIO7 RX │◄───────────┤ MCU-side RX out├═══════════════╡ panel DB9 │
│ 3V3      ├───────────►│ VCC           │                │ (male)    │
│ GND      ├────────────┤ GND           │                └───────────┘
└──────────┘            └───────────────┘
```

| ESP32-C6 | MAX3232 module |
|---|---|
| GPIO6 (UART1 TX) | the pin that feeds the transmitter (T1IN) |
| GPIO7 (UART1 RX) | the pin that comes from the receiver (R1OUT) |
| 3V3 | VCC |
| GND | GND |

The UART pins are build options (`idf.py menuconfig` → *ETA bridge*). Avoid the board's LED pin, BOOT
pin and USB pins (on the Waveshare board: GPIO8, GPIO9, GPIO12/13).

**TX/RX labels differ between modules.** Some modules label their pins from the microcontroller's side, others
from the DB9 side. If the loopback test below shows no echo, swap the two data wires.

### Loopback test (do this before connecting the boiler)

1. Unplug the DB9 cable from the level shifter.
2. Short **pin 2 and pin 3** of the shifter's own DB9 connector with a wire.
3. Power the ESP32 and watch its log. Every poll (10 s) must come back: `rx bytes so far` rises by 40 and a
   hex dump starting `7b 4d 43` is printed.

No echo means the problem is on the ESP32 / level shifter side (swap TX/RX, check the 3.3 V supply, check the
wiring). With an echo, the electronics are fine.

### On the boiler

* Plug the cable into the DB9 on the boiler's **operating panel** (*Bedienteil*).
* Nothing needs to be enabled on the controller for the read-out. For heating-mode commands, ETA says each
  heating circuit needs *SMS Erlauben = Ja*.

## Power

The RS-232 port cannot power the board, so use a small **mains USB charger**.

> **Do not use a USB power bank.** Power banks switch themselves off when the load is low, and an idle ESP32
> draws only about 50–100 mA. The bridge then disappears from your network until someone unplugs and replugs it.

The board is a Zigbee **router**: it must stay powered, and as a side effect it strengthens your mesh.

## Placement and radio range

* Pair the board **close to your Zigbee coordinator first** (a few metres, ideally line of sight). The network
  keys are stored in flash, so the board rejoins by itself after every power cycle, wherever it sits.
* Boiler rooms are often in the basement, behind concrete and metal. If the link is weak, put a mains-powered
  Zigbee router (many smart plugs act as one) between the board and the coordinator.
* Keep the antenna a few centimetres away from large metal surfaces such as the boiler casing.
* Battery-powered devices (e.g. smoke detectors) do not relay traffic, so they do not help.

## Status LED

The board's RGB LED (Waveshare: WS2812 on GPIO8) shows what the bridge is doing:

| LED | Meaning |
|---|---|
| red, green, blue (0.5 s each) | boot self-test |
| green (dim) | joined to Zigbee, boiler answering |
| yellow | joined to Zigbee, boiler **not** answering |
| red, blinking | no Zigbee network |
| red, steady | the boiler reported an error |

The Waveshare board's LED takes its colours in **RGB** order, not the usual WS2812 GRB order. The self-test
exists to make this visible: if you see green, red, blue on another board, flip `ETA_LED_ORDER_RGB` in
`firmware/sdkconfig.defaults`.
