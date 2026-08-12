# TimeKeeper

An ESP32-S3 N16R8 clock for the Jaycar XC3728 128 x 64 OLED. It gets the
current time from internet time servers, shows 12-hour time with seconds and
AM/PM, and displays the percentage of the local day completed above the clock
to four decimal places.

The firmware is configured for Brisbane time (`AEST-10`, UTC+10 with no daylight
saving).

## Hardware

- ESP32-S3 DevKitC-1 with an N16R8 module (16 MB flash, 8 MB PSRAM)
- [Jaycar XC3728 1.3-inch 128 x 64 OLED](https://www.jaycar.com.au/duinotech-arduino-compatible-1-3-inch-monochrome-oled-display/p/XC3728)
- Seven female-to-female jumper wires (or wires appropriate for your headers)
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
             DAY COMPLETE
              42.1234%
    ----------------
           10:06:34 AM
```

The percentage includes fractions of a second, so the four-decimal value
updates smoothly and is calculated from local Brisbane time.

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

Jaycar's [XC3728 sample manual](https://media.jaycar.com.au/product/resources/XC3728_manualMain_92743.pdf)
identifies the controller as SH1106. Espressif documents the official
[ESP32-S3 DevKitC-1 pin layout](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html).
