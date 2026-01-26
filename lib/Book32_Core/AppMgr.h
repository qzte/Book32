#pragma once
#include <vector>
#include "BaseApp.h"
#include "DisplayMgr.h"

class AppMgr {
public:
    static AppMgr& getInstance();
    
    void registerApp(App* app);
    void switchTo(int index);
    void switchTo(const char* name);
    
    void update(); // Main loop update
    void draw();   // Trigger draw on current app if needed
    
    App* getCurrentApp() { return currentApp; }
    std::vector<App*>& getApps() { return apps; }

private:
    AppMgr();
    std::vector<App*> apps;
    App* currentApp = nullptr;
};
