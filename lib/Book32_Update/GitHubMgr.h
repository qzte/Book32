#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "../Book32_Core/UpdateCheckLogic.h"

// Cada campo tem valor por omissao e o checkUpdate() so escreve o que souber:
// antes isto era inicializado por agregado com uma lista posicional de onze
// valores, que qualquer campo novo partia em silencio.
struct UpdateInfo {
    bool available = false;
    // Porque o `available` sozinho nao chegava: um false dizia ao mesmo tempo
    // "nao ha nada novo" e "nao consegui perguntar" — sem rede, 403, 404,
    // resposta ilegivel — e quem chamava dizia ao utilizador que estava
    // actualizado nos dois casos. Ver UpdateCheckLogic.h.
    UpdateCheckStatus status = UpdateCheckStatus::Offline;
    // Codigo devolvido pelo HTTPClient, so para diagnostico na UI e no log
    // (positivo = estado HTTP, <= 0 = erro de transporte). 0 quando o pedido
    // nem chegou a ser feito.
    int httpCode = 0;
    String version;
    String firmwareUrl;
    String filesystemUrl;
    String notes;
    bool hasFirmware = false;
    bool hasFilesystem = false;
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
