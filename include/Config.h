#pragma once

// System Information
#define SYSTEM_VERSION "1.3.19"
#define DEVICE_NAME "Book32"

// Pin Definitions for Seeed XIAO ESP32-S3 (TRMNL 7.5" OG DIY Kit)
#define PIN_BAT_VOLT 1  
#define PIN_VBAT_SWITCH 6
#define VBAT_SWITCH_LEVEL HIGH
#define PIN_BUTTON   5  // "KEY3" button

// Display Pins (TRMNL 7.5" OG DIY Kit)
#define EPD_SCK     7
#define EPD_MOSI    9
#define EPD_MISO    -1 
#define EPD_CS      44
#define EPD_DC      10
#define EPD_RST     38
#define EPD_BUSY    4

// Display Settings (Portrait 7.5")
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 800
#define FONT_SIZE_DEFAULT 28 // Default font size (maps to FreeSans18pt GFX font)

// GitHub OTA Config
#define GITHUB_REPO "rolohaun/Book32"
#define GITHUB_USER "rolohaun"

// NTP Config
#define NTP_SERVER "pool.ntp.org"
#define TIMEZONE "MST7MDT,M3.2.0,M11.1.0" // Mountain Time with DST
