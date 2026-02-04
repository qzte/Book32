#pragma once
#include "BaseApp.h"
#include "../Book32_Core/InputMgr.h"

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
    
    // Update Notification
    bool _updateAvailable = false;
    String _updateVersion = "";
    TaskHandle_t _updateTaskHandle = nullptr;
    static void updateCheckTask(void* parameter);
};
