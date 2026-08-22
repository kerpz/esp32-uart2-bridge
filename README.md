# ESP32 UART2 Bridge

ESP-IDF firmware that turns an ESP32 into a Wi-Fi-connected UART2 bridge. It
provides a browser-based status and configuration panel, forwards UART2 data to
TCP clients and WebSocket clients, and parses GPS NMEA sentences to maintain
the device time.

## Features

- Wi-Fi AP available continuously in `WIFI_MODE_APSTA`.
- Optional station connection with automatic reconnect.
- Bidirectional UART2-to-TCP bridge on port `5000`.
- WebSocket endpoint for live UART data and debugging.
- Browser interface for device status, Wi-Fi settings, component flags, API
  settings, reboot, and OTA firmware upload.
- NMEA `RMC` and `GGA` parsing for fix status, position, speed, satellites,
  altitude, and system time.
- Configuration persisted in the ESP32 NVS partition.

## Hardware Defaults

| Function | Default |
| --- | --- |
| UART peripheral | `UART_NUM_2` |
| Baud rate | `9600` |
| Data format | 8 data bits, no parity, 1 stop bit |
| TX | GPIO17 |
| RX | GPIO16 |
| RTS / CTS | Not connected |
| TCP port | `5000` |

Connect the external UART device's TX to GPIO16 (ESP32 RX), RX to GPIO17
(ESP32 TX), and connect the grounds. Change the definitions in
`main/uart2.h` if your board uses different pins or baud rate.

## Requirements

- ESP32 hardware with UART2 pins available.
- ESP-IDF 5.5 or a compatible ESP-IDF installation.
- The ESP-IDF environment exported in the shell used to run `idf.py`.

The project uses the ESP-IDF component manager and depends on
`espressif/mdns`.

## Build and Flash

From the project directory, with the ESP-IDF environment enabled:

```bash
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with the serial port for your board. On Linux or macOS, use a
device path such as `/dev/ttyUSB0` instead.

The repository's CI build uses ESP-IDF `release-v5.5` and runs `idf.py build`.
The custom partition table reserves space for NVS, two OTA application slots,
and SPIFFS; the checked-in configuration is set for a 2 MB flash device.

## First Boot

On first boot, the firmware creates the following configuration in NVS:

- AP SSID: `ESP32`
- AP password: `12345678`
- Station mode: enabled
- Station SSID: `KERPZ-AP2`
- Station password: `yourpassword`
- Beep and analog components: enabled
- Display and ADS1115 components: disabled
- API posting: disabled

Connect a computer or phone to the `ESP32` access point and open the ESP-IDF
SoftAP address, normally `http://192.168.4.1/`. The web panel can be used to
inspect the assigned addresses and update the saved configuration. A reboot
may be required for changed Wi-Fi credentials to take effect.

## Network Interfaces

### TCP bridge

Connect a TCP client to port `5000` on the ESP32's AP or station address:

```bash
nc 192.168.4.1 5000
```

Bytes received from the TCP client are written to UART2. Bytes received from
UART2 are sent to the connected TCP client. The current implementation accepts
one TCP client at a time.

### HTTP and WebSocket endpoints

| Endpoint | Method | Purpose |
| --- | --- | --- |
| `/` | `GET` | Embedded web panel |
| `/system` | `POST` | System and network status |
| `/config` | `POST` | Read or save configuration |
| `/app` | `POST` | Application/WebSocket panel data |
| `/firmware` | `POST` | Firmware upload form metadata |
| `/upload` | `POST` | OTA binary upload |
| `/scan` | `GET` | Wi-Fi scan |
| `/ws` | `GET` | WebSocket connection for UART data |

The WebSocket endpoint broadcasts UART2 input as text frames to connected
clients. Text received from a WebSocket client is currently echoed back and is
not written to UART2.

## GPS and NMEA

The UART task recognizes NMEA lines containing:

- `RMC`: UTC time/date, fix state, latitude, longitude, and speed.
- `GGA`: fix quality, satellite count, and altitude.

When a valid fix includes date and time, the firmware updates the ESP32 system
clock. Once the station interface obtains an IP address, SNTP also starts and
uses `pool.ntp.org`.

## Configuration and Security

Configuration is stored in the NVS namespace `cfg` and is recreated with
defaults if it is missing or invalid. The web server and TCP bridge currently
have no authentication or transport encryption. Change the default AP
credentials before deploying the device on an untrusted network, and treat API
URLs and keys entered through the panel as device-local configuration.

## Project Layout

```text
main/main.c          Application startup
main/network.c       Wi-Fi AP/STA and SNTP
main/storage.c       NVS configuration
main/uart2.c         UART2 and TCP bridge tasks
main/nmea_parser.c   GPS NMEA parsing
main/webserver.c     HTTP, WebSocket, OTA, and configuration handlers
main/html.h          Embedded web panel assets
partitions.csv       NVS, OTA, and SPIFFS layout
```

## License

No license file is currently included in this repository.