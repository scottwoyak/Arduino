# Waveshare ESP32-P4-WIFI6-Touch-LCD-4.3 Notes

Reference notes for projects using this board. Items marked UNVERIFIED must be confirmed against the board silkscreen before wiring.

## Links
- Product page: https://www.waveshare.com/esp32-p4-wifi6-touch-lcd-4.3.htm
- Docs: https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4.3
- Resources: https://docs.waveshare.com/ESP32-P4-WIFI6-Touch-LCD-4.3/Resources-And-Documents
- Schematic PDF (also saved in docs/): https://files.waveshare.com/wiki/ESP32-P4-WIFI6-Touch-LCD-4.3/ESP32-P4-WIFI6-Touch-LCD-4.3-schematic.pdf
- Examples and hardware audit: https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-4.3 (docs/HARDWARE.md)
- The old wiki URL (waveshare.com/wiki/...) returns 404.

## Board facts
- ESP32-P4 (32MB PSRAM in package) with 32MB external NOR flash; ESP32-C6-MINI coprocessor provides Wi-Fi 6 / BLE5 over SDIO.
- Top Type-C = "USB TO UART" (CH343 bridge, UART0 TX=37 RX=38). Bottom Type-C = native USB OTG HS.
- Serial monitor speed must match the sketch (115200 with SerialX).
- 40-pin 2.54 mm header, Raspberry Pi HAT style 2x20 layout.

## GPIOs used by onboard hardware (do not use on the header)
| Function | GPIO |
|---|---|
| Touch / RTC / audio I2C | SDA 7, SCL 8 |
| LCD backlight, LCD reset | 26, 27 |
| Audio I2S | MCLK 13, SCLK 12, LRCK 10, DOUT 9, DIN 11 |
| Audio amp enable | 53 |
| MicroSD (SDMMC) | D0-D3 39/40/41/42, CMD 44, CLK 43 |
| BOOT button | 35 |
| USB UART | TX 37, RX 38 |
| USB OTG | DP, DM (header labels DP / DM) |

Source: docs/HARDWARE.md in the Waveshare GitHub repo, plus the existing Woyak library constants.

## 40-pin header J3 (from the schematic silkscreen)
Two rows of 20, read left to right as printed on the board next to the "ESP32-P4-WIFI6-Touch-LCD-4.3" text. Row A is the upper row of labels, Row B the lower row. Each column index pairs one Row A pin with the Row B pin directly below it.

| Col | Row A | Row B |
|---|---|---|
| 1 | GND | 48 |
| 2 | 52 | 47 |
| 3 | 51 | 46 |
| 4 | 50 | GND |
| 5 | 49 | 32 |
| 6 | 35 (BOOT) | GND |
| 7 | 34 | DP |
| 8 | GND | DM |
| 9 | 31 | 25 |
| 10 | 30 | 24 |
| 11 | 29 | GND |
| 12 | 3V3 | 22 |
| 13 | 28 | 21 |
| 14 | 4 | GND |
| 15 | 3 | 5 |
| 16 | GND | 38 (UART RX) |
| 17 | 2 | 37 (UART TX) |
| 18 | SCL (8) | GND |
| 19 | SDA (7) | 5V |
| 20 | 3V3 | 5V |

Notes:
- 5V is only available in Row B, columns 19 and 20.
- DP / DM (GPIO24/25 labels) are the USB OTG pair. Row B also lists 25 and 24 next to them.
- GPIO 28, 29, 30, 31 do not appear in the onboard-hardware list above, so they look free.
- Row A, columns 9 to 17 hold 31, 30, 29, 3V3, 28, 4, 3, GND, 2: a run of mostly free GPIOs with 3V3 and GND on the same side.

## Wire color standard
Use these colors for external wiring on all projects:

| Signal | Color |
|---|---|
| CS | White |
| MISO or SDA | Blue |
| MOSI | Green |
| Any clock signal (SCL, SCLK, etc.) | Yellow |
| VCC (5V or 3.3V) | Red |
| GND | Black |

Other colors are chosen per sketch and recorded in that sketch's header comment.
