# TimeKeeper

TimeKeeper is a desk clock built on an ESP32-S3 and a 128x64 OLED. It syncs
time over Wi-Fi, shows a live "percentage of the day completed" readout, and
cycles between a clock, an exam countdown, and a local weather screen with a
single tap on an attached accelerometer.

<p align="center">
  <code>WED 12 AUG   WiFi</code><br>
  <code>DAY COMPLETE</code><br>
  <code>42.123%</code><br>
  <code>██████████░░░░░░░░░░░░</code><br>
  <code>10:06:34 AM</code>
</p>

## Features

- **Live day-progress clock** — 12-hour time with seconds, plus the percent
  of the day elapsed to three decimal places and a progress bar.
- **Exam countdown** — automatically advances through a configured exam
  schedule, switching from a days/hours/minutes countdown to hours/minutes/seconds
  inside the final 24 hours.
- **Local weather** — current conditions plus daily low/high and rain chance,
  from [Open-Meteo](https://open-meteo.com/en/docs).
- **Tap-to-cycle screens** — a single tap on the attached accelerometer
  switches between the clock, exam, and weather screens.
- **Quiet-hours display sleep** — the OLED dims and eventually powers off
  overnight when there's no vibration nearby, and wakes instantly on the
  next tap or nearby activity.
- **Resilient networking** — keeps time locally through Wi-Fi drops,
  auto-reconnects, and shows clear on-screen status (`SYNCING TIME`,
  `NO WI-FI`, `----`, `STALE`) instead of failing silently.

## Hardware

| Part | Notes |
| --- | --- |
| ESP32-S3 DevKitC-1 (N16R8) | 16 MB flash, 8 MB PSRAM |
| [Jaycar XC3728](https://www.jaycar.com.au/duinotech-arduino-compatible-1-3-inch-monochrome-oled-display/p/XC3728) 1.3" 128x64 OLED | SH1106 controller, 4-wire SPI |
| [Jaycar XC3732](https://www.jaycar.com.au/arduino-compatible-tri-axis-digital-tilt-sensor/p/XC3732) tri-axis tilt sensor | MMA8452Q, I²C |
| Female-to-female jumper wires | Or wires appropriate for your headers |
| USB cable | Power and programming |

The XC3728's labels are printed beside its seven-pin header.

## Wiring

> **Disconnect USB power before making or changing connections.**

### OLED (XC3728)

| XC3728 label | ESP32-S3 connection | Purpose |
| --- | --- | --- |
| `GND` | `GND` | Common ground |
| `VCC` | `3V3` | Display power |
| `CLK` | `GPIO 12` | SPI clock |
| `MOS` | `GPIO 11` | SPI data (MOSI) |
| `RES` | `GPIO 8` | Display reset |
| `DC` | `GPIO 9` | Data/command select |
| `CS` | `GPIO 10` | Chip select |

Use `3V3`, not `5V`, so the display power and ESP32-S3 logic levels are
safely matched. `MOS` on this display means MOSI; no MISO wire is required.

### Tap sensor (XC3732)

The XC3732 header is labelled `INT1`, `INT2`, `SCL`, `SDA`, `+`, `-` from top
to bottom when viewed as shown in Jaycar's product photo.

| XC3732 label | ESP32-S3 connection | Purpose |
| --- | --- | --- |
| `+` | `3V3` | Sensor power |
| `-` | `GND` | Common ground |
| `SDA` | `GPIO 4` | I²C data |
| `SCL` | `GPIO 5` | I²C clock |
| `INT1` | `GPIO 6` | Single-tap interrupt |
| `INT2` | Not connected | Unused interrupt output |

The sensor is an MMA8452Q; its I²C address is detected automatically at
`0x1D` or `0x1C`. It's configured for single-pulse detection on all three
axes, and is also sampled continuously for the lighter vibration used by the
quiet-hours display sleep feature. The module operates from 1.6–3.6 V, so use
the ESP32-S3 `3V3` pin. Its I²C bus should have pull-ups to 3V3; if your
board revision doesn't already provide them, add a 4.7 kΩ resistor from
`SDA` to `3V3` and another from `SCL` to `3V3`.

## Getting started

1. Install [Visual Studio Code](https://code.visualstudio.com/) and the
   [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode).
2. Open this folder in VS Code.
3. Copy `include/secrets.example.h` to `include/secrets.h` and add your
   2.4 GHz Wi-Fi credentials:

   ```cpp
   #define WIFI_SSID "My Wi-Fi"
   #define WIFI_PASSWORD "my-password"
   ```

   `include/secrets.h` is ignored by Git, so credentials are never committed.
4. Connect the ESP32-S3 by USB, then use PlatformIO's **Upload** action.
5. If upload doesn't start, hold **BOOT**, tap **RESET**, release **BOOT**,
   and upload again.

The equivalent terminal commands:

```sh
pio run                    # build
pio run --target upload    # flash
pio device monitor         # serial monitor, 115200 baud
```

On startup, the display reports Wi-Fi and time-sync status. After a
successful sync it keeps time locally through temporary Wi-Fi drops and
retries reconnecting every 30 seconds.

### Running the unit tests

The day-progress calculation has native (no-hardware) unit tests:

```sh
pio test -e native
```

## Usage

Tap the connected XC3732 once to advance to the next screen. The firmware
uses `INT1` for an immediate notification and also polls the latched pulse
status over I²C, so a tap is still detected even if the interrupt wire is
missing.

### Clock

```text
WED 12 AUG                 WiFi
           DAY COMPLETE
              42.123%
   ██████████░░░░░░░░░░░░
          10:06:34 AM
```

The percentage includes fractions of a second, so the three-decimal value
updates smoothly. The progress bar gives a quick visual sense of the day
completed, and the top-right status changes to `----` whenever Wi-Fi is
disconnected.

### Exam countdown

```text
NEXT EXAM                  WiFi
              MATH1051
        37d 14h 22m
    SAT 19 SEP 08:00 AM
```

The countdown is the largest element on screen, followed by the exam date
and course name. It shows days/hours/minutes until the final 24 hours, then
switches to hours/minutes/seconds. Once an exam starts, this screen
automatically advances to the next one in the schedule.

### Weather

```text
WEATHER                   WiFi
             24°C
        PARTLY CLOUDY
        LOW 18  HIGH 27
            RAIN 35%
```

Shows current temperature and conditions alongside the day's forecast
minimum, maximum, and maximum precipitation probability, refreshed every
five minutes. A failed refresh keeps the last good reading and shows `STALE`
once it's more than 30 minutes old; before the first successful reading,
request errors are shown instead of leaving the screen on `LOADING...`.
Failed requests retry every 30 seconds.

## Configuration

Everything below is a constant near the top of `src/main.cpp`.

| Setting | Constant | Notes |
| --- | --- | --- |
| Timezone | `kTimezone` | POSIX timezone syntax, not an IANA city name. Locations with daylight saving need a full POSIX rule, not just a fixed UTC offset. Defaults to `AEST-10` (Brisbane, UTC+10, no DST). |
| Exam schedule | `kExams` | Array of `{course, year, month, day, hour, minute}` entries in local time. Keep them in chronological order — the countdown logic returns the first future entry, it doesn't sort. |
| Weather location | `WEATHER_API_URL` in `include/secrets.h` | An `http://api.open-meteo.com/v1/forecast?...` URL containing `current=temperature_2m,weather_code` and the daily variables `temperature_2m_max,temperature_2m_min,precipitation_probability_max`. Kept out of tracked source since it encodes your location. |
| Quiet hours | `kSleepStartHour`, `kSleepEndHour` | Default 10:00 PM–5:00 AM local time. |
| Display dimming | `kDisplayDimDelayMs`, `kDisplayDimRampMs`, `kDisplaySleepDelayMs`, `kNightDimContrast` | During quiet hours the display stays at full brightness until the dim delay, fades to the night contrast level over the ramp duration, then fully powers off at the sleep delay — all measured from the last detected vibration. |
| Vibration sensitivity | `kVibrationThresholdCounts` | Increase if background vibration prevents dimming/sleep; decrease if nearby activity doesn't reliably wake the display. |
| Screen orientation | `U8G2_R2` in the `oled` constructor | Set for an upside-down enclosure mount. Use `U8G2_R0` for an upright mounting. |

Quiet-hours dimming in detail: the OLED stays at full brightness for five
minutes after the last vibration, fades over the next two minutes to a faint
night level, and powers off once fifteen minutes have passed without
vibration. Any vibration — typing nearby, moving the desk, tapping the
sensor — immediately restores full brightness and restarts the timers. Only
the display is affected: the ESP32 keeps time, refreshes weather, maintains
Wi-Fi, and monitors the sensor throughout. At the configured wake hour the
OLED returns to full brightness automatically.

Any configuration change requires re-flashing the board
(`pio run --target upload`).

## Project structure

```text
include/secrets.example.h   Template for Wi-Fi/weather credentials
include/secrets.h           Your credentials (git-ignored)
lib/day_progress/           Day-progress percentage calculation
src/main.cpp                Firmware: display, sensors, networking
test/test_day_progress/     Native unit tests (pio test -e native)
platformio.ini              Build configuration
```

## Troubleshooting

- **Screen stays blank:** Recheck all seven wires, especially `GND`, `VCC`,
  `RES`, and `CS`. Confirm the OLED is the XC3728/SH1106 model.
- **Screen orientation is wrong:** The firmware uses `U8G2_R2` because the
  OLED is mounted upside down in its housing. Use `U8G2_R0` for an upright
  mounting.
- **It says `SET UP WI-FI`:** Create `include/secrets.h` from the example
  file.
- **It says `NO WI-FI`:** The ESP32-S3 supports 2.4 GHz Wi-Fi, not a
  5 GHz-only network. Check the credentials and signal.
- **It stays on `SYNCING TIME`:** The network may be blocking NTP (UDP port
  123) or may not have internet access. Serial output has more detail.
- **Tapping does nothing:** The serial monitor should report
  `MMA8452Q ready` and the detected I²C address. If it says `not detected`,
  check `+`, `-`, `SDA`, and `SCL` first, including any required 4.7 kΩ
  pull-ups. If it is ready, tap the sensor board firmly once and make sure
  it's powered from `3V3`; `INT1` is recommended but the firmware also has
  an I²C polling fallback.

## References

- Jaycar's [XC3728 sample manual](https://media.jaycar.com.au/product/resources/XC3728_manualMain_92743.pdf)
  identifies the controller as SH1106.
- Espressif's [ESP32-S3 DevKitC-1 pin layout](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html).
