/*
 * ============================================================
 *  ESP32-S3 Pocket Weather
 *  ------------------------------------------------------------
 *  Pocket e-paper clock & weather station.
 *
 *  Target board : Waveshare ESP32-S3 e-Paper 1.54
 *  Display      : 1.54" e-paper, 200x200, SSID16810 (GxEPD2_154_D67)
 *  Sensors      : SHTC3 (temp/hum), PCF85063A (RTC)
 *  Power        : deep sleep between updates, NTP sync every
 *                 NTP_SYNC_MINUTES, USB mode via BOOT button.
 *
 *  Files        : sketch + config.h (see config.example.h)
 *  License      : MIT
 * ============================================================
 */


#include <Wire.h>
#include <SPI.h>
#include <WiFi.h>
#include <time.h>
#include <esp_sleep.h>

#include <GxEPD2_BW.h>
#include <Adafruit_SHTC3.h>
#include "PCF85063A-SOLDERED.h"

#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>

#include "config.h"

// ---------------- pins ----------------
#define EPD_DC      10
#define EPD_CS      11
#define EPD_SCK     12
#define EPD_MOSI    13
#define EPD_RST      9
#define EPD_BUSY     8
#define EPD_PWR      6
#define VBAT_PWR    17
#define I2C_SDA     47
#define I2C_SCL     48
#define BAT_ADC_PIN  4

#define EPD_PWR_ON  LOW
#define EPD_PWR_OFF HIGH

// ---------------- display ----------------
GxEPD2_BW<GxEPD2_154_D67, GxEPD2_154_D67::HEIGHT> display(
  GxEPD2_154_D67(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

Adafruit_SHTC3 shtc3;
PCF85063A rtc;

RTC_DATA_ATTR int bootCount = 0;
bool usbMode = false;
unsigned long lastDraw = 0;
unsigned long holdStart = 0;

char timeStr[6];
char dateStr[16];
char tempStr[8];
char humStr[8];
int  voltageSegments = 0;
int  dayIndex = 0;

static const char* dayNames[7]    = {"MON","TUE","WED","THU","FRI","SAT","SUN"};
static const char* monthNames[12] = {"JAN","FEB","MAR","APR","MAY","JUN",
                                     "JUL","AUG","SEP","OCT","NOV","DEC"};

// ---------------- layout (200x200 px) ----------------
static const int DAY_PLATE_X = 6,   DAY_PLATE_Y = 8,  DAY_PLATE_W = 56, DAY_PLATE_H = 22;
static const int BAT_X = 146,       BAT_Y = 10,       BAT_W = 40,       BAT_H = 16;
static const int CLK_DIGIT_W = 26,  CLK_DIGIT_H = 46, CLK_TH = 6,       CLK_GAP = 5;
static const int CLK_Y = 40;
static const int SEP_X0 = 30, SEP_X1 = 170, SEP_Y = 96;
static const int CARD_Y = 138, CARD_H = 58, CARD_W = 92, CARD_GAP = 8;
static const int CARD1_X = 4, CARD2_X = CARD1_X + CARD_W + CARD_GAP;
static const int USB_X = 45,  USB_Y = 8,  USB_W = 110, USB_H = 22;
static const int DATE_BASELINE = 122;

// ---------------- text helper ----------------
void drawCenteredText(int cx, int baseline, const char* s, const GFXfont* font, uint16_t color) {
  int16_t x1, y1; uint16_t w, h;
  display.setFont(font);
  display.getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  display.setCursor(cx - w / 2, baseline);
  display.setTextColor(color);
  display.print(s);
}

// ---------------- seven-segment digits ----------------
static const uint8_t SEG_MASK[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

void drawSevenSegDigit(int x, int y, char c) {
  if (c < '0' || c > '9') return;
  const int w = CLK_DIGIT_W, h = CLK_DIGIT_H, t = CLK_TH, half = h / 2;
  uint8_t m = SEG_MASK[c - '0'];
  if (m & 0x01) display.fillRect(x + 2, y, w - 4, t, GxEPD_BLACK);
  if (m & 0x40) display.fillRect(x + 2, y + half - t / 2, w - 4, t, GxEPD_BLACK);
  if (m & 0x08) display.fillRect(x + 2, y + h - t, w - 4, t, GxEPD_BLACK);
  if (m & 0x20) display.fillRect(x, y + 2, t, half - 1, GxEPD_BLACK);
  if (m & 0x02) display.fillRect(x + w - t, y + 2, t, half - 1, GxEPD_BLACK);
  if (m & 0x10) display.fillRect(x, y + half + 1, t, half - 1, GxEPD_BLACK);
  if (m & 0x04) display.fillRect(x + w - t, y + half + 1, t, half - 1, GxEPD_BLACK);
}

void drawClockTime() {
  int totalW = 4 * CLK_DIGIT_W + 14 + 4 * CLK_GAP;
  int x = (200 - totalW) / 2;
  for (int i = 0; i < 5; i++) {
    char c = timeStr[i];
    if (c == ':') {
      display.fillRect(x + 4, CLK_Y + 11, 6, 6, GxEPD_BLACK);
      display.fillRect(x + 4, CLK_Y + 29, 6, 6, GxEPD_BLACK);
      x += 14 + CLK_GAP;
    } else {
      drawSevenSegDigit(x, CLK_Y, c);
      x += CLK_DIGIT_W + CLK_GAP;
    }
  }
}

void drawBattery() {
  display.drawRect(BAT_X, BAT_Y, BAT_W, BAT_H, GxEPD_BLACK);
  display.drawRect(BAT_X + 2, BAT_Y + 2, BAT_W - 4, BAT_H - 4, GxEPD_BLACK);
  display.fillRect(BAT_X + BAT_W, BAT_Y + 4, 4, BAT_H - 8, GxEPD_BLACK);
  int n = voltageSegments;
  if (n < 0) n = 0;
  if (n > 5) n = 5;
  for (int i = 0; i < n; i++)
    display.fillRect(BAT_X + 4 + i * 7, BAT_Y + 4, 4, BAT_H - 8, GxEPD_BLACK);
}

void drawDayPlate() {
  display.fillRoundRect(DAY_PLATE_X, DAY_PLATE_Y, DAY_PLATE_W, DAY_PLATE_H, 4, GxEPD_BLACK);
  drawCenteredText(DAY_PLATE_X + DAY_PLATE_W / 2, DAY_PLATE_Y + DAY_PLATE_H / 2 + 6,
                   dayNames[dayIndex], &FreeSansBold9pt7b, GxEPD_WHITE);
}

void drawUsbBanner() {
  display.fillRoundRect(USB_X, USB_Y, USB_W, USB_H, 4, GxEPD_BLACK);
  // Reduced font from 12pt to 9pt to fit inside the frame
  drawCenteredText(100, USB_Y + USB_H / 2 + 6, "USB MODE", &FreeSansBold9pt7b, GxEPD_WHITE);
}

void drawThermoIcon(int x, int y) {
  display.fillRect(x + 4, y, 4, 9, GxEPD_BLACK);
  display.fillCircle(x + 6, y + 11, 3, GxEPD_BLACK);
  display.fillRect(x + 5, y + 2, 2, 7, GxEPD_WHITE);
  display.fillCircle(x + 6, y + 11, 1, GxEPD_WHITE);
}

void drawDropletIcon(int x, int y) {
  display.fillCircle(x + 6, y + 8, 4, GxEPD_BLACK);
  display.fillTriangle(x + 3, y + 6, x + 9, y + 6, x + 6, y, GxEPD_BLACK);
}

void drawCard(int cx, const char* label, const char* value, bool thermo) {
  display.drawRoundRect(cx, CARD_Y, CARD_W, CARD_H, 5, GxEPD_BLACK);
  display.drawRoundRect(cx + 3, CARD_Y + 3, CARD_W - 6, CARD_H - 6, 4, GxEPD_BLACK);
  display.setFont(&FreeSansBold9pt7b);
  int16_t x1, y1; uint16_t w, h;
  display.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
  int unitW = 12 + 4 + (int)w;
  int unitX = cx + (CARD_W - unitW) / 2;
  if (thermo) drawThermoIcon(unitX, CARD_Y + 10);
  else        drawDropletIcon(unitX, CARD_Y + 10);
  display.setTextColor(GxEPD_BLACK);
  display.setCursor(unitX + 16, CARD_Y + 22);
  display.print(label);
  drawCenteredText(cx + CARD_W / 2, CARD_Y + CARD_H - 12, value, &FreeSansBold12pt7b, GxEPD_BLACK);
}

void epdDraw() {
  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.epd2.selectSPI(SPI, SPISettings(SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0));
  display.init();
  display.setRotation(0);
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    if (usbMode) drawUsbBanner();
    else {
      drawDayPlate();
      drawBattery();
    }
    drawClockTime();
    display.fillRect(SEP_X0, SEP_Y, SEP_X1 - SEP_X0, 2, GxEPD_BLACK);
    drawCenteredText(100, DATE_BASELINE, dateStr, &FreeSansBold12pt7b, GxEPD_BLACK);
    drawCard(CARD1_X, "TEMP", tempStr, true);
    drawCard(CARD2_X, "HUM",  humStr,  false);
  } while (display.nextPage());
}

uint8_t toBCD(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

void rtcWriteReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(0x51);
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

static uint32_t ntpSyncEveryWakes() {
  uint32_t wakes = (uint32_t)NTP_SYNC_MINUTES * 60UL / UPDATE_INTERVAL_SECONDS;
  return (wakes == 0) ? 1 : wakes;
}

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
    rtcWriteDateTime(ti.tm_sec, ti.tm_min, ti.tm_hour,
                     ti.tm_mday, ti.tm_wday, ti.tm_mon + 1, ti.tm_year + 1900);
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void readSensor() {
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
}

int readBatteryVoltage() {
  analogReadResolution(12);
  analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
  long sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(BAT_ADC_PIN);
  int mv = (int)((sum / 8.0f) * ((200000.0f + 200000.0f) / 200000.0f));
  if (mv < 3100) mv = 3100;
  if (mv > 4200) mv = 4200;
  return mv;
}

void refreshData() {
  int h = rtc.getHour();
  int m = rtc.getMinute();
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", h, m);
  dayIndex = (rtc.getWeekday() + 6) % 7;
  snprintf(dateStr, sizeof(dateStr), "%02d %s %d",
           rtc.getDay(), monthNames[(rtc.getMonth() - 1) & 0x0F], (int)DISPLAY_YEAR);
  readSensor();
  voltageSegments = map(readBatteryVoltage(), 3100, 4200, 0, 5);
}

void goDeepSleep() {
  display.hibernate();
#if EPD_KEEP_POWERED_DURING_SLEEP
  digitalWrite(EPD_PWR, EPD_PWR_ON);
#else
  digitalWrite(EPD_PWR, EPD_PWR_OFF);
#endif
  gpio_hold_en((gpio_num_t)VBAT_PWR);
  gpio_hold_en((gpio_num_t)EPD_PWR);
  gpio_deep_sleep_hold_en();
  delay(5);

  uint64_t sleepTimeUs = (uint64_t)(UPDATE_INTERVAL_SECONDS - ACTIVE_WINDOW_SECONDS) * 1000000ULL;
  if (sleepTimeUs < 1000000) sleepTimeUs = 1000000; 

  esp_sleep_enable_timer_wakeup(sleepTimeUs);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_POWER, 0);
  
  esp_deep_sleep_start();
}

void exitUsbMode() {
  usbMode = false;
  refreshData();
  epdDraw();
  goDeepSleep();
}

void setup() {
  Serial.begin(115200);

  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)VBAT_PWR);
  gpio_hold_dis((gpio_num_t)EPD_PWR);

  pinMode(VBAT_PWR, OUTPUT);
  digitalWrite(VBAT_PWR, HIGH);

  pinMode(EPD_PWR, OUTPUT);
  digitalWrite(EPD_PWR, EPD_PWR_ON);

  pinMode(3, OUTPUT);
  digitalWrite(3, HIGH);

  delay(10);

  pinMode(BTN_POWER, INPUT_PULLUP);
  usbMode = (digitalRead(BTN_POWER) == LOW);

  Wire.begin(I2C_SDA, I2C_SCL);
  rtc.begin();
  shtc3.begin();

  if (bootCount % ntpSyncEveryWakes() == 0) {
    syncNTP();
  }
  bootCount++;

  refreshData();
  epdDraw();

  if (usbMode) {
    lastDraw = millis();
  }
}

void loop() {
  unsigned long now = millis();

  if (!usbMode) {
    if (digitalRead(BTN_POWER) == LOW) {
      if (holdStart == 0) holdStart = now;
      if (now - holdStart >= 3000) {
        usbMode = true;
        lastDraw = now;
      }
    } else {
      holdStart = 0;
    }

    if (now >= (unsigned long)ACTIVE_WINDOW_SECONDS * 1000UL) {
      goDeepSleep();
    }
  } 
  else {
    if (digitalRead(BTN_POWER) == LOW) {
      if (holdStart == 0) holdStart = now;
      if (now - holdStart >= USB_EXIT_HOLD_MS) {
        exitUsbMode();
        return;
      }
    } else {
      holdStart = 0;
    }

    if (now - lastDraw >= (unsigned long)UPDATE_INTERVAL_SECONDS * 1000UL) {
      lastDraw = now;
      if (bootCount % ntpSyncEveryWakes() == 0) syncNTP();
      bootCount++;
      refreshData();
      epdDraw();
    }
  }
  delay(20);
}
