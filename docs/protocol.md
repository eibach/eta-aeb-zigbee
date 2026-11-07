# ETA serial protocol (µC ↔ PC over RS-232)

ETA's older pellet / wood boiler controllers ("AEB" family, ca. 2006 onwards) have a DB9 RS-232 port on the
operating panel (*Bedienteil*) that ETA's PC software uses for remote control and commissioning logs. The
same port speaks a small binary protocol that is enough to read temperatures and the pellet stock and to send
a few commands.

This page describes the protocol as **implemented and verified on an ETA PE-25 pellet boiler (built 2008)**. ETA's
own document calls the controller family **AEB**; a PE-15 belongs to the same controller type as the PE-25 (AEL,
pellet boiler up to 30 kW) and should behave the same, but this was not tested.
It is based on ETA's protocol excerpt ("Protokoll µC–PC Auszug", 2007, not redistributed here) plus
observations on a real boiler. Anything not verified on hardware is marked **(unverified)**.

> This is an unofficial description. It is not affiliated with or endorsed by ETA Heiztechnik.

## 1. Physical layer

| | |
|---|---|
| Connector | DB9 **male** on the boiler's operating panel |
| Signals | RS-232 levels (±5…12 V), only RXD / TXD / GND are used |
| Pins (PC's point of view) | 2 = RXD, 3 = TXD, 5 = GND |
| Cable | **straight-through** (not null-modem); PC side female, boiler side male |
| Format | 19200 baud, 8 data bits, no parity, 1 stop bit, no flow control |

The port cannot power anything. A microcontroller needs a level shifter (MAX3232) between its 3.3 V UART and
the RS-232 levels.

## 2. Frame format

Every message in both directions has the same frame:

| Byte | Meaning |
|---|---|
| 0 | start byte `0x7B` (`{`) |
| 1–2 | service ID, two ASCII characters |
| 3 | number of payload bytes `n` (0…255) |
| 4 | checksum: sum of all payload bytes, modulo 256 |
| 5 … 5+n−1 | payload |
| last | stop byte `0x7D` (`}`) |

The length byte makes the frame self-delimiting, so `{` or `}` may appear inside the payload. A receiver
should resynchronise on the next `0x7B` after a checksum or stop-byte error.

16-bit values are **big-endian** (high byte first).

## 3. Services

| Service | Direction | Purpose |
|---|---|---|
| `MC` | PC → boiler | request data (start cyclic reports) |
| `MD` | boiler → PC | data (cyclic) |
| `ME` | PC → boiler | stop reporting |
| `IM` | boiler → PC | error message text (sent unprompted when an error occurs) |
| `IH` | PC → boiler | heating / boiler command (originally made for the SMS remote control) |

### 3.1 `MC` – request data

Payload: `refresh_s` (1 byte, seconds between reports) followed by 1…20 triples
`node (1) | index_hi (1) | index_lo (1)`.

Example, the request this project sends (11 values, refresh 30 s, node `0x10`):

```
7b 4d 43 22 1e | 1e | 10 00 14  10 00 13  10 00 4d  10 00 4e  10 00 15  10 00 86
                      10 00 16  10 00 17  10 00 18  10 00 58  10 00 56 | 7d
   │  │  │  │  │   └ refresh = 30 s
   │  │  │  │  └ checksum 0x1e
   │  │  │  └ payload length 34 = 1 + 11·3
   └──┴──┴ "MC"
```

### 3.2 `MD` – data

Payload: `n` records of 5 bytes: `node (1) | index (2) | value (2, signed)`.

Observed on a real PE-25 (first reply after the request above, about 0.3 s later):

```
7b 4d 44 37 0e |
  10 00 13 03 59   index  19 (flue gas)         raw  857  → 85.7 °C
  10 00 14 02 62   index  20 (boiler)           raw  610  → 61.0 °C
  10 00 15 00 00   index  21                    raw    0
  10 00 16 00 01   index  22 (outside)          raw    1  →  0.1 °C
  10 00 17 02 02   index  23 (flow, direct)     raw  514  → 51.4 °C
  10 00 18 01 ac   index  24 (flow, mixer 0)    raw  428  → 42.8 °C
  10 00 4d 00 00   index  77                    raw    0
  10 00 4e 00 00   index  78                    raw    0
  10 00 56 ff 9d   index  86 (pellet stock)     raw  -99  → -0.99 t
  10 00 58 00 00   index  88                    raw    0
  10 00 86 00 00   index 134                    raw    0
| 7d
```

Things to know:

* **The boiler answers in ascending index order, not in the order requested.** Always match records by
  index.
* The first `MD` arrives immediately after the `MC`; after that one `MD` per `refresh_s` seconds.
* Re-sending `MC` restarts the cycle. This project re-sends it every 10 s until the boiler answers, then every
  5 minutes as a keep-alive, and treats the link as down after 3 refresh periods without data.
* Sensors that are not installed report **0** (exactly). A real 0.0 °C outside temperature is
  indistinguishable from "no sensor", so judge by plant knowledge.
* Temperatures are °C × 10 as a signed 16-bit integer. A plausibility window of −50…400 °C is applied by this
  project.
* The **pellet stock is a counter kept by the controller** (tons × 100). The owner has to enter each delivery
  at the controller; until then the value keeps decreasing and **can become negative** (it was −0.99 t on the
  test boiler before the last delivery was entered). A negative value is real data, not an error marker.

### 3.3 `ME` – stop

No payload: `7b 4d 45 00 00 7d`. Not used by this project (it keeps polling).

### 3.4 `IM` – messages

Payload: message text in ASCII, **not** NUL-terminated. Sent by the boiler unprompted.

ETA's document calls this an error message, but the boiler also uses it for **information notices**: each `IH`
command below made the boiler print "changed via SMS" on its display, and within about two seconds an `IM`
frame arrived (observed through its timing: the fault flag of this project rose two seconds after the first
command, after every command). The text of those frames was not captured, and no real error message has been
seen yet, so the texts of real errors are **unknown**.

There is no "message cleared" frame. This project raises a fault flag on every `IM` and holds it for 30 minutes
after the last one, so it **also fires after your own commands**. A cleaner approach would ignore frames that
arrive within a few seconds after an `IH` command and expose the text.

### 3.5 `IH` – commands

Payload: 2 bytes, `data0`, `data1`. `data1` is always `0x00`. `data0` is a bit mask, one bit per command:

| `data0` | Command | Frame |
|---|---|---|
| `0x01` | heating: reset (use the physical switch again) | `7b 49 48 02 01 01 00 7d` |
| `0x02` | heating: Auto | `7b 49 48 02 02 02 00 7d` |
| `0x04` | heating: Day (*Tag*) | `7b 49 48 02 04 04 00 7d` |
| `0x08` | heating: Night (*Nacht*) | `7b 49 48 02 08 08 00 7d` |
| `0x10` | boiler **on** | `7b 49 48 02 10 10 00 7d` |
| `0x20` | boiler **off** | `7b 49 48 02 20 20 00 7d` |
| `0x40` | charge hot water tank now | `7b 49 48 02 40 40 00 7d` |

There is no reply frame. The boiler shows a "changed via SMS" message on its display.

Verified on the PE-25:

* **Boiler on / off (`0x10` / `0x20`) work.**
* The heating-mode commands (`0x01`…`0x08`) produced the "via SMS" message on the display, but the mode shown
  in the boiler menu did not change. ETA's description says the heating circuits only react if the
  parameter **"SMS Erlauben"** (bottom of each heating circuit menu) is set to *Ja*. Whether that explains the
  observation was not verified.
* Charge hot water (`0x40`) was only tested against a simulator, because the test plant has no hot water tank.
* **There is no summer / off mode in this command set.** If it exists it needs a different command or a parameter
  write that this excerpt does not cover.

There is no way to read the current mode or on/off state back with the commands above.

## 4. Node numbers

The node number in `MC`/`MD` is a base number plus the position of a DIP / rotary switch on the controller
board (normally 0):

| Base | Controller |
|---|---|
| `0x08` | log-wood boiler (AEC) |
| `0x10` | **pellet boiler up to 30 kW (AEL)**, used here |
| `0x18` | pellet boiler from 35 kW, chip firing (AELK, main board) |
| `0x20` | heating extension module (AEH) |
| `0x28` | chip firing, three-phase board |

Only `0x10` was tested.

## 5. Monitor list indices

Which value an index refers to depends on the controller type. This is the list for **AEL (pellet boiler up to
30 kW)**; the values marked ✔ were checked against the boiler display on a PE-25.

| Index | Value | Used here as |
|---:|---|---|
| 20 | boiler temperature | ✔ boiler |
| 19 | flue gas | ✔ flue gas |
| 23 | boiler flow = flow direct circuit (same index in ETA's list) | ✔ flow direct circuit |
| 24 | flow mixer circuit 0 | ✔ flow mixer circuit 0 |
| 22 | outside | ✔ outside |
| 86 | pellet stock (tons × 100) | ✔ pellet stock |
| 77 / 78 | buffer top / bottom | buffer (0 on a plant without buffer) |
| 21 / 134 | hot water tank / tank bottom | hot water (0 without tank) |
| 88 | external heat | external heat (0 without) |
| 25 / 124 | room, direct circuit / mixer circuit 0 | not used |
| 96 / 97 | thermostat hot / cold | not used |

The other controller types (AEC, AEH, AELK) have their own index lists in ETA's document. They are not
reproduced here; contributions with verified values for other models are welcome (see
[development.md](development.md#adding-or-changing-values)).

## 6. Testing the link without the boiler

Short pins 2 and 3 of the DB9 connector on your level shifter (cable removed). The shifter then echoes your own
bytes: you should receive your `MC` frame back. If you see it, the microcontroller, the level shifter and the
wiring are fine, and any remaining problem is the cable or the boiler.
