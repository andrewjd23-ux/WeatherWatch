# Wiring guide

## ESP32 display controller

This is the separate ESP32 board that drives the e-ink panel, rotary encoder, and WeatherWatch access point.

## E-ink display

Driver: `GxEPD2_270_GDEY027T91`

| E-ink signal | ESP32 GPIO |
|---|---:|
| DIN / MOSI | 23 |
| CLK / SCK | 18 |
| CS | 5 |
| DC | 17 |
| RST | 16 |
| BUSY | 4 |
| VCC | 3V3 |
| GND | GND |

## Rotary encoder

Bare rotary encoder with integrated switch.

The final software mapping swaps A/B compared with the physical labels because that gave correct direction and stable quarter-turn navigation during bench testing.

| Encoder signal | ESP32 GPIO |
|---|---:|
| A | 33 |
| B | 32 |
| Switch | 25 |
| Common / GND | GND |

## Heltec V3 weather node

The Heltec remains stock Meshtastic firmware and keeps the BME280 wired directly to the Heltec so Meshtastic can report proper native environment telemetry.

WeatherWatch does **not** power the Heltec. Power both devices separately for now.

## Network link

The WeatherWatch ESP32 creates:

- SSID: `WeatherWatch`
- PSK: `holymoses`
- AP IP: `192.168.4.1`

The Heltec is configured as a Wi-Fi client and normally receives:

- Heltec IP: `192.168.4.2`
- TCP API port: `4403`
