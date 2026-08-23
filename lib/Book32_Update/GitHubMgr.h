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
    // v1.11.0: Ed25519 signature over the asset's raw SHA-256 digest, parsed
    // from the release body. Empty when the release did not publish one —
    // the download then aborts, same as a missing SHA-256.
    String firmwareEd25519Sig;
    String filesystemEd25519Sig;
};

class GitHubMgr {
public:
    static GitHubMgr& getInstance();

    void init();
    UpdateInfo checkUpdate(const char* currentVersion);
    bool performFirmwareUpdate(const char* url, bool restartAfter = true, int step = 0, int totalSteps = 0,
                               const char* expectedSha256 = nullptr, const char* expectedEd25519Sig = nullptr);
    bool performFilesystemUpdate(const char* url, bool restartAfter = true, int step = 0, int totalSteps = 0,
                                 const char* expectedSha256 = nullptr, const char* expectedEd25519Sig = nullptr);
    bool performFullUpdate(const char* currentVersion);
    void triggerUpdate(const char* currentVersion);

private:
    GitHubMgr();

    // Corpo partilhado pelos dois performXxxUpdate(). As duas variantes eram
    // 130 linhas copiadas que só diferiam na partição de destino e nos textos
    // do ecrã — e cada correcção (paragem do stream, verificação do digest)
    // teve de ser feita duas vezes, com o risco óbvio de ficar só numa.
    // `partition` é U_FLASH ou U_SPIFFS.
    bool downloadAndFlash(const char* url, int partition, const char* label,
                          bool restartAfter, int step, int totalSteps,
                          const char* expectedSha256, const char* expectedEd25519Sig);
};
