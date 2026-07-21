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
    // v1.6.0: expected SHA-256 of each asset, parsed from the release body.
    // Empty when the release did not publish one — the download then aborts.
    String firmwareSha256;
    String filesystemSha256;
};

class GitHubMgr {
public:
    static GitHubMgr& getInstance();

    void init();
    UpdateInfo checkUpdate(const char* currentVersion);
    bool performFirmwareUpdate(const char* url, bool restartAfter = true, int step = 0, int totalSteps = 0,
                               const char* expectedSha256 = nullptr);
    bool performFilesystemUpdate(const char* url, bool restartAfter = true, int step = 0, int totalSteps = 0,
                                 const char* expectedSha256 = nullptr);
    bool performFullUpdate(const char* currentVersion);
    void triggerUpdate(const char* currentVersion);

private:
    GitHubMgr();
};
