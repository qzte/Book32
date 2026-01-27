# Book32 OS (TRMNL Edition)

A custom E-Ink Application OS designed for the **Seeed Studio TRMNL** (7.5" OG DIY Kit).

## Hardware Specs
- **MCU:** Seeed Studio XIAO ESP32-S3 (8MB Flash)
- **Display:** 7.5-inch E-Ink Module (800x480 resolution)
- **Driver:** GxEPD2 (`GxEPD2_750_T7`)
- **Interaction:** Single-button navigation (Click: Next, Double-Click: Prev, Long-Press: Select)

## Hardware Wiring

Connect the **7.5" E-Ink Module** to the **Seeed XIAO ESP32-S3** as follows:

| E-Ink Pin | Function | XIAO S3 Pin (Config.h) |
| :--- | :--- | :--- |
| **VCC** | 3.3V Power | **3V3** |
| **GND** | Ground | **GND** |
| **DIN** | MOSI | **GPIO 9** |
| **CLK** | SCK | **GPIO 7** |
| **CS** | Chip Select | **GPIO 44** |
| **DC** | Data/Command | **GPIO 10** |
| **RST** | Reset | **GPIO 38** |
| **BUSY** | Busy Signal | **GPIO 4** |

**Button:**
- Pin: **GPIO 5** (Silkscreened "KEY3" on TRMNL board)
- Ground: Connect to GND

**Battery Voltage:**
- ADC Pin: **GPIO 1**
- Measurement Switch: **GPIO 6** (Active HIGH)

## Legacy Support
The original LilyGo T-Energy-S3 version of this project is archived in the **`lilygo-t-energy`** branch.

## Setup Instructions

1.  **WiFi Setup**:
    - Power on the device.
    - Connect to the WiFi Hotspot named **`Book32-Setup`**.
    - A portal should open (or go to `192.168.4.1`).
    - Select your WiFi network and enter the password.

2.  **Web Interface**:
    - Once connected, find the IP address displayed on the device's main menu.
    - Open `http://<DEVICE_IP>/` in your browser to manage books and apps.

3.  **Updates**:
    - The device checks for OTA updates from `https://github.com/rolohaun/Book32`.
    - Create an `include/Secrets.h` file on your build machine with a GitHub Personal Access Token:
      ```cpp
      #define GITHUB_TOKEN "your_token_here"
      ```

## Features
- **EPUB Reader:** High-fidelity reading with Atkinson dithering for cover art.
- **Klipper Monitor:** Subnet scanning and real-time status for Moonraker-based 3D printers.
- **Web Management:** Easy book uploads and system configuration via local network.
