#include "AppMainMenu.h"
#include "DisplayMgr.h"
#include "AppMgr.h"
#include "../Book32_Core/BatteryMgr.h"
#include "../Book32_Core/InputMgr.h"
#include <WiFi.h>

void AppMainMenu::start() {
    Serial.println("Main Menu Setup");
    selectedIndex = 1; // Start with first app (skip main menu itself)
    _needsRedraw = true; // Trigger initial draw
    InputMgr::getInstance().setCallback(std::bind(&AppMainMenu::handleInput, this, std::placeholders::_1));
}

void AppMainMenu::handleInput(InputAction action) {
    AppMgr& appMgr = AppMgr::getInstance();
    std::vector<App*>& apps = appMgr.getApps();
    
    if (action == INPUT_NEXT) {
        // Single press: Navigate to next app
        selectedIndex++;
        if (selectedIndex >= (int)apps.size()) selectedIndex = 1; // Skip index 0 (main menu)
        if (selectedIndex == 0) selectedIndex = 1; // Ensure we never select main menu
        _needsRedraw = true;
    }
    else if (action == INPUT_SELECT) {
        // Long press: Open selected app
        if (selectedIndex > 0 && selectedIndex < (int)apps.size()) {
            appMgr.switchTo(selectedIndex);
        }
    }
}

void AppMainMenu::update() {
    // Input handled via callback
}

void AppMainMenu::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;
    
    Serial.println("Drawing Main Menu");
    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    AppMgr& appMgr = AppMgr::getInstance();
    std::vector<App*>& apps = appMgr.getApps();

    // Get screen dimensions (400x300 for 4.2" display)
    int16_t screenW = display.width();
    int16_t screenH = display.height();

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // === "Book32" Title at Top (centered) ===
        display.setTextSize(3);
        const char* title = "Book32";
        int16_t tbx, tby;
        uint16_t tbw, tbh;
        display.getTextBounds(title, 0, 0, &tbx, &tby, &tbw, &tbh);
        int titleX = (screenW - tbw) / 2;
        display.setCursor(titleX, 20);
        display.print(title);

        // === Battery Status (Top Right) ===
        float voltage = BatteryMgr::getInstance().getVoltage();
        int percentage = BatteryMgr::getInstance().getPercentage();
        int batX = screenW - 60;
        int batY = 5;

        // Draw Voltage Text
        display.setTextSize(1);
        display.setCursor(batX - 35, batY + 8);
        if (BatteryMgr::getInstance().isCharging()) {
            display.print("CHG");
        } else {
            display.print(String(voltage, 2) + "V");
        }

        // Draw Battery Icon
        display.drawRect(batX, batY, 40, 20, GxEPD_BLACK); // Body
        display.fillRect(batX + 40, batY + 5, 3, 10, GxEPD_BLACK); // Tip
        
        // Fill based on percentage
        int fillWidth = (percentage * 36) / 100;
        if(fillWidth > 36) fillWidth = 36;
        if(fillWidth < 0) fillWidth = 0;
        
        if (percentage > 0) {
            display.fillRect(batX + 2, batY + 2, fillWidth, 16, GxEPD_BLACK);
        }

        // === App Icons Grid ===
        int iconSize = 64;
        int iconSpacing = 110;
        int startY = 120;
        int startX = (screenW - (3 * iconSpacing + iconSize)) / 2; // Center 4 icons

        for (size_t i = 0; i < apps.size(); i++) {
            if (i == 0) continue; // Skip main menu itself

            App* app = apps[i];
            int col = (i - 1) % 4;  // 4 icons per row
            int row = (i - 1) / 4;
            
            int x = startX + col * iconSpacing;
            int y = startY + row * iconSpacing * 1.2; // Extra vertical spacing

            // Draw selection rectangle
            if ((int)i == selectedIndex) {
                display.drawRect(x - 4, y - 4, iconSize + 8, iconSize + 8, GxEPD_BLACK);
                display.drawRect(x - 3, y - 3, iconSize + 6, iconSize + 6, GxEPD_BLACK);
            }

            // Draw icon if available
            const uint8_t* icon = app->getIconImage();
            if (icon) {
                display.drawBitmap(x, y, icon, iconSize, iconSize, GxEPD_BLACK);
            } else {
                // Draw placeholder box
                display.drawRect(x, y, iconSize, iconSize, GxEPD_BLACK);
            }

            // Draw app name below icon
            display.setTextSize(1);
            const char* name = app->getName();
            display.getTextBounds(name, 0, 0, &tbx, &tby, &tbw, &tbh);
            int nameX = x + (iconSize - tbw) / 2;
            display.setCursor(nameX, y + iconSize + 12);
            display.print(name);
        }

        // === IP Address at Bottom (centered) ===
        display.setTextSize(2);
        String ipStr = WiFi.localIP().toString();
        display.getTextBounds(ipStr.c_str(), 0, 0, &tbx, &tby, &tbw, &tbh);
        int ipX = (screenW - tbw) / 2;
        display.setCursor(ipX, screenH - 20);
        display.print(ipStr);

    } while (display.nextPage());
}
