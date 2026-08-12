#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <ctype.h>
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
constexpr uint8_t kSensorSdaPin = 4;
constexpr uint8_t kSensorSclPin = 5;
constexpr uint8_t kSensorInterruptPin = 6;

constexpr char kTimezone[] = "AEST-10";  // Australia/Brisbane (no DST)
constexpr unsigned long kDisplayIntervalMs = 100;
constexpr unsigned long kReconnectIntervalMs = 30000;
constexpr unsigned long kTapPollIntervalMs = 20;
constexpr unsigned long kWeatherRefreshIntervalMs = 5UL * 60UL * 1000UL;
constexpr unsigned long kWeatherRetryIntervalMs = 30UL * 1000UL;
constexpr unsigned long kWeatherStaleIntervalMs = 30UL * 60UL * 1000UL;
constexpr time_t kMinimumValidTime = 1704067200;  // 2024-01-01 UTC
constexpr char kWeatherUrl[] =
    "https://api.open-meteo.com/v1/forecast?latitude=-26.6553&longitude="
    "153.0933&current=temperature_2m,weather_code&daily=temperature_2m_max,"
    "temperature_2m_min,precipitation_probability_max&timezone="
    "Australia%2FBrisbane&forecast_days=1";

enum class Screen : uint8_t {
  Clock,
  Exam,
  Weather,
  Count,
};

struct Exam {
  const char* course;
  int year;
  int month;
  int day;
  int hour;
  int minute;
};

struct WeatherData {
  bool available = false;
  float temperature = 0;
  float minimumTemperature = 0;
  float maximumTemperature = 0;
  int weatherCode = 0;
  int rainChance = 0;
  unsigned long updatedAt = 0;
};

// Brisbane local time. Keep the exams ordered from earliest to latest.
constexpr Exam kExams[] = {
    {"MATH1051", 2026, 9, 19, 8, 0},
    {"ENGG1300", 2026, 9, 19, 14, 0},
};

U8G2_SH1106_128X64_NONAME_F_4W_SW_SPI oled(
    U8G2_R0, kOledClockPin, kOledDataPin, kOledCsPin, kOledDcPin,
    kOledResetPin);

bool clockIsValid = false;
bool clockSyncStarted = false;
Screen currentScreen = Screen::Clock;
WeatherData weather;
bool weatherRequestAttempted = false;
bool lastWeatherRequestSucceeded = false;
bool tapSensorReady = false;
uint8_t tapSensorAddress = 0;
volatile bool tapInterruptPending = false;
unsigned long lastDisplayUpdate = 0;
unsigned long lastReconnectAttempt = 0;
unsigned long lastTapPoll = 0;
unsigned long lastTapHandled = 0;
unsigned long lastWeatherAttempt = 0;

constexpr uint8_t kMmaWhoAmI = 0x0D;
constexpr uint8_t kMmaPulseCfg = 0x21;
constexpr uint8_t kMmaPulseSrc = 0x22;
constexpr uint8_t kMmaPulseThsx = 0x23;
constexpr uint8_t kMmaPulseThsy = 0x24;
constexpr uint8_t kMmaPulseThsz = 0x25;
constexpr uint8_t kMmaPulseTmLt = 0x26;
constexpr uint8_t kMmaPulseLtcy = 0x27;
constexpr uint8_t kMmaPulseWind = 0x28;
constexpr uint8_t kMmaCtrlReg1 = 0x2A;
constexpr uint8_t kMmaCtrlReg3 = 0x2C;
constexpr uint8_t kMmaCtrlReg4 = 0x2D;
constexpr uint8_t kMmaCtrlReg5 = 0x2E;

void drawCentered(const char* text, int baseline) {
  const int width = oled.getStrWidth(text);
  oled.drawStr((128 - width) / 2, baseline, text);
}

void makeUppercase(char* text) {
  for (char* character = text; *character != '\0'; ++character) {
    *character = static_cast<char>(toupper(*character));
  }
}

void IRAM_ATTR onTapInterrupt() {
  tapInterruptPending = true;
}

bool readSensorRegister(uint8_t address, uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 ||
      Wire.requestFrom(static_cast<uint8_t>(address),
                       static_cast<uint8_t>(1)) != 1) {
    return false;
  }
  value = Wire.read();
  return true;
}

bool writeSensorRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(tapSensorAddress);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool beginTapSensor() {
  Wire.begin(kSensorSdaPin, kSensorSclPin, 100000);

  for (const uint8_t address : {static_cast<uint8_t>(0x1D),
                                static_cast<uint8_t>(0x1C)}) {
    uint8_t identity = 0;
    if (readSensorRegister(address, kMmaWhoAmI, identity) && identity == 0x2A) {
      tapSensorAddress = address;
      break;
    }
  }

  if (tapSensorAddress == 0) {
    Serial.printf("MMA8452Q not found on I2C SDA GPIO%u / SCL GPIO%u\n",
                  kSensorSdaPin, kSensorSclPin);
    return false;
  }

  // Configure the MMA8452Q pulse engine while in standby, then enable it.
  if (!writeSensorRegister(kMmaCtrlReg1, 0x00) ||
      // ELE latches the event until PULSE_SRC is read. The lower bits enable
      // single-pulse detection on X, Y, and Z.
      !writeSensorRegister(kMmaPulseCfg, 0x55) ||
      !writeSensorRegister(kMmaPulseThsx, 0x18) ||
      !writeSensorRegister(kMmaPulseThsy, 0x18) ||
      !writeSensorRegister(kMmaPulseThsz, 0x18) ||
      !writeSensorRegister(kMmaPulseTmLt, 0x10) ||
      !writeSensorRegister(kMmaPulseLtcy, 0x20) ||
      !writeSensorRegister(kMmaPulseWind, 0x10) ||
      !writeSensorRegister(kMmaCtrlReg3, 0x02) ||
      !writeSensorRegister(kMmaCtrlReg4, 0x08) ||
      !writeSensorRegister(kMmaCtrlReg5, 0x08) ||
      // CTRL_REG1 bit 0 is ACTIVE. 0x18 selects 100 Hz but leaves the IMU in
      // standby; 0x19 selects 100 Hz and actually starts measurements.
      !writeSensorRegister(kMmaCtrlReg1, 0x19)) {
    Serial.println("MMA8452Q configuration failed.");
    tapSensorAddress = 0;
    return false;
  }

  uint8_t ignored = 0;
  readSensorRegister(tapSensorAddress, kMmaPulseSrc, ignored);
  pinMode(kSensorInterruptPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(kSensorInterruptPin), onTapInterrupt,
                  RISING);
  Serial.printf("MMA8452Q detected at I2C address 0x%02X\n", tapSensorAddress);
  return true;
}

void serviceTapSensor() {
  if (!tapSensorReady) {
    return;
  }

  const unsigned long now = millis();
  bool pending = false;
  noInterrupts();
  pending = tapInterruptPending;
  tapInterruptPending = false;
  interrupts();

  // INT1 gives an immediate notification, while polling PULSE_SRC keeps the
  // sensor usable if INT1 is miswired or omitted. PULSE_SRC is latched by ELE
  // in PULSE_CFG and is cleared by this read.
  if (!pending && now - lastTapPoll < kTapPollIntervalMs) {
    return;
  }
  lastTapPoll = now;

  uint8_t pulseSource = 0;
  if (readSensorRegister(tapSensorAddress, kMmaPulseSrc, pulseSource) &&
      (pulseSource & 0x80) != 0 && now - lastTapHandled >= 250) {
    const uint8_t nextScreen =
        (static_cast<uint8_t>(currentScreen) + 1) %
        static_cast<uint8_t>(Screen::Count);
    currentScreen = static_cast<Screen>(nextScreen);
    lastTapHandled = now;
    lastDisplayUpdate = 0;
  }
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

  char dateText[11];
  strftime(dateText, sizeof(dateText), "%a %d %b", &localTime);
  makeUppercase(dateText);

  char timeText[12];
  strftime(timeText, sizeof(timeText), "%I:%M:%S %p", &localTime);
  const char* displayedTime = timeText[0] == '0' ? timeText + 1 : timeText;

  const double progress = dayProgressPercent(
      localTime.tm_hour, localTime.tm_min, localTime.tm_sec, now.tv_usec);
  char progressText[12];
  snprintf(progressText, sizeof(progressText), "%.3f%%", progress);
  const uint8_t progressBarWidth =
      static_cast<uint8_t>(progress * 1.2 + 0.5);
  const char* wifiText = WiFi.status() == WL_CONNECTED ? "WiFi" : "----";

  oled.clearBuffer();
  oled.setFont(u8g2_font_5x8_tf);
  oled.drawStr(0, 7, dateText);
  oled.drawStr(128 - oled.getStrWidth(wifiText), 7, wifiText);
  drawCentered("DAY COMPLETE", 15);
  // Use the full font so the trailing percent sign is rendered; the numeric
  // variant silently omits that glyph.
  oled.setFont(u8g2_font_logisoso20_tf);
  if (oled.getStrWidth(progressText) > 124) {
    oled.setFont(u8g2_font_logisoso18_tf);
  }
  drawCentered(progressText, 37);
  oled.drawFrame(3, 40, 122, 7);
  if (progressBarWidth > 0) {
    oled.drawBox(4, 41, progressBarWidth, 5);
  }
  oled.setFont(u8g2_font_helvB10_tf);
  drawCentered(displayedTime, 62);
  oled.sendBuffer();
}

time_t examTimestamp(const Exam& exam) {
  tm examTime{};
  examTime.tm_year = exam.year - 1900;
  examTime.tm_mon = exam.month - 1;
  examTime.tm_mday = exam.day;
  examTime.tm_hour = exam.hour;
  examTime.tm_min = exam.minute;
  examTime.tm_isdst = -1;
  return mktime(&examTime);
}

const Exam* findNextExam(time_t now, time_t& startsAt) {
  for (const Exam& exam : kExams) {
    const time_t candidate = examTimestamp(exam);
    if (candidate > now) {
      startsAt = candidate;
      return &exam;
    }
  }
  return nullptr;
}

void drawExamCountdown() {
  time_t now;
  time(&now);

  time_t startsAt = 0;
  const Exam* exam = findNextExam(now, startsAt);
  if (exam == nullptr) {
    drawStatus("NO UPCOMING", "EXAMS SET");
    return;
  }

  long remaining = static_cast<long>(difftime(startsAt, now));
  const long days = remaining / 86400;
  remaining %= 86400;
  const long hours = remaining / 3600;
  remaining %= 3600;
  const long minutes = remaining / 60;
  const long seconds = remaining % 60;

  char countdownText[20];
  if (days > 0) {
    snprintf(countdownText, sizeof(countdownText), "%ldd %02ldh %02ldm", days,
             hours, minutes);
  } else {
    snprintf(countdownText, sizeof(countdownText), "%02ld:%02ld:%02ld", hours,
             minutes, seconds);
  }

  tm localExamTime{};
  localtime_r(&startsAt, &localExamTime);
  char examDateText[22];
  strftime(examDateText, sizeof(examDateText), "%a %d %b %I:%M %p",
           &localExamTime);
  makeUppercase(examDateText);

  const char* wifiText = WiFi.status() == WL_CONNECTED ? "WiFi" : "----";
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x8_tf);
  oled.drawStr(0, 7, "NEXT EXAM");
  oled.drawStr(128 - oled.getStrWidth(wifiText), 7, wifiText);
  // Visual priority: countdown, exam date, then course name.
  oled.setFont(u8g2_font_helvB08_tf);
  drawCentered(exam->course, 17);

  if (days > 0) {
    oled.setFont(u8g2_font_helvB18_tf);
    if (oled.getStrWidth(countdownText) > 122) {
      oled.setFont(u8g2_font_helvB14_tf);
    }
    if (oled.getStrWidth(countdownText) > 122) {
      oled.setFont(u8g2_font_helvB12_tf);
    }
  } else {
    oled.setFont(u8g2_font_logisoso26_tn);
    if (oled.getStrWidth(countdownText) > 122) {
      oled.setFont(u8g2_font_logisoso24_tn);
    }
    if (oled.getStrWidth(countdownText) > 122) {
      oled.setFont(u8g2_font_logisoso22_tn);
    }
  }
  drawCentered(countdownText, 43);

  oled.setFont(u8g2_font_6x12_tf);
  drawCentered(examDateText, 63);
  oled.sendBuffer();
}

const char* weatherDescription(int code) {
  if (code == 0) return "CLEAR";
  if (code == 1) return "MOSTLY CLEAR";
  if (code == 2) return "PARTLY CLOUDY";
  if (code == 3) return "OVERCAST";
  if (code == 45 || code == 48) return "FOG";
  if (code >= 51 && code <= 55) return "DRIZZLE";
  if (code == 56 || code == 57) return "FRZ DRIZZLE";
  if (code == 61 || code == 63) return "RAIN";
  if (code == 65) return "HEAVY RAIN";
  if (code == 66 || code == 67) return "FRZ RAIN";
  if (code >= 71 && code <= 75) return "SNOW";
  if (code == 77) return "SNOW GRAINS";
  if (code >= 80 && code <= 82) return "SHOWERS";
  if (code == 85 || code == 86) return "SNOW SHOWERS";
  if (code == 95) return "THUNDERSTORM";
  if (code == 96 || code == 99) return "STORM + HAIL";
  return "UNKNOWN";
}

bool fetchWeather() {
  WiFiClientSecure client;
  // The request carries no credentials or private data. Avoid pinning a CA
  // certificate that would eventually expire and stop weather updates.
  client.setInsecure();

  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  if (!http.begin(client, kWeatherUrl)) {
    Serial.println("Weather request could not be started.");
    return false;
  }

  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    Serial.printf("Weather request failed with HTTP status %d.\n", status);
    http.end();
    return false;
  }

  JsonDocument document;
  const DeserializationError error = deserializeJson(document, http.getStream());
  if (error) {
    Serial.printf("Weather response could not be parsed: %s\n", error.c_str());
    http.end();
    return false;
  }

  const JsonVariant current = document["current"];
  const JsonVariant daily = document["daily"];
  if (current["temperature_2m"].isNull() || current["weather_code"].isNull() ||
      daily["temperature_2m_min"][0].isNull() ||
      daily["temperature_2m_max"][0].isNull() ||
      daily["precipitation_probability_max"][0].isNull()) {
    Serial.println("Weather response is missing required values.");
    http.end();
    return false;
  }

  weather.temperature = current["temperature_2m"].as<float>();
  weather.weatherCode = current["weather_code"].as<int>();
  weather.minimumTemperature = daily["temperature_2m_min"][0].as<float>();
  weather.maximumTemperature = daily["temperature_2m_max"][0].as<float>();
  weather.rainChance = daily["precipitation_probability_max"][0].as<int>();
  weather.updatedAt = millis();
  weather.available = true;
  http.end();

  Serial.printf("Maroochydore weather updated: %.1f C, code %d, rain %d%%\n",
                weather.temperature, weather.weatherCode, weather.rainChance);
  return true;
}

void serviceWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  const unsigned long now = millis();
  const unsigned long interval =
      lastWeatherRequestSucceeded ? kWeatherRefreshIntervalMs
                                  : kWeatherRetryIntervalMs;
  if (weatherRequestAttempted && now - lastWeatherAttempt < interval) {
    return;
  }

  weatherRequestAttempted = true;
  lastWeatherAttempt = now;
  lastWeatherRequestSucceeded = fetchWeather();
}

void drawWeather() {
  if (!weather.available) {
    drawStatus("WEATHER", WiFi.status() == WL_CONNECTED ? "LOADING..."
                                                     : "NO CONNECTION");
    return;
  }

  const bool stale = millis() - weather.updatedAt >= kWeatherStaleIntervalMs;
  const char* statusText = stale ? "STALE"
                                 : (WiFi.status() == WL_CONNECTED ? "WiFi"
                                                                  : "----");
  char temperatureText[10];
  snprintf(temperatureText, sizeof(temperatureText), "%.0f\xB0" "C",
           weather.temperature);
  char summaryText[28];
  snprintf(summaryText, sizeof(summaryText), "L%.0f H%.0f  RAIN %d%%",
           weather.minimumTemperature, weather.maximumTemperature,
           weather.rainChance);

  oled.clearBuffer();
  oled.setFont(u8g2_font_5x8_tf);
  oled.drawStr(0, 7, "WEATHER");
  oled.drawStr(128 - oled.getStrWidth(statusText), 7, statusText);
  drawCentered("MAROOCHYDORE", 16);

  oled.setFont(u8g2_font_helvB24_tf);
  drawCentered(temperatureText, 41);
  oled.setFont(u8g2_font_helvB08_tf);
  drawCentered(weatherDescription(weather.weatherCode), 51);
  oled.setFont(u8g2_font_5x8_tf);
  drawCentered(summaryText, 63);
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

  tapSensorReady = beginTapSensor();
  Serial.printf("Tap sensor: %s\n", tapSensorReady ? "MMA8452Q ready"
                                                      : "not detected");

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
  serviceWeather();
  // Keep the tap input responsive even while Wi-Fi/NTP is unavailable.
  serviceTapSensor();

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
    switch (currentScreen) {
      case Screen::Clock:
        drawClock();
        break;
      case Screen::Exam:
        drawExamCountdown();
        break;
      case Screen::Weather:
        drawWeather();
        break;
      case Screen::Count:
        currentScreen = Screen::Clock;
        break;
    }
  }

  delay(5);
}
