# Book32

[![CI](https://github.com/qzte/Book32/actions/workflows/ci.yml/badge.svg)](https://github.com/qzte/Book32/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/qzte/Book32)](https://github.com/qzte/Book32/releases/latest)

Book32 is a custom E-Ink application OS for the Seeed Studio XIAO ESP32-S3
TRMNL 7.5 inch OG DIY kit. It includes an EPUB reader and a local web
interface for books, settings, and OTA updates.

## Hardware

- MCU: Seeed Studio XIAO ESP32-S3
- Display: 7.5 inch E-Ink panel, 800 x 480
- Storage layout: separate firmware, web UI, and ebook partitions
- Input: single button navigation
- Battery: LiPo voltage monitoring

## Controls

KEY3 (GPIO 5):

- Click: move to the next item or page
- Long press: select or open

KEY1 (GPIO 2):

- Click: go back to the previous page
- Long press: return to the main menu

KEY2 (GPIO 3):

- Click: force a full display refresh, clearing accumulated e-ink ghosting
- Long press: enter standby. Press KEY3 to wake.

## Wiring

| E-Ink Pin | Function | XIAO ESP32-S3 Pin |
| --- | --- | --- |
| VCC | 3.3V | 3V3 |
| GND | Ground | GND |
| DIN | MOSI | GPIO 9 |
| CLK | SCK | GPIO 7 |
| CS | Chip select | GPIO 44 |
| DC | Data/command | GPIO 10 |
| RST | Reset | GPIO 38 |
| BUSY | Busy | GPIO 4 |

Button:

- Next: GPIO 5
- Back: GPIO 2
- Other side: GND

Battery sense:

- Voltage ADC: GPIO 1
- Measurement switch: GPIO 6, active high

## Install PlatformIO

The easiest path is Visual Studio Code plus the PlatformIO extension.

1. Install Visual Studio Code.
2. Install the PlatformIO IDE extension.
3. Install Git if it is not already installed.
4. Clone this repo:

```powershell
git clone https://github.com/qzte/Book32.git
cd Book32
```

You can also use PlatformIO from the command line:

```powershell
python -m pip install platformio
```

## Flash Book32 To The TRMNL Kit

Connect the XIAO ESP32-S3 to your computer over USB. From the repo folder, flash
the firmware:

```powershell
python -m platformio run --target upload
```

Then flash the web interface filesystem:

```powershell
python -m platformio run --target uploadfs
```

The ebook storage partition is separate. These commands update firmware and the
web UI, but they do not erase uploaded ebooks.

On a brand-new board, the firmware creates the ebook filesystem on first boot.
After the first successful boot, the web interface should report roughly 10 MB
of ebook storage. If it reports 0 bytes, confirm that the firmware was flashed
from this project so the custom `partitions_16MB.csv` partition table was
installed.

To watch boot logs:

```powershell
python -m platformio device monitor
```

If upload fails because the board is not in bootloader mode, hold BOOT, tap
RESET, then run the upload command again.

## First Boot

1. Power on Book32.
2. If WiFi is not configured, connect to the `Book32-Setup` access point.
3. Open `192.168.4.1` if the setup portal does not open automatically.
4. Choose your WiFi network and enter the password.
5. After connection, Book32 shows its IP address on the main menu.
6. Open `http://<BOOK32_IP>/` in a browser to manage books and settings.

## Access Control

Since v1.9.0 the web interface asks for no login: every endpoint is open to any
client that can reach the device on port 80. The only remaining barrier is the
network itself — your router's password on the home LAN, or the WPA2 passphrase
shown on the e-ink footer while the `Book32` hotspot is up. Do not expose the
device to an untrusted network or forward port 80 to it.

## OTA Updates

Book32 uses the public GitHub release feed:

```text
https://github.com/qzte/Book32/releases/latest
```

No GitHub personal access token is required. Every release published by
`.github/workflows/release.yml` includes:

- `firmware.bin`
- `littlefs.bin`
- a SHA-256 checksum for each asset in the release notes

The device downloads those public release assets directly when you run an update
from the web interface or the device menu, and refuses to install an asset
whose checksum is missing or does not match — see [Releases](https://github.com/qzte/Book32/releases)
for the published versions. A brand-new board flashed by USB with an older
firmware picks up subsequent releases over the air automatically; there is no
separate bootstrap step.

## Useful PlatformIO Commands

Build firmware:

```powershell
python -m platformio run
```

Build the web UI filesystem image:

```powershell
python -m platformio run --target buildfs
```

Flash firmware:

```powershell
python -m platformio run --target upload
```

Flash web UI:

```powershell
python -m platformio run --target uploadfs
```

Open serial monitor:

```powershell
python -m platformio device monitor
```

## Features

- Polished boot screen with E-Ink progress feedback
- EPUB reader with per-book reading progress and boot resume
- Library state export/import (progress, book names and manual order)
- Library menu optimized for E-Ink
- Local web interface for uploading and deleting books
- Battery indicator and charging status
- Public GitHub OTA firmware and web UI updates

## Partition Notes

Book32 uses a custom partition table. The ebook partition is mounted separately
from the firmware and web UI filesystem, so normal firmware and `uploadfs`
updates do not overwrite user ebook storage.

Fresh hardware setup uses three pieces:

- `python -m platformio run --target upload` flashes the bootloader, firmware,
  and the custom partition table.
- `python -m platformio run --target uploadfs` flashes the 1 MB LittleFS web UI
  partition named `spiffs`.
- The 10 MB `ebooks` partition is not flashed by PlatformIO. Book32 formats it
  automatically the first time it sees that partition is blank.
