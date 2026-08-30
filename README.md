# Book32

[![CI](https://github.com/qzte/Book32/actions/workflows/ci.yml/badge.svg)](https://github.com/qzte/Book32/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/qzte/Book32)](https://github.com/qzte/Book32/releases/latest)

Book32 is a custom E-Ink application OS for the Seeed Studio XIAO ESP32-S3
TRMNL 7.5 inch OG DIY kit. It includes an EPUB reader — reading progress,
bookmarks, reading status and Portuguese hyphenation — and a local web
interface (in European Portuguese) for books, settings, and signed OTA
updates.

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

## Install From A Browser

If Book32 is already flashed and you just want to update it, the quickest
path is the [Book32 Browser Installer](https://qzte.github.io/Book32/). It
works in desktop Chrome or Edge with a data-capable USB cable and does not
require PlatformIO or a local development environment. It only writes the
firmware and web UI partitions — uploaded ebooks and settings are untouched.

Flashing brand-new or blank hardware for the first time still needs
PlatformIO; see below.

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
- an Ed25519 signature over each of those checksums (since v1.11.0)

The device downloads those public release assets directly when you run an update
from the web interface or the device menu. Both checks are fail-closed: an asset
whose checksum is missing or does not match is refused, and so is one whose
Ed25519 signature is missing, malformed, or does not verify against the public
key built into the firmware (`lib/Book32_Core/OtaEd25519PublicKey.h`). The
digest is compared and the signature verified before the update is committed,
so a rejected image never becomes the boot partition. See
[Releases](https://github.com/qzte/Book32/releases) for the published versions.

A brand-new board flashed by USB with an older firmware picks up subsequent
releases over the air automatically; there is no separate bootstrap step.

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

Reader:

- EPUB reader with per-book reading progress and boot resume
- Book titles read from the EPUB metadata, shown over two lines in the library
- European Portuguese hyphenation, so long words break with a hyphen instead of
  mid-digraph (the hyphen is visual only and never shifts the saved position)
- Six bundled fonts, adjustable text size and screen rotation
- Named bookmarks and "go to %", both set from the web interface and applied the
  next time the book is opened on the device
- Per-book reading status (unread / reading % / read) with manual override
- Reading dates: started, finished and last read, from SNTP once WiFi is up
- Library menu optimized for E-Ink, with a full-refresh key and standby

Web interface (in European Portuguese):

- Dashboard with battery, uptime and free ebook storage
- Upload and delete books, and reorder the device library
- Filter by reading status and sort by device order, title or progress
- PC library folder diffed against the device, with batch sending
- Library state export/import (progress, status, dates, names and manual order)
- Reader settings, sleep timeout, screen rotation and WiFi/hotspot setup
- Signed OTA updates for both firmware and the web UI

Device:

- Polished boot screen with E-Ink progress feedback
- On-device settings menu mirroring the main web settings
- Battery indicator and charging status

## Development

Host tests cover the pure logic (page fitting, hyphenation, progress merging,
semver, OTA digests, and so on) and need nothing but a compiler:

```bash
for t in tools/tests/test_*.cpp; do
  g++ -std=c++17 -Wall -Werror -I lib/Book32_Core "$t" -o "/tmp/$(basename "$t" .cpp)"
  "/tmp/$(basename "$t" .cpp)"
done
```

CI (`.github/workflows/ci.yml`) runs those tests, builds the firmware and the
filesystem image, syntax-checks `data/script.js`, and runs `clang-format` over
the lines a pull request changed. Library versions and the Espressif platform
are pinned to exact versions in `platformio.ini` so a tagged release stays
reproducible.

Other tools:

- `tools/format.sh` — run clang-format locally
- `tools/slim_epub.py` — shrink an EPUB before sending it to the device
- `tools/converter` — font and asset conversion helper

Design notes for each feature live in `docs/plans/`, and `TODO.txt` tracks the
on-device checks that the host tests cannot cover.

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
