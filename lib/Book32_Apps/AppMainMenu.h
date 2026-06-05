#pragma once
#include "BaseApp.h"
#include "../Book32_Core/InputMgr.h"
#include "../Book32_Core/BatteryMgr.h"

class AppMainMenu : public App {
public:
    const char* getName() override { return "Main Menu"; }
    
    void start() override;
    void update() override;
    void draw() override;
    
    void handleInput(InputAction action);
    
private:
    int selectedIndex = 0;
    bool _needsRedraw = false;
    bool _firstDraw = true;
    bool _selectionOnlyRedraw = false;
    bool _batteryOnlyRedraw = false;
    bool _footerOnlyRedraw = false;
    int _previousSelectedIndex = 1;
    bool _lastWifiConnected = false;
    bool _wifiStarting = false;
    String _lastIp = "";
    String _lastWifiFooterText = "";
    unsigned long _lastNetworkPoll = 0;
    unsigned long _lastBatteryPoll = 0;
    BatteryStatus _lastBatteryStatus = {0.0f, -1, false};
    
    // Update Notification
    bool _updateAvailable = false;
    String _updateVersion = "";
    TaskHandle_t _updateTaskHandle = nullptr;
    TaskHandle_t _wifiTaskHandle = nullptr;
    static void updateCheckTask(void* parameter);
    static void wifiWakeTask(void* parameter);
    void ensureWifiAwake();
    String getWifiFooterText() const;
};
