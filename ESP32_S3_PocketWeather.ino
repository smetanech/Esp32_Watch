/**
 * =====================================================================
 *  ESP32-S3 Pocket Weather - E-paper clock & meteostation
 * =====================================================================
 *  Target board : Waveshare ESP32-S3 e-Paper 1.54
 *                 https://www.waveshare.com/esp32-s3-epaper-1.54.htm
 *
 *  Hardware     : ESP32-S3, 1.54" 200x200 e-paper (GDEH0154D67 class,
 *                 driven via GxEPD2), SHTC3 temp/humidity sensor,
 *                 PCF85063A external RTC, Li-Ion battery with
 *                 200k/200k divider on the ADC pin.
 *
 *  Behavior     : wakes up once per SLEEP_SECONDS, draws a full frame
 *                 (time, date, weekday, temp/hum, battery), then goes
 *                 into deep sleep. Wi-Fi + NTP sync runs only once per
 *                 NTP_SYNC_MINUTES. The displayed YEAR is FIXED and
 *                 taken from DISPLAY_YEAR in the config (PCF85063A
 *                 stores only a 2-digit year, so it is not trusted).
 *
 *  License      : MIT
 * =====================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <esp_sleep.h>

#include <GxEPD2_BW.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SHTC3.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include "PCF85063A-SOLDERED.h"

/* User overrides (optional). Everything inside config.example.h is
 * wrapped in #ifndef, so config.h may define only a few values. */
#if __has_include("config.h")
  #include "config.h"
#endif
#include "config.example.h"

/* ---------------- Pin map (Waveshare ESP32-S3 1.54) ---------------- */
#define EPD_DC     10
#define EPD_CS     11
#define EPD_SCK    12
#define EPD_MOSI   13
#define EPD_RST     9
#define EPD_BUSY    8
#define EPD_PWR     6
#define VBAT_PWR   17

#define I2C_SDA    47
#define I2C_SCL    48

#define PCF85063A_ADDR 0x51

/* ---------------- Display layout (200 x 200 px) ---------------- */
/* Weekday badge (top-left) */
#define BADGE_X   6
#define BADGE_Y   6
#define BADGE_W   56
#define BADGE_H   24

/* Battery indicator (top-right) */
#define BAT_X   146
#define BAT_Y   12
#define BAT_W   42
#define BAT_H   16

/* Big seven-segment clock */
#define CLK_DIGIT_W  26
#define CLK_DIGIT_H  50
#define CLK_SEG_T     7
#define CLK_GAP       8
#define CLK_COLON_W  14
#define CLK_TOTAL_W  (4 * CLK_DIGIT_W + 2 * CLK_GAP + CLK_COLON_W)
#define CLK_Y        58

/* Divider + date line */
#define DIV_X1   12
#define DIV_X2   188
#define DIV_Y    120
#define DATE_BASELINE 140

/* Info cards (bottom) */
#define CARD_W   88
#define CARD_H   46
#define CARD_Y   148
#define CARD1_X  10
#define CARD2_X  102

/* ---------------- Driver objects ---------------- */
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
  GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

Adafruit_SHTC3 shtc3;
PCF85063A rtc;

char timeStr[6];
char dateStr[12];
char tempStr[8];
char humStr[8];
int  voltageSegments = 0;

const char* dayNames[7]    = {"MON","TUE","WED","THU","FRI","SAT","SUN"};
const char* monthNames[12] = {"JAN","FEB","MAR","APR","MAY","JUN",
                              "JUL","AUG","SEP","OCT","NOV","DEC"};

RTC_DATA_ATTR int bootCount = 0;   /* survives deep sleep */

/* ================= BCD helpers ================= */
uint8_t toBCD(uint8_t v) {
  return ((v / 10) << 4) | (v % 10);
}

void rtcWriteReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(PCF85063A_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

void rtcWriteDateTime(uint8_t sec, uint8_t mn, uint8_t hr,
                      uint8_t day, uint8_t wd, uint8_t mon, uint16_t year) {
  rtcWriteReg(0x04, toBCD(sec));
  rtcWriteReg(0x05, toBCD(mn));
  rtcWriteReg(0x06, toBCD(hr));
  rtcWriteReg(0x07, toBCD(day));
  rtcWriteReg(0x08, wd & 0x07);
  rtcWriteReg(0x09, toBCD(mon));
  rtcWriteReg(0x0A, toBCD(year % 100));
}

/* ================= Wi-Fi + NTP ================= */
void syncNTP() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_TIMEOUT_MS) {
    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.mode(WIFI_OFF);
    return;
  }

  configTzTime(TZ_INFO, NTP_SERVER);

  struct tm ti;
  if (getLocalTime(&ti, 5000)) {
    uint8_t ourWday = (ti.tm_wday + 6) % 7;   /* Monday-first index */
    rtcWriteDateTime(ti.tm_sec, ti.tm_min, ti.tm_hour,
                     ti.tm_mday, ourWday, ti.tm_mon + 1, DISPLAY_YEAR);
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

/* ================= Text helpers ================= */
/* Prints text horizontally centered at (cx, baselineY).
 * Font must be set by the caller. */
void printCentered(int cx, int baselineY, const char* text) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(cx - w / 2, baselineY);
  display.print(text);
}

/* ================= Battery icon ================= */
void drawBatteryIcon(int x, int y, int w, int h, int segs) {
  display.drawRoundRect(x, y, w, h, 2, GxEPD_BLACK);
  display.fillRect(x + w, y + h / 2 - 3, 3, 6, GxEPD_BLACK);   /* cap */
  for (int i = 0; i < segs; i++) {
    display.fillRect(x + 3 + i * 7, y + 3, 5, h - 6, GxEPD_BLACK);
  }
}

/* ================= Seven-segment clock (procedural) ================= */
static const uint8_t SEG_MAP[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66,
  0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

void drawSegRect(int x, int y, int w, int h) {
  display.fillRoundRect(x, y, w, h, 2, GxEPD_BLACK);
}

/* mask bits: 0=A(top) 1=B(tr) 2=C(br) 3=D(bottom)
 *           4=E(bl) 5=F(tl) 6=G(middle)                    */
void drawSevenSegDigit(int x, int y, int mask) {
  const int w = CLK_DIGIT_W, h = CLK_DIGIT_H, t = CLK_SEG_T;
  int ym = y + h / 2;

  if (mask & 0x01) drawSegRect(x + t,     y,          w - 2 * t, t);        /* A */
  if (mask & 0x02) drawSegRect(x + w - t, y + 1,      t,        h / 2);      /* B */
  if (mask & 0x04) drawSegRect(x + w - t, ym,         t,        h / 2 - 1);  /* C */
  if (mask & 0x08) drawSegRect(x + t,     y + h - t,  w - 2 * t, t);        /* D */
  if (mask & 0x10) drawSegRect(x,         ym,         t,        h / 2 - 1);  /* E */
  if (mask & 0x20) drawSegRect(x,         y + 1,      t,        h / 2);      /* F */
  if (mask & 0x40) drawSegRect(x + t,     ym - t / 2, w - 2 * t, t);        /* G */
}

void drawClock(int x0, int y0) {
  const int w = CLK_DIGIT_W, h = CLK_DIGIT_H;
  int x = x0;

  drawSevenSegDigit(x, y0, SEG_MAP[timeStr[0] - '0']);
  x += w + CLK_GAP;
  drawSevenSegDigit(x, y0, SEG_MAP[timeStr[1] - '0']);
  x += w;

  int colX = x + (CLK_COLON_W - 7) / 2;
  display.fillRect(colX, y0 + 8,     7, 7, GxEPD_BLACK);
  display.fillRect(colX, y0 + h - 15, 7, 7, GxEPD_BLACK);

  x += CLK_COLON_W + CLK_GAP;
  drawSevenSegDigit(x, y0, SEG_MAP[timeStr[3] - '0']);
  x += w + CLK_GAP;
  drawSevenSegDigit(x, y0, SEG_MAP[timeStr[4] - '0']);
}

/* ================= Small icons ================= */
void drawThermometerIcon(int x, int y) {
  display.drawRoundRect(x, y, 7, 11, 3, GxEPD_BLACK);
  display.fillCircle(x + 3, y + 13, 4, GxEPD_BLACK);
  display.fillCircle(x + 3, y + 13, 2, GxEPD_WHITE);
}

void drawDropletIcon(int x, int y) {
  display.fillTriangle(x + 5, y, x, y + 7, x + 10, y + 7, GxEPD_BLACK);
  display.fillCircle(x + 5, y + 10, 5, GxEPD_BLACK);
}

/* ================= Info card ================= */
void drawInfoCard(int cx, int cy, int cw, int ch,
                  const char* label, const char* value, int icon) {
  display.drawRoundRect(cx, cy, cw, ch, 6, GxEPD_BLACK);

  /* label chip (top-left) */
  display.setFont(&FreeSansBold9pt7b);
  int16_t x1, y1; uint16_t tw, th;
  display.getTextBounds(label, 0, 0, &x1, &y1, &tw, &th);
  int chipW = tw + 12;
  display.fillRoundRect(cx + 6, cy + 7, chipW, 15, 3, GxEPD_BLACK);
  display.setTextColor(GxEPD_WHITE);
  display.setCursor(cx + 12, cy + 19);
  display.print(label);

  /* icon (top-right) */
  if (icon == 0) drawThermometerIcon(cx + cw - 19, cy + 8);
  else           drawDropletIcon(cx + cw - 21, cy + 8);

  /* value (bottom, centered) */
  display.setFont(&FreeSansBold12pt7b);
  display.setTextColor(GxEPD_BLACK);
  printCentered(cx + cw / 2, cy + ch - 6, value);
}

/* ================= Full frame ================= */
void epdDraw() {
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.epd2.selectSPI(SPI, SPISettings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0));

  display.init(115200);
  display.setRotation(0);
  display.setFullWindow();

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    /* weekday badge */
    display.fillRoundRect(BADGE_X, BADGE_Y, BADGE_W, BADGE_H, 4, GxEPD_BLACK);
    display.setFont(&FreeSansBold9pt7b);
    display.setTextColor(GxEPD_WHITE);
    printCentered(BADGE_X + BADGE_W / 2, BADGE_Y + 17, dayNames[rtc.getWeekday()]);

    /* battery */
    drawBatteryIcon(BAT_X, BAT_Y, BAT_W, BAT_H, voltageSegments);

    /* clock */
    display.setTextColor(GxEPD_BLACK);
    drawClock((200 - CLK_TOTAL_W) / 2, CLK_Y);

    /* divider */
    display.fillRect(DIV_X1, DIV_Y, DIV_X2 - DIV_X1, 2, GxEPD_BLACK);

    /* date (year is fixed, comes from DISPLAY_YEAR) */
    display.setFont(&FreeSansBold12pt7b);
    printCentered(100, DATE_BASELINE, dateStr);

    /* info cards */
    drawInfoCard(CARD1_X, CARD_Y, CARD_W, CARD_H, "TEMP", tempStr, 0);
    drawInfoCard(CARD2_X, CARD_Y, CARD_W, CARD_H, "HUM",  humStr,  1);

  } while (display.nextPage());

  display.hibernate();
}

/* ================= Battery voltage ================= */
/* 8 averaged ADC samples through a 200k/200k divider (ratio 2.0),
 * result clamped to BATTERY_MIN_MV..BATTERY_MAX_MV so that the
 * segments can never overflow the battery outline. */
int readBatteryVoltage() {
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);

  uint32_t sum = 0;
  for (int i = 0; i < BATTERY_SAMPLES; i++) {
    sum += analogReadMilliVolts(BATTERY_ADC_PIN);
    delay(2);
  }
  float mv = (sum / (float)BATTERY_SAMPLES) * BATTERY_DIVIDER_RATIO;
  return constrain((int)mv, BATTERY_MIN_MV, BATTERY_MAX_MV);
}

/* ================= Main cycle ================= */
void setup() {
  Serial.begin(115200);

  /* release GPIO holds from the previous deep sleep */
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)VBAT_PWR);
  gpio_hold_dis((gpio_num_t)EPD_PWR);

  pinMode(VBAT_PWR, OUTPUT);
  digitalWrite(VBAT_PWR, HIGH);      /* power for RTC + sensor */

  pinMode(EPD_PWR, OUTPUT);
  digitalWrite(EPD_PWR, LOW);        /* display power on (active low) */

  pinMode(3, OUTPUT);
  digitalWrite(3, HIGH);

  delay(10);

  Wire.begin(I2C_SDA, I2C_SCL);
  rtc.begin();

  /* NTP sync once per NTP_SYNC_MINUTES, derived from sleep interval */
  const int syncEvery = (NTP_SYNC_MINUTES * 60) / SLEEP_SECONDS;
  if (bootCount % syncEvery == 0) {
    syncNTP();
  }
  bootCount++;

  int h = rtc.getHour();
  int m = rtc.getMinute();
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", h, m);

  /* Year is FIXED from config: PCF85063A stores only yy */
  snprintf(dateStr, sizeof(dateStr), "%02d %s %04d",
           rtc.getDay(),
           monthNames[(rtc.getMonth() - 1) % 12],
           DISPLAY_YEAR);

  Serial.printf("RTC: %02d:%02d %02d %s | year fixed at %d\n",
                h, m, rtc.getDay(),
                monthNames[(rtc.getMonth() - 1) % 12], DISPLAY_YEAR);

  /* SHTC3: one retry, then visible "--" */
  shtc3.begin();
  sensors_event_t hum, temp;
  bool ok = shtc3.getEvent(&hum, &temp);
  if (!ok) { delay(5); ok = shtc3.getEvent(&hum, &temp); }
  if (ok) {
    dtostrf(temp.temperature, 1, 1, tempStr);
    dtostrf(hum.relative_humidity, 1, 0, humStr);
  } else {
    strcpy(tempStr, "--");
    strcpy(humStr, "--");
  }

  voltageSegments = map(readBatteryVoltage(),
                        BATTERY_MIN_MV, BATTERY_MAX_MV, 0, BATTERY_SEGMENTS);
  voltageSegments = constrain(voltageSegments, 0, BATTERY_SEGMENTS);

  epdDraw();

#if EPD_KEEP_POWERED_DURING_SLEEP
  digitalWrite(EPD_PWR, HIGH);       /* board-specific: keep display rail on */
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

void loop() {}   /* never reached: setup() ends in deep sleep */
