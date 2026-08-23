#include "GitHubMgr.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>
#include "../../include/Config.h"
#include "../Book32_Core/SemVer.h"
#include "../Book32_Core/OtaDigest.h"
#include "../Book32_Core/OtaEd25519PublicKey.h"
#include <mbedtls/sha256.h>
#include <Ed25519.h>
#include "../Book32_Core/DisplayMgr.h"
#include "../Book32_Core/FontMgr.h"

// v1.4.1: abort a download that makes no progress for this long (ms).
static const unsigned long OTA_STALL_TIMEOUT_MS = 15000;

// Draw OTA progress bar on display
static void drawOTAProgress(int progress, const char* title, const char* status) {
    auto& display = DisplayMgr::getInstance().getDisplay();
    auto& fontMgr = FontMgr::getInstance();

    int screenW = display.width();
    int screenH = display.height();

    // Progress bar dimensions
    int barWidth = screenW - 100;
    int barHeight = 30;
    int barX = 50;
    int barY = screenH / 2;
    int fillWidth = (barWidth * progress) / 100;

    // Clear and draw with partial refresh for faster updates
    display.setPartialWindow(0, 0, screenW, screenH);
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Title (e.g., "Updating Firmware (1/2)" or "Updating Filesystem (2/2)")
        fontMgr.drawTextCentered(display, title, barY - 60, FONT_SIZE_TITLE, GxEPD_BLACK);

        // Status text
        fontMgr.drawTextCentered(display, status, barY - 25, FONT_SIZE_BODY, GxEPD_BLACK);

        // Progress bar outline
        display.drawRect(barX, barY, barWidth, barHeight, GxEPD_BLACK);
        display.drawRect(barX + 1, barY + 1, barWidth - 2, barHeight - 2, GxEPD_BLACK);

        // Progress bar fill
        if (fillWidth > 4) {
            display.fillRect(barX + 2, barY + 2, fillWidth - 4, barHeight - 4, GxEPD_BLACK);
        }

        // Percentage text
        char percentText[16];
        snprintf(percentText, sizeof(percentText), "%d%%", progress);
        fontMgr.drawTextCentered(display, percentText, barY + barHeight + 40, FONT_SIZE_TITLE, GxEPD_BLACK);

    } while (display.nextPage());
}


GitHubMgr::GitHubMgr() {}

GitHubMgr& GitHubMgr::getInstance() {
    static GitHubMgr instance;
    return instance;
}

void GitHubMgr::init() {
    // any init?
}

UpdateInfo GitHubMgr::checkUpdate(const char* currentVersion) {
    UpdateInfo info = {false, "", "", "", "", false, false, "", "", "", ""};

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected, cannot check for updates");
        return info;
    }

    HTTPClient http;
    String apiURL = String("https://api.github.com/repos/") + GITHUB_REPO + "/releases/latest";

    Serial.printf("Checking: %s\n", apiURL.c_str());

    http.begin(apiURL);
    http.setUserAgent("Book32-ESP32");
    http.setTimeout(10000);  // 10 second timeout

    Serial.println("Using public GitHub release API");

    int httpCode = http.GET();
    Serial.printf("HTTP Response: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
        // Filtro: a resposta do GitHub traz o objecto `author`, o `uploader`
        // de cada asset e dezenas de URLs que não usamos. Uma release com
        // notas longas e dois assets passava dos 8 KB do documento e o parse
        // falhava com NoMemory — ou seja, o dispositivo dizia "sem
        // actualizações" precisamente quando havia uma. Ficar só com os
        // campos abaixo mantém o documento pequeno seja qual for o resto.
        StaticJsonDocument<192> filter;
        filter["tag_name"] = true;
        filter["body"] = true;
        filter["assets"][0]["name"] = true;
        filter["assets"][0]["browser_download_url"] = true;

        DynamicJsonDocument doc(8192);
        DeserializationError err = deserializeJson(doc, http.getStream(),
                                                   DeserializationOption::Filter(filter));

        if (err) {
            Serial.printf("JSON parse error: %s\n", err.c_str());
            http.end();
            return info;
        }

        const char* tagName = doc["tag_name"];
        info.version = tagName ? tagName : "";
        info.notes = doc["body"].as<String>();

        Serial.printf("Latest version: %s, Current: %s\n", info.version.c_str(), currentVersion);

        // v1.4.1: compare Major.Minor.Patch numerically instead of testing for
        // mere inequality. The old check reported a *downgrade* as an available
        // update, and String::replace("v","") stripped every 'v' in the tag,
        // not just the prefix. See lib/Book32_Core/SemVer.h.
        String currentV = String(currentVersion);
        String latestV = info.version;

        if (semverIsNewer(latestV, currentV)) {
            info.available = true;
            Serial.println("Update IS available");

            // Find asset URLs
            JsonArray assets = doc["assets"];
            for (JsonObject asset : assets) {
                String name = asset["name"].as<String>();

                String url = asset["browser_download_url"].as<String>();

                if (name == "firmware.bin" || name.endsWith("_firmware.bin")) {
                    info.firmwareUrl = url;
                    info.hasFirmware = true;
                    Serial.printf("Found firmware: %s\n", name.c_str());
                }
                else if (name == "littlefs.bin" || name == "filesystem.bin" || name.endsWith("_littlefs.bin")) {
                    info.filesystemUrl = url;
                    info.hasFilesystem = true;
                    Serial.printf("Found filesystem: %s\n", name.c_str());
                }
            }
            // v1.6.0: pull the expected SHA-256 for each asset out of the
            // release body. Absent or malformed digests leave these empty,
            // which makes the download abort (fail closed).
            if (info.hasFirmware) {
                if (extractSha256(info.notes, "firmware.bin", info.firmwareSha256)) {
                    Serial.printf("Expected firmware SHA-256: %s\n", info.firmwareSha256.c_str());
                } else {
                    Serial.println("WARNING: release publishes no SHA-256 for firmware.bin");
                }
            }
            if (info.hasFilesystem) {
                if (!extractSha256(info.notes, "littlefs.bin", info.filesystemSha256)) {
                    extractSha256(info.notes, "filesystem.bin", info.filesystemSha256);
                }
                if (info.filesystemSha256.length() == 0) {
                    Serial.println("WARNING: release publishes no SHA-256 for the filesystem image");
                }
            }
            // v1.11.0: pull the expected Ed25519 signature (over the asset's
            // SHA-256 digest) for each asset. Absent or malformed signatures
            // leave these empty, which makes the download abort (fail
            // closed) — same treatment as a missing SHA-256.
            if (info.hasFirmware) {
                if (extractEd25519Signature(info.notes, "firmware.bin", info.firmwareEd25519Sig)) {
                    Serial.println("Expected firmware Ed25519 signature found");
                } else {
                    Serial.println("WARNING: release publishes no Ed25519 signature for firmware.bin");
                }
            }
            if (info.hasFilesystem) {
                if (!extractEd25519Signature(info.notes, "littlefs.bin", info.filesystemEd25519Sig)) {
                    extractEd25519Signature(info.notes, "filesystem.bin", info.filesystemEd25519Sig);
                }
                if (info.filesystemEd25519Sig.length() == 0) {
                    Serial.println("WARNING: release publishes no Ed25519 signature for the filesystem image");
                }
            }
        } else {
            Serial.println("Already up to date");
        }
    } else if (httpCode == 404) {
        Serial.println("No releases found on GitHub");
    } else if (httpCode == 403) {
        Serial.println("GitHub API rate limited - try again later");
    } else {
        Serial.printf("GitHub API Failed: %d\n", httpCode);
    }
    http.end();
    return info;
}

bool GitHubMgr::downloadAndFlash(const char* url, int partition, const char* label,
                                 bool restartAfter, int step, int totalSteps,
                                 const char* expectedSha256, const char* expectedEd25519Sig) {
    if (WiFi.status() != WL_CONNECTED) return false;

    Serial.printf("Downloading %s from: %s\n", label, url);

    // v1.6.0: refuse to flash anything we can't verify. The release workflow
    // publishes "SHA256 (<asset>) = <hex>" in the release body; a missing or
    // malformed line lands here.
    if (!expectedSha256 || strlen(expectedSha256) != BOOK32_SHA256_HEX_LEN) {
        Serial.println("Refusing update: release publishes no valid SHA-256 for this asset");
        drawOTAProgress(0, "Update Blocked", "No checksum in release");
        delay(3000);
        return false;
    }

    // v1.11.0: same fail-closed treatment for the Ed25519 signature. Without
    // this, an attacker able to forge the GitHub API response (the same
    // response the SHA-256 above is fetched from) could simply omit the
    // ED25519 line and fall back to a SHA-256-only check they also control.
    if (!expectedEd25519Sig || strlen(expectedEd25519Sig) != BOOK32_ED25519_SIG_HEX_LEN) {
        Serial.println("Refusing update: release publishes no valid Ed25519 signature for this asset");
        drawOTAProgress(0, "Update Blocked", "No signature in release");
        delay(3000);
        return false;
    }

    // Build title with step indicator if provided
    char title[64];
    if (totalSteps > 1) {
        snprintf(title, sizeof(title), "%s (%d/%d)", label, step, totalSteps);
    } else {
        snprintf(title, sizeof(title), "%s Update", label);
    }

    HTTPClient http;
    http.begin(url);
    http.setUserAgent("Book32-ESP32");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(30000);  // 30 second timeout for large downloads

    http.addHeader("Accept", "application/octet-stream");

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("%s download failed: %d\n", label, httpCode);
        http.end();
        return false;
    }

    int contentLength = http.getSize();
    Serial.printf("%s size: %d bytes\n", label, contentLength);

    // v1.4.1: getSize() returns -1 for chunked responses. The download loop
    // compares size_t against this int, so -1 promotes to SIZE_MAX and the
    // loop never terminates (watchdog reset). Fail closed instead.
    if (contentLength <= 0) {
        Serial.printf("Invalid or unknown %s content length; aborting\n", label);
        http.end();
        return false;
    }

    // U_SPIFFS é usado tanto para SPIFFS como para LittleFS.
    if (!Update.begin(contentLength, partition)) {
        Serial.printf("Not enough space for %s update\n", label);
        http.end();
        return false;
    }

    // Show initial progress on display
    drawOTAProgress(0, title, "Downloading...");

    WiFiClient* stream = http.getStreamPtr();

    // Use chunked download with periodic yields to prevent watchdog
    uint8_t buff[4096];
    size_t written = 0;
    int lastProgress = 0;

    // v1.6.0: hash the stream as it is written, so verification costs no
    // extra flash reads and no second download.
    mbedtls_sha256_context shaCtx;
    mbedtls_sha256_init(&shaCtx);
    mbedtls_sha256_starts(&shaCtx, 0);  // 0 = SHA-256, not SHA-224

    // v1.4.1: abort if the stream stalls, instead of spinning forever on
    // available() == 0 when the connection drops mid-download.
    unsigned long lastDataMs = millis();

    while (written < (size_t)contentLength) {
        size_t available = stream->available();
        if (available == 0) {
            if (millis() - lastDataMs > OTA_STALL_TIMEOUT_MS) {
                Serial.printf("Download stalled; aborting %s update\n", label);
                mbedtls_sha256_free(&shaCtx);
                Update.abort();
                http.end();
                return false;
            }
            delay(1);  // Yield to other tasks
            continue;
        }
        lastDataMs = millis();

        size_t toRead = min(available, sizeof(buff));
        size_t bytesRead = stream->readBytes(buff, toRead);
        if (bytesRead == 0) continue;

        size_t bytesWritten = Update.write(buff, bytesRead);
        if (bytesWritten != bytesRead) {
            Serial.printf("Write error during %s update\n", label);
            mbedtls_sha256_free(&shaCtx);
            Update.abort();
            http.end();
            return false;
        }
        mbedtls_sha256_update(&shaCtx, buff, bytesRead);
        written += bytesWritten;

        // Progress and yield every ~5%
        int progress = (written * 100) / contentLength;
        if (progress / 5 > lastProgress / 5) {
            Serial.printf("%s progress: %d%%\n", label, progress);
            drawOTAProgress(progress, title, "Downloading...");
            lastProgress = progress;
        }

        // Yield frequently to feed watchdog
        yield();
    }

    if (written != (size_t)contentLength) {
        mbedtls_sha256_free(&shaCtx);
        // Sem o abort, a sessão de Update ficava aberta e o próximo
        // Update.begin() recusava-se a arrancar até um reinício.
        Update.abort();
        Serial.printf("%s write failed. Written: %u / %d\n", label, (unsigned)written, contentLength);
        http.end();
        return false;
    }

    Serial.printf("%s written successfully\n", label);
    drawOTAProgress(100, title, "Verifying...");

    // v1.6.0: finalise the digest and compare BEFORE Update.end() commits
    // the image. Update.abort() on mismatch leaves the running firmware
    // untouched.
    uint8_t digest[32];
    mbedtls_sha256_finish(&shaCtx, digest);
    mbedtls_sha256_free(&shaCtx);

    char actualHex[BOOK32_SHA256_HEX_LEN + 1];
    for (int i = 0; i < 32; i++) {
        snprintf(actualHex + (i * 2), 3, "%02x", digest[i]);
    }

    String actual = String(actualHex);
    String expected = String(expectedSha256);
    if (!sha256Equal(actual, expected)) {
        Serial.println("SHA-256 MISMATCH - refusing to install");
        Serial.printf("  expected: %s\n", expected.c_str());
        Serial.printf("  actual:   %s\n", actual.c_str());
        Update.abort();
        http.end();
        drawOTAProgress(0, "Update Blocked", "Checksum mismatch");
        delay(3000);
        return false;
    }
    Serial.println("SHA-256 verified OK");

    // v1.11.0: verify the Ed25519 signature over that same digest before
    // committing the image. This is the check that actually defends against
    // an active MITM — the SHA-256 above only catches corruption, since its
    // expected value comes from the same (forgeable) API response.
    uint8_t sigBytes[BOOK32_ED25519_SIG_LEN];
    if (!hexDecode(String(expectedEd25519Sig), (size_t)BOOK32_ED25519_SIG_HEX_LEN, sigBytes)) {
        Serial.println("Ed25519 signature is not valid hex - refusing to install");
        Update.abort();
        http.end();
        drawOTAProgress(0, "Update Blocked", "Malformed signature");
        delay(3000);
        return false;
    }
    if (!Ed25519::verify(sigBytes, BOOK32_OTA_ED25519_PUBLIC_KEY, digest, sizeof(digest))) {
        Serial.println("Ed25519 SIGNATURE INVALID - refusing to install");
        Update.abort();
        http.end();
        drawOTAProgress(0, "Update Blocked", "Signature invalid");
        delay(3000);
        return false;
    }
    Serial.println("Ed25519 signature verified OK");

    drawOTAProgress(100, title, "Installing...");
    if (!Update.end()) {
        Serial.printf("%s install failed: %s\n", label, Update.errorString());
        http.end();
        return false;
    }

    Serial.printf("%s update complete\n", label);
    drawOTAProgress(100, title, "Complete!");
    delay(500);
    http.end();
    if (restartAfter) {
        Serial.println("Restarting...");
        ESP.restart();
    }
    return true;
}

bool GitHubMgr::performFirmwareUpdate(const char* url, bool restartAfter, int step, int totalSteps,
                                      const char* expectedSha256, const char* expectedEd25519Sig) {
    return downloadAndFlash(url, U_FLASH, "Firmware", restartAfter, step, totalSteps, expectedSha256,
                            expectedEd25519Sig);
}

bool GitHubMgr::performFilesystemUpdate(const char* url, bool restartAfter, int step, int totalSteps,
                                        const char* expectedSha256, const char* expectedEd25519Sig) {
    return downloadAndFlash(url, U_SPIFFS, "Web Interface", restartAfter, step, totalSteps, expectedSha256,
                            expectedEd25519Sig);
}

void GitHubMgr::triggerUpdate(const char* currentVersion) {
    Serial.println("Triggering Update Check...");
    UpdateInfo info = checkUpdate(currentVersion);
    if (info.available && info.hasFirmware) {
        Serial.printf("Update Available: %s. Starting firmware download.\n", info.version.c_str());
        performFirmwareUpdate(info.firmwareUrl.c_str(), true, 1, 1, info.firmwareSha256.c_str(),
                              info.firmwareEd25519Sig.c_str());
    } else {
        Serial.println("No firmware update available.");
    }
}

bool GitHubMgr::performFullUpdate(const char* currentVersion) {
    Serial.println("Starting full update check...");
    UpdateInfo info = checkUpdate(currentVersion);

    if (!info.available) {
        Serial.println("No update available.");
        return false;
    }

    Serial.printf("Update available: %s\n", info.version.c_str());

    bool firmwareUpdated = false;
    bool filesystemUpdated = false;

    // Calculate total steps for progress display
    int totalSteps = (info.hasFirmware ? 1 : 0) + (info.hasFilesystem ? 1 : 0);
    int currentStep = 0;

    // Update firmware first (don't restart yet)
    if (info.hasFirmware) {
        currentStep++;
        Serial.println("Updating firmware...");
        firmwareUpdated = performFirmwareUpdate(info.firmwareUrl.c_str(), false, currentStep, totalSteps,
                                                info.firmwareSha256.c_str(), info.firmwareEd25519Sig.c_str());
        if (!firmwareUpdated) {
            Serial.println("Firmware update failed!");
            return false;
        }
    }

    // Then update filesystem (don't restart yet)
    if (info.hasFilesystem) {
        currentStep++;
        Serial.println("Updating filesystem...");
        filesystemUpdated = performFilesystemUpdate(info.filesystemUrl.c_str(), false, currentStep, totalSteps,
                                                    info.filesystemSha256.c_str(),
                                                    info.filesystemEd25519Sig.c_str());
        if (!filesystemUpdated) {
            Serial.println("Filesystem update failed!");
            // Still restart if firmware was updated
            if (firmwareUpdated) {
                Serial.println("Restarting after firmware update...");
                ESP.restart();
            }
            return false;
        }
    }

    // Restart after all updates complete
    if (firmwareUpdated || filesystemUpdated) {
        // Show restarting message
        drawOTAProgress(100, "Update Complete!", "Restarting...");
        delay(1000);
        Serial.println("All updates complete. Restarting...");
        ESP.restart();
        return true;
    }

    return false;
}
