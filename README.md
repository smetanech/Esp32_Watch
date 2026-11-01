# ESP32-S3 Pocket Weather

<p align="center"><img src="docs/preview.png" alt="Screen preview" width="640"></p>

A pocket-sized e-paper clock and meteostation. Wakes up once a minute, updates
the screen and falls back into deep sleep — the e-paper keeps the image with
zero power, so a small Li-ion cell runs the device for weeks.

## Features

- 1.54" 200×200 monochrome e-paper (GDEH0154D67 / SSD1681 family)
- Fully **vector UI** — no `background.h` bitmap, no custom font files:
  procedural 7-segment clock digits + Adafruit GFX built-in FreeFonts
- SHTC3 temperature & humidity, visible `--` fallback if the sensor fails
- PCF85063A RTC keeps real time between deep-sleep cycles
- NTP sync over Wi-Fi every 30 min (~10 s radio burst, off otherwise)
- Battery gauge: 8-sample averaged ADC reading through a 200k/200k divider
- Deep sleep 60 s (configurable), GPIO holds keep power rails stable
- Builds with Arduino IDE **and** PlatformIO

## Hardware

| Part | Note |
|---|---|
| ESP32-S3 dev board | any S3 board with the pins below free |
| 1.54" BW e-paper 200×200 | GDEH0154D67 / SSD1681 |
| SHTC3 | I²C temperature + humidity |
| PCF85063A | I²C RTC (address 0x51) |
| Li-ion cell + charger | divider 200k/200k on the ADC pin |

## Wiring

| Signal | GPIO |
|---|---|
| EPD DC / CS / RST / BUSY | 10 / 11 / 9 / 8 |
| EPD SCK / MOSI | 12 / 13 |
| EPD power rail | 6 |
| Peripheral power rail | 17 |
| SHTC3 / RTC rail | 3 |
| Battery ADC | 4 |
| I²C SDA / SCL | 47 / 48 |

## Build & flash

### Arduino IDE
1. Install libraries: **GxEPD2**, **Adafruit SHTC3 Library**, **Adafruit GFX**,
   and [PCF85063A-SOLDERED](https://github.com/SolderedElectronics/PCF85063A-SOLDERED-Arduino-Library).
2. Copy `firmware/config.example.h` → `firmware/config.h`, fill in Wi-Fi.
3. Open `firmware/ESP32_S3_PocketWeather.ino` (keep the file in a folder with
   the same name), select your ESP32-S3 board, flash.

### PlatformIO
```bash
cp firmware/config.example.h firmware/config.h   # then edit
pio run -t upload && pio device monitor
```

## Configuration

All tunables live in `config.h` (never committed): Wi-Fi credentials, timezone
(`TZ_INFO`), refresh interval (`SLEEP_SECONDS`), NTP sync period, battery
calibration endpoints and the e-paper power policy for deep sleep.

## How it works

```
wake ──> rails on ──> RTC time ──> SHTC3 ──> battery ──> e-paper draw ──> sleep
 │                                                        (~2.5 s, ~45 mA)
 └── every 30 min: Wi-Fi + NTP (≤10 s, ~90 mA) instead of the fast path
deep sleep: ~15 µA · e-paper holds the image with no power
```

## Power budget (approx.)

| Phase | Current | Duty |
|---|---|---|
| Deep sleep | ~15 µA | 57.5 s of 60 s |
| Active (RTC + sensors + draw) | ~45 mA | ~2.5 s every 60 s |
| Wi-Fi + NTP | ~90 mA | ≤10 s every 30 min |

≈ 2 mA average at 60 s refresh → **~2 weeks** on an 800 mAh cell.
Set `SLEEP_SECONDS 300` → ≈ 0.25 mA → **months**. The e-paper does not care.

## Project structure

```
firmware/ESP32_S3_PocketWeather.ino   — the whole sketch
firmware/config.example.h             — template for your private config.h
docs/preview.png, docs/frame_*.png    — simulated screen states
platformio.ini                        — PlatformIO build config
```

## License

MIT — see [LICENSE](LICENSE).

## Credits

Built on [GxEPD2](https://github.com/ZinggJM/GxEPD2) by ZinggJM, Adafruit GFX /
SHTC3 libraries and the Soldered PCF85063A library.
