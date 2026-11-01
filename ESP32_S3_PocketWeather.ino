// ============================================================================
//  ESP32-S3 Pocket Weather — e-paper pocket clock & meteostation
// ---------------------------------------------------------------------------
//  Hardware : ESP32-S3 · 1.54" 200x200 BW e-paper (GDEH0154D67)
//             SHTC3 (temperature / humidity) · PCF85063A RTC
//             Li-ion cell via 200k/200k ADC divider
//  Behavior : wake -> read RTC + SHTC3 + battery -> draw -> deep sleep
//             Wi-Fi / NTP burst only every NTP_SYNC_MINUTES (default 30)
//  UI       : fully vector, no bitmap assets, no custom font files
//  License  : MIT (see LICENSE)
// ============================================================================

#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <GxEPD2_BW.h>
#include <Adafruit_SHTC3.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <esp_sleep.h>
#include <PCF85063A-SOLDERED.h>

#if __has_include("config.h")
  #include "config.h"          // your private settings (git-ignored)
#else
  #include "config.example.h"  // copy -> config.h and fill in
#endif

// ----------------------------- Pins -----------------------------------------
#define EPD_DC   10
#define EPD_CS   11
#define EPD_SCK  12
#define EPD_MOSI 13
#define EPD_RST  9
#define EPD_BUSY 8
#define EPD_PWR  6     // e-paper module power rail
#define VBAT_PWR 17    // peripheral power rail
#define PERIPH_PWR 3   // SHTC3 / RTC rail (kept HIGH)
#define ADC_PIN  4     // battery voltage via 200k/200k divider

#define I2C_SDA  47
#define I2C_SCL  48
#define PCF85063A_ADDR 0x51

#define SPI_CLOCK_HZ   4000000

// ----------------------------- Display --------------------------------------
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
  GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

Adafruit_SHTC3 shtc3;
PCF85063A rtc;

RTC_DATA_ATTR int bootCount = 0;   // survives deep sleep

// ----------------------------- UI layout (200x200) ---------------------------
static const int CHIP_X = 8,  CHIP_Y = 8,   CHIP_W = 50, CHIP_H = 24;
static const int BAT_X  = 150, BAT_Y = 8,   BAT_W  = 38, BAT_H  = 16;

static const int DIG_W = 32, DIG_H = 56, DIG_T = 6;
static const int TIME_Y = 38;
static const int X_D1 = 20, X_D2 = 58, X_COLON = 96, X_D3 = 108, X_D4 = 146;

static const int RULE_Y = 102;

static const int CARD_Y = 132, CARD_H = 62, CARD_W = 92;
static const int CARD_L_X = 6, CARD_R_X = 102;

static const char* const DAY_NAMES[7] =
  {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};
static const char* const MONTH_NAMES[12] =
  {"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};

// 7-segment mask: bit0=a(top) ... bit6=g(middle)
static const uint8_t SEG7[10] =
  {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};

char timeStr[6];
char tempStr[8];
char humStr[8];
sensors_event_t humEv, tempEv;

// ----------------------------- Small helpers ---------------------------------
static uint8_t toBCD(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

static void seg(int x, int y, int w, int h) {
  display.fillRoundRect(x, y, w, h, 1, GxEPD_BLACK);
}

// Procedural 7-segment digit — crisp on e-paper, no font file needed
static void drawSevenSeg(char c, int x, int y) {
  if (c >= '0' && c <= '9') {
    uint8_t m = SEG7[c - '0'];
    int half = DIG_H / 2;
    int vh   = half - DIG_T - DIG_T / 2;          // vertical segment length
    if (m & 0x01) seg(x + DIG_T, y, DIG_W - 2 * DIG_T, DIG_T);                // a
    if (m & 0x02) seg(x + DIG_W - DIG_T, y + DIG_T, DIG_T, vh);               // b
    if (m & 0x04) seg(x + DIG_W - DIG_T, y + half + DIG_T / 2, DIG_T, vh);    // c
    if (m & 0x08) seg(x + DIG_T, y + DIG_H - DIG_T, DIG_W - 2 * DIG_T, DIG_T);// d
    if (m & 0x10) seg(x, y + half + DIG_T / 2, DIG_T, vh);                    // e
    if (m & 0x20) seg(x, y + DIG_T, DIG_T, vh);                               // f
    if (m & 0x40) seg(x + DIG_T, y + half - DIG_T / 2, DIG_W - 2 * DIG_T, DIG_T); // g
  } else if (c == ':') {
    display.fillRect(x + (DIG_W - 7) / 2, y + 14, 7, 7, GxEPD_BLACK);
    display.fillRect(x + (DIG_W - 7) / 2, y + 35, 7, 7, GxEPD_BLACK);
  }
}

static void drawBattery(int segments) {
  display.drawRect(BAT_X, BAT_Y, BAT_W, BAT_H, GxEPD_BLACK);
  display.drawRect(BAT_X + 1, BAT_Y + 1, BAT_W - 2, BAT_H - 2, GxEPD_BLACK);
  display.fillRect(BAT_X + BAT_W + 1, BAT_Y + 4, 4, 8, GxEPD_BLACK); // contact
  for (int i = 0; i < segments; i++)
    display.fillRect(BAT_X + 4 + i * 6, BAT_Y + 4, 5, 8, GxEPD_BLACK);
}

static void drawWeekdayChip(int wday) {
  display.fillRoundRect(CHIP_X, CHIP_Y, CHIP_W, CHIP_H, 5, GxEPD_BLACK);
  const char* name = DAY_NAMES[constrain(wday, 0, 6)];
  int16_t bx, by; uint16_t bw, bh;
  display.setFont(&FreeSansBold9pt7b);
  display.getTextBounds(name, 0, 0, &bx, &by, &bw, &bh);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(CHIP_X + (CHIP_W - bw) / 2, CHIP_Y + (CHIP_H + bh) / 2);
  display.print(name);
  display.setTextColor(GxEPD_BLACK);
}

static void drawDate(int day, int mon, int year) {
  char buf[16];
  const char* m = MONTH_NAMES[(mon >= 1 && mon <= 12) ? mon - 1 : 0];
  snprintf(buf, sizeof(buf), "%02d %s %04d", day, m, year);
  int16_t bx, by; uint16_t bw, bh;
  display.setFont(&FreeSansBold12pt7b);
  display.getTextBounds(buf, 0, 0, &bx, &by, &bw, &bh);
  display.setCursor((200 - bw) / 2, 124);
  display.print(buf);
}

static void drawCard(int x, const char* label, const char* value, bool isTemp) {
  display.drawRoundRect(x, CARD_Y, CARD_W, CARD_H, 6, GxEPD_BLACK);

  display.setFont(&FreeSansBold9pt7b);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(x + 10, CARD_Y + 18);
  display.print(label);

  // icon, top-right corner of the card
  int cx = x + CARD_W - 12, cy = CARD_Y + 15;
  if (isTemp) {                                   // thermometer
    display.fillRect(cx - 1, cy - 12, 2, 10, GxEPD_BLACK);
    display.fillCircle(cx, cy, 3, GxEPD_BLACK);
    display.fillCircle(cx, cy, 1, GxEPD_WHITE);
  } else {                                        // droplet
    display.fillTriangle(cx - 3, cy - 1, cx + 3, cy - 1, cx, cy - 8, GxEPD_BLACK);
    display.fillCircle(cx, cy + 1, 4, GxEPD_BLACK);
  }

  // value, centered in the card
  display.setFont(&FreeSansBold18pt7b);
  int16_t bx, by; uint16_t bw, bh;
  int baseline = CARD_Y + CARD_H - 10;
  display.getTextBounds(value, 0, 0, &bx, &by, &bw, &bh);
  display.setCursor(x + (CARD_W - bw) / 2, baseline);
  display.print(value);

  if (isTemp) {  // degree sign as a small circle right of the value
    display.getTextBounds(value, x + (CARD_W - bw) / 2, baseline, &bx, &by, &bw, &bh);
    display.fillCircle(bx + bw + 4, by + 4, 2, GxEPD_BLACK);
  }
}

// ----------------------------- RTC -------------------------------------------
static void rtcWriteReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(PCF85063A_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static void rtcWriteDateTime(uint8_t sec, uint8_t mn, uint8_t hr,
                             uint8_t day, uint8_t wd, uint8_t mon, uint16_t year) {
  rtcWriteReg(0x04, toBCD(sec));
  rtcWriteReg(0x05, toBCD(mn));
  rtcWriteReg(0x06, toBCD(hr));
  rtcWriteReg(0x07, toBCD(day));
  rtcWriteReg(0x08, wd & 0x07);
  rtcWriteReg(0x09, toBCD(mon));
  rtcWriteReg(0x0A, toBCD(year % 100));
}

// ----------------------------- Wi-Fi + NTP -----------------------------------
static void syncNTP() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS)
    delay(100);

  if (WiFi.status() == WL_CONNECTED) {
    configTzTime(TZ_INFO, NTP_SERVER);
    struct tm ti;
    if (getLocalTime(&ti, 5000)) {
      uint8_t ourWday = (ti.tm_wday + 6) % 7;   // 0 = Monday
      rtcWriteDateTime(ti.tm_sec, ti.tm_min, ti.tm_hour,
                       ti.tm_mday, ourWday, ti.tm_mon + 1, ti.tm_year + 1900);
      Serial.println("NTP: time written to RTC");
    }
  } else {
    Serial.println("Wi-Fi: connection failed, keeping RTC time");
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// ----------------------------- Sensors & battery ------------------------------
static void readSensors() {
  bool ok = shtc3.begin() && shtc3.getEvent(&humEv, &tempEv);
  if (!ok) { delay(5); ok = shtc3.begin() && shtc3.getEvent(&humEv, &tempEv); }
  if (ok) {
    dtostrf(tempEv.temperature, 1, 1, tempStr);
    dtostrf(humEv.relative_humidity, 1, 0, humStr);
  } else {
    strcpy(tempStr, "--");   // visible failure state instead of garbage
    strcpy(humStr, "--");
  }
}

static int readBatteryVoltage() {
  analogReadResolution(12);
  analogSetPinAttenuation(ADC_PIN, ADC_11db);
  long acc = 0;
  for (int i = 0; i < 8; i++) acc += analogReadMilliVolts(ADC_PIN);  // averaging
  int mv = (int)((acc / 8) * BATT_DIVIDER);
  return constrain(mv, BATT_MIN_MV, BATT_MAX_MV);
}

static int batterySegments() {
  return map(readBatteryVoltage(), BATT_MIN_MV, BATT_MAX_MV, 0, 5); // always 0..5
}

// ----------------------------- Screen -----------------------------------------
static void epdDraw() {
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.epd2.selectSPI(SPI, SPISettings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0));
  display.init(115200);
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    drawBattery(batterySegments());
    drawWeekdayChip(rtc.getWeekday());

    drawSevenSeg(timeStr[0], X_D1, TIME_Y);
    drawSevenSeg(timeStr[1], X_D2, TIME_Y);
    drawSevenSeg(':', X_COLON, TIME_Y);
    drawSevenSeg(timeStr[3], X_D3, TIME_Y);
    drawSevenSeg(timeStr[4], X_D4, TIME_Y);

    display.fillRect(20, RULE_Y, 160, 2, GxEPD_BLACK);
    drawDate(rtc.getDay(), rtc.getMonth(), rtc.getYear());

    drawCard(CARD_L_X, "TEMP", tempStr, true);
    drawCard(CARD_R_X, "HUM",  humStr,  false);

  } while (display.nextPage());

  display.hibernate();
}

// ----------------------------- Setup ------------------------------------------
void setup() {
  Serial.begin(115200);

  // release holds from the previous deep-sleep cycle
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)VBAT_PWR);
  gpio_hold_dis((gpio_num_t)EPD_PWR);

  pinMode(VBAT_PWR, OUTPUT);
  digitalWrite(VBAT_PWR, HIGH);
  pinMode(PERIPH_PWR, OUTPUT);
  digitalWrite(PERIPH_PWR, HIGH);
  pinMode(EPD_PWR, OUTPUT);
  digitalWrite(EPD_PWR, LOW);
  delay(10);

  Wire.begin(I2C_SDA, I2C_SCL);
  rtc.begin();

  if (bootCount % (NTP_SYNC_MINUTES * 60 / SLEEP_SECONDS) == 0) syncNTP();
  bootCount++;

  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", rtc.getHour(), rtc.getMinute());

  readSensors();
  epdDraw();

  // power policy for the e-paper rail between wakes
#if EPD_KEEP_POWERED_DURING_SLEEP
  digitalWrite(EPD_PWR, HIGH);
#else
  digitalWrite(EPD_PWR, LOW);
#endif
  gpio_hold_en((gpio_num_t)VBAT_PWR);
  gpio_hold_en((gpio_num_t)EPD_PWR);
  gpio_deep_sleep_hold_en();
  delay(5);

  esp_sleep_enable_timer_wakeup((uint64_t)SLEEP_SECONDS * 1000000ULL);
  esp_deep_sleep_start();
}

void loop() {}  // unreachable — setup() ends in deep sleep
