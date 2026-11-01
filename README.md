# ⏱ ESP32-S3 Pocket Weather

![Version](https://img.shields.io/badge/version-1.1-blue)
![Board](https://img.shields.io/badge/board-Waveshare%20ESP32--S3%201.54-black)
![License](https://img.shields.io/badge/license-MIT-green)
![Platform](https://img.shields.io/badge/platform-Arduino%20IDE%20%7C%20PlatformIO-teal)

![Display preview](docs/preview.png)

> **⚠ This firmware is written specifically for the Waveshare ESP32-S3 e-Paper 1.54 board.**
> The pin map, display driver selection and power scheme in this project are matched
> to that exact hardware. Do not expect it to run unchanged on other boards —
> check the [Components](#components) section first.

A pocket-sized e-paper clock and meteostation. Once a minute the device wakes up,
draws a full frame — big seven-segment clock, weekday badge, date, temperature,
humidity and a battery gauge — and instantly goes back into deep sleep. The e-paper
panel keeps the picture with zero power, so the battery only pays for a couple of
seconds of activity per cycle.

## Components

| Board | Manufacturer |
|---|---|
| waveshare esp32-s3 1.54 | [Waveshare — ESP32-S3 e-Paper 1.54](https://www.waveshare.com/esp32-s3-epaper-1.54.htm) |

**All subsequent code is written for this exact board.** The pin mapping, the
`GxEPD2_154_D67` display profile and the power-hold logic follow the Waveshare
reference design. You can find additional information about this board in the
manufacturer's repository: **[waveshareteam/ESP32-S3-ePaper-1.54](https://github.com/waveshareteam/ESP32-S3-ePaper-1.54)** —
it contains the original examples of working with the display functions,
the vendor e-paper driver and hardware documentation. If something on the
hardware level behaves differently from what this firmware expects, that
repository is the first place to check.

## What it does

- **Time `HH:MM`** — large procedural seven-segment digits (no font files needed).
- **Weekday badge** — MON…SUN, top-left.
- **Date `DD MON YYYY`** — with a **fixed year from the config** (`DISPLAY_YEAR 2026`
  by default), because the PCF85063A RTC stores only a 2-digit year.
- **Temperature and humidity** from the SHTC3 sensor; `--` is shown if the sensor fails.
- **Battery gauge** — 5 segments, averaged ADC readings, clamped so segments never overflow.
- **Deep sleep between updates** — Wi-Fi + NTP sync only once every 30 minutes.

## Repository structure

```
.
├── ESP32_S3_PocketWeather/       # Arduino IDE sketch folder
│   ├── ESP32_S3_PocketWeather.ino
│   └── config.example.h          # copy to config.h and edit
├── docs/
│   └── preview.png
├── platformio.ini
├── .gitignore
├── LICENSE
├── README.md
└── README_RU.md
```

## Pin map

| Function | GPIO |
|---|---|
| EPD_DC | 10 |
| EPD_CS | 11 |
| EPD_SCK | 12 |
| EPD_MOSI | 13 |
| EPD_RST | 9 |
| EPD_BUSY | 8 |
| EPD_PWR | 6 |
| VBAT_PWR | 17 |
| I2C SDA | 47 |
| I2C SCL | 48 |
| Battery ADC | 4 (200k/200k divider) |

## Configuration

Copy `config.example.h` to `config.h` (it is in `.gitignore`, so your secrets
will never be committed) and edit:

| Define | Default | Meaning |
|---|---|---|
| `WIFI_SSID` / `WIFI_PASS` | — | Your Wi-Fi network |
| `NTP_SERVER` | `pool.ntp.org` | NTP source |
| `TZ_INFO` | `VLAT-10` | POSIX timezone string |
| `NTP_SYNC_MINUTES` | 30 | Re-sync interval |
| `DISPLAY_YEAR` | 2026 | **Fixed year shown on screen** |
| `SLEEP_SECONDS` | 60 | Update period |
| `BATTERY_*` | 3100–4200 mV | Gauge calibration |
| `EPD_KEEP_POWERED_DURING_SLEEP` | 1 | Display rail during sleep |

## Build & flash

### Arduino IDE

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| Port | COM5 (select yours) |
| USB CDC On Boot | Enabled |
| CPU Frequency | 240MHz (WiFi) |
| Flash Mode | QIO 80MHz |
| Flash Size | 4MB (32Mb) |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
| PSRAM | OPI PSRAM |
| Upload Speed | 921600 |

Step by step:

1. **Install Arduino IDE** (2.x recommended) from arduino.cc.
2. **Add the ESP32 core:** `File → Preferences → Additional boards manager URLs`
   and paste `https://espressif.github.io/arduino-esp32/package_esp32_index.json`.
3. **Install the core:** `Tools → Board → Boards Manager…`, search `esp32`,
   install **esp32 by Espressif Systems**.
4. **Install libraries** via `Sketch → Include Library → Manage Libraries…`:
   `GxEPD2`, `Adafruit GFX Library`, `Adafruit BusIO`, `Adafruit SHTC3 Library`.
5. **Install the RTC library:** the project uses the Soldered `PCF85063A`
   library. Search `PCF85063A` in the Library Manager; if it is not there,
   grab the ZIP from the Soldered GitHub and use `Sketch → Include Library → Add .ZIP Library…`.
6. **Prepare the sketch folder:** clone or download this repository and keep
   the folder name `ESP32_S3_PocketWeather` — the Arduino IDE requires the
   folder and the `.ino` file to have the same name. Open the `.ino`.
7. **Create your config:** copy `config.example.h` to `config.h` in the same
   folder and enter your Wi-Fi credentials.
8. **Select the board:** under `Tools`, apply every row of the table above.
   Wrong `Flash Size` or `Partition Scheme` is the most common cause of
   "Sketch too big" errors.
9. **Select the port:** plug the board in over USB, choose the port that
   appears (`COM5` here — yours may differ). If no port shows up, install
   the CH340/CP210x driver for your board revision.
10. **Upload:** press the Upload button (→). If the upload stalls at
    "Connecting…", hold the **BOOT** button on the board until the
    transfer starts.
11. **Verify:** open `Tools → Serial Monitor` at `115200` baud — after each
    wake-up you should see the `RTC: ...` line with the current time.

### PlatformIO

```bash
pio run -t upload && pio device monitor
```

The library list is already in `platformio.ini`. Copy `config.example.h`
to `config.h` before the first build.

## How it works

1. The hardware timer wakes the chip every `SLEEP_SECONDS`.
2. GPIO holds are released, peripheral power rails go up, I²C starts.
3. Time is read from the PCF85063A (it keeps time across sleep cycles).
4. Temperature and humidity are read from the SHTC3.
5. Battery voltage is measured (8 averaged ADC samples).
6. A full frame is drawn over SPI, the panel controller is hibernated.
7. Power rails are held, deep sleep starts.

Every 30 minutes (configurable) one cycle additionally brings up Wi-Fi,
fetches NTP time with the configured timezone and writes it into the RTC.

## Power budget

Measured/typical currents used in the model:

| Phase | Current | Duration |
|---|---|---|
| Active cycle (draw + sensors) | ~55 mA | ~2 s |
| Wi-Fi + NTP burst | ~95 mA | ~8 s, every 30 min |
| Deep sleep | ~20 µA | rest of the time |

Average current for an update period `T` (seconds):

```
N     = 3600 / T                                  # wake-ups per hour
I_avg = (55mA · 2s · N + 95mA · 8s · 2 + 0.02mA · 3600s) / 3600s
life  = capacity(mAh) / I_avg(mA) / 24            # days
```

Worked example for `T = 60 s` and a 300 mAh cell:
`I_avg = (55·2·60 + 95·8·2 + 0.02·3600) / 3600 ≈ 2.28 mA`,
`life = 300 / 2.28 / 24 ≈ 5.5 days`.

| Battery | Update every 60 s | Update every 5 min |
|---|---|---|
| 300 mAh | ~5.5 days | ~15.5 days |
| 400 mAh | ~7.3 days | ~20.6 days |
| 500 mAh | ~9.2 days | ~25.8 days |
| 1000 mAh | ~18.3 days | ~51.5 days |

These are estimates: the real figure depends on your cell, the Wi-Fi
environment and how long the NTP window actually lasts.

## Troubleshooting

| Symptom | Fix |
|---|---|
| Time resets after power loss | RTC backup battery missing/dead — the PCF85063A needs its own cell |
| `--` on TEMP/HUM | SHTC3 wiring or I²C address — check the 47/48 lines |
| Upload stuck on "Connecting…" | Hold BOOT while flashing |
| Sketch too big | Wrong partition scheme — use Huge APP (3MB No OTA/1MB SPIFFS) |
| Ghosting on the display | Full refresh runs every cycle; if you changed to partial mode, do a full refresh every N cycles |

## Ideas for improvements

- Dark theme (inverted plates).
- Temperature trend arrow vs the previous reading.
- Multiple Wi-Fi credentials with fallback.
- Web config portal instead of editing `config.h`.

## License

MIT — see [LICENSE](LICENSE).
