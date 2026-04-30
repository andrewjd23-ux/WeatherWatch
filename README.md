# WeatherWatch

WeatherWatch is a standalone ESP32 e-ink dashboard for a Meshtastic Heltec V3 weather node.

Current architecture:

- The WeatherWatch ESP32 creates a local Wi-Fi access point called `WeatherWatch`.
- The Heltec V3 joins that AP as a Wi-Fi client.
- WeatherWatch connects to the Heltec over TCP at `192.168.4.2:4403`.
- It sends a Meshtastic `want_config_id` request and reads framed protobuf packets.
- It drives a 2.7 inch e-ink display and rotary encoder UI.

This first repo version keeps the parser deliberately small and pragmatic. It extracts useful values from observed Meshtastic frames rather than attempting to replicate the full Android app.

## Hardware

### Display

Driver: `GxEPD2_270_GDEY027T91`

| E-ink | ESP32 GPIO |
|---|---:|
| CS | 5 |
| DC | 17 |
| RST | 16 |
| BUSY | 4 |
| DIN/MOSI | 23 |
| CLK/SCK | 18 |
| VCC | 3V3 |
| GND | GND |

### Rotary encoder

The working software mapping is intentionally swapped to give correct direction:

| Encoder signal | ESP32 GPIO |
|---|---:|
| A | 33 |
| B | 32 |
| Switch | 25 |

## Heltec network setup

Configure the Heltec Meshtastic node to join:

- SSID: `WeatherWatch`
- PSK: `holymoses`
- Wi-Fi enabled: true
- TCP API available on port `4403`

The Heltec is expected at `192.168.4.2` when it is the only station connected to the ESP32 access point.

## Arduino libraries

Install:

- `GxEPD2`
- `Adafruit GFX Library`
- ESP32 board package, known-good tested version: `3.0.7`

## Current pages

1. Status
2. Weather
3. Power
4. Messages
5. Nodes
6. Radio Link

Canned messages are deliberately left out for now.
