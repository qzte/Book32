#include "AppMainMenu.h"
#include "DisplayMgr.h"
#include "AppMgr.h"
#include "../Book32_Core/BatteryMgr.h"
#include "../Book32_Core/InputMgr.h"
#include "../Book32_Core/FontMgr.h"
#include "../../include/Config.h"
#include <WiFi.h>
#include "icon_update.h"
#include "../Book32_Update/GitHubMgr.h"

void AppMainMenu::updateCheckTask(void* parameter) {
    AppMainMenu* self = (AppMainMenu*)parameter;
    
    // Wait for connection (max 10s)
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        vTaskDelay(pdMS_TO_TICKS(500));
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        UpdateInfo info = GitHubMgr::getInstance().checkUpdate(SYSTEM_VERSION);
        if (info.available) {
            self->_updateAvailable = true;
            self->_updateVersion = info.version;
            self->_needsRedraw = true; // Trigger redraw to show icon
        }
    }
    
    self->_updateTaskHandle = nullptr;
    vTaskDelete(NULL);
}

void AppMainMenu::start() {
    selectedIndex = 1; // Start with first app (skip main menu itself)
    _needsRedraw = true;
    _firstDraw = true;  // Force full refresh on first draw
    InputMgr::getInstance().setCallback(std::bind(&AppMainMenu::handleInput, this, std::placeholders::_1));
    
    // Spawn update check task if not already found
    if (!_updateTaskHandle && !_updateAvailable) {
        xTaskCreatePinnedToCore(updateCheckTask, "UpdateCheck", 8192, this, 1, &_updateTaskHandle, 0);
    }
}

void AppMainMenu::handleInput(InputAction action) {
    AppMgr& appMgr = AppMgr::getInstance();
    std::vector<App*>& apps = appMgr.getApps();
    
    // Max index is apps.size() - 1 + 1 (if update available)
    int maxIndex = apps.size() - 1 + (_updateAvailable ? 1 : 0); // 0-based index? No selectedIndex is 1-based (starts at 1)
    // Actually selectedIndex starts at 1. App 1 is index 1.
    // apps[0] is MainMenu. apps[1]...apps[N-1] are apps.
    // Update button would be index N (apps.size())
    int maxSelectable = apps.size() - 1 + (_updateAvailable ? 1 : 0);

    if (action == INPUT_NEXT) {
        selectedIndex++;
        if (selectedIndex > maxSelectable) selectedIndex = 1;
        if (selectedIndex == 0) selectedIndex = 1; // Should not happen but safety
        _needsRedraw = true;
    }
    else if (action == INPUT_SELECT) {
        if (_updateAvailable && selectedIndex == (int)apps.size()) {
            // Update selected
             GitHubMgr::getInstance().triggerUpdate(SYSTEM_VERSION);
        }
        else if (selectedIndex > 0 && selectedIndex < (int)apps.size()) {
            appMgr.switchTo(selectedIndex);
        }
    }
}

void AppMainMenu::update() {}

void AppMainMenu::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    DisplayMgr& dispMgr = DisplayMgr::getInstance();
    Book32Display& display = dispMgr.getDisplay();
    FontMgr& fontMgr = FontMgr::getInstance();
    AppMgr& appMgr = AppMgr::getInstance();
    std::vector<App*>& apps = appMgr.getApps();

    int16_t screenW = display.width();   // 480
    int16_t screenH = display.height();  // 800

    // Layout constants
    const int ICON_SIZE = 80;
    const int COLS = 2;
    const int ROW_HEIGHT = 150;
    const int START_Y = 200;

    // Use full refresh only on first draw, partial refresh for navigation
    if (_firstDraw) {
        display.setFullWindow();
        _firstDraw = false;
    } else {
        display.setPartialWindow(0, 0, screenW, screenH);
    }

    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        // === Title (only on full draw, persists on partial) ===
        fontMgr.drawText(display, "Book32", 15, 35, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
        int book32Width = fontMgr.getTextWidth("Book32", FONT_SIZE_SUBTITLE);
        char versionStr[16];
        snprintf(versionStr, sizeof(versionStr), " v%s", SYSTEM_VERSION);
        fontMgr.drawText(display, versionStr, 15 + book32Width, 35, FONT_SIZE_SMALL, GxEPD_BLACK);

        // === Battery Status ===
        float voltage = BatteryMgr::getInstance().getVoltage();
        int percentage = BatteryMgr::getInstance().getPercentage();
        int batX = screenW - 60;
        int batY = 10;

        char batText[16];
        if (BatteryMgr::getInstance().isCharging()) {
            snprintf(batText, sizeof(batText), "CHG");
        } else {
            snprintf(batText, sizeof(batText), "%.2fV", voltage);
        }
        fontMgr.drawTextRight(display, batText, batX - 5, batY + 14, FONT_SIZE_SMALL, GxEPD_BLACK);

        display.drawRect(batX, batY, 40, 20, GxEPD_BLACK);
        display.fillRect(batX + 40, batY + 5, 3, 10, GxEPD_BLACK);

        int fillWidth = (percentage * 36) / 100;
        if(fillWidth > 36) fillWidth = 36;
        if(fillWidth < 0) fillWidth = 0;
        if (percentage > 0) {
            display.fillRect(batX + 2, batY + 2, fillWidth, 16, GxEPD_BLACK);
        }

        // === App Icons Grid ===
        int colWidth = screenW / COLS;

        for (size_t i = 0; i < apps.size(); i++) {
            if (i == 0) continue;

            App* app = apps[i];
            int idx = i - 1;
            int col = idx % COLS;
            int row = idx / COLS;

            int x = col * colWidth + (colWidth - ICON_SIZE) / 2;
            int y = START_Y + row * ROW_HEIGHT;

            if ((int)i == selectedIndex) {
                display.drawRect(x - 8, y - 8, ICON_SIZE + 16, ICON_SIZE + 16, GxEPD_BLACK);
                display.drawRect(x - 7, y - 7, ICON_SIZE + 14, ICON_SIZE + 14, GxEPD_BLACK);
            }

            const uint8_t* icon = app->getIconImage();
            if (icon) {
                display.drawBitmap(x, y, icon, 80, 80, GxEPD_BLACK);
            } else {
                display.drawRect(x, y, ICON_SIZE, ICON_SIZE, GxEPD_BLACK);
            }

            const char* name = app->getName();
            int nameWidth = fontMgr.getTextWidth(name, FONT_SIZE_MENU);
            int nameX = x + (ICON_SIZE - nameWidth) / 2;
            fontMgr.drawText(display, name, nameX, y + ICON_SIZE + 25, FONT_SIZE_MENU, GxEPD_BLACK);
        }
        
        // Render Update Icon if available
        if (_updateAvailable) {
            int i = apps.size(); // Index for update app (virtual index)
            int idx = i - 1;
            int col = idx % COLS;
            int row = idx / COLS;
            
            int x = col * colWidth + (colWidth - ICON_SIZE) / 2;
            int y = START_Y + row * ROW_HEIGHT;
            
             if ((int)i == selectedIndex) {
                 // Selection Box
                display.drawRect(x - 8, y - 8, ICON_SIZE + 16, ICON_SIZE + 16, GxEPD_BLACK);
                display.drawRect(x - 7, y - 7, ICON_SIZE + 14, ICON_SIZE + 14, GxEPD_BLACK);
            }
            
            display.drawBitmap(x, y, icon_update_80x80, 80, 80, GxEPD_BLACK);
            
            String updateText = "Update " + _updateVersion;
            int nameWidth = fontMgr.getTextWidth(updateText.c_str(), FONT_SIZE_MENU);
            int nameX = x + (ICON_SIZE - nameWidth) / 2;
            fontMgr.drawText(display, updateText.c_str(), nameX, y + ICON_SIZE + 25, FONT_SIZE_MENU, GxEPD_BLACK);
        }

        // === Footer ===
        fontMgr.drawTextCentered(display, "Press: Next  |  Hold: Select", screenH - 45, FONT_SIZE_SMALL, GxEPD_BLACK);
        String ipStr = WiFi.localIP().toString();
        fontMgr.drawTextCentered(display, ipStr.c_str(), screenH - 20, FONT_SIZE_SMALL, GxEPD_BLACK);

    } while (display.nextPage());
}
