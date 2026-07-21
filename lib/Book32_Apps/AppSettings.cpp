#include "AppSettings.h"
#include "DisplayMgr.h"
#include "AppMgr.h"
#include "icon_settings.h"
#include "../Book32_Core/FontMgr.h"
#include "../Book32_Core/BatteryMgr.h"
#include "../Book32_Core/Book32FS.h"
#include "../Book32_Web/WebMgr.h"
#include "../Book32_Update/GitHubMgr.h"
#include "../../include/Config.h"
#include <WiFi.h>

// --- Row identifiers --------------------------------------------------------
// Kept as an enum so the draw loop, the input handler and the value formatter
// can't drift out of sync when a row is inserted.
enum SettingsRow {
    ROW_FONT_SIZE = 0,
    ROW_FONT_FAMILY,
    ROW_ROTATION,
    ROW_REFRESH,
    ROW_SLEEP,
    ROW_WIFI,
    ROW_NETWORK,
    ROW_SYSTEM,
    ROW_SAVE,
    ROW_DISCARD,
    ROW_COUNT
};

static const char* ROW_LABELS[ROW_COUNT] = {
    "Tamanho da letra",
    "Tipo de letra",
    "Orientacao",
    "Actualizar ecra",
    "Suspensao",
    "Wi-Fi",
    "Rede",
    "Sistema",
    "Guardar",
    "Descartar"
};

static const char* FONT_FAMILY_NAMES[5] = {
    "FreeSans",
    "Merriweather",
    "Literata",
    "Source Serif 4",
    "Gelasio"
};

// Cycle sets. Every value here must survive its SettingsStore clamp, otherwise
// cycling would silently snap back and the row would appear stuck.
static const int FONT_SIZES[] = {9, 12, 18};
static const int REFRESH_FREQS[] = {5, 10, 20, 50};
static const int SLEEP_TIMEOUTS[] = {0, 5, 15, 30, 60};

static int cycleInt(const int* values, int count, int current) {
    for (int i = 0; i < count; i++) {
        if (values[i] == current) return values[(i + 1) % count];
    }
    return values[0];  // Current value not in the set: snap to the first
}

// Layout
static const int LIST_START_Y = 130;
static const int ROW_HEIGHT = 52;

AppSettings::AppSettings()
    : _screen(SCREEN_MAIN), _selectedIndex(0), _subSelectedIndex(0),
      _needsRedraw(true), _dirty(false), _statusUntil(0), _lastNetworkPoll(0) {}

const uint8_t* AppSettings::getIconImage() {
    return icon_settings_160x160;
}

void AppSettings::start() {
    SettingsStore& store = SettingsStore::getInstance();
    _reader = store.loadReader();
    _display = store.loadDisplay();
    _sleep = store.loadSleep();

    _readerSaved = _reader;
    _displaySaved = _display;

    _screen = SCREEN_MAIN;
    _selectedIndex = 0;
    _subSelectedIndex = _reader.fontFamily;
    _dirty = false;
    _statusMessage = "";
    _statusUntil = 0;
    _needsRedraw = true;

    InputMgr::getInstance().setCallback(
        std::bind(&AppSettings::handleInput, this, std::placeholders::_1));
}

void AppSettings::stop() {
    // Leaving via the main-menu shortcut shouldn't lose edits silently.
    saveDraftIfDirty();
}

void AppSettings::forceRedraw() {
    _needsRedraw = true;
}

void AppSettings::recomputeDirty() {
    _dirty = _reader.fontSize != _readerSaved.fontSize ||
             _reader.fontFamily != _readerSaved.fontFamily ||
             _reader.refreshFrequency != _readerSaved.refreshFrequency ||
             _display.rotation != _displaySaved.rotation ||
             _sleep.timeout != SettingsStore::getInstance().loadSleep().timeout;
}

bool AppSettings::rowChanged(int index) const {
    switch (index) {
        case ROW_FONT_SIZE:   return _reader.fontSize != _readerSaved.fontSize;
        case ROW_FONT_FAMILY: return _reader.fontFamily != _readerSaved.fontFamily;
        case ROW_ROTATION:    return _display.rotation != _displaySaved.rotation;
        case ROW_REFRESH:     return _reader.refreshFrequency != _readerSaved.refreshFrequency;
        default:              return false;
    }
}

void AppSettings::setStatus(const String& msg, unsigned long durationMs) {
    _statusMessage = msg;
    _statusUntil = millis() + durationMs;
    _needsRedraw = true;
}

// --- Wi-Fi ------------------------------------------------------------------
bool AppSettings::isWifiOn() const {
    wifi_mode_t mode = WiFi.getMode();
    return mode != WIFI_OFF && mode != WIFI_MODE_NULL;
}

void AppSettings::toggleWifi() {
    if (isWifiOn()) {
        WebMgr::getInstance().stop();
        WiFi.disconnect(false);
        WiFi.mode(WIFI_OFF);
        setStatus("Wi-Fi desligado");
    } else {
        WiFi.mode(WIFI_STA);
        WiFi.begin();  // Reuses the credentials stored by WiFiManager
        setStatus("Wi-Fi a ligar...");
    }
}

// --- Value formatting -------------------------------------------------------
String AppSettings::valueForRow(int index) const {
    switch (index) {
        case ROW_FONT_SIZE:
            return String(_reader.fontSize) + " pt";
        case ROW_FONT_FAMILY:
            return String(FONT_FAMILY_NAMES[SettingsStore::clampFontFamily(_reader.fontFamily)]);
        case ROW_ROTATION:
            return _display.rotation == 3 ? "Botao a esquerda" : "Botao a direita";
        case ROW_REFRESH:
            return String(_reader.refreshFrequency) + " pags";
        case ROW_SLEEP:
            return _sleep.timeout == 0 ? String("Desligado")
                                       : String(_sleep.timeout) + " min";
        case ROW_WIFI:
            return isWifiOn() ? "Ligado" : "Desligado";
        case ROW_NETWORK:
        case ROW_SYSTEM:
            return ">";
        default:
            return "";
    }
}

// --- Input ------------------------------------------------------------------
void AppSettings::cycleValue(int index) {
    switch (index) {
        case ROW_FONT_SIZE:
            _reader.fontSize = cycleInt(FONT_SIZES, 3, _reader.fontSize);
            break;
        case ROW_ROTATION:
            _display.rotation = (_display.rotation == 3) ? 1 : 3;
            break;
        case ROW_REFRESH:
            _reader.refreshFrequency = cycleInt(REFRESH_FREQS, 4, _reader.refreshFrequency);
            break;
        case ROW_SLEEP:
            _sleep.timeout = cycleInt(SLEEP_TIMEOUTS, 5, _sleep.timeout);
            break;
        case ROW_WIFI:
            // Acts immediately: it's a command, not a stored preference.
            toggleWifi();
            return;
        default:
            return;
    }
    recomputeDirty();
    _needsRedraw = true;
}

void AppSettings::activate(int index) {
    switch (index) {
        case ROW_FONT_FAMILY:
            _screen = SCREEN_FONT;
            _subSelectedIndex = SettingsStore::clampFontFamily(_reader.fontFamily);
            break;
        case ROW_NETWORK:
            _screen = SCREEN_NETWORK;
            break;
        case ROW_SYSTEM:
            _screen = SCREEN_SYSTEM;
            _subSelectedIndex = 0;
            break;
        case ROW_SAVE:
            if (applyAndSave()) {
                AppMgr::getInstance().switchTo(0);
                return;
            }
            break;
        case ROW_DISCARD:
            discardChanges();
            AppMgr::getInstance().switchTo(0);
            return;
        default:
            cycleValue(index);
            return;
    }
    _needsRedraw = true;
}

void AppSettings::handleInput(InputAction action) {
    if (_screen == SCREEN_CONFIRM) {
        // 0 = guardar, 1 = descartar, 2 = cancelar
        if (action == INPUT_NEXT) {
            _subSelectedIndex = (_subSelectedIndex + 1) % 3;
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _subSelectedIndex = (_subSelectedIndex + 2) % 3;
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            if (_subSelectedIndex == 0) {
                if (applyAndSave()) AppMgr::getInstance().switchTo(0);
            } else if (_subSelectedIndex == 1) {
                discardChanges();
                AppMgr::getInstance().switchTo(0);
            } else {
                _screen = SCREEN_MAIN;
                _needsRedraw = true;
            }
        }
        return;
    }

    if (_screen == SCREEN_FONT) {
        if (action == INPUT_NEXT) {
            _subSelectedIndex = (_subSelectedIndex + 1) % 5;
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _subSelectedIndex = (_subSelectedIndex + 4) % 5;
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            _reader.fontFamily = _subSelectedIndex;
            recomputeDirty();
            _screen = SCREEN_MAIN;
            _needsRedraw = true;
        } else if (action == INPUT_BACK || action == INPUT_GO_TO_MAIN_MENU) {
            _screen = SCREEN_MAIN;
            _needsRedraw = true;
        }
        return;
    }

    if (_screen == SCREEN_NETWORK) {
        if (action == INPUT_BACK || action == INPUT_GO_TO_MAIN_MENU ||
            action == INPUT_SELECT) {
            _screen = SCREEN_MAIN;
            _needsRedraw = true;
        }
        return;
    }

    if (_screen == SCREEN_SYSTEM) {
        // 0 = procurar actualizacao, 1 = reiniciar
        if (action == INPUT_NEXT) {
            _subSelectedIndex = (_subSelectedIndex + 1) % 2;
            _needsRedraw = true;
        } else if (action == INPUT_PREV) {
            _subSelectedIndex = (_subSelectedIndex + 1) % 2;
            _needsRedraw = true;
        } else if (action == INPUT_SELECT) {
            if (_subSelectedIndex == 0) {
                if (WiFi.status() != WL_CONNECTED) {
                    setStatus("Sem rede. Ligue o Wi-Fi primeiro.");
                } else {
                    setStatus("A procurar...", 1000);
                    draw();  // Paint the notice before the blocking HTTP call
                    UpdateInfo info = GitHubMgr::getInstance().checkUpdate(SYSTEM_VERSION);
                    if (info.available) {
                        // Persist edits first: the update reboots the device.
                        saveDraftIfDirty();
                        GitHubMgr::getInstance().triggerUpdate(SYSTEM_VERSION);
                    } else {
                        setStatus("Ja tem a versao mais recente.");
                    }
                }
            } else {
                saveDraftIfDirty();
                setStatus("A reiniciar...", 1000);
                draw();
                delay(400);
                ESP.restart();
            }
        } else if (action == INPUT_BACK || action == INPUT_GO_TO_MAIN_MENU) {
            _screen = SCREEN_MAIN;
            _needsRedraw = true;
        }
        return;
    }

    // Main screen
    if (action == INPUT_NEXT) {
        _selectedIndex = (_selectedIndex + 1) % ROW_COUNT;
        _needsRedraw = true;
    } else if (action == INPUT_PREV) {
        _selectedIndex = (_selectedIndex + ROW_COUNT - 1) % ROW_COUNT;
        _needsRedraw = true;
    } else if (action == INPUT_SELECT) {
        activate(_selectedIndex);
    } else if (action == INPUT_BACK || action == INPUT_GO_TO_MAIN_MENU) {
        if (_dirty) {
            _screen = SCREEN_CONFIRM;
            _subSelectedIndex = 0;
            _needsRedraw = true;
        } else {
            AppMgr::getInstance().switchTo(0);
        }
    }
}

// --- Save / discard ---------------------------------------------------------
bool AppSettings::applyAndSave() {
    SettingsStore& store = SettingsStore::getInstance();

    bool ok = store.saveReader(_reader);
    ok = store.saveDisplay(_display) && ok;
    ok = store.saveSleep(_sleep) && ok;

    if (!ok) {
        // Keep the draft intact so the user's edits aren't thrown away.
        setStatus("Erro ao guardar. Alteracoes mantidas.", 4000);
        return false;
    }

    // Rotation first: it repaints every screen.
    if (_display.rotation != _displaySaved.rotation) {
        DisplayMgr::getInstance().setRotation(_display.rotation);
        for (App* app : AppMgr::getInstance().getApps()) {
            if (app) app->forceRedraw();
        }
    }

    // Font changes go through the BaseApp hooks; only the reader reacts.
    if (_reader.fontSize != _readerSaved.fontSize) {
        for (App* app : AppMgr::getInstance().getApps()) {
            if (app) app->applyFontSize(_reader.fontSize);
        }
    }
    if (_reader.fontFamily != _readerSaved.fontFamily) {
        for (App* app : AppMgr::getInstance().getApps()) {
            if (app) app->applyFontFamily(_reader.fontFamily);
        }
    }

    BatteryMgr::getInstance().loadSleepSettings();

    _readerSaved = _reader;
    _displaySaved = _display;
    _dirty = false;
    return true;
}

void AppSettings::discardChanges() {
    _reader = _readerSaved;
    _display = _displaySaved;
    _sleep = SettingsStore::getInstance().loadSleep();
    _dirty = false;
}

void AppSettings::saveDraftIfDirty() {
    if (!_dirty) return;
    Serial.println("AppSettings: flushing unsaved draft before sleep/exit");
    applyAndSave();
}

// --- Update -----------------------------------------------------------------
void AppSettings::update() {
    unsigned long now = millis();

    if (_statusUntil != 0 && now >= _statusUntil) {
        _statusUntil = 0;
        _statusMessage = "";
        _needsRedraw = true;
    }

    // Refresh the Wi-Fi row and the network screen while a connection settles.
    if (now - _lastNetworkPoll >= 2000) {
        _lastNetworkPoll = now;
        if (_screen == SCREEN_NETWORK) {
            _needsRedraw = true;
        }
    }
}

// --- Drawing ----------------------------------------------------------------
void AppSettings::drawHeader(const char* title) {
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();

    font.drawText(display, title, 20, 45, FONT_SIZE_SUBTITLE, GxEPD_BLACK);
    display.drawLine(20, 62, display.width() - 20, 62, GxEPD_BLACK);

    if (_dirty) {
        font.drawText(display, "* alteracoes por guardar", 20, 90,
                      FONT_SIZE_SMALL, GxEPD_BLACK);
    }
}

void AppSettings::drawFooter(const char* hint) {
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();
    int h = display.height();

    if (_statusMessage.length() > 0) {
        String msg = FontMgr::utf8ToLatin1(_statusMessage);
        font.drawTextCentered(display, msg.c_str(), h - 50, FONT_SIZE_SMALL, GxEPD_BLACK);
    }
    font.drawTextCentered(display, hint, h - 22, FONT_SIZE_SMALL, GxEPD_BLACK);
}

void AppSettings::drawMainScreen() {
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();
    int w = display.width();

    drawHeader("Definicoes");

    for (int i = 0; i < ROW_COUNT; i++) {
        int y = LIST_START_Y + i * ROW_HEIGHT;

        if (i == _selectedIndex) {
            display.fillRect(12, y - 30, w - 24, ROW_HEIGHT - 8, GxEPD_BLACK);
            display.setTextColor(GxEPD_WHITE);
        } else {
            display.setTextColor(GxEPD_BLACK);
        }

        uint16_t color = (i == _selectedIndex) ? GxEPD_WHITE : GxEPD_BLACK;

        String label = String(ROW_LABELS[i]);
        if (rowChanged(i)) label = "*" + label;
        font.drawText(display, label.c_str(), 26, y, FONT_SIZE_BODY, color);

        String value = valueForRow(i);
        if (value.length() > 0) {
            int vw = font.getTextWidth(value.c_str(), FONT_SIZE_BODY);
            font.drawText(display, value.c_str(), w - 26 - vw, y, FONT_SIZE_BODY, color);
        }
    }

    display.setTextColor(GxEPD_BLACK);
    drawFooter("KEY3: seguinte / longo: escolher  |  KEY1: anterior / longo: sair");
}

void AppSettings::drawFontScreen() {
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();
    int w = display.width();

    drawHeader("Tipo de letra");

    for (int i = 0; i < 5; i++) {
        int y = LIST_START_Y + i * ROW_HEIGHT;
        uint16_t color = GxEPD_BLACK;

        if (i == _subSelectedIndex) {
            display.fillRect(12, y - 30, w - 24, ROW_HEIGHT - 8, GxEPD_BLACK);
            color = GxEPD_WHITE;
        }

        String label = String(FONT_FAMILY_NAMES[i]);
        if (i == _reader.fontFamily) label += "  (actual)";
        font.drawText(display, label.c_str(), 26, y, FONT_SIZE_BODY, color);
    }

    display.setTextColor(GxEPD_BLACK);
    drawFooter("KEY3 longo: escolher  |  KEY1 longo: voltar");
}

void AppSettings::drawNetworkScreen() {
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();

    drawHeader("Rede");

    bool connected = WiFi.status() == WL_CONNECTED;
    int y = LIST_START_Y;

    font.drawText(display, "Estado:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    font.drawText(display, connected ? "Ligado" : (isWifiOn() ? "A ligar / sem ligacao" : "Desligado"),
                  200, y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT;

    font.drawText(display, "SSID:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    String ssid = connected ? WiFi.SSID() : String("-");
    font.drawText(display, FontMgr::utf8ToLatin1(ssid).c_str(), 200, y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT;

    font.drawText(display, "IP:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    font.drawText(display, connected ? WiFi.localIP().toString().c_str() : "-",
                  200, y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT;

    font.drawText(display, "MAC:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    font.drawText(display, WiFi.macAddress().c_str(), 200, y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT;

    if (connected) {
        font.drawText(display, "Sinal:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
        String rssi = String(WiFi.RSSI()) + " dBm";
        font.drawText(display, rssi.c_str(), 200, y, FONT_SIZE_BODY, GxEPD_BLACK);
    }

    drawFooter("KEY1 longo: voltar");
}

void AppSettings::drawSystemScreen() {
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();
    int w = display.width();

    drawHeader("Sistema");

    int y = LIST_START_Y;
    font.drawText(display, "Versao:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    font.drawText(display, SYSTEM_VERSION, 220, y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT;

    font.drawText(display, "Bateria:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    BatteryStatus bat = BatteryMgr::getInstance().getStatus();
    String batStr = String(bat.percentage) + "%  (" + String(bat.voltage, 2) + " V)";
    font.drawText(display, batStr.c_str(), 220, y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT;

    font.drawText(display, "Livros:", 26, y, FONT_SIZE_BODY, GxEPD_BLACK);
    size_t usedKb = EbookFS.usedBytes() / 1024;
    size_t totalKb = EbookFS.totalBytes() / 1024;
    String fsStr = String((unsigned long)usedKb) + " / " + String((unsigned long)totalKb) + " KB";
    font.drawText(display, fsStr.c_str(), 220, y, FONT_SIZE_BODY, GxEPD_BLACK);
    y += ROW_HEIGHT + 20;

    const char* actions[2] = {"Procurar actualizacao", "Reiniciar"};
    for (int i = 0; i < 2; i++) {
        int ay = y + i * ROW_HEIGHT;
        uint16_t color = GxEPD_BLACK;
        if (i == _subSelectedIndex) {
            display.fillRect(12, ay - 30, w - 24, ROW_HEIGHT - 8, GxEPD_BLACK);
            color = GxEPD_WHITE;
        }
        font.drawText(display, actions[i], 26, ay, FONT_SIZE_BODY, color);
    }

    display.setTextColor(GxEPD_BLACK);
    drawFooter("KEY3 longo: executar  |  KEY1 longo: voltar");
}

void AppSettings::drawConfirmScreen() {
    Book32Display& display = DisplayMgr::getInstance().getDisplay();
    FontMgr& font = FontMgr::getInstance();
    int w = display.width();

    drawHeader("Alteracoes por guardar");

    font.drawTextCentered(display, "Tem alteracoes que ainda nao gravou.",
                          LIST_START_Y, FONT_SIZE_BODY, GxEPD_BLACK);

    const char* options[3] = {"Guardar e sair", "Descartar e sair", "Cancelar"};
    int y = LIST_START_Y + 70;
    for (int i = 0; i < 3; i++) {
        int oy = y + i * ROW_HEIGHT;
        uint16_t color = GxEPD_BLACK;
        if (i == _subSelectedIndex) {
            display.fillRect(12, oy - 30, w - 24, ROW_HEIGHT - 8, GxEPD_BLACK);
            color = GxEPD_WHITE;
        }
        font.drawText(display, options[i], 26, oy, FONT_SIZE_BODY, color);
    }

    display.setTextColor(GxEPD_BLACK);
    drawFooter("KEY3: seguinte  |  KEY3 longo: confirmar");
}

void AppSettings::draw() {
    if (!_needsRedraw) return;
    _needsRedraw = false;

    Book32Display& display = DisplayMgr::getInstance().getDisplay();

    // Full refresh throughout: a settings list isn't scrolled continuously, and
    // this keeps the drawing code far simpler than the main menu's dirty-rect
    // bookkeeping.
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);

        switch (_screen) {
            case SCREEN_FONT:    drawFontScreen();    break;
            case SCREEN_NETWORK: drawNetworkScreen(); break;
            case SCREEN_SYSTEM:  drawSystemScreen();  break;
            case SCREEN_CONFIRM: drawConfirmScreen(); break;
            case SCREEN_MAIN:
            default:             drawMainScreen();    break;
        }
    } while (display.nextPage());
}
