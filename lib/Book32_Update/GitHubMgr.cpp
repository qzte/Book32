#include "GitHubMgr.h"
#include <WiFi.h>
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

// Check if a real token is configured (not placeholder)
static bool hasValidToken() {
    String token = GITHUB_TOKEN;
    return token.length() > 10 && !token.startsWith("your_");
}

UpdateInfo GitHubMgr::checkUpdate(const char* currentVersion) {
    UpdateInfo info = {false, "", "", "", "", false, false};

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

    // Only add auth header if we have a valid token
    if (hasValidToken()) {
        http.addHeader("Authorization", String("token ") + GITHUB_TOKEN);
        Serial.println("Using authenticated request");
    } else {
        Serial.println("Using unauthenticated request (public repo)");
    }

    int httpCode = http.GET();
    Serial.printf("HTTP Response: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        DynamicJsonDocument doc(8192);  // Increased size for release notes
        DeserializationError err = deserializeJson(doc, payload);

        if (err) {
            Serial.printf("JSON parse error: %s\n", err.c_str());
            http.end();
            return info;
        }

        const char* tagName = doc["tag_name"];
        info.version = tagName ? tagName : "";
        info.notes = doc["body"].as<String>();

        Serial.printf("Latest version: %s, Current: %s\n", info.version.c_str(), currentVersion);

        // Check if version is different (handle both "1.0.5" and "v1.0.5" formats)
        String currentV = currentVersion;
        String latestV = info.version;
        latestV.replace("v", "");  // Remove 'v' prefix if present

        if (latestV.length() > 0 && latestV != currentV) {
            info.available = true;
            Serial.println("Update IS available");

            // Find asset URLs
            JsonArray assets = doc["assets"];
            for (JsonObject asset : assets) {
                String name = asset["name"].as<String>();

                // Use browser_download_url for public repos (no auth needed)
                // Use url (API endpoint) for private repos (needs auth)
                String url;
                if (hasValidToken()) {
                    url = asset["url"].as<String>();
                } else {
                    url = asset["browser_download_url"].as<String>();
                }

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

bool GitHubMgr::performFirmwareUpdate(const char* url, bool restartAfter) {
    if (WiFi.status() != WL_CONNECTED) return false;

    Serial.printf("Downloading firmware from: %s\n", url);

    HTTPClient http;
    http.begin(url);
    http.setUserAgent("Book32-ESP32");
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setTimeout(30000);  // 30 second timeout for large downloads

    // Only add auth for API URLs (private repos)
    if (hasValidToken()) {
        http.addHeader("Authorization", String("token ") + GITHUB_TOKEN);
    }
    http.addHeader("Accept", "application/octet-stream");

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        int contentLength = http.getSize();
        Serial.printf("Firmware size: %d bytes\n", contentLength);

        if (!Update.begin(contentLength, U_FLASH)) {
            Serial.println("Not enough space for firmware update");
            http.end();
            return false;
        }

        WiFiClient *stream = http.getStreamPtr();
        
        // Use chunked download with periodic yields to prevent watchdog
        uint8_t buff[4096];
        size_t written = 0;
        int lastProgress = 0;
        
        while (written < contentLength) {
            // Read chunk
            size_t available = stream->available();
            if (available == 0) {
                delay(1);  // Yield to other tasks
                continue;
            }
            
            size_t toRead = min(available, sizeof(buff));
            size_t bytesRead = stream->readBytes(buff, toRead);
            
            if (bytesRead > 0) {
                size_t bytesWritten = Update.write(buff, bytesRead);
                if (bytesWritten != bytesRead) {
                    Serial.println("Write error during update");
                    Update.abort();
                    http.end();
                    return false;
                }
                written += bytesWritten;
                
                // Progress and yield every ~10%
                int progress = (written * 100) / contentLength;
                if (progress / 10 > lastProgress / 10) {
                    Serial.printf("Progress: %d%%\n", progress);
                    lastProgress = progress;
                }
                
                // Yield frequently to feed watchdog
                yield();
            }
        }

        if (written == contentLength) {
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
            Serial.printf("Firmware write failed. Written: %d / %d\n", written, contentLength);
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
    http.setTimeout(30000);  // 30 second timeout for large downloads

    // Only add auth for API URLs (private repos)
    if (hasValidToken()) {
        http.addHeader("Authorization", String("token ") + GITHUB_TOKEN);
    }
    http.addHeader("Accept", "application/octet-stream");

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        int contentLength = http.getSize();
        Serial.printf("Filesystem size: %d bytes\n", contentLength);

        // U_SPIFFS is used for both SPIFFS and LittleFS partitions
        if (!Update.begin(contentLength, U_SPIFFS)) {
            Serial.println("Not enough space for filesystem update");
            http.end();
            return false;
        }

        WiFiClient *stream = http.getStreamPtr();
        
        // Use chunked download with periodic yields to prevent watchdog
        uint8_t buff[4096];
        size_t written = 0;
        int lastProgress = 0;
        
        while (written < contentLength) {
            size_t available = stream->available();
            if (available == 0) {
                delay(1);
                continue;
            }
            
            size_t toRead = min(available, sizeof(buff));
            size_t bytesRead = stream->readBytes(buff, toRead);
            
            if (bytesRead > 0) {
                size_t bytesWritten = Update.write(buff, bytesRead);
                if (bytesWritten != bytesRead) {
                    Serial.println("Write error during filesystem update");
                    Update.abort();
                    http.end();
                    return false;
                }
                written += bytesWritten;
                
                int progress = (written * 100) / contentLength;
                if (progress / 10 > lastProgress / 10) {
                    Serial.printf("FS Progress: %d%%\n", progress);
                    lastProgress = progress;
                }
                yield();
            }
        }

        if (written == contentLength) {
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
            Serial.printf("Filesystem write failed. Written: %d / %d\n", written, contentLength);
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
