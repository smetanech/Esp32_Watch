/*
 * ESP32-S3 Pocket Weather — configuration
 */

#define WIFI_SSID  "YourSSID"
#define WIFI_PASS  "YourPASS"

#define NTP_SERVER       "pool.ntp.org"
#define TZ_INFO          "VLAT-10"    // Vladivostok, UTC+10
#define NTP_SYNC_MINUTES 30           // Wi-Fi sync interval, minutes
#define WIFI_TIMEOUT_MS  10000UL      // Wi-Fi connect timeout, ms

// --- Timing Settings ---
#define UPDATE_INTERVAL_SECONDS 60    // Total time between updates (1 minute)
#define ACTIVE_WINDOW_SECONDS  10    // How long device stays awake for button hold
// The device will automatically sleep for (UPDATE_INTERVAL_SECONDS - ACTIVE_WINDOW_SECONDS)

#define DISPLAY_YEAR  2026            // fixed year shown on the display

#define BTN_POWER          0          // BOOT button GPIO (USB mode)
#define USB_EXIT_HOLD_MS   1500       // hold time to leave USB mode, ms
#define EPD_KEEP_POWERED_DURING_SLEEP 1  // 1 = keep display rail on

#define SPI_CLOCK_HZ 4000000UL        // e-paper SPI speed
