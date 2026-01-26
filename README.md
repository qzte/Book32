# Book32 OS

A custom E-Ink Application OS for the LilyGo T-Energy-S3.

## Hardware Wiring

![Wiring Diagram](docs/Tenergypin.png)


Connect the **Waveshare 4.2inch E-Ink Module** to the **LilyGo T-Energy-S3** as follows:

| E-Ink Pin | Function | ESP32-S3 Pin (Config.h) | Color (Standard Wire) |
| :--- | :--- | :--- | :--- |
| **VCC** | 3.3V Power | **3V3** | Red |
| **GND** | Ground | **GND** | Black |
| **DIN** | MOSI | **GPIO 11** | Blue |
| **CLK** | SCK | **GPIO 12** | Yellow |
| **CS** | Chip Select | **GPIO 10** | Orange |
| **DC** | Data/Command | **GPIO 18** | Green |
| **RST** | Reset | **GPIO 17** | White |
| **BUSY** | Busy Signal | **GPIO 13** | Purple |

**Button:**
- Pin: **GPIO 0** (Built-in BOOT button) or External Button to **GPIO 0** + GND.


## Setup Instructions

1.  **WiFi Setup**:
    - Power on the device.
    - Connect to the WiFi Hotspot named **`Book32-Setup`**.
    - A portal should open (or go to `192.168.4.1`).
    - Select your WiFi network and enter the password.

2.  **Web Interface**:
    - Once connected, find the IP address of the device (Monitor Serial 115200 or check Router).
    - Open `http://<DEVICE_IP>/` in your browser.

3.  **Updates**:
    - Build numbers are checked against `https://github.com/rolohaun/Book32`.
    - Ensure you have a valid **Read-Only Token** in `Secrets.h` to fetch updates.
