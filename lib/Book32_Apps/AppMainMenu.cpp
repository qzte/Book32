#include "AppMainMenu.h"
#include "DisplayMgr.h"
#include "AppMgr.h"
#include "../Book32_Core/BatteryMgr.h"
#include "../Book32_Core/InputMgr.h"
#include "../Book32_Core/FontMgr.h"
#include "../Book32_Web/WebMgr.h"
#include "../Book32_Core/DeviceCred.h"
#include "../Book32_Core/TimeMgr.h"
#include "../../include/Config.h"
#include "../../include/NetworkState.h"
#include <WiFi.h>
#include <qrcode.h>
#include "icon_update.h"
#include "../Book32_Update/GitHubMgr.h"

struct MenuDirtyRect {
    int x;
    int y;
    int w;
    int h;
};

static MenuDirtyRect menuItemRect(int index, int screenW) {
    const int ICON_SIZE = 160;
    const int COLS = 2;
    const int ROW_HEIGHT = 240;
    const int START_Y = 180;
    int colWidth = screenW / COLS;
    int idx = index - 1;
    int col = idx % COLS;
    int row = idx / COLS;
    int x = col * colWidth + (colWidth - ICON_SIZE) / 2;
    int y = START_Y + row * ROW_HEIGHT;
    return {x - 14, y - 14, ICON_SIZE + 28, ICON_SIZE + 70};
}

static MenuDirtyRect unionRect(MenuDirtyRect a, MenuDirtyRect b) {
    int x1 = min(a.x, b.x);
    int y1 = min(a.y, b.y);
    int x2 = max(a.x + a.w, b.x + b.w);
    int y2 = max(a.y + a.h, b.y + b.h);
    return {x1, y1, x2 - x1, y2 - y1};
}

static bool isReaderActive() {
    App* current = AppMgr::getInstance().getCurrentApp();
    return current && strcmp(current->getName(), "eReader") == 0;
}

// A String de um campo do formato "WIFI:" (secção 3 do padrão de payload da
// ZXing) trata ; , : \ e " como separadores — sem escapar, um SSID ou uma
// password que os contivesse partia o payload a meio. O SSID e a password
// derivados por este dispositivo (ver Config.h/DeviceCred.h) nunca os usam,
// mas escapar sempre é mais barato do que confiar nisso.
static String escapeWifiQrField(const String& value) {
    String escaped;
    escaped.reserve(value.length() + 4);
    for (size_t i = 0; i < value.length(); ++i) {
        char c = value.charAt(i);
        if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') escaped += '\\';
        escaped += c;
    }
    return escaped;
}

// Desenha um código QR "WIFI:" (WPA2) que junta o telemóvel ao hotspot de
// gestão sem o utilizador ter de escrever a palavra-passe derivada (10
// caracteres, ilegíveis ao acaso) a partir do ecrã. Devolve false sem
// desenhar nada se a geração do QR falhar (payload demasiado grande para a
// versão fixa abaixo) — o chamador decide o que mostrar em alternativa.
static bool drawWifiSetupQr(Book32Display& display, const String& ssid, const String& password, int tileX,
                            int tileY, int tileWidth) {
    // Versão 3 (29x29 módulos) com correcção de erro baixa: chega para um
    // payload "WIFI:T:WPA;S:<ssid>;P:<password>;;" com o SSID e a password
    // deste dispositivo (ambos curtos e fixos — ver Config.h/DeviceCred.h).
    constexpr uint8_t QR_VERSION = 3;
    constexpr int QUIET_MODULES = 4; // margem branca em módulos, à volta do QR
    constexpr int QR_BOX_SIZE = 185;
    uint8_t modules[qrcode_getBufferSize(QR_VERSION)];
    QRCode qr;
    String payload =
        String("WIFI:T:WPA;S:") + escapeWifiQrField(ssid) + ";P:" + escapeWifiQrField(password) + ";;";
    if (qrcode_initText(&qr, modules, QR_VERSION, ECC_LOW, payload.c_str()) != 0) return false;

    int totalModules = qr.size + (QUIET_MODULES * 2);
    int scale = max(1, QR_BOX_SIZE / totalModules);
    int pixelSize = totalModules * scale;
    int originX = tileX + (tileWidth - pixelSize) / 2;
    int originY = tileY;
    // Quadrado branco de fundo: o QR não preenche o tile todo (fica centrado
    // dentro dele), e sem isto ficavam restos do que estivesse desenhado
    // antes (ícone de update, por exemplo) à volta dos módulos.
    display.fillRect(originX, originY, pixelSize, pixelSize, GxEPD_WHITE);

    int moduleX = originX + (QUIET_MODULES * scale);
    int moduleY = originY + (QUIET_MODULES * scale);
    for (uint8_t y = 0; y < qr.size; ++y) {
        for (uint8_t x = 0; x < qr.size; ++x) {
            if (qrcode_getModule(&qr, x, y)) {
                display.fillRect(moduleX + (x * scale), moduleY + (y * scale), scale, scale, GxEPD_BLACK);
            }
        }
    }
    return true;
}

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
            {
                Book32Guard guard(self->_updateMutex);
                self->_updateAvailable = true;
                self->_updateVersion = info.version;
            }
            self->_needsRedraw = true; // Trigger redraw to show icon
        }
    }
    
    self->_updateTaskHandle = nullptr;
    vTaskDelete(NULL);
}

void AppMainMenu::wifiWakeTask(void* parameter) {
    AppMainMenu* self = (AppMainMenu*)parameter;
    Serial.println("Main menu WiFi wake task started");

    if (isReaderActive()) {
        self->_wifiStarting = false;
        self->_wifiTaskHandle = nullptr;
        vTaskDelete(NULL);
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.begin();

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        if (isReaderActive()) {
            WebMgr::getInstance().stop();
            WiFi.disconnect(false);
            WiFi.mode(WIFI_OFF);
        self->_wifiStarting = false;
        self->_footerOnlyRedraw = true;
        self->_needsRedraw = true;
        self->_wifiTaskHandle = nullptr;
            Serial.println("Main menu WiFi wake cancelled; eReader is active");
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED && !isReaderActive()) {
        Serial.println("Main menu WiFi connected");
        Serial.println(WiFi.localIP());
        TimeMgr::getInstance().syncIfNeeded();
        WebMgr::getInstance().init();
    } else {
        Serial.println("Main menu WiFi wake did not connect; bringing up hotspot");
        self->_wifiTaskHandle = nullptr;  // Clear before starting the hotspot
        if (!isReaderActive()) self->startHotspot();
        self->_wifiStarting = false;
        self->_footerOnlyRedraw = true;
        self->_needsRedraw = true;
        vTaskDelete(NULL);
        return;
    }

    self->_wifiStarting = false;
    self->_footerOnlyRedraw = true;
    self->_needsRedraw = true;
    self->_wifiTaskHandle = nullptr;
    vTaskDelete(NULL);
}

String AppMainMenu::getWifiFooterText() const {
    if (WiFi.status() == WL_CONNECTED) {
        IPAddress ip = WiFi.localIP();
        if (ip != INADDR_NONE) {
            return ip.toString();
        }
    }
    if (_hotspotActive) {
        // Show the passphrase only while the hotspot is up. On a normal
        // station connection the credential stays off-screen, so simply
        // picking the device up doesn't reveal the API password.
        return String("Wi-Fi: ") + AP_SSID + " / " + WebMgr::devicePassword() +
               "  ->  192.168.4.1";
    }
    return _wifiStarting ? "Wi-Fi a ligar" : "Wi-Fi desligado";
}

void AppMainMenu::startHotspot() {
    if (_hotspotActive) return;
    if (isReaderActive()) return;

    Serial.println("Main menu: starting Book32 management hotspot (offline)");
    WiFi.mode(WIFI_AP_STA);  // AP serves the web UI; STA stays available for joining a network
    // v1.5.0 (security): the hotspot was previously open, giving anyone in
    // radio range full access to the API. WPA2 needs >= 8 characters; the
    // derived credential is always 10. The passphrase is shown in the footer
    // while the hotspot is up so it can be read off the e-ink screen.
    WiFi.softAP(AP_SSID, WebMgr::devicePassword());
    delay(100);  // Let the AP interface come up before binding the server
    WebMgr::getInstance().init();
    _hotspotActive = true;

    Serial.print("Hotspot ready at ");
    Serial.println(WiFi.softAPIP());

    // Não _footerOnlyRedraw: o hotspot a ligar é o que faz aparecer o QR na
    // quarta célula da grelha (ver draw()), não só o texto do rodapé — um
    // refresh parcial só do rodapé nunca chegava a essa zona do ecrã.
    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _footerOnlyRedraw = false;
    _needsRedraw = true;
}

void AppMainMenu::stopHotspot() {
    if (!_hotspotActive) return;

    Serial.println("Main menu: stopping management hotspot");
    WiFi.softAPdisconnect(true);
    // Drop back to station-only; preserves an active connection if one exists.
    WiFi.mode(WIFI_STA);
    _hotspotActive = false;
}

void AppMainMenu::ensureWifiAwake() {
    if (WiFi.status() == WL_CONNECTED) {
        WebMgr::getInstance().init();
        _wifiStarting = false;
        return;
    }

    if (gNetworkStartupInProgress) {
        _wifiStarting = true;
        return;
    }

    if (!_wifiTaskHandle) {
        _wifiStarting = true;
        xTaskCreatePinnedToCore(wifiWakeTask, "WiFiWake", 6144, this, 1, &_wifiTaskHandle, 0);
    }
}

void AppMainMenu::start() {
    selectedIndex = 1; // Start with first app (skip main menu itself)
    _needsRedraw = true;
    _firstDraw = true;  // Force full refresh on first draw
    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _previousSelectedIndex = selectedIndex;
    _lastWifiConnected = WiFi.status() == WL_CONNECTED;
    _lastIp = _lastWifiConnected ? WiFi.localIP().toString() : "";
    _lastWifiFooterText = "";
    _lastBatteryPoll = millis();
    _lastBatteryStatus = BatteryMgr::getInstance().refreshNow();
    InputMgr::getInstance().setCallback(std::bind(&AppMainMenu::handleInput, this, std::placeholders::_1));
    ensureWifiAwake();
    
    // Spawn update check task if not already found
    if (!_updateTaskHandle && !_updateAvailable) {
        xTaskCreatePinnedToCore(updateCheckTask, "UpdateCheck", 8192, this, 1, &_updateTaskHandle, 0);
    }
}

void AppMainMenu::stop() {
    // Hotspot is a main-menu-only convenience. Leaving the menu tears it down so
    // it doesn't keep the radio (and battery) busy inside other apps. Normal
    // station connections are left untouched for management services.
    stopHotspot();
}

void AppMainMenu::forceRedraw() {
    _firstDraw = true;  // Full-frame repaint at the new orientation
    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _footerOnlyRedraw = false;
    _needsRedraw = true;
}

void AppMainMenu::handleInput(InputAction action) {
    AppMgr& appMgr = AppMgr::getInstance();
    std::vector<App*>& apps = appMgr.getApps();
    
    Serial.printf("AppMainMenu::handleInput - action: %d\n", action);
    
    // Max index is apps.size() - 1 + 1 (if update available)
    int maxIndex = apps.size() - 1 + (_updateAvailable ? 1 : 0); // 0-based index? No selectedIndex is 1-based (starts at 1)
    // Actually selectedIndex starts at 1. App 1 is index 1.
    // apps[0] is MainMenu. apps[1]...apps[N-1] are apps.
    // Update button would be index N (apps.size())
    int maxSelectable = apps.size() - 1 + (_updateAvailable ? 1 : 0);

    if (action == INPUT_NEXT) {
        _previousSelectedIndex = selectedIndex;
        selectedIndex++;
        if (selectedIndex > maxSelectable) selectedIndex = 1;
        if (selectedIndex == 0) selectedIndex = 1; // Should not happen but safety
        _selectionOnlyRedraw = !_firstDraw;
        _needsRedraw = true;
    }
    else if (action == INPUT_SELECT) {
        if (_updateAvailable && selectedIndex == (int)apps.size()) {
            // Update selected. Run the full firmware+filesystem update on its own
            // task with a large stack: doing this synchronously on the loop task
            // (TLS + JSON parsing + SHA-256 + a 4KB download buffer) has overflowed
            // the loop task's stack and reset the device mid-download.
            xTaskCreatePinnedToCore(
                [](void* param) {
                    Serial.println("OTA task started");
                    // On success this restarts the device internally and never returns.
                    bool updated = GitHubMgr::getInstance().performFullUpdate(SYSTEM_VERSION);
                    if (!updated) {
                        Serial.println("OTA task: update failed or unavailable, returning to menu.");
                    }
                    vTaskDelete(NULL);
                },
                "OTA_Task",
                16384, // 16KB stack
                nullptr,
                1, // Priority
                nullptr,
                1 // Core 1
            );
        }
        else if (selectedIndex > 0 && selectedIndex < (int)apps.size()) {
            appMgr.switchTo(selectedIndex);
        }
    }
    else if (action == INPUT_GO_TO_MAIN_MENU) {
        // Already at main menu, no action needed
        Serial.println("AppMainMenu: INPUT_GO_TO_MAIN_MENU - already at main menu");
    }
}

void AppMainMenu::update() {
    unsigned long now = millis();

    if (now - _lastNetworkPoll >= 1000) {
        _lastNetworkPoll = now;

        bool connected = WiFi.status() == WL_CONNECTED;
        String ip = connected ? WiFi.localIP().toString() : "";
        if (connected) {
            _wifiStarting = false;
        } else if (!gNetworkStartupInProgress && !_wifiTaskHandle) {
            _wifiStarting = false;
            // Offline and idle: bring up the management hotspot so a phone can
            // still reach the web interface without a router.
            if (!_hotspotActive && !isReaderActive()) {
                startHotspot();
            }
        }

        String footerText = getWifiFooterText();
        if (connected != _lastWifiConnected || ip != _lastIp || footerText != _lastWifiFooterText) {
            _lastWifiConnected = connected;
            _lastIp = ip;
            _selectionOnlyRedraw = false;
            _batteryOnlyRedraw = false;
            // O hotspot a ligar/desligar muda o QR da quarta célula da grelha,
            // não só o rodapé — um refresh só do rodapé nunca chegava a essa
            // zona do ecrã, por isso este caso não pode reduzir-se a
            // rodapé-só mesmo que o texto do rodapé também tenha mudado.
            _footerOnlyRedraw = !_firstDraw && (_hotspotActive == _lastHotspotActive);
            _lastHotspotActive = _hotspotActive;
            _needsRedraw = true;
        }
    }

    if (now - _lastBatteryPoll >= 10000) {
        _lastBatteryPoll = now;
        BatteryStatus status = BatteryMgr::getInstance().refreshNow();
        bool changed = status.charging != _lastBatteryStatus.charging ||
                       status.percentage != _lastBatteryStatus.percentage ||
                       fabsf(status.voltage - _lastBatteryStatus.voltage) >= 0.03f;
        if (changed) {
            _lastBatteryStatus = status;
            _selectionOnlyRedraw = false;
            _batteryOnlyRedraw = !_firstDraw;
            _needsRedraw = true;
        }
    }
}

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

    // Cópia coerente do estado que a tarefa de verificação de updates escreve.
    bool updateAvailable;
    String updateVersion;
    {
        Book32Guard guard(_updateMutex);
        updateAvailable = _updateAvailable;
        updateVersion = _updateVersion;
    }

    // Layout constants
    const int ICON_SIZE = 160;
    const int COLS = 2;
    const int ROW_HEIGHT = 240;
    const int START_Y = 180;

    // Use full refresh only on first draw, partial refresh for navigation
    if (_firstDraw) {
        display.setFullWindow();
        _firstDraw = false;
    } else if (_selectionOnlyRedraw) {
        MenuDirtyRect dirty = unionRect(menuItemRect(_previousSelectedIndex, screenW),
                                       menuItemRect(selectedIndex, screenW));
        dirty.x = max(0, dirty.x);
        dirty.y = max(0, dirty.y);
        if (dirty.x + dirty.w > screenW) dirty.w = screenW - dirty.x;
        if (dirty.y + dirty.h > screenH) dirty.h = screenH - dirty.y;
        display.setPartialWindow(dirty.x, dirty.y, dirty.w, dirty.h);
    } else if (_batteryOnlyRedraw) {
        display.setPartialWindow(screenW - 150, 0, 150, 42);
    } else if (_footerOnlyRedraw) {
        display.setPartialWindow(0, screenH - 70, screenW, 70);
    } else {
        display.setPartialWindow(0, 0, screenW, screenH);
    }
    _selectionOnlyRedraw = false;
    _batteryOnlyRedraw = false;
    _footerOnlyRedraw = false;

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

        // === Battery Status (single cached read) ===
        BatteryStatus bat = BatteryMgr::getInstance().getStatus();
        int batX = screenW - 60;
        int batY = 10;

        display.drawRect(batX, batY, 40, 20, GxEPD_BLACK);
        display.fillRect(batX + 40, batY + 5, 3, 10, GxEPD_BLACK);

        int fillWidth = (bat.percentage * 36) / 100;
        if(fillWidth > 36) fillWidth = 36;
        if(fillWidth < 0) fillWidth = 0;
        if (bat.percentage > 0) {
            display.fillRect(batX + 2, batY + 2, fillWidth, 16, GxEPD_BLACK);
        }
        // Draw lightning bolt if charging
        if (bat.charging) {
            display.drawLine(batX + 20, batY + 2, batX + 14, batY + 10, GxEPD_WHITE);
            display.drawLine(batX + 14, batY + 10, batX + 24, batY + 10, GxEPD_WHITE);
            display.drawLine(batX + 24, batY + 10, batX + 18, batY + 18, GxEPD_WHITE);
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
                display.drawBitmap(x, y, icon, ICON_SIZE, ICON_SIZE, GxEPD_BLACK);
            } else {
                display.drawRect(x, y, ICON_SIZE, ICON_SIZE, GxEPD_BLACK);
            }

            const char* name = app->getName();
            int nameWidth = fontMgr.getTextWidth(name, FONT_SIZE_MENU);
            int nameX = x + (ICON_SIZE - nameWidth) / 2;
            fontMgr.drawText(display, name, nameX, y + ICON_SIZE + 25, FONT_SIZE_MENU, GxEPD_BLACK);
        }
        
        // Render Update Icon if available
        if (updateAvailable) {
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
            
            display.drawBitmap(x, y, icon_update_160x160, ICON_SIZE, ICON_SIZE, GxEPD_BLACK);

            String updateText = "Actualizar " + updateVersion;
            int nameWidth = fontMgr.getTextWidth(updateText.c_str(), FONT_SIZE_MENU);
            int nameX = x + (ICON_SIZE - nameWidth) / 2;
            fontMgr.drawText(display, updateText.c_str(), nameX, y + ICON_SIZE + 25, FONT_SIZE_MENU, GxEPD_BLACK);
        } else if (_hotspotActive) {
            // A actualização tem prioridade sobre esta célula (ver acima); só
            // sobra livre quando não há nenhuma disponível. Estático — sem
            // caixa de selecção nem entrada no ciclo de INPUT_NEXT (ver
            // handleInput()): não há acção nenhuma a fazer ao "seleccionar"
            // um código QR, só a mostrar.
            int i = apps.size();
            int idx = i - 1;
            int col = idx % COLS;
            int row = idx / COLS;

            int tileX = col * colWidth;
            int tileY = START_Y + row * ROW_HEIGHT;

            if (drawWifiSetupQr(display, AP_SSID, WebMgr::devicePassword(), tileX, tileY, colWidth)) {
                const char* qrLabel = "Wi-Fi por QR";
                int nameWidth = fontMgr.getTextWidth(qrLabel, FONT_SIZE_MENU);
                int nameX = tileX + (colWidth - nameWidth) / 2;
                fontMgr.drawText(display, qrLabel, nameX, tileY + 210, FONT_SIZE_MENU, GxEPD_BLACK);
            }
        }

        // === Footer ===
        fontMgr.drawTextCentered(display, "Premir: Seguinte  |  Manter: Seleccionar", screenH - 45,
                                 FONT_SIZE_SMALL, GxEPD_BLACK);
        String ipStr = getWifiFooterText();
        fontMgr.drawTextCentered(display, ipStr.c_str(), screenH - 20, FONT_SIZE_SMALL, GxEPD_BLACK);
        _lastWifiFooterText = ipStr;

    } while (display.nextPage());
}
