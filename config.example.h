/**
 * ESP32-S3 Pocket Weather - configuration template
 * ------------------------------------------------
 * Copy this file to `config.h` and fill in your values.
 * config.h is git-ignored; this template is safe to commit.
 * All values are wrapped in #ifndef, so config.h may override
 * only some of them.
 */

#ifndef POCKET_WEATHER_CONFIG_H
#define POCKET_WEATHER_CONFIG_H

/* ---------------- Wi-Fi ---------------- */
#ifndef WIFI_SSID
  #define WIFI_SSID "YourWiFiName"
#endif
#ifndef WIFI_PASS
  #define WIFI_PASS "YourWiFiPassword"
#endif

/* ---------------- NTP / timezone ---------------- */
#ifndef NTP_SERVER
  #define NTP_SERVER "pool.ntp.org"
#endif
/* POSIX TZ string, e.g. Vladivostok: VLAT-10, Moscow: MSK-3 */
#ifndef TZ_INFO
  #define TZ_INFO "VLAT-10"
#endif
#ifndef WIFI_TIMEOUT_MS
  #define WIFI_TIMEOUT_MS 10000
#endif
/* How often to re-sync time over NTP, minutes */
#ifndef NTP_SYNC_MINUTES
  #define NTP_SYNC_MINUTES 30
#endif

/* ---------------- Fixed displayed year ---------------- */
/* PCF85063A keeps only a 2-digit year, so the year shown on the
 * screen is taken from this define. Change it when needed. */
#ifndef DISPLAY_YEAR
  #define DISPLAY_YEAR 2026
#endif

/* ---------------- Timing ---------------- */
#ifndef SPI_CLOCK_HZ
  #define SPI_CLOCK_HZ 4000000
#endif
/* Update period in seconds. 60 = every minute, 300 = every 5 min. */
#ifndef SLEEP_SECONDS
  #define SLEEP_SECONDS 60
#endif

/* ---------------- Battery gauge ---------------- */
#ifndef BATTERY_ADC_PIN
  #define BATTERY_ADC_PIN 4
#endif
#ifndef BATTERY_SAMPLES
  #define BATTERY_SAMPLES 8
#endif
#ifndef BATTERY_DIVIDER_RATIO
  #define BATTERY_DIVIDER_RATIO 2.0f   /* 200k / 200k */
#endif
#ifndef BATTERY_MIN_MV
  #define BATTERY_MIN_MV 3100
#endif
#ifndef BATTERY_MAX_MV
  #define BATTERY_MAX_MV 4200
#endif
#ifndef BATTERY_SEGMENTS
  #define BATTERY_SEGMENTS 5
#endif

/* ---------------- Power scheme ---------------- */
/* On the stock Waveshare board the display rail is held during deep
 * sleep (original behavior). Set to 0 to cut display power in sleep
 * and save extra current. */
#ifndef EPD_KEEP_POWERED_DURING_SLEEP
  #define EPD_KEEP_POWERED_DURING_SLEEP 1
#endif

#endif /* POCKET_WEATHER_CONFIG_H */
