# TimeKeeper

An ESP32-S3 N16R8 clock for the Jaycar XC3728 128 x 64 OLED. It gets the
current time from internet time servers, shows 12-hour time with seconds and
AM/PM, and displays the percentage of the local day completed above the clock
to three decimal places. Tapping the connected XC3732 sensor switches to the
countdown for the next scheduled exam.

The firmware is configured for Brisbane time (`AEST-10`, UTC+10 with no daylight
saving).

## Hardware

- ESP32-S3 DevKitC-1 with an N16R8 module (16 MB flash, 8 MB PSRAM)
- [Jaycar XC3728 1.3-inch 128 x 64 OLED](https://www.jaycar.com.au/duinotech-arduino-compatible-1-3-inch-monochrome-oled-display/p/XC3728)
- [Jaycar XC3732 MMA8452Q tri-axis digital tilt sensor](https://www.jaycar.com.au/arduino-compatible-tri-axis-digital-tilt-sensor/p/XC3732)
- Female-to-female jumper wires (or wires appropriate for your headers)
- USB cable for power and programming

The XC3728 uses an SH1106 controller and its factory configuration is 4-wire
SPI. The labels are printed beside its seven-pin header.

## Wiring

Disconnect USB power before making or changing connections.

| XC3728 label | ESP32-S3 connection | Purpose |
| --- | --- | --- |
| `GND` | `GND` | Common ground |
| `VCC` | `3V3` | Display power |
| `CLK` | `GPIO 12` | SPI clock |
| `MOS` | `GPIO 11` | SPI data (MOSI) |
| `RES` | `GPIO 8` | Display reset |
| `DC` | `GPIO 9` | Data/command select |
| `CS` | `GPIO 10` | Chip select |

Use `3V3`, not `5V`, so the display power and ESP32-S3 logic levels are safely
matched. `MOS` on this display means MOSI; no MISO wire is required.

### XC3732 tap sensor

The XC3732 header is labelled `INT1`, `INT2`, `SCL`, `SDA`, `+`, `-` from top
to bottom when viewed as shown in Jaycar's product photo. Connect it as follows:

| XC3732 label | ESP32-S3 connection | Purpose |
| --- | --- | --- |
| `+` | `3V3` | Sensor power |
| `-` | `GND` | Common ground |
| `SDA` | `GPIO 4` | I²C data |
| `SCL` | `GPIO 5` | I²C clock |
| `INT1` | `GPIO 6` | Single-tap interrupt |
| `INT2` | Not connected | Unused interrupt output |

The sensor is an MMA8452Q. Its firmware address is detected automatically at
`0x1D` or `0x1C`. The firmware configures the sensor for single-pulse detection
on all three axes. A tap toggles between the clock screen and the next-exam
countdown screen; the previous automatic 30-second screen change is disabled.
The module operates from 1.6–3.6 V, so use the ESP32-S3 `3V3` pin. The module's
I²C bus should have pull-ups to 3V3; if your particular board revision does not
already provide them, add one 4.7 kΩ resistor from `SDA` to `3V3` and another
from `SCL` to `3V3`.

## First-time setup

1. Install [Visual Studio Code](https://code.visualstudio.com/) and the
   [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode).
2. Open this folder in VS Code.
3. Copy `include/secrets.example.h` to `include/secrets.h`.
4. Put your 2.4 GHz Wi-Fi name and password in `include/secrets.h`:

   ```cpp
   #define WIFI_SSID "My Wi-Fi"
   #define WIFI_PASSWORD "my-password"
   ```

   `include/secrets.h` is ignored by Git, so credentials will not be committed.
5. Connect the ESP32-S3 by USB, then use PlatformIO's **Upload** action.
6. If upload does not start, hold **BOOT**, tap **RESET**, release **BOOT**, and
   upload again.

From a terminal, the equivalent commands are:

```sh
pio run
pio run --target upload
pio device monitor
```

The serial monitor runs at 115200 baud. On startup, the display reports Wi-Fi
and time-sync status. After a successful sync it continues keeping time if Wi-Fi
temporarily drops, and attempts to reconnect every 30 seconds.

## Display layout

```text
WED 12 AUG                 WiFi
           DAY COMPLETE
              42.123%
   ██████████░░░░░░░░░░░░
          10:06:34 AM
```

The percentage includes fractions of a second, so the three-decimal value
updates smoothly and is calculated from local Brisbane time. The progress bar
provides a quick visual indication of the day completed, and the top-right
status changes to `----` whenever Wi-Fi is disconnected.

The alternate screen automatically advances to the next future exam:

```text
NEXT EXAM                  WiFi
              MATH1051
        37d 14h 22m
    SAT 19 SEP 08:00 AM
```

The configured Brisbane exam schedule is:

- MATH1051 — Saturday 19 September 2026 at 8:00 AM
- ENGG1300 — Saturday 19 September 2026 at 2:00 PM

The countdown is the largest element on this screen, followed by the exam date
and then the course name. It shows days, hours, and minutes until the final 24
hours, when it switches to hours, minutes, and seconds. Once MATH1051 starts,
the next exam screen automatically changes to ENGG1300. The schedule is stored
in `kExams` near the top of `src/main.cpp`.

Tap the connected XC3732 once to switch screens. The firmware uses `INT1` for
an immediate notification and also polls the latched pulse status over I²C, so
it can still detect a tap if the interrupt wire is missing. The display refresh
interval does not determine whether a tap is detected.

## Changing the timezone

Change `kTimezone` near the top of `src/main.cpp`. It uses POSIX timezone syntax,
not an IANA city name. For locations with daylight saving, use a complete POSIX
rule rather than a fixed UTC offset.

## Troubleshooting

- **Screen stays blank:** Recheck all seven wires and especially `GND`, `VCC`,
  `RES`, and `CS`. Confirm the OLED is the XC3728/SH1106 model.
- **Screen is upside down:** Change `U8G2_R0` to `U8G2_R2` in `src/main.cpp`.
- **It says `SET UP WI-FI`:** Create `include/secrets.h` from the example file.
- **It says `NO WI-FI`:** ESP32-S3 supports 2.4 GHz Wi-Fi, not a 5 GHz-only
  network. Check the credentials and signal.
- **It stays on `SYNCING TIME`:** The network may be blocking NTP (UDP port 123)
  or may not have internet access. Serial output provides more detail.
- **Tapping does nothing:** The serial monitor should report `MMA8452Q ready`
  and its detected I²C address. If it says `not detected`, check `+`, `-`,
  `SDA`, and `SCL` first, including any required 4.7 kΩ pull-ups. If it is
  ready, tap the sensor board firmly once and make sure it is powered from
  `3V3`; `INT1` is recommended but the current firmware also has an I²C polling
  fallback.

Jaycar's [XC3728 sample manual](https://media.jaycar.com.au/product/resources/XC3728_manualMain_92743.pdf)
identifies the controller as SH1106. Espressif documents the official
[ESP32-S3 DevKitC-1 pin layout](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html).
