#ifndef APP_SETTINGS_H
#define APP_SETTINGS_H

#include "BaseApp.h"
#include "../Book32_Core/InputMgr.h"
#include "../Book32_Core/SettingsStore.h"

// =============================================================================
// AppSettings
// =============================================================================
// On-device configuration menu, so the reader can be configured without a
// phone or a Wi-Fi connection. Registered like any other app, which means the
// main menu picks it up as a grid icon automatically.
//
// Edits happen on an in-RAM draft; nothing is written until the user picks
// "Guardar". The one exception is the Wi-Fi toggle, which acts immediately
// because it's a command rather than a stored preference (and you need to see
// it connect before the IP is readable).
// =============================================================================

enum SettingsScreen {
    SCREEN_MAIN,      // Top-level settings list
    SCREEN_FONT,      // Font family picker (too many entries to cycle blind)
    SCREEN_NETWORK,   // Read-only network status
    SCREEN_SYSTEM,    // Version/space/battery plus OTA and restart actions
    SCREEN_CONFIRM    // "Unsaved changes" prompt on exit
};

class AppSettings : public App {
public:
    AppSettings();

    // App interface
    void start() override;
    void update() override;
    void draw() override;
    void stop() override;
    void forceRedraw() override;

    const char* getName() override { return "Definicoes"; }
    const uint8_t* getIconImage() override;

    void handleInput(InputAction action);

    // Persist the draft if it has unsaved edits. Called before the device
    // enters standby so a KEY2 press never silently discards the user's work.
    void saveDraftIfDirty();

private:
    SettingsScreen _screen;
    int _selectedIndex;
    int _subSelectedIndex;
    bool _needsRedraw;
    bool _dirty;

    // Draft state: edited freely, only flushed to disk on save.
    ReaderSettings _reader;
    DisplaySettings _display;
    SleepSettings _sleep;

    // Baseline, kept so we can tell which individual rows changed.
    ReaderSettings _readerSaved;
    DisplaySettings _displaySaved;

    // Transient status line ("Guardado", "Erro ao guardar", ...).
    String _statusMessage;
    unsigned long _statusUntil;

    unsigned long _lastNetworkPoll;

    void cycleValue(int index);
    void activate(int index);
    bool applyAndSave();
    void discardChanges();
    void recomputeDirty();
    void setStatus(const String& msg, unsigned long durationMs = 2500);

    void toggleWifi();
    bool isWifiOn() const;

    void drawMainScreen();
    void drawFontScreen();
    void drawNetworkScreen();
    void drawSystemScreen();
    void drawConfirmScreen();
    void drawHeader(const char* title);
    void drawFooter(const char* hint);

    String valueForRow(int index) const;
    bool rowChanged(int index) const;
};

#endif
