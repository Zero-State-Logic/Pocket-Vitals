# Hardware Build Guide

Complete step-by-step guide to assembling VitalScope on a veroboard.

## Tools required

- Soldering iron (25–40 W)
- Solder (60/40 lead or lead-free, 0.6–0.8 mm)
- Wire cutters / strippers
- Multimeter (for continuity checks)
- Tweezers
- Helping hands or PCB holder (optional but recommended)

## Recommended assembly order

### 1. Plan the layout

Before any soldering, lay out parts on the veroboard and decide positions. Aim for:
- ESP32 in the center
- Power rails along one edge
- Sensors grouped by signal type (I²C sensors together, AD8232 separate)
- Buttons within easy reach

### 2. Solder the ESP32 socket

Use two 19-pin female header strips. Insert the ESP32 into the headers first, then position on the veroboard — this ensures perfect alignment.

Solder one corner pin first, check that the ESP32 sits flat, then solder the remaining pins.

**Important:** Cut the veroboard tracks between the two rows of ESP32 pins. Without this, left-side pins short to right-side pins.

### 3. Lay down power rails

Use solid wire (red for 3.3V, black for GND, orange for 5V).

- Red rail along one edge → connects to ESP32 3V3
- Black rail along the opposite edge → connects to ESP32 GND
- Orange short jumper near VIN → for any 5V-powered components

### 4. Wire the I²C bus (OLED + MAX30102)

These two devices share GPIO 19 (SCL) and GPIO 23 (SDA). Wire both devices to the same two veroboard tracks.

| Device | VCC | GND | SCL | SDA |
|---|---|---|---|---|
| OLED | 3.3V | GND | GPIO 19 | GPIO 23 |
| MAX30102 | 3.3V | GND | GPIO 19 | GPIO 23 |

### 5. Wire the DS18B20

Three wires plus one critical resistor.

| DS18B20 wire | Color | Connection |
|---|---|---|
| VCC | Red | 3.3V |
| GND | Black | GND |
| Data | Yellow | GPIO 26 |

**Required:** Solder a 4.7 kΩ resistor between the yellow data wire and the 3.3V rail. Without this pull-up, the sensor cannot communicate.

### 6. Wire the AD8232

Five wires. Note that GPIO 0 is a strapping pin — see notes below.

| AD8232 pin | ESP32 pin |
|---|---|
| 3.3V | 3.3V |
| GND | GND |
| OUTPUT | GPIO 36 (VP) |
| LO+ | GPIO 18 |
| LO- | GPIO 0 |

**GPIO 0 workflow:** Flash the ESP32 firmware *before* plugging it into the veroboard. After flashing, insert the ESP32 into the socket. Avoids the boot-mode conflict the LO- pin can cause.

### 7. Wire the buttons

Three buttons, each with two terminals:

- One terminal of each button → its dedicated GPIO (32, 33, 25)
- Other terminal of each button → shared GND rail

The firmware uses ESP32's internal pull-up resistors, so no external resistors needed.

| Button | GPIO |
|---|---|
| UP | 32 |
| SELECT | 25 |
| DOWN | 33 |

## Pre-power-on checklist

Before applying power, verify with a multimeter:

**Continuity checks (must beep):**
- 3.3V rail ↔ ESP32 3V3 socket pin
- GND rail ↔ ESP32 GND socket pin
- Each I²C wire from ESP32 to each I²C device

**Short checks (must NOT beep):**
- 3.3V rail ↔ GND rail
- 5V rail ↔ GND rail (if applicable)
- Adjacent ESP32 socket pins

**Resistor check:**
- DS18B20 data wire ↔ 3.3V rail → should read ~4.7 kΩ

## Troubleshooting common issues

| Symptom | Likely cause | Fix |
|---|---|---|
| OLED stays dark | I²C wires swapped or no power | Check VCC/GND, then SCL/SDA assignment |
| MAX30102 not detected | I²C address conflict or no power | Run I²C scanner sketch |
| DS18B20 shows -127°C | Missing 4.7 kΩ pull-up | Add resistor between data and 3.3V |
| Random reset on boot | GPIO 0 conflict from AD8232 LO- | Disconnect AD8232 during boot, reconnect after |
| ECG shows flat line | Electrodes not contacting | Clean skin, press pads firmly, check snap clicks |
| Buttons unresponsive | Pull-ups not enabled in code | Use `INPUT_PULLUP` mode (already in firmware) |

## Going further

When the breadboard build is verified working, the next steps are:

1. **3D-printed enclosure** — protect the electronics and make it look professional
2. **Battery integration** — LiPo + TP4056 charger module + MT3608 boost converter
3. **Custom PCB** — KiCad design to replace the veroboard for a smaller form factor
4. **OTA firmware updates** — Wi-Fi-based code updates without USB connection
