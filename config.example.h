// ============================================================================
//  config.example.h — copy this file to config.h and fill in your values
//  config.h is git-ignored, so private Wi-Fi credentials never get committed
// ============================================================================

#pragma once

// -------- Wi-Fi / NTP --------
#define WIFI_SSID        "your-wifi-ssid"
#define WIFI_PASS        "your-wifi-password"
#define NTP_SERVER       "pool.ntp.org"
#define TZ_INFO          "VLAT-10"     // POSIX TZ string, Vladivostok UTC+10
#define WIFI_TIMEOUT_MS  10000         // give up connecting after 10 s
#define NTP_SYNC_MINUTES 30            // resync the RTC every 30 min

// -------- Duty cycle --------
#define SLEEP_SECONDS    60            // refresh every 60 s; 300 -> ~4x battery
#define EPD_KEEP_POWERED_DURING_SLEEP 1 // 1 = hold EPD rail (original board), 0 = cut it

// -------- Battery gauge --------
#define ADC_PIN       4
#define BATT_DIVIDER  2.0f             // (200k + 200k) / 200k
#define BATT_MIN_MV   3100             // treated as empty
#define BATT_MAX_MV   4200             // treated as full
