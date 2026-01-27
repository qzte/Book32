#include "AppMainMenu.h"
#include "DisplayMgr.h"
#include "AppMgr.h"
#include "../Book32_Core/BatteryMgr.h"
#include "../Book32_Core/InputMgr.h"
#include "../Book32_Core/FontMgr.h"
#include <WiFi.h>

void AppMainMenu::start() {
    Serial.println("Main Menu Setup");
    selectedIndex = 1; // Start with first app (skip main menu itself)
    _needsRedraw = true;
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

    int16_t screenW = display.width();
    int16_t screenH = display.height();

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // === "Book32" Title at Top (36px, centered) ===
        fontMgr.drawTextCentered(display, "Book32", 50, FONT_SIZE_HEADER, GxEPD_BLACK);

        // === Battery Status (Top Right) ===
        float voltage = BatteryMgr::getInstance().getVoltage();
        int percentage = BatteryMgr::getInstance().getPercentage();
        int batX = screenW - 60;
        int batY = 10;

        // Draw Voltage/Charging Text (16px)
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

        // === App Icons Grid ===
        int iconSize = 96;
        int iconSpacing = 160;
        int startY = 180;
        int startX = (screenW - (3 * iconSpacing + iconSize)) / 2;

        for (size_t i = 0; i < apps.size(); i++) {
            if (i == 0) continue; // Skip main menu itself

            App* app = apps[i];
            int col = (i - 1) % 4;
            int row = (i - 1) / 4;
            
            int x = startX + col * iconSpacing;
            int y = startY + row * iconSpacing * 1.3;

            // Draw selection rectangle
            if ((int)i == selectedIndex) {
                display.drawRect(x - 8, y - 8, iconSize + 16, iconSize + 16, GxEPD_BLACK);
                display.drawRect(x - 7, y - 7, iconSize + 14, iconSize + 14, GxEPD_BLACK);
            }

            // Draw icon
            const uint8_t* icon = app->getIconImage();
            if (icon) {
                display.drawBitmap(x + (iconSize-48)/2, y + (iconSize-48)/2, icon, 48, 48, GxEPD_BLACK);
            } else {
                display.drawRect(x, y, iconSize, iconSize, GxEPD_BLACK);
            }

            // Draw app name below icon (20px)
            const char* name = app->getName();
            int nameWidth = fontMgr.getTextWidth(name, FONT_SIZE_MENU);
            int nameX = x + (iconSize - nameWidth) / 2;
            fontMgr.drawText(display, name, nameX, y + iconSize + 25, FONT_SIZE_MENU, GxEPD_BLACK);
        }

        // === IP Address at Bottom (16px, centered) ===
        String ipStr = WiFi.localIP().toString();
        fontMgr.drawTextCentered(display, ipStr.c_str(), screenH - 25, FONT_SIZE_SMALL, GxEPD_BLACK);

    } while (display.nextPage());
}
