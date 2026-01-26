#include "GitHubMgr.h"
#include <HTTPClient.h>
#include <Update.h>
#include "../../include/Config.h"
#include "../../include/Secrets.h"

GitHubMgr::GitHubMgr() {}

GitHubMgr& GitHubMgr::getInstance() {
    static GitHubMgr instance;
    return instance;
}

void GitHubMgr::init() {
    // any init?
}

UpdateInfo GitHubMgr::checkUpdate(const char* currentVersion) {
    UpdateInfo info = {false, "", "", "", "", false, false};

    if (WiFi.status() != WL_CONNECTED) return info;

    HTTPClient http;
    String apiURL = String("https://api.github.com/repos/") + GITHUB_REPO + "/releases/latest";

    http.begin(apiURL);
    http.setUserAgent("Book32-ESP32");
    http.addHeader("Authorization", String("token ") + GITHUB_TOKEN);

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(4096);
        deserializeJson(doc, payload);

        const char* tagName = doc["tag_name"];
        info.version = tagName ? tagName : "";
        info.notes = doc["body"].as<String>();

        // Check if version is different
        if (info.version.length() > 0 && info.version != currentVersion && info.version != String("v") + currentVersion) {
            info.available = true;

            // Find asset URLs - use API URL for private repo compatibility
            JsonArray assets = doc["assets"];
            for (JsonObject asset : assets) {
                String name = asset["name"].as<String>();
                // Use "url" (API endpoint) instead of "browser_download_url" for private repos
                String url = asset["url"].as<String>();

                if (name == "firmware.bin" || name.endsWith("_firmware.bin")) {
                    info.firmwareUrl = url;
                    info.hasFirmware = true;
                    Serial.printf("Found firmware: %s\n", url.c_str());
                }
                else if (name == "littlefs.bin" || name == "filesystem.bin" || name.endsWith("_littlefs.bin")) {
                    info.filesystemUrl = url;
                    info.hasFilesystem = true;
                    Serial.printf("Found filesystem: %s\n", url.c_str());
                }
            }
        }
    } else {
        Serial.printf("GitHub API Failed: %d\n", httpCode);
    }
    http.end();
    return info;
}

bool GitHubMgr::performFirmwareUpdate(const char* url, bool restartAfter) {
    if (WiFi.status() != WL_CONNECTED) return false;

    Serial.printf("Downloading firmware from: %s\n", url);

    HTTPClient http;
    http.begin(url);
    http.setUserAgent("Book32-ESP32");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("Authorization", String("token ") + GITHUB_TOKEN);
    http.addHeader("Accept", "application/octet-stream");

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        int len = http.getSize();
        Serial.printf("Firmware size: %d bytes\n", len);

        if (!Update.begin(len, U_FLASH)) {
            Serial.println("Not enough space for firmware update");
            http.end();
            return false;
        }

        // Disable all watchdogs during long write operation
        disableCore0WDT();
        disableCore1WDT();
        disableLoopWDT();

        WiFiClient *stream = http.getStreamPtr();
        size_t written = Update.writeStream(*stream);

        // Re-enable watchdogs
        enableCore0WDT();
        enableCore1WDT();
        enableLoopWDT();

        if (written == len) {
            Serial.println("Firmware written successfully");
            if (Update.end()) {
                Serial.println("Firmware update complete");
                http.end();
                if (restartAfter) {
                    Serial.println("Restarting...");
                    ESP.restart();
                }
                return true;
            }
        } else {
            Serial.printf("Firmware write failed. Written: %d / %d\n", written, len);
        }
    } else {
        Serial.printf("Firmware download failed: %d\n", httpCode);
    }
    http.end();
    return false;
}

bool GitHubMgr::performFilesystemUpdate(const char* url, bool restartAfter) {
    if (WiFi.status() != WL_CONNECTED) return false;

    Serial.printf("Downloading filesystem from: %s\n", url);

    HTTPClient http;
    http.begin(url);
    http.setUserAgent("Book32-ESP32");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.addHeader("Authorization", String("token ") + GITHUB_TOKEN);
    http.addHeader("Accept", "application/octet-stream");

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        int len = http.getSize();
        Serial.printf("Filesystem size: %d bytes\n", len);

        // U_SPIFFS is used for both SPIFFS and LittleFS partitions
        if (!Update.begin(len, U_SPIFFS)) {
            Serial.println("Not enough space for filesystem update");
            http.end();
            return false;
        }

        // Disable all watchdogs during long write operation
        disableCore0WDT();
        disableCore1WDT();
        disableLoopWDT();

        WiFiClient *stream = http.getStreamPtr();
        size_t written = Update.writeStream(*stream);

        // Re-enable watchdogs
        enableCore0WDT();
        enableCore1WDT();
        enableLoopWDT();

        if (written == len) {
            Serial.println("Filesystem written successfully");
            if (Update.end()) {
                Serial.println("Filesystem update complete");
                http.end();
                if (restartAfter) {
                    Serial.println("Restarting...");
                    ESP.restart();
                }
                return true;
            }
        } else {
            Serial.printf("Filesystem write failed. Written: %d / %d\n", written, len);
        }
    } else {
        Serial.printf("Filesystem download failed: %d\n", httpCode);
    }
    http.end();
    return false;
}

void GitHubMgr::triggerUpdate(const char* currentVersion) {
    Serial.println("Triggering Update Check...");
    UpdateInfo info = checkUpdate(currentVersion);
    if (info.available && info.hasFirmware) {
        Serial.printf("Update Available: %s. Starting firmware download.\n", info.version.c_str());
        performFirmwareUpdate(info.firmwareUrl.c_str(), true);
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

    // Update firmware first (don't restart yet)
    if (info.hasFirmware) {
        Serial.println("Updating firmware...");
        firmwareUpdated = performFirmwareUpdate(info.firmwareUrl.c_str(), false);
        if (!firmwareUpdated) {
            Serial.println("Firmware update failed!");
            return false;
        }
    }

    // Then update filesystem (don't restart yet)
    if (info.hasFilesystem) {
        Serial.println("Updating filesystem...");
        filesystemUpdated = performFilesystemUpdate(info.filesystemUrl.c_str(), false);
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
        Serial.println("All updates complete. Restarting...");
        ESP.restart();
        return true;
    }

    return false;
}
