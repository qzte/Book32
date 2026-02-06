#include <Arduino.h>
#include <WiFi.h>
#include "Config.h"
#include "Secrets.h"

#include "DisplayMgr.h"
#include "InputMgr.h"
#include "AppMgr.h"
#include "WebMgr.h"
#include "GitHubMgr.h"
#include "BatteryMgr.h"
#include "TimeMgr.h"
#include "FontMgr.h"

#include "../Book32_Apps/AppMainMenu.h"
#include "../Apps/AppReader/AppReader.h"
#include "../Apps/AppKlipper/AppKlipper.h"
#include "../Apps/AppTodo/AppTodo.h"
#include <WiFiManager.h>

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n\n");
    Serial.println("╔═══════════════════════════════════════╗");
    Serial.println("║         Book32 OS Starting...         ║");
    Serial.printf( "║  Build: %s %s  ║\n", __DATE__, __TIME__);
    Serial.println("╚═══════════════════════════════════════╝");

    // Get singleton instances (must be done after Arduino init, not at global scope)
    DisplayMgr& displayMgr = DisplayMgr::getInstance();
    InputMgr& inputMgr = InputMgr::getInstance();
    AppMgr& appMgr = AppMgr::getInstance();
    WebMgr& webMgr = WebMgr::getInstance();
    GitHubMgr& gitHubMgr = GitHubMgr::getInstance();
    TimeMgr& timeMgr = TimeMgr::getInstance();

    // 1. Display Init
    displayMgr.init();

    // 2. Mount Filesystems EARLY (before WiFi, prevents race conditions)
    webMgr.mountFilesystems();
    
    // 2.5. Initialize Font Manager (after filesystems, before UI)
    FontMgr::getInstance().init();

    // 3. WiFi Init (using WiFiManager)
    WiFiManager wm;
    bool res = wm.autoConnect("Book32-Setup");

    if(!res) {
        Serial.println("Failed to connect");
    }
    else {
        Serial.println("connected...yeey :)");
        Serial.println(WiFi.localIP());
    }

    // Wait for WiFiManager's server to fully release port 80
    delay(1500);

    // 4. Web Server Init (after WiFi connected)
    webMgr.init();

    // 5. Time Init (Sync NTP - after WiFi)
    if(res) {
        timeMgr.init();
    }

    // 3.5 Battery Init
    BatteryMgr::getInstance().init();

    // 4. Input Init
    inputMgr.init();

    // 5. App Init
    appMgr.registerApp(new AppMainMenu());
    appMgr.registerApp(new AppReader());
    appMgr.registerApp(new AppTodo());
    appMgr.registerApp(new AppKlipper());

    appMgr.switchTo(0);

    Serial.println("Setup Complete");
}

void loop() {
    InputMgr::getInstance().update();
    AppMgr::getInstance().update();
    AppMgr::getInstance().draw();  // Trigger app rendering
    WebMgr::getInstance().update();
    BatteryMgr::getInstance().update();  // Check charging state and critical battery
}
