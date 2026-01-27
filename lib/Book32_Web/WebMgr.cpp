#include "WebMgr.h"
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <LittleFS.h>
#include <JPEGDEC.h>
#include "../Book32_Core/Book32FS.h"
#include "../Book32_Update/GitHubMgr.h"
#include "../Book32_Core/BatteryMgr.h"
#include "../Book32_Core/TimeMgr.h"
#include "../Apps/AppReader/EpubLoader.h"
#include "../../include/Config.h"

// Cover thumbnail dimensions
static const int THUMB_WIDTH = 60;
static const int THUMB_HEIGHT = 80;
static const int THUMB_SIZE = (THUMB_WIDTH * THUMB_HEIGHT + 7) / 8;  // 600 bytes

// Queue for deferred cover extraction (runs in separate task)
static String g_pendingCoverPath = "";
static TaskHandle_t g_coverTaskHandle = nullptr;

// Globals for JPEG decoder callback
static uint8_t* g_thumbBitmap = nullptr;
static int g_srcWidth = 0;
static int g_srcHeight = 0;

// JPEG decoder callback - scales and dithers to 1-bit bitmap
static int thumbDrawCallback(JPEGDRAW *pDraw) {
    if (!g_thumbBitmap) return 0;

    float scaleX = (float)THUMB_WIDTH / g_srcWidth;
    float scaleY = (float)THUMB_HEIGHT / g_srcHeight;

    for (int y = 0; y < pDraw->iHeight; y++) {
        int srcY = pDraw->y + y;
        int dstY = (int)(srcY * scaleY);
        if (dstY >= THUMB_HEIGHT) continue;

        for (int x = 0; x < pDraw->iWidth; x++) {
            int srcX = pDraw->x + x;
            int dstX = (int)(srcX * scaleX);
            if (dstX >= THUMB_WIDTH) continue;

            // Get pixel (RGB565 format)
            uint16_t pixel = pDraw->pPixels[y * pDraw->iWidth + x];

            // Convert RGB565 to grayscale
            int r = ((pixel >> 11) & 0x1F) << 3;
            int g = ((pixel >> 5) & 0x3F) << 2;
            int b = (pixel & 0x1F) << 3;
            int gray = (r * 30 + g * 59 + b * 11) / 100;

            // Ordered dithering threshold
            int threshold = 128 + ((dstX + dstY) % 2) * 32 - 16;

            // Set bit in bitmap (1 = black, 0 = white)
            int byteIndex = (dstY * THUMB_WIDTH + dstX) / 8;
            int bitIndex = 7 - ((dstY * THUMB_WIDTH + dstX) % 8);

            if (gray < threshold) {
                g_thumbBitmap[byteIndex] |= (1 << bitIndex);  // Black
            }
        }
    }
    return 1;
}

// Extract cover from EPUB and save as thumbnail (runs in task context)
static void extractAndSaveCover(const String& epubPath) {
    Serial.printf("Extracting cover from: %s\n", epubPath.c_str());

    // Create covers directory if needed on EbookFS
    if (!EbookFS.exists("/covers")) {
        EbookFS.mkdir("/covers");
    }

    // Open EPUB and get cover data
    EpubLoader loader;
    if (!loader.open(epubPath.c_str())) {
        Serial.println("Failed to open EPUB for cover extraction");
        return;
    }

    size_t coverSize = 0;
    uint8_t* coverData = loader.getCoverData(&coverSize);
    loader.close();

    if (!coverData || coverSize == 0) {
        Serial.println("No cover found in EPUB");
        return;
    }

    Serial.printf("Cover data: %d bytes, decoding...\n", coverSize);

    // Allocate thumbnail bitmap in PSRAM
    g_thumbBitmap = (uint8_t*)ps_malloc(THUMB_SIZE);
    if (!g_thumbBitmap) {
        g_thumbBitmap = (uint8_t*)malloc(THUMB_SIZE);
    }
    if (!g_thumbBitmap) {
        Serial.println("Failed to allocate thumbnail buffer");
        free(coverData);
        return;
    }
    memset(g_thumbBitmap, 0, THUMB_SIZE);  // White background

    // Decode JPEG
    JPEGDEC jpeg;
    if (!jpeg.openRAM(coverData, coverSize, thumbDrawCallback)) {
        Serial.println("Failed to open JPEG");
        free(coverData);
        free(g_thumbBitmap);
        g_thumbBitmap = nullptr;
        return;
    }

    g_srcWidth = jpeg.getWidth();
    g_srcHeight = jpeg.getHeight();
    Serial.printf("JPEG: %dx%d -> %dx%d\n", g_srcWidth, g_srcHeight, THUMB_WIDTH, THUMB_HEIGHT);

    jpeg.setPixelType(RGB565_BIG_ENDIAN);
    jpeg.decode(0, 0, 0);
    jpeg.close();
    free(coverData);

    // Generate thumbnail path from epub path
    String thumbPath = "/covers" + epubPath;
    thumbPath.replace(".epub", ".thumb");

    // Save thumbnail to EbookFS
    File thumbFile = EbookFS.open(thumbPath, FILE_WRITE);
    if (thumbFile) {
        thumbFile.write(g_thumbBitmap, THUMB_SIZE);
        thumbFile.close();
        Serial.printf("Thumbnail saved: %s (%d bytes)\n", thumbPath.c_str(), THUMB_SIZE);
    } else {
        Serial.println("Failed to save thumbnail");
    }

    free(g_thumbBitmap);
    g_thumbBitmap = nullptr;
}

// FreeRTOS task for cover extraction (runs with larger stack)
static void coverExtractionTask(void* param) {
    String path = *((String*)param);
    Serial.printf("Cover extraction task started for: %s\n", path.c_str());

    vTaskDelay(500 / portTICK_PERIOD_MS);  // Brief delay to let upload complete
    extractAndSaveCover(path);

    Serial.println("Cover extraction task completed");
    g_coverTaskHandle = nullptr;
    vTaskDelete(NULL);
}

// Queue cover extraction to run in separate task
static void queueCoverExtraction(const String& epubPath) {
    // Store path for task
    g_pendingCoverPath = epubPath;

    // Create task with 32KB stack (UNZIP uses ~41KB internal buffer + JPEG decoding)
    xTaskCreatePinnedToCore(
        coverExtractionTask,
        "CoverExtract",
        32768,  // 32KB stack
        &g_pendingCoverPath,
        1,  // Low priority
        &g_coverTaskHandle,
        1   // Core 1
    );
}

WebMgr::WebMgr() {
    server = new AsyncWebServer(80);
}

WebMgr& WebMgr::getInstance() {
    static WebMgr instance;
    return instance;
}

static void listFiles(fs::FS &fs, const char * dirname, uint8_t levels) {
    Serial.printf("Listing directory: %s\n", dirname);
    File root = fs.open(dirname);
    if(!root){
        Serial.println("- failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println("- not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.printf("  DIR : %s\n", file.name());
            if(levels){
                listFiles(fs, file.path(), levels -1);
            }
        } else {
            Serial.printf("  FILE: %s  SIZE: %d\n", file.name(), file.size());
        }
        file = root.openNextFile();
    }
}

void WebMgr::mountFilesystems() {
    // 1. Mount System Partition (Primary LittleFS instance)
    // Label: spiffs, Mount: / (No prefix for server compatibility)
    // Try to begin with format-on-fail to ensure it works
    if(!SystemFS.begin(true, "/", 10, "spiffs")) {
        Serial.println("SystemFS Mount Failed! Partition labeled 'spiffs' not found or corrupt.");
    } else {
        Serial.println("SystemFS mounted successfully at /.");
    }

    // 2. Mount Ebook Partition (Secondary LittleFS instance)
    // Label: ebooks, Mount: /ebooks
    if(!EbookFS.begin(false, "/ebooks", 10, "ebooks")) {
        Serial.println("EbookFS Mount Failed, formatting (expected on first run)...");
        if(!EbookFS.begin(true, "/ebooks", 10, "ebooks")) {
            Serial.println("EbookFS definitely failed.");
        }
    } else {
        Serial.println("EbookFS mounted successfully at /ebooks.");
    }

    Serial.println("\n--- Filesystem Map ---");
    Serial.printf("SystemFS: %u / %u bytes used\n", SystemFS.usedBytes(), SystemFS.totalBytes());
    listFiles(SystemFS, "/", 1);
    Serial.printf("EbookFS : %u / %u bytes used\n", EbookFS.usedBytes(), EbookFS.totalBytes());
    listFiles(EbookFS, "/", 1);
    Serial.println("----------------------\n");
}

void WebMgr::init() {
    setupEndpoints();
    server->begin();
    Serial.println("Web Server Started");
}

// Helper: Save original filename to metadata
void saveBookMetadata(const String& truncatedName, const String& originalName) {
    DynamicJsonDocument doc(4096);
    
    // Load existing metadata if it exists from SystemFS
    File metaFile = SystemFS.open("/books_meta.json", FILE_READ);
    if (metaFile) {
        DeserializationError error = deserializeJson(doc, metaFile);
        metaFile.close();
        if (error) {
            Serial.println("Failed to parse metadata, creating new");
            doc.clear();
        }
    }
    
    // Add/update this book's metadata
    doc[truncatedName] = originalName;
    
    // Save back to file on SystemFS
    metaFile = SystemFS.open("/books_meta.json", FILE_WRITE);
    if (metaFile) {
        serializeJson(doc, metaFile);
        metaFile.close();
        Serial.printf("Saved metadata: %s -> %s\n", truncatedName.c_str(), originalName.c_str());
    } else {
        Serial.println("Failed to save metadata");
    }
}

// Helper: Get original filename from metadata
String getOriginalFilename(const String& truncatedName) {
    File metaFile = SystemFS.open("/books_meta.json", FILE_READ);
    if (!metaFile) {
        return truncatedName; // No metadata file, return as-is
    }
    
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, metaFile);
    metaFile.close();
    
    if (error) {
        return truncatedName; // Parse error, return as-is
    }
    
    // Return original name if found, otherwise return truncated name
    if (doc.containsKey(truncatedName)) {
        return doc[truncatedName].as<String>();
    }
    return truncatedName;
}

// Helper: Remove book from metadata
void removeBookMetadata(const String& truncatedName) {
    File metaFile = SystemFS.open("/books_meta.json", FILE_READ);
    if (!metaFile) return; // No metadata file
    
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, metaFile);
    metaFile.close();
    
    if (error) return;
    
    // Remove the entry
    doc.remove(truncatedName);
    
    // Save back to SystemFS
    metaFile = SystemFS.open("/books_meta.json", FILE_WRITE);
    if (metaFile) {
        serializeJson(doc, metaFile);
        metaFile.close();
    }
}

void WebMgr::setupEndpoints() {
    // API: Status
    server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(512);

        // Format uptime as hours:minutes:seconds
        unsigned long totalSeconds = millis() / 1000;
        unsigned long hours = totalSeconds / 3600;
        unsigned long minutes = (totalSeconds % 3600) / 60;
        unsigned long seconds = totalSeconds % 60;
        char uptimeStr[20];
        snprintf(uptimeStr, sizeof(uptimeStr), "%luh %lum %lus", hours, minutes, seconds);
        doc["uptime"] = uptimeStr;
        doc["uptimeSeconds"] = totalSeconds;


        doc["rssi"] = WiFi.RSSI();

        // Get Real Battery Data
        doc["battery"] = BatteryMgr::getInstance().getPercentage();
        doc["voltage"] = BatteryMgr::getInstance().getVoltage();
        doc["charging"] = BatteryMgr::getInstance().isCharging();
        doc["version"] = SYSTEM_VERSION;
        doc["time"] = TimeMgr::getInstance().getFormattedTime();

        // Filesystem info (Combined)
        doc["freeSpace"] = EbookFS.totalBytes() - EbookFS.usedBytes();
        doc["totalSpace"] = EbookFS.totalBytes();
        doc["usedSpace"] = EbookFS.usedBytes();
        
        doc["systemFree"] = SystemFS.totalBytes() - SystemFS.usedBytes();

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: List Books from EbookFS
    server->on("/api/books", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(2048);
        JsonArray books = doc.createNestedArray("books");

        File root = EbookFS.open("/");
        if (root && root.isDirectory()) {
            File file = root.openNextFile();
            while (file) {
                String name = file.name();
                if (name.endsWith(".epub") || name.endsWith(".ttf")) {
                    JsonObject book = books.createNestedObject();
                    // Get original filename from metadata (SystemFS)
                    String displayName = getOriginalFilename(name);
                    book["name"] = displayName;  // Display name (original)
                    book["filename"] = name;      // Actual filesystem name (truncated)
                    book["size"] = file.size();
                }
                file = root.openNextFile();
            }
        }

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: Upload Book to EbookFS
    server->on("/api/books/upload", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            request->send(200, "text/plain", "Upload Complete");
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            static File uploadFile;
            static String savedPath;
            static String originalFilename;

            if (index == 0) {
                // Store original filename
                originalFilename = filename;
                
                // Sanitize filename - LittleFS has ~31 char limit
                String safeName = filename;

                // Remove path separators if any
                int lastSlash = safeName.lastIndexOf('/');
                if (lastSlash >= 0) safeName = safeName.substring(lastSlash + 1);
                lastSlash = safeName.lastIndexOf('\\');
                if (lastSlash >= 0) safeName = safeName.substring(lastSlash + 1);

                // If filename too long, truncate but keep extension
                if (safeName.length() > 28) {
                    int dotPos = safeName.lastIndexOf('.');
                    String ext = (dotPos != -1) ? safeName.substring(dotPos) : "";
                    safeName = safeName.substring(0, 28 - ext.length()) + ext;
                }

                // Check for filename collision and add suffix if needed
                String testPath = "/" + safeName;
                if (EbookFS.exists(testPath)) {
                    int dotPos = safeName.lastIndexOf('.');
                    String baseName = (dotPos != -1) ? safeName.substring(0, dotPos) : safeName;
                    String ext = (dotPos != -1) ? safeName.substring(dotPos) : "";
                    
                    if (baseName.length() > 20) baseName = baseName.substring(0, 20);

                    int suffix = 1;
                    while (suffix < 100) {
                        safeName = baseName + "_" + String(suffix) + ext;
                        testPath = "/" + safeName;
                        if (!EbookFS.exists(testPath)) break;
                        suffix++;
                    }
                }

                savedPath = "/" + safeName;
                Serial.printf("Upload Start: %s (original: %s)\n", savedPath.c_str(), filename.c_str());
                uploadFile = EbookFS.open(savedPath, FILE_WRITE);
                if (!uploadFile) return;
            }

            if (uploadFile && len) {
                uploadFile.write(data, len);
            }

            if (final) {
                if (uploadFile) {
                    uploadFile.close();
                    String truncatedName = savedPath.substring(1); 
                    String origName = originalFilename;
                    int lastSlash = origName.lastIndexOf('/');
                    if (lastSlash >= 0) origName = origName.substring(lastSlash + 1);
                    lastSlash = origName.lastIndexOf('\\');
                    if (lastSlash >= 0) origName = origName.substring(lastSlash + 1);

                    saveBookMetadata(truncatedName, origName);

                    if (savedPath.endsWith(".epub")) queueCoverExtraction(savedPath);
                }
            }
        }
    );

    // API: Delete Book from EbookFS
    server->on("/api/books/delete", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("name")) {
            request->send(400, "text/plain", "Missing name param");
            return;
        }

        String filename = request->getParam("name")->value();
        String path = "/" + filename;

        if (EbookFS.exists(path)) {
            if (EbookFS.remove(path)) {
                Serial.printf("Deleted: %s\n", path.c_str());
                removeBookMetadata(filename);
                String thumbPath = "/covers/" + filename;
                thumbPath.replace(".epub", ".thumb");
                if (EbookFS.exists(thumbPath)) EbookFS.remove(thumbPath);
                request->send(200, "text/plain", "Deleted");
            } else {
                request->send(500, "text/plain", "Delete failed");
            }
        } else {
            request->send(404, "text/plain", "Not found");
        }
    });

    // Static Files from SystemFS
    server->serveStatic("/", SystemFS, "/").setDefaultFile("index.html");
}

void WebMgr::update() {}
