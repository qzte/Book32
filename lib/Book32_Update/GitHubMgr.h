#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

struct UpdateInfo {
    bool available;
    String version;
    String firmwareUrl;
    String filesystemUrl;
    String notes;
    bool hasFirmware;
    bool hasFilesystem;
};

class GitHubMgr {
public:
    static GitHubMgr& getInstance();

    void init();
    UpdateInfo checkUpdate(const char* currentVersion);
    bool performFirmwareUpdate(const char* url, bool restartAfter = true);
    bool performFilesystemUpdate(const char* url, bool restartAfter = true);
    bool performFullUpdate(const char* currentVersion);
    void triggerUpdate(const char* currentVersion);

private:
    GitHubMgr();
};
