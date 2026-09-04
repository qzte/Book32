#include <Arduino.h>
#include <WiFi.h>
#include <esp_ota_ops.h>
#include "Config.h"
#include "NetworkState.h"

#include "DisplayMgr.h"
#include "InputMgr.h"
#include "AppMgr.h"
#include "WebMgr.h"
#include "BatteryMgr.h"
#include "FontMgr.h"

#include "../Book32_Apps/AppMainMenu.h"
#include "../Apps/AppReader/AppReader.h"
#include "../Book32_Apps/AppSettings.h"
#include <WiFiManager.h>

volatile bool gNetworkStartupInProgress = false;
static WiFiManager* gWifiManager = nullptr;

static void networkStartupTask(void* parameter) {
    (void)parameter;

    Serial.println("Network startup task started");
    if (!gWifiManager) {
        gWifiManager = new WiFiManager();
    }
    // Don't let the setup portal block forever when no known network is in
    // range. On timeout autoConnect returns false and the main menu brings up
    // the Book32 management hotspot instead.
    gWifiManager->setConfigPortalTimeout(120);
    bool connected = gWifiManager->autoConnect("Book32-Setup");

    if (!connected) {
        Serial.println("WiFi setup did not connect; continuing offline");
        gNetworkStartupInProgress = false;
        vTaskDelete(nullptr);
        return;
    }

    Serial.println("WiFi connected");
    Serial.println(WiFi.localIP());

    App* currentApp = AppMgr::getInstance().getCurrentApp();
    if (currentApp && strcmp(currentApp->getName(), "eReader") == 0) {
        Serial.println("Network startup skipped services; eReader is active");
        WebMgr::getInstance().stop();
        WiFi.disconnect(false);
        WiFi.mode(WIFI_OFF);
        gNetworkStartupInProgress = false;
        vTaskDelete(nullptr);
        return;
    }

    // WiFiManager releases its config portal before returning, but a short yield
    // gives the networking stack a clean handoff before starting our server.
    vTaskDelay(pdMS_TO_TICKS(250));

    WebMgr::getInstance().init();

    Serial.println("Network services ready");
    gNetworkStartupInProgress = false;
    vTaskDelete(nullptr);
}

void setup() {
    // v1.11.0: confirm to the bootloader that this image booted far enough to
    // be trusted, cancelling any pending auto-rollback to the previous OTA
    // slot. Safe to call even where the running build's bootloader has no
    // anti-rollback support compiled in (see
    // docs/plans/2026-08-23-post-ota-rollback-design.md for the current
    // limits of what this actually buys us) — it is then a no-op.
    esp_ota_mark_app_valid_cancel_rollback();

    Serial.begin(115200);
    delay(250);

    // Bring the E-ink panel up before the slower startup work begins.
    DisplayMgr& displayMgr = DisplayMgr::getInstance();
    displayMgr.init();
    displayMgr.showBootScreen(8, "Display ready");

    Serial.println("\n\n");
    Serial.println("╔═══════════════════════════════════════╗");
    Serial.println("║         Book32 OS Starting...         ║");
    Serial.printf( "║  Build: %s %s  ║\n", __DATE__, __TIME__);
    Serial.println("╚═══════════════════════════════════════╝");

    // Get singleton instances (must be done after Arduino init, not at global scope)
    InputMgr& inputMgr = InputMgr::getInstance();
    AppMgr& appMgr = AppMgr::getInstance();
    WebMgr& webMgr = WebMgr::getInstance();

    // 2. Mount Filesystems EARLY (before WiFi, prevents race conditions)
    displayMgr.showBootScreen(28, "Mounting storage");
    webMgr.mountFilesystems();
    
    // 2.5. Initialize Font Manager (after filesystems, before UI)
    FontMgr::getInstance().init();

    // Apply the saved display orientation now that the filesystem is mounted
    // (the boot screen briefly showed in the default orientation before this).
    displayMgr.loadDisplaySettings();

    // 3. Battery/Input/App Init. Network services start in the background so
    // the menu is usable while WiFi and the web server finish coming up.
    displayMgr.showBootScreen(72, "Preparing controls");
    BatteryMgr::getInstance().init();

    // 4. Input Init
    inputMgr.init();

    // 5. App Init
    appMgr.registerApp(new AppMainMenu());
    AppReader* readerApp = new AppReader();
    appMgr.registerApp(readerApp);
    // On-device settings menu. The main menu renders one grid icon per
    // registered app, so this appears on the home screen automatically.
    appMgr.registerApp(new AppSettings());

    displayMgr.showBootScreen(90, "Starting network");
    gNetworkStartupInProgress = true;
    BaseType_t networkTaskStarted = xTaskCreatePinnedToCore(
        networkStartupTask,
        "NetworkStart",
        12288,
        nullptr,
        1,
        nullptr,
        0
    );
    if (networkTaskStarted != pdPASS) {
        gNetworkStartupInProgress = false;
        Serial.println("Failed to start network task; continuing offline");
    }

    if (readerApp->hasBootResume()) {
        displayMgr.showBootScreen(100, "Opening reader");
        readerApp->resumeSavedBookOnStart();
        appMgr.switchTo(1);
    } else {
        displayMgr.showBootScreen(100, "Opening menu");
        appMgr.switchTo(0);
    }

    Serial.println("Setup Complete");
}

void loop() {
    InputMgr::getInstance().update();
    AppMgr::getInstance().update();
    AppMgr::getInstance().draw();  // Trigger app rendering
    DisplayMgr::getInstance().update(); // Runs a queued KEY2 full refresh, if any
    WebMgr::getInstance().update();
    BatteryMgr::getInstance().update();  // Check charging state and critical battery

    // Charging indicator (partial refresh, top-right). Apps that own the whole
    // screen opt out — in the reader this used to land on top of the page text.
    App* currentApp = AppMgr::getInstance().getCurrentApp();
    if (!currentApp || currentApp->allowsSystemStatusIndicator()) {
        BatteryMgr::getInstance().drawStatusIndicator();
    }

    // Yield a tick: without it this loop never blocks, the idle task on this
    // core is starved and the CPU stays pinned at 100% (which the battery
    // pays for). Input arrives through its own task and its own queue, so
    // this costs no responsiveness.
    delay(1);
}
