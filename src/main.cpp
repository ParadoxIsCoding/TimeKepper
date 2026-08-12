#include <Arduino.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <sys/time.h>
#include <time.h>

#include "day_progress.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#define WIFI_SSID ""
#define WIFI_PASSWORD ""
#endif

namespace {
// XC3728 7-pin SPI header: GND, VCC, CLK, MOS, RES, DC, CS.
constexpr uint8_t kOledClockPin = 12;
constexpr uint8_t kOledDataPin = 11;
constexpr uint8_t kOledResetPin = 8;
constexpr uint8_t kOledDcPin = 9;
constexpr uint8_t kOledCsPin = 10;

constexpr char kTimezone[] = "AEST-10";  // Australia/Brisbane (no DST)
constexpr unsigned long kDisplayIntervalMs = 100;
constexpr unsigned long kReconnectIntervalMs = 30000;
constexpr time_t kMinimumValidTime = 1704067200;  // 2024-01-01 UTC

U8G2_SH1106_128X64_NONAME_F_4W_SW_SPI oled(
    U8G2_R0, kOledClockPin, kOledDataPin, kOledCsPin, kOledDcPin,
    kOledResetPin);

bool clockIsValid = false;
bool clockSyncStarted = false;
unsigned long lastDisplayUpdate = 0;
unsigned long lastReconnectAttempt = 0;

void drawCentered(const char* text, int baseline) {
  const int width = oled.getStrWidth(text);
  oled.drawStr((128 - width) / 2, baseline, text);
}

void drawStatus(const char* heading, const char* detail) {
  oled.clearBuffer();
  oled.setFont(u8g2_font_helvB10_tf);
  drawCentered(heading, 25);
  oled.setFont(u8g2_font_helvR08_tf);
  drawCentered(detail, 45);
  oled.sendBuffer();
}

bool credentialsAreConfigured() {
  return WIFI_SSID[0] != '\0' && strcmp(WIFI_SSID, "your-wifi-name") != 0;
}

bool currentTimeIsValid() {
  time_t now;
  time(&now);
  return now >= kMinimumValidTime;
}

void startClockSync() {
  configTzTime(kTimezone, "pool.ntp.org", "time.cloudflare.com",
               "time.nist.gov");
  clockSyncStarted = true;
}

void connectToWifi() {
  drawStatus("CONNECTING", WIFI_SSID);
  Serial.printf("Connecting to Wi-Fi network %s", WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < 20000) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected; IP address: %s\n", WiFi.localIP().toString().c_str());
    drawStatus("SYNCING TIME", "Please wait...");
    startClockSync();

    const unsigned long syncStartedAt = millis();
    while (!currentTimeIsValid() && millis() - syncStartedAt < 15000) {
      delay(100);
    }
    clockIsValid = currentTimeIsValid();
    Serial.println(clockIsValid ? "Clock synchronized." : "Time sync timed out.");
  } else {
    Serial.println("Wi-Fi connection timed out; retrying in the background.");
  }
}

void drawClock() {
  timeval now{};
  gettimeofday(&now, nullptr);

  tm localTime{};
  localtime_r(&now.tv_sec, &localTime);

  char timeText[9];
  strftime(timeText, sizeof(timeText), "%H:%M:%S", &localTime);

  const double progress = dayProgressPercent(
      localTime.tm_hour, localTime.tm_min, localTime.tm_sec, now.tv_usec);
  char progressText[12];
  snprintf(progressText, sizeof(progressText), "%.3f%%", progress);

  oled.clearBuffer();
  oled.setFont(u8g2_font_helvB08_tf);
  drawCentered("DAY COMPLETE", 9);
  oled.setFont(u8g2_font_helvB14_tf);
  drawCentered(progressText, 27);
  oled.drawHLine(8, 32, 112);
  oled.setFont(u8g2_font_logisoso18_tn);
  drawCentered(timeText, 59);
  oled.sendBuffer();
}

void serviceWifi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!clockSyncStarted) {
      Serial.println("Wi-Fi connected; starting time synchronization.");
      startClockSync();
    }
    if (!clockIsValid && currentTimeIsValid()) {
      clockIsValid = true;
      Serial.println("Clock synchronized.");
    }
    return;
  }

  if (millis() - lastReconnectAttempt < kReconnectIntervalMs) {
    return;
  }

  lastReconnectAttempt = millis();
  Serial.println("Wi-Fi disconnected; reconnecting...");
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  oled.begin();
  oled.setContrast(180);
  drawStatus("TIMEKEEPER", "Starting...");

  if (!credentialsAreConfigured()) {
    Serial.println("Wi-Fi credentials are missing. Copy include/secrets.example.h "
                   "to include/secrets.h and edit it.");
    drawStatus("SET UP WI-FI", "See README.md");
    return;
  }

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  connectToWifi();
}

void loop() {
  if (!credentialsAreConfigured()) {
    delay(1000);
    return;
  }

  serviceWifi();

  if (!clockIsValid) {
    if (millis() - lastDisplayUpdate >= 1000) {
      lastDisplayUpdate = millis();
      drawStatus(WiFi.status() == WL_CONNECTED ? "SYNCING TIME" : "NO WI-FI",
                 "Retrying...");
    }
    delay(10);
    return;
  }

  if (millis() - lastDisplayUpdate >= kDisplayIntervalMs) {
    lastDisplayUpdate = millis();
    drawClock();
  }

  delay(5);
}
