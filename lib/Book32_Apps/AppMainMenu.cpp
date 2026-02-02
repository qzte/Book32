#include "AppMainMenu.h"
#include "DisplayMgr.h"
#include "AppMgr.h"
#include "../Book32_Core/BatteryMgr.h"
#include "../Book32_Core/InputMgr.h"
#include "../Book32_Core/FontMgr.h"
#include "../../include/Config.h"
#include <WiFi.h>

void AppMainMenu::start() {
    Serial.println("Main Menu Setup");
    selectedIndex = 1; // Start with first app (skip main menu itself)
    _needsRedraw = true;
    _firstDraw = true;  // Force full refresh on first draw
    InputMgr::getInstance().setCallback(std::bind(&AppMainMenu::handleInput, this, std::placeholders::_1));
}

void AppMainMenu::handleInput(InputAction action) {
    AppMgr& appMgr = AppMgr::getInstance();
    std::vector<App*>& apps = appMgr.getApps();

    if (action == INPUT_NEXT) {
        selectedIndex++;
        if (selectedIndex >= (int)apps.size()) selectedIndex = 1;
        if (selectedIndex == 0) selectedIndex = 1;
        _needsRedraw = true;
    }
    else if (action == INPUT_SELECT) {
        if (selectedIndex > 0 && selectedIndex < (int)apps.size()) {
            appMgr.switchTo(selectedIndex);
        }
    }
}

void AppMainMenu::update() {}

void AppMainMenu::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    Serial.println("Drawing Main Menu");
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();
    AppMgr& appMgr = AppMgr::getInstance();
    std::vector<App*>& apps = appMgr.getApps();

    int16_t screenW = display.width();   // 480
    int16_t screenH = display.height();  // 800

    // Use full refresh only on first draw, partial refresh for navigation
    if (_firstDraw) {
        display.setFullWindow();
        _firstDraw = false;
        Serial.println("MainMenu: Full refresh");
    } else {
        display.setPartialWindow(0, 0, screenW, screenH);
        Serial.println("MainMenu: Partial refresh");
    }

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // === "Book32 v1.0.5" Title at Top Left (version smaller, same line) ===
        fontMgr.drawText(display, "Book32", 15, 35, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        int book32Width = fontMgr.getTextWidth("Book32", FONT_SIZE_SUBTITLE);
        char versionStr[16];
        snprintf(versionStr, sizeof(versionStr), " v%s", SYSTEM_VERSION);
        fontMgr.drawText(display, versionStr, 15 + book32Width, 35, FONT_SIZE_SMALL, GxEPD_BLACK);

        // === Battery Status (Top Right) ===
        float voltage = BatteryMgr::getInstance().getVoltage();
        int percentage = BatteryMgr::getInstance().getPercentage();
        int batX = screenW - 60;
        int batY = 10;

        // Draw Voltage/Charging Text (14px)
        char batText[16];
        if (BatteryMgr::getInstance().isCharging()) {
            snprintf(batText, sizeof(batText), "CHG");
        } else {
            snprintf(batText, sizeof(batText), "%.2fV", voltage);
        }
        fontMgr.drawTextRight(display, batText, batX - 5, batY + 14, FONT_SIZE_SMALL, GxEPD_BLACK);

        // Draw Battery Icon
        display.drawRect(batX, batY, 40, 20, GxEPD_BLACK);
        display.fillRect(batX + 40, batY + 5, 3, 10, GxEPD_BLACK);

        int fillWidth = (percentage * 36) / 100;
        if(fillWidth > 36) fillWidth = 36;
        if(fillWidth < 0) fillWidth = 0;
        if (percentage > 0) {
            display.fillRect(batX + 2, batY + 2, fillWidth, 16, GxEPD_BLACK);
        }

        // === App Icons Grid (2 columns for 480px wide screen) ===
        int iconSize = 80;          // Slightly smaller icons
        int cols = 2;               // 2 columns for 480px width
        int colWidth = screenW / cols;  // 240px per column
        int rowHeight = 150;        // Height per row including text
        int startY = 200;           // Start below title/battery

        int appCount = apps.size() - 1;  // Exclude main menu

        for (size_t i = 0; i < apps.size(); i++) {
            if (i == 0) continue; // Skip main menu itself

            App* app = apps[i];
            int appIdx = i - 1;  // 0-based index for layout
            int col = appIdx % cols;
            int row = appIdx / cols;

            // Center icon in its column
            int x = col * colWidth + (colWidth - iconSize) / 2;
            int y = startY + row * rowHeight;

            // Draw selection rectangle
            if ((int)i == selectedIndex) {
                display.drawRect(x - 8, y - 8, iconSize + 16, iconSize + 16, GxEPD_BLACK);
                display.drawRect(x - 7, y - 7, iconSize + 14, iconSize + 14, GxEPD_BLACK);
            }

            // Draw icon
            const uint8_t* icon = app->getIconImage();
            if (icon) {
                display.drawBitmap(x + (iconSize - 48) / 2, y + (iconSize - 48) / 2, icon, 48, 48, GxEPD_BLACK);
            } else {
                display.drawRect(x, y, iconSize, iconSize, GxEPD_BLACK);
            }

            // Draw app name below icon (20px, centered under icon)
            const char* name = app->getName();
            int nameWidth = fontMgr.getTextWidth(name, FONT_SIZE_MENU);
            int nameX = x + (iconSize - nameWidth) / 2;
            fontMgr.drawText(display, name, nameX, y + iconSize + 25, FONT_SIZE_MENU, GxEPD_BLACK);
        }

        // === Navigation Help at Bottom ===
        fontMgr.drawTextCentered(display, "Press: Next App  |  Hold: Select App", screenH - 45, FONT_SIZE_SMALL, GxEPD_BLACK);

        // === IP Address at Bottom (14px, centered) ===
        String ipStr = WiFi.localIP().toString();
        fontMgr.drawTextCentered(display, ipStr.c_str(), screenH - 20, FONT_SIZE_SMALL, GxEPD_BLACK);

    } while (display.nextPage());
}
