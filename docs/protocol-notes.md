# Protocol notes

These notes capture the working route discovered during bench testing.

## What did not work well

Direct UART from the Heltec V3 GPIO serial module to the display ESP32 was unreliable for this project. It produced useful PROTO traffic in some cases, but it also echoed requests, conflicted with Bluetooth, and could leave the node in a confused state.

Bluetooth and Wi-Fi can also fight for resources on ESP32 Meshtastic devices. Treat the Heltec as having one active client transport at a time.

## Working route

The reliable route is:

1. WeatherWatch ESP32 creates a Wi-Fi AP.
2. Heltec joins that AP as a Wi-Fi client.
3. WeatherWatch opens TCP to `192.168.4.2:4403`.
4. WeatherWatch sends a framed `want_config_id` request.
5. The Heltec returns Meshtastic `FromRadio` protobuf frames.

## Meshtastic TCP framing

Frames use this structure:

```text
0x94 0xC3 <length_hi> <length_lo> <protobuf payload bytes>
```

Observed request copied from a working Python CLI `--listen` session:

```text
94 C3 00 06 18 8F EA E7 DD 0A
```

The payload is a `ToRadio` message containing a `want_config_id` value.

## Useful observed frame types

Observed first payload bytes:

- `0x1A` - device metadata / hardware information observed with `heltec-v3x` text.
- `0x22` - node database / node info / packet-ish frames. These often contain readable node names and sometimes telemetry-like nested payloads.
- `0x6A` - firmware / version info observed with `2.7.15.567b8ea`.
- `0x7A` - file information paths such as `/prefs/config.proto`.
- `0x12` - live packet-ish frames. These carry telemetry and message-like data during streaming.

## Current parser strategy

This project does not yet include a complete Meshtastic protobuf decoder. Instead it uses a small pragmatic parser:

- Catch valid TCP frames.
- Scan for readable strings to identify node names and message snippets.
- Scan known float32 protobuf tags for likely environment values.
- Prefer values in plausible ranges for temperature, humidity, pressure, voltage, and SNR.

This is intentionally rough. The next major improvement is to replace float sniffing with a proper minimal protobuf walker for `FromRadio -> MeshPacket -> Telemetry -> EnvironmentMetrics`.
