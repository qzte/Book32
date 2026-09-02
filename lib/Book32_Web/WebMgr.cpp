#include "WebMgr.h"
#include "../Book32_Core/SettingsStore.h"
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <esp_partition.h>
#include <vector>
#include <map>
#include "../Book32_Core/Book32FS.h"
#include "../Book32_Core/FileExt.h"
#include "../Book32_Core/SafeName.h"
#include "../Book32_Core/UploadGuard.h"
#include "../Book32_Core/DeviceCred.h"
#include "../Book32_Core/BookOrderLogic.h"
#include "../Book32_Core/BookMeta.h"
#include "../Book32_Core/TimeMgr.h"
#include "../Book32_Core/PageCountStore.h"
#include "../Book32_Core/BookStatusLogic.h"
#include "../Book32_Core/ProgressStore.h"
#include "../Book32_Core/BookmarkStore.h"
#include "../Book32_Core/GoToPercentStore.h"
#include "../Book32_Core/ChapterTocStore.h"
#include "../Book32_Core/ChapterNarrativeStore.h"
#include "../Book32_Core/ChapterGuideTypeStore.h"
#include "../Book32_Core/GoToChapterStore.h"
#include "../Book32_Update/GitHubMgr.h"
#include "../Book32_Core/BatteryMgr.h"
#include "../Book32_Core/AppMgr.h"
#include "../Book32_Core/DisplayMgr.h"
#include "../../include/Config.h"

// v1.9.0: HTTP Basic Auth was removed from every endpoint by request.
//
// The web API is now completely open to anyone who can reach the device on
// port 80 — reading status, uploading/deleting books, changing settings,
// joining a WiFi network and triggering OTA all work with no credential.
// Whoever re-adds a login must protect *every* mutating endpoint plus the
// GETs that are mutating by effect (/api/app/switch) or leak network
// topology (/api/wifi/status, /api/wifi/scan), and must also guard the two
// upload *body* handlers, which run before their response handler and would
// otherwise write to flash before a 401 could be sent.
//
// devicePassword() is kept: it is still the SoftAP WPA2 passphrase shown on
// the e-ink footer (see AppMainMenu.cpp), which is the only access control
// left when the device is running as a hotspot.
const char* WebMgr::devicePassword() {
    static char pw[BOOK32_CRED_LEN] = {0};
    if (pw[0] == '\0') {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        deriveDevicePassword(mac, pw, sizeof(pw));
    }
    return pw;
}

WebMgr::WebMgr() {
    server = new AsyncWebServer(80);
}

WebMgr& WebMgr::getInstance() {
    static WebMgr instance;
    return instance;
}

static void listFiles(fs::FS &fs, const char * dirname, uint8_t levels) {
#if BOOK32_VERBOSE_BOOT_LOG
    Serial.printf("Listing directory: %s\n", dirname);
    File root = fs.open(dirname);
    if(!root){
        Serial.println("- failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println("- not a directory");
        root.close();
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
        file.close();
        file = root.openNextFile();
    }
    root.close();
#else
    (void)fs;
    (void)dirname;
    (void)levels;
#endif
}

static bool partitionLooksBlank(const esp_partition_t* partition) {
    if (!partition) return false;

    uint8_t buffer[256];
    size_t bytesToCheck = min((size_t)4096, partition->size);
    for (size_t offset = 0; offset < bytesToCheck; offset += sizeof(buffer)) {
        size_t readLen = min(sizeof(buffer), bytesToCheck - offset);
        if (esp_partition_read(partition, offset, buffer, readLen) != ESP_OK) {
            return false;
        }
        for (size_t i = 0; i < readLen; i++) {
            if (buffer[i] != 0xFF) {
                return false;
            }
        }
    }

    return true;
}

void WebMgr::mountFilesystems() {
#if BOOK32_VERBOSE_BOOT_LOG
    Serial.println("\n========== PARTITION TABLE DUMP ==========");
    
    // Iterate through ALL partitions on the chip
    esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, NULL);
    while (it != NULL) {
        const esp_partition_t* part = esp_partition_get(it);
        Serial.printf("  [%s] type=%d subtype=0x%02X addr=0x%06X size=0x%06X (%dKB)\n",
            part->label,
            part->type,
            part->subtype,
            part->address,
            part->size,
            part->size / 1024);
        it = esp_partition_next(it);
    }
    esp_partition_iterator_release(it);
    
    Serial.println("===========================================\n");
#endif
    Serial.println("=== Mounting Filesystems ===");
    
    // Look for "spiffs" partition specifically
    const esp_partition_t* spiffsPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, "spiffs");
    if (spiffsPart) {
#if BOOK32_VERBOSE_BOOT_LOG
        Serial.printf("Found 'spiffs' partition at 0x%06X, size %dKB\n", spiffsPart->address, spiffsPart->size/1024);
#endif
    } else {
        Serial.println("ERROR: No partition with label 'spiffs' and subtype SPIFFS found!");
        
#if BOOK32_VERBOSE_BOOT_LOG
        // Try to find ANY spiffs-subtype partition
        const esp_partition_t* anySpiffs = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
        if (anySpiffs) {
            Serial.printf("Found unlabeled SPIFFS partition '%s' at 0x%06X\n", anySpiffs->label, anySpiffs->address);
        }
#endif
    }
    
    // Mount SystemFS
    bool sysOK = SystemFS.begin(true, "/littlefs", 10, "spiffs");
    if (sysOK) {
        Serial.printf("SystemFS OK: %u / %u bytes used\n", SystemFS.usedBytes(), SystemFS.totalBytes());
        listFiles(SystemFS, "/", 1);
    } else {
        Serial.println("WARNING: SystemFS mount FAILED!");
    }

    // Look for "ebooks" partition (now uses custom subtype 0x82 to avoid uploadfs conflict)
    const esp_partition_t* ebooksPart = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "ebooks");
    if (ebooksPart) {
#if BOOK32_VERBOSE_BOOT_LOG
        Serial.printf("Found 'ebooks' partition at 0x%06X, size %dKB\n", ebooksPart->address, ebooksPart->size/1024);
#endif
    } else {
        Serial.println("WARNING: No partition with label 'ebooks' found!");
    }

    // Mount EbookFS without formatting first to preserve user data. If the
    // partition is brand-new/blank, format it once so new boards have storage.
    bool ebookOK = EbookFS.begin(false, "/ebooks", 10, "ebooks");
    if (!ebookOK && partitionLooksBlank(ebooksPart)) {
        Serial.println("EbookFS appears blank; formatting first-use ebook storage...");
        ebookOK = EbookFS.begin(true, "/ebooks", 10, "ebooks");
    }

    if (ebookOK) {
        Serial.printf("EbookFS OK: %u / %u bytes used\n", EbookFS.usedBytes(), EbookFS.totalBytes());
        listFiles(EbookFS, "/", 1);

        // Uploads interrompidos deixam ficheiros .part. Como não são listados
        // por /api/books nem abertos pelo leitor, só ocupam espaço — limpar no
        // arranque, que é o único momento em que nenhum upload está em curso.
        {
            std::vector<String> stale;
            File root = EbookFS.open("/");
            if (root && root.isDirectory()) {
                File f = root.openNextFile();
                while (f) {
                    String n = f.name();
                    if (hasExtensionCI(n, ".part")) stale.push_back(n);
                    f.close();
                    f = root.openNextFile();
                }
                root.close();
            }
            for (const String& n : stale) {
                Serial.printf("A remover upload incompleto: %s\n", n.c_str());
                EbookFS.remove("/" + n);
            }
        }
    } else {
        Serial.println("ERROR: EbookFS mount failed! Ebooks partition is not available.");
    }
    
    Serial.println("============================\n");
}

void WebMgr::init() {
    if (_initialized) return;
    if (!_endpointsConfigured) {
        setupEndpoints();
        _endpointsConfigured = true;
    }
    server->begin();
    _initialized = true;
    Serial.println("Web Server Started");

    // A janela em que o WiFi está ligado é a única em que o relógio se pode
    // acertar — o leitor corre com o WiFi desligado. Idempotente, por isso
    // chamar aqui a cada init() não custa nada. Ver TimeMgr.h.
    TimeMgr::getInstance().syncIfNeeded();

    // mDNS: http://book32.local/send funciona sem saber o IP. Falha
    // silenciosamente em redes que bloqueiam multicast — o IP continua a
    // funcionar, por isso isto nunca é fatal.
    if (MDNS.begin("book32")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("mDNS: http://book32.local/");
    } else {
        Serial.println("mDNS: arranque falhou (o IP continua a funcionar)");
    }
}

void WebMgr::stop() {
    if (!_initialized) return;
    MDNS.end();
    server->end();
    _initialized = false;
    Serial.println("Web Server Stopped");
}

void WebMgr::update() {
    // Apply a pending display rotation from the main loop (never draw on the
    // async server task). Repaints whatever app is currently on screen.
    if (_pendingRotation != 0) {
        int rot = _pendingRotation;
        _pendingRotation = 0;
        DisplayMgr::getInstance().setRotation(rot);
        App* current = AppMgr::getInstance().getCurrentApp();
        if (current) current->forceRedraw();
    }

    // Apply a pending reading font-size change from the main loop.
    if (_pendingReaderFontSize != 0) {
        int pt = _pendingReaderFontSize;
        _pendingReaderFontSize = 0;
        for (auto* app : AppMgr::getInstance().getApps()) {
            if (strcmp(app->getName(), "eReader") == 0) {
                app->applyFontSize(pt);
                break;
            }
        }
    }

    // Apply a pending reading font-family change from the main loop.
    if (_pendingReaderFontFamily != -1) {
        int fam = _pendingReaderFontFamily;
        _pendingReaderFontFamily = -1;
        for (auto* app : AppMgr::getInstance().getApps()) {
            if (strcmp(app->getName(), "eReader") == 0) {
                app->applyFontFamily(fam);
                break;
            }
        }
    }

    // Trocar de app a partir do loop principal. switchTo() corre stop(), start()
    // e um draw() completo do e-ink; feito na tarefa do servidor, isso conduzia
    // o mesmo SPI que o loop principal está a usar. Pior ainda no caso do
    // leitor, cujo start() faz WebMgr::stop() + WiFi.mode(WIFI_OFF) — ou seja,
    // desligava o servidor a partir de dentro do handler, antes de a resposta
    // chegar ao cliente. Aqui a resposta já saiu.
    if (_pendingAppSwitch >= 0) {
        int index = _pendingAppSwitch;
        _pendingAppSwitch = -1;
        AppMgr::getInstance().switchTo(index);
    }

    // Check if OTA was requested from web UI
    if (_otaPending) {
        _otaPending = false;
        Serial.println("Scheduling OTA update in separate task...");
        
        // Stop the web server to free up async_tcp and memory
        stop();
        delay(100);  // Give time for connections to close
        
        // Create OTA task with 16KB stack (OTA needs significant stack space)
        xTaskCreatePinnedToCore(
            [](void* param) {
                Serial.println("OTA task started");
                GitHubMgr::getInstance().performFullUpdate(SYSTEM_VERSION);
                Serial.println("OTA task complete, restarting...");
                vTaskDelay(100 / portTICK_PERIOD_MS);
                ESP.restart();
            },
            "OTA_Task",
            16384,  // 16KB stack
            nullptr,
            1,      // Priority
            nullptr,
            1       // Core 1
        );
    }
}

// === Manual book ordering (v1.2.0) ===
// Order persisted in SystemFS /book_order.json as {"order":["a.epub","b.epub"]}.
// Merge rule: files present in the order list come first (in list order);
// files on the FS but absent from the list are appended in FS enumeration order.
static const char* BOOK_ORDER_PATH = "/book_order.json";

static void loadBookOrder(std::vector<String>& order) {
    order.clear();
    File f = SystemFS.open(BOOK_ORDER_PATH, FILE_READ);
    if (!f) return;
    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return;
    JsonArray arr = doc["order"].as<JsonArray>();
    for (JsonVariant v : arr) {
        order.push_back(v.as<String>());
    }
}

static void saveBookOrder(const std::vector<String>& order) {
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.createNestedArray("order");
    for (const String& s : order) arr.add(s);
    File f = SystemFS.open(BOOK_ORDER_PATH, FILE_WRITE);
    if (f) {
        serializeJson(doc, f);
        f.close();
    }
}

static void removeFromBookOrder(const String& filename) {
    std::vector<String> order;
    loadBookOrder(order);
    bool changed = false;
    for (auto it = order.begin(); it != order.end();) {
        if (*it == filename) { it = order.erase(it); changed = true; }
        else ++it;
    }
    if (changed) saveBookOrder(order);
}

// Merge: ordered entries that still exist first, then the rest in FS order.
// Logic shared with AppReader::scanBooks() via BookOrderLogic.h
// (host test: tools/tests/test_book_order.cpp).
static void applyBookOrder(const std::vector<String>& order, std::vector<String>& fsNames) {
    applyBookOrderT(order, fsNames,
        [](const String& item, const String& key) { return item == key; });
}

static String jsonEscape(const String& s);

// Diagnóstico (/api/fs): percorre a partição inteira, não só os ficheiros que
// /api/books mostra. O indicador de espaço vem de usedBytes(), que conta tudo —
// covers órfãos, .part em subpastas, import.tmp — por isso a lista de livros
// pode mostrar 3 entradas com a partição quase cheia.
//
// A recursão fecha o handle do directório antes de descer: o LittleFS do ESP32
// tem poucos handles simultâneos e uma árvore funda esgotava-os.
static void streamFsTree(AsyncResponseStream* out, fs::FS& fs, const String& dir,
                         uint8_t depth, bool& first, size_t& totalSize, size_t& count) {
    File root = fs.open(dir);
    if (!root) return;
    if (!root.isDirectory()) { root.close(); return; }

    std::vector<String> subdirs;
    File f = root.openNextFile();
    while (f) {
        String path = f.path();
        if (f.isDirectory()) {
            if (depth) subdirs.push_back(path);
        } else {
            size_t sz = f.size();
            totalSize += sz;
            count++;
            if (!first) out->print(",");
            first = false;
            out->printf("{\"path\":\"%s\",\"size\":%u}", jsonEscape(path).c_str(), (unsigned)sz);
        }
        f.close();
        f = root.openNextFile();
    }
    root.close();

    for (const String& sub : subdirs) {
        streamFsTree(out, fs, sub, depth - 1, first, totalSize, count);
    }
}

// Minimal JSON string escaping for streaming serialization.
static String jsonEscape(const String& s) {
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if ((uint8_t)c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

// v1.8.0: progress lives behind ProgressStore, keyed by original filename.
void removeBookProgress(const String& filename) {
    ProgressStore::getInstance().remove(getOriginalFilename(filename));
}

// Estado de um upload em curso. Partilhado entre o body handler (que decide) e
// o response handler (que reporta). Como o estado é global e o LittleFS é
// single-writer, o endpoint é single-flight: `owner` marca o pedido que detém
// o estado e qualquer outro pedido concorrente é recusado com 409 sem tocar
// no upload activo. `Idle` é o estado "ninguém a usar" — distinto de
// `WriteFailed`, que significa mesmo erro de escrita.
enum class UploadStatus { Idle, Ok, BadExtension, UnsafeName, NoSpace, WriteFailed };

struct UploadState {
    UploadStatus status = UploadStatus::Idle;
    AsyncWebServerRequest* owner = nullptr;
    File file;
    String path;
    String tempPath;
    String finalName;
    String originalName;

    void reset() {
        if (file) file.close();
        status = UploadStatus::Idle;
        owner = nullptr;
        path = "";
        tempPath = "";
        finalName = "";
        originalName = "";
    }
};

static UploadState g_uploadState;

// === Library state import (v1.8.0) ===
static const char* IMPORT_TMP_PATH = "/import.tmp";
static const size_t IMPORT_MAX_BYTES = 64 * 1024;

// Single-flight, same reasoning as the ebook upload: LittleFS is a single
// writer and the state is global.
struct ImportState {
    AsyncWebServerRequest* owner = nullptr;
    File file;
    size_t size = 0;
    bool received = false;
    bool tooBig = false;

    void reset() {
        if (file) file.close();
        owner = nullptr;
        size = 0;
        received = false;
        tooBig = false;
    }
};

static ImportState g_importState;

struct ImportOutcome {
    bool ok = false;
    ImportReport report;
    int metaAdded = 0;
    bool orderApplied = false;
    String error;
};

static ImportOutcome applyImportBundle(const char* path) {
    ImportOutcome outcome;

    File f = EbookFS.open(path, FILE_READ);
    if (!f) {
        outcome.error = "ficheiro temporario ilegivel";
        return outcome;
    }

    DynamicJsonDocument doc(f.size() * 2 + 2048);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        outcome.error = String("JSON invalido: ") + err.c_str();
        return outcome;
    }

    // An unrecognised schema is refused whole: half-applying a bundle written
    // by a future firmware would be worse than rejecting it.
    int schema = doc["book32"]["schema"] | 0;
    if (!isSupportedSchema(schema)) {
        outcome.error = "schema do bundle nao suportado";
        return outcome;
    }

    outcome.report = ProgressStore::getInstance().applyImportedJson(
        doc["progress"].as<JsonObjectConst>());
    if (!outcome.report.ok) {
        outcome.error = outcome.report.error;
        return outcome;
    }

    // Metadata is only applied to files that exist here and have no entry yet:
    // importing another device's mapping for absent files would invent wrong
    // associations.
    JsonObjectConst meta = doc["meta"].as<JsonObjectConst>();
    if (!meta.isNull()) {
        std::map<String, String> local;
        loadBookMetadata(local);
        for (JsonPairConst kv : meta) {
            String original = kv.value().as<String>();
            if (original.length() == 0) continue;
            String filename = findFilenameForOriginal(original);
            if (filename.length() == 0) continue;
            if (local.find(filename) != local.end()) continue;
            saveBookMetadata(filename, original);
            outcome.metaAdded++;
        }
    }

    // Order arrives as original names; store it as local filenames, dropping
    // books that are not on this device.
    JsonArrayConst order = doc["order"].as<JsonArrayConst>();
    if (!order.isNull()) {
        std::vector<String> localOrder;
        for (JsonVariantConst v : order) {
            String filename = findFilenameForOriginal(v.as<String>());
            if (filename.length() > 0) localOrder.push_back(filename);
        }
        if (!localOrder.empty()) {
            saveBookOrder(localOrder);
            outcome.orderApplied = true;
        }
    }

    outcome.ok = true;
    return outcome;
}

void WebMgr::setupEndpoints() {
    // API: Status
    server->on("/api/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(512);

        unsigned long totalSeconds = millis() / 1000;
        unsigned long hours = totalSeconds / 3600;
        unsigned long minutes = (totalSeconds % 3600) / 60;
        unsigned long seconds = totalSeconds % 60;
        char uptimeStr[20];
        snprintf(uptimeStr, sizeof(uptimeStr), "%luh %lum %lus", hours, minutes, seconds);
        doc["uptime"] = uptimeStr;
        doc["uptimeSeconds"] = totalSeconds;

        doc["rssi"] = WiFi.RSSI();
        doc["battery"] = BatteryMgr::getInstance().getPercentage();
        doc["voltage"] = BatteryMgr::getInstance().getVoltage();
        doc["charging"] = BatteryMgr::getInstance().isCharging();
        doc["version"] = SYSTEM_VERSION;

        doc["freeSpace"] = EbookFS.totalBytes() - EbookFS.usedBytes();
        doc["totalSpace"] = EbookFS.totalBytes();
        doc["usedSpace"] = EbookFS.usedBytes();
        doc["systemFree"] = SystemFS.totalBytes() - SystemFS.usedBytes();

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: diagnóstico de armazenamento. Lista *todos* os ficheiros das duas
    // partições com o tamanho real, ao contrário de /api/books, que só mostra
    // .epub e .ttf na raiz. `used` menos `accounted` é o custo de metadados e
    // arredondamento do LittleFS; uma diferença grande entre `accounted` e o
    // que se vê na lista de livros é espaço preso em ficheiros órfãos.
    server->on("/api/fs", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");

        struct Target { const char* key; fs::FS* fs; size_t used; size_t total; };
        Target targets[] = {
            { "ebooks", &EbookFS,  EbookFS.usedBytes(),  EbookFS.totalBytes()  },
            { "system", &SystemFS, SystemFS.usedBytes(), SystemFS.totalBytes() },
        };

        response->print("{");
        bool firstTarget = true;
        for (Target& t : targets) {
            if (!firstTarget) response->print(",");
            firstTarget = false;
            response->printf("\"%s\":{\"total\":%u,\"used\":%u,\"files\":[",
                             t.key, (unsigned)t.total, (unsigned)t.used);
            bool first = true;
            size_t accounted = 0;
            size_t count = 0;
            streamFsTree(response, *t.fs, "/", 4, first, accounted, count);
            response->printf("],\"accounted\":%u,\"fileCount\":%u}",
                             (unsigned)accounted, (unsigned)count);
        }
        response->print("}");

        request->send(response);
    });

    // Página de envio dedicada: caminho curto e memorizável para o atalho no
    // ecrã principal do telemóvel. Desde a v1.9.0 nem a página nem o POST
    // para /api/books/upload pedem credenciais.
    server->on("/send", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (SystemFS.exists("/send.html")) {
            request->send(SystemFS, "/send.html", "text/html");
        } else {
            request->send(404, "text/plain", "send.html nao encontrado - correr uploadfs");
        }
    });

    // API: List Books from EbookFS
    // v1.2.0: streamed serialization (no fixed 2KB buffer — previously books
    // beyond the buffer were silently truncated) + manual ordering applied.
    server->on("/api/books", HTTP_GET, [](AsyncWebServerRequest *request) {
        // 1) Enumerate FS: books (.epub) and fonts (.ttf) separately.
        std::vector<String> epubs, fonts;
        File root = EbookFS.open("/");
        if (root && root.isDirectory()) {
            File file = root.openNextFile();
            while (file) {
                String name = file.name();
                if (hasExtensionCI(name, ".epub")) epubs.push_back(name);
                else if (hasExtensionCI(name, ".ttf")) fonts.push_back(name);
                file.close();
                file = root.openNextFile();
            }
            root.close();
        }

        // 2) Apply manual order to books; fonts stay appended in FS order.
        std::vector<String> order;
        loadBookOrder(order);
        applyBookOrder(order, epubs);
        for (const String& f : fonts) epubs.push_back(f);

        // 3) Reading state for the same list. Both stores are already in-RAM
        //    maps, so this costs a lookup per book and no extra flash reads —
        //    which is why the status rides on this response instead of a second
        //    endpoint the UI would have to join against.
        ReaderSettings rs = SettingsStore::getInstance().loadReader();
        ProgressStore& progress = ProgressStore::getInstance();
        PageCountStore& counts = PageCountStore::getInstance();

        // 4) Stream JSON directly — O(1) memory w.r.t. number of books.
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->print("{\"books\":[");
        bool first = true;
        for (const String& name : epubs) {
            File f = EbookFS.open("/" + name, FILE_READ);
            size_t sz = f ? f.size() : 0;
            if (f) f.close();
            if (!first) response->print(",");
            first = false;

            String original = getOriginalFilename(name);
            response->printf("{\"name\":\"%s\",\"filename\":\"%s\",\"size\":%u", jsonEscape(original).c_str(),
                             jsonEscape(name).c_str(), (unsigned)sz);

            // Reading state only for books. This list also carries the
            // uploaded .ttf fonts (appended above), and a font reported as
            // "unread" would be a lie the UI would then have to filter back
            // out.
            if (hasExtensionCI(name, ".epub")) {
                BookProgress p;
                bool hasEntry = progress.get(original, p);
                // 0 when this book has not been counted through yet, or was
                // counted at font settings that are no longer in use.
                // deriveStatus treats that as "percent unknown", never as
                // "finished".
                int totalPages = counts.get(original, rs.fontSize, rs.fontFamily);
                BookStatusView view = deriveStatus(hasEntry, p.override, p.globalPage, totalPages);

                response->printf(",\"status\":\"%s\",\"override\":\"%s\"", statusKey(view.status),
                                 overrideKey(p.override));
                // null, not 0: "unknown" and "at the very start" are different
                // things and the UI renders them differently.
                if (view.percent >= 0)
                    response->printf(",\"percent\":%d", view.percent);
                else
                    response->print(",\"percent\":null");
                response->printf(",\"startedAt\":%lu,\"finishedAt\":%lu,\"lastReadAt\":%lu",
                                 (unsigned long)p.startedAt, (unsigned long)p.finishedAt,
                                 (unsigned long)p.lastReadAt);
            }
            response->print("}");
        }
        response->print("],");
        // Lets the UI say "the device has no clock yet" instead of showing bare
        // dashes for books that are genuinely undated.
        response->printf("\"clockSynced\":%s}", TimeMgr::getInstance().isSynced() ? "true" : "false");
        request->send(response);
    });

    // API: manual status override. Body:
    //   {"filename":"livro.epub","status":"read","at":1756480000}
    //
    // `at` is the *browser's* clock. The device may have none (no NTP since the
    // last power cut), and the moment you mark a book read is exactly when a
    // good timestamp is guaranteed to be at hand — see ProgressStore::setOverride.
    AsyncCallbackJsonWebHandler* bookStatusHandler = new AsyncCallbackJsonWebHandler(
        "/api/books/status", [](AsyncWebServerRequest* request, JsonVariant& json) {
            JsonObject body = json.as<JsonObject>();
            if (body.isNull()) {
                request->send(400, "application/json",
                              "{\"status\":\"error\",\"message\":\"corpo invalido\"}");
                return;
            }

            String filename = body["filename"] | "";
            // Same allow-list as every other endpoint that takes a book name:
            // this one only ever keys a map, but the name is persisted and read
            // back by code that does build paths from it.
            if (!isSafeBookName(filename) || !hasExtensionCI(filename, ".epub")) {
                request->send(400, "application/json",
                              "{\"status\":\"error\",\"message\":\"nome invalido\"}");
                return;
            }

            StatusOverride override = StatusOverride::Auto;
            if (!parseOverride(body["status"] | "", override)) {
                request->send(400, "application/json",
                              "{\"status\":\"error\",\"message\":\"estado invalido\"}");
                return;
            }

            if (!EbookFS.exists("/" + filename)) {
                request->send(404, "application/json",
                              "{\"status\":\"error\",\"message\":\"livro nao encontrado\"}");
                return;
            }

            uint32_t at = body["at"] | 0UL;
            // A browser with a badly wrong clock (or a hand-written request)
            // must not be able to plant an absurd date in the history. Below
            // 2020 is not a real date; well past now is not either.
            uint32_t deviceNow = TimeMgr::getInstance().nowOrZero();
            if (at != 0 && (at < TIME_PLAUSIBLE_EPOCH || (deviceNow != 0 && at > deviceNow + 86400UL))) {
                at = 0;
            }

            String original = getOriginalFilename(filename);
            if (!ProgressStore::getInstance().setOverride(original, override, at)) {
                request->send(500, "application/json",
                              "{\"status\":\"error\",\"message\":\"falha ao gravar\"}");
                return;
            }
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        });
    bookStatusHandler->setMethod(HTTP_POST);
    server->addHandler(bookStatusHandler);

    // API (v1.2.0): Save manual book order. Body: {"order":["a.epub","b.epub"]}
    AsyncCallbackJsonWebHandler* bookOrderHandler = new AsyncCallbackJsonWebHandler("/api/books/order",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            JsonArray arr = json["order"].as<JsonArray>();
            if (arr.isNull()) {
                request->send(400, "text/plain", "Missing 'order' array");
                return;
            }
            std::vector<String> order;
            for (JsonVariant v : arr) {
                String name = v.as<String>();
                // v1.4.1 (security): the order list is persisted and later
                // used to build paths, so apply the same allow-list.
                if (isSafeBookName(name) && hasExtensionCI(name, ".epub")) {
                    order.push_back(name);
                }
            }
            saveBookOrder(order);
            request->send(200, "application/json", "{\"ok\":true}");
        });
    bookOrderHandler->setMethod(HTTP_POST);
    server->addHandler(bookOrderHandler);

    // API: Upload Book to EbookFS
    //
    // Estado partilhado entre o body handler e o response handler. O
    // ESPAsyncWebServer corre o body handler primeiro, por isso o veredito
    // aqui guardado já está decidido quando a resposta é montada. Uploads são
    // servidos em série (o LittleFS é single-writer), logo `static` é seguro.
    server->on("/api/books/upload", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (g_uploadState.owner != request) {
                if (g_uploadState.owner != nullptr) {
                    // Outro upload detém o estado: recusar sem lhe tocar.
                    request->send(409, "application/json",
                        "{\"ok\":false,\"error\":\"outro envio em curso - tenta daqui a pouco\"}");
                } else {
                    // O body handler nunca correu para este pedido (parte
                    // multipart sem filename): não há nada a reportar.
                    request->send(400, "application/json",
                        "{\"ok\":false,\"error\":\"pedido sem ficheiro\"}");
                }
                return;
            }

            switch (g_uploadState.status) {
                case UploadStatus::Ok: {
                    String body = "{\"ok\":true,\"name\":\"" +
                                  jsonEscape(g_uploadState.finalName) + "\"}";
                    request->send(200, "application/json", body);
                    break;
                }
                case UploadStatus::BadExtension:
                    request->send(415, "application/json",
                        "{\"ok\":false,\"error\":\"tipo de ficheiro nao suportado\"}");
                    break;
                case UploadStatus::UnsafeName:
                    request->send(400, "application/json",
                        "{\"ok\":false,\"error\":\"nome de ficheiro invalido\"}");
                    break;
                case UploadStatus::NoSpace:
                    request->send(507, "application/json",
                        "{\"ok\":false,\"error\":\"sem espaco na particao de ebooks\"}");
                    break;
                case UploadStatus::WriteFailed:
                default:
                    request->send(500, "application/json",
                        "{\"ok\":false,\"error\":\"falha a escrever no armazenamento\"}");
                    break;
            }
            // Libertar o estado: o corpo JSON já foi construído acima. Sem
            // isto o endpoint ficaria trancado para sempre e um POST sem
            // ficheiro herdaria o veredito do upload anterior.
            g_uploadState.reset();
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0) {
                // Single-flight: se outro pedido detém o estado, sair sem
                // tocar em nada — nem reset(), que fecharia o File dele.
                if (g_uploadState.owner != nullptr && g_uploadState.owner != request) {
                    return;
                }
                g_uploadState.reset();
                g_uploadState.owner = request;

                // A ligação pode cair a meio do corpo: o `final` nunca chega e
                // o estado ficaria trancado. Este callback também dispara
                // depois de uma resposta normal, mas aí o response handler já
                // limpou o `owner`, por isso a guarda torna-o inofensivo.
                request->onDisconnect([request]() {
                    if (g_uploadState.owner == request) {
                        if (g_uploadState.file) g_uploadState.file.close();
                        if (g_uploadState.tempPath.length()) EbookFS.remove(g_uploadState.tempPath);
                        g_uploadState.reset();
                    }
                });

                String safeName = filename;
                int lastSlash = safeName.lastIndexOf('/');
                if (lastSlash >= 0) safeName = safeName.substring(lastSlash + 1);
                lastSlash = safeName.lastIndexOf('\\');
                if (lastSlash >= 0) safeName = safeName.substring(lastSlash + 1);

                if (safeName.length() > 28) {
                    int dotPos = safeName.lastIndexOf('.');
                    String ext = (dotPos != -1) ? safeName.substring(dotPos) : "";
                    safeName = safeName.substring(0, 28 - ext.length()) + ext;
                }

                size_t freeBytes = EbookFS.totalBytes() - EbookFS.usedBytes();
                switch (checkUpload(safeName, request->contentLength(), freeBytes)) {
                    case UploadVerdict::BadExtension:
                        g_uploadState.status = UploadStatus::BadExtension;
                        return;
                    case UploadVerdict::UnsafeName:
                        g_uploadState.status = UploadStatus::UnsafeName;
                        return;
                    case UploadVerdict::NoSpace:
                        g_uploadState.status = UploadStatus::NoSpace;
                        return;
                    case UploadVerdict::Ok:
                        break;
                }

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

                g_uploadState.finalName = safeName;
                g_uploadState.originalName = filename;
                int origSlash = g_uploadState.originalName.lastIndexOf('/');
                if (origSlash >= 0) g_uploadState.originalName = g_uploadState.originalName.substring(origSlash + 1);
                origSlash = g_uploadState.originalName.lastIndexOf('\\');
                if (origSlash >= 0) g_uploadState.originalName = g_uploadState.originalName.substring(origSlash + 1);

                g_uploadState.path = "/" + safeName;
                Serial.printf("Upload Start: %s (original: %s)\n",
                              g_uploadState.path.c_str(), filename.c_str());
                g_uploadState.tempPath = g_uploadState.path + ".part";
                g_uploadState.file = EbookFS.open(g_uploadState.tempPath, FILE_WRITE);
                if (!g_uploadState.file) {
                    g_uploadState.status = UploadStatus::WriteFailed;
                    return;
                }
                g_uploadState.status = UploadStatus::Ok;
            }

            // Chunks de um pedido que não é o dono são descartados sem tocar
            // no ficheiro. Um chunk depois de um erro também: nada foi aberto.
            if (g_uploadState.owner != request) return;
            if (g_uploadState.status != UploadStatus::Ok) return;

            if (g_uploadState.file && len) {
                if (g_uploadState.file.write(data, len) != len) {
                    Serial.println("Upload: write failed (disco cheio?)");
                    g_uploadState.file.close();
                    EbookFS.remove(g_uploadState.tempPath);
                    g_uploadState.status = UploadStatus::WriteFailed;
                    return;
                }
            }

            if (final && g_uploadState.file) {
                g_uploadState.file.close();
                if (!EbookFS.rename(g_uploadState.tempPath, g_uploadState.path)) {
                    Serial.println("Upload: rename do .part falhou");
                    EbookFS.remove(g_uploadState.tempPath);
                    g_uploadState.status = UploadStatus::WriteFailed;
                    return;
                }
                saveBookMetadata(g_uploadState.finalName, g_uploadState.originalName);
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

        // v1.4.1 (security): reject path separators, ".." and unknown
        // extensions before building a filesystem path. Without this,
        // ?name=../spiffs/index.html escaped the ebooks root.
        if (!isSafeBookName(filename)) {
            Serial.printf("Rejected unsafe delete request: %s\n", filename.c_str());
            request->send(400, "text/plain", "Invalid name");
            return;
        }

        String path = "/" + filename;

        if (EbookFS.exists(path)) {
            if (EbookFS.remove(path)) {
                Serial.printf("Deleted: %s\n", path.c_str());
                removeBookMetadata(filename);
                removeBookProgress(filename);
                removeFromBookOrder(filename);
                // Ficheiros derivados em /covers. A extensão é retirada pela
                // posição do último ponto, não por String::replace(".epub"):
                // esse substituía a primeira ocorrência em qualquer sítio do
                // nome ("a.epub.v2.epub" perdia a errada) e só cobria o caso
                // minúsculas para o .thumb, deixando lixo para trás nos
                // ficheiros ".EPUB".
                int dot = filename.lastIndexOf('.');
                String base = (dot > 0) ? filename.substring(0, dot) : filename;
                const char* derivedExts[] = { ".thumb", ".cover", ".cover2" };
                for (const char* ext : derivedExts) {
                    String derived = "/covers/" + base + ext;
                    if (EbookFS.exists(derived)) EbookFS.remove(derived);
                }
                request->send(200, "text/plain", "Deleted");
            } else {
                request->send(500, "text/plain", "Delete failed");
            }
        } else {
            request->send(404, "text/plain", "Not found");
        }
    });

    // API: Check for Updates
    server->on("/api/check_update", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(1024);

        Serial.println("Checking for updates...");
        UpdateInfo info = GitHubMgr::getInstance().checkUpdate(SYSTEM_VERSION);

        doc["hasUpdate"] = info.available;
        doc["latest"] = info.version;
        doc["current"] = SYSTEM_VERSION;
        doc["hasFirmware"] = info.hasFirmware;
        doc["hasFilesystem"] = info.hasFilesystem;
        doc["release_notes"] = info.notes;

        if (info.available) {
            Serial.printf("Update available: %s\n", info.version.c_str());
        } else {
            Serial.println("No update available");
        }

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: Perform Full Update (firmware + filesystem)
    // Sets flag to perform OTA from main loop (avoids blocking async_tcp)
    server->on("/api/update/all", HTTP_POST, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain", "Update scheduled - will start in a moment");
        Serial.println("OTA update requested via web UI, scheduling...");
        WebMgr::getInstance()._otaPending = true;
    });

    // API: Reader Settings - GET
    server->on("/api/settings/reader", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(256);

        ReaderSettings s = SettingsStore::getInstance().loadReader();
        doc["refreshFrequency"] = s.refreshFrequency;
        doc["fontSize"] = s.fontSize;
        doc["fontFamily"] = s.fontFamily;

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: Reader Settings - POST
    AsyncCallbackJsonWebHandler* readerSettingsHandler = new AsyncCallbackJsonWebHandler("/api/settings/reader",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            // Merge into the existing config so one setting doesn't wipe the
            // other. Clamping lives in SettingsStore, shared with the
            // on-device settings menu.
            //
            // A transacção cobre o ler-modificar-gravar inteiro: o menu de
            // definições no dispositivo escreve o mesmo ficheiro a partir do
            // loop principal, e sem isto a última gravação apagava a
            // alteração da outra tarefa.
            SettingsStore::Transaction tx;
            SettingsStore& store = SettingsStore::getInstance();
            ReaderSettings s = store.loadReader();

            if (json.containsKey("refreshFrequency")) {
                s.refreshFrequency = json["refreshFrequency"].as<int>();
            }
            if (json.containsKey("fontSize")) {
                s.fontSize = SettingsStore::clampFontSize(json["fontSize"].as<int>());
                // Apply live from the main loop if a book is open.
                WebMgr::getInstance()._pendingReaderFontSize = s.fontSize;
            }
            if (json.containsKey("fontFamily")) {
                s.fontFamily = SettingsStore::clampFontFamily(json["fontFamily"].as<int>());
                // Apply live from the main loop if a book is open.
                WebMgr::getInstance()._pendingReaderFontFamily = s.fontFamily;
            }

            if (store.saveReader(s)) {
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to save\"}");
            }
        }
    );
    server->addHandler(readerSettingsHandler);

    // API: Display Settings (orientation) - GET
    server->on("/api/settings/display", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(128);

        doc["rotation"] = SettingsStore::getInstance().loadDisplay().rotation;

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: Display Settings (orientation) - POST. Applies OS-wide; persisted on
    // EbookFS so it survives OTA updates.
    AsyncCallbackJsonWebHandler* displaySettingsHandler = new AsyncCallbackJsonWebHandler("/api/settings/display",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            DisplaySettings s;
            s.rotation = SettingsStore::clampRotation(json["rotation"] | 3);

            if (SettingsStore::getInstance().saveDisplay(s)) {
                // Apply from the main loop (never rotate/draw on the async task).
                WebMgr::getInstance()._pendingRotation = s.rotation;
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to save\"}");
            }
        }
    );
    server->addHandler(displaySettingsHandler);

    // API: Reader Progress - GET
    server->on("/api/reader/progress", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(1024);

        ProgressStore& store = ProgressStore::getInstance();
        String last = store.lastBook();

        doc["exists"] = last.length() > 0;
        doc["lastBook"] = last;
        doc["displayName"] = last;   // v1.8.0: the key already is the original name
        doc["resumeOnBoot"] = store.resumeOnBoot();

        BookProgress p;
        if (last.length() > 0 && store.get(last, p)) {
            doc["chapter"] = p.chapter;
            doc["page"] = p.globalPage;
        }

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: Reader Progress - DELETE
    server->on("/api/reader/progress", HTTP_DELETE, [](AsyncWebServerRequest *request) {
        ProgressStore::getInstance().clearAll();
        request->send(200, "application/json", "{\"status\":\"ok\"}");
    });

    // API (v1.14.0): Bookmarks — named, saved reading positions distinct from
    // the resume-on-open position above. Managed entirely from the web UI:
    // adding one snapshots whatever position ProgressStore currently holds
    // for that book, and jumping to one overwrites that saved position, so it
    // takes effect the next time the book is opened on the device (same
    // "web sets it, device picks it up on next open" shape as library
    // reorder and progress import already use). See
    // docs/plans/2026-08-29-bookmarks-and-goto-percent-design.md.
    server->on("/api/bookmarks", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("book")) {
            request->send(400, "application/json", "{\"error\":\"missing 'book'\"}");
            return;
        }
        String book = request->getParam("book")->value();
        std::vector<Bookmark> marks = BookmarkStore::getInstance().list(book);

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        response->printf("{\"book\":\"%s\",\"bookmarks\":[", jsonEscape(book).c_str());
        bool first = true;
        for (const Bookmark& b : marks) {
            if (!first) response->print(",");
            first = false;
            response->printf("{\"seq\":%lu,\"label\":\"%s\",\"chapter\":%d,\"page\":%d}", b.seq,
                             jsonEscape(b.label).c_str(), b.chapter, b.globalPage);
        }
        response->print("]}");
        request->send(response);
    });

    // API: Add bookmark. Body: {"book":"<original name>","label":"..."}.
    // Fails with 404 if the book has no saved position yet (nothing to
    // snapshot) and 409 once it already holds BookmarkStore::MAX_BOOKMARKS_PER_BOOK.
    AsyncCallbackJsonWebHandler* bookmarkAddHandler = new AsyncCallbackJsonWebHandler(
        "/api/bookmarks/add", [](AsyncWebServerRequest* request, JsonVariant& json) {
            String book = json["book"] | "";
            String label = json["label"] | "";
            if (book.length() == 0) {
                request->send(400, "application/json", "{\"error\":\"missing 'book'\"}");
                return;
            }
            BookProgress pos;
            if (!ProgressStore::getInstance().get(book, pos)) {
                request->send(404, "application/json",
                              "{\"error\":\"no saved reading position for this book yet\"}");
                return;
            }
            unsigned long seq = BookmarkStore::getInstance().add(book, label, pos.chapter, pos.nodeIndex,
                                                                 pos.charOffset, pos.globalPage);
            if (seq == 0) {
                request->send(409, "application/json",
                              "{\"error\":\"bookmark limit reached for this book\"}");
                return;
            }
            request->send(200, "application/json", "{\"status\":\"ok\",\"seq\":" + String(seq) + "}");
        });
    bookmarkAddHandler->setMethod(HTTP_POST);
    server->addHandler(bookmarkAddHandler);

    // API: Remove bookmark. Body: {"book":"...","seq":N}.
    AsyncCallbackJsonWebHandler* bookmarkRemoveHandler = new AsyncCallbackJsonWebHandler(
        "/api/bookmarks/remove", [](AsyncWebServerRequest* request, JsonVariant& json) {
            String book = json["book"] | "";
            unsigned long seq = json["seq"] | 0UL;
            if (book.length() == 0 || seq == 0) {
                request->send(400, "application/json", "{\"error\":\"missing 'book' or 'seq'\"}");
                return;
            }
            bool ok = BookmarkStore::getInstance().remove(book, seq);
            if (ok)
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            else
                request->send(404, "application/json", "{\"error\":\"bookmark not found\"}");
        });
    bookmarkRemoveHandler->setMethod(HTTP_POST);
    server->addHandler(bookmarkRemoveHandler);

    // API: Jump to bookmark. Body: {"book":"...","seq":N}. Overwrites the
    // book's saved progress with the bookmark's position — same field, same
    // store AppReader::loadBookProgress() reads on open.
    AsyncCallbackJsonWebHandler* bookmarkJumpHandler = new AsyncCallbackJsonWebHandler(
        "/api/bookmarks/jump", [](AsyncWebServerRequest* request, JsonVariant& json) {
            String book = json["book"] | "";
            unsigned long seq = json["seq"] | 0UL;
            if (book.length() == 0 || seq == 0) {
                request->send(400, "application/json", "{\"error\":\"missing 'book' or 'seq'\"}");
                return;
            }
            std::vector<Bookmark> marks = BookmarkStore::getInstance().list(book);
            for (const Bookmark& b : marks) {
                if (b.seq != seq) continue;
                BookProgress p;
                p.chapter = b.chapter;
                p.nodeIndex = b.nodeIndex;
                p.charOffset = b.charOffset;
                p.globalPage = b.globalPage;
                ProgressStore::getInstance().set(book, p);
                request->send(200, "application/json", "{\"status\":\"ok\"}");
                return;
            }
            request->send(404, "application/json", "{\"error\":\"bookmark not found\"}");
        });
    bookmarkJumpHandler->setMethod(HTTP_POST);
    server->addHandler(bookmarkJumpHandler);

    // API (v1.14.0): Go to approximate percent. Body: {"book":"...","percent":N}.
    // Computing the target position needs the EPUB's text (to know how much
    // precedes it), which only the reader app parses — the web server just
    // records the intent; AppReader applies it the next time this book opens
    // (see GoToPercentStore, AppReader::startPercentSeek/updatePercentSeek).
    AsyncCallbackJsonWebHandler* goToPercentHandler = new AsyncCallbackJsonWebHandler(
        "/api/reader/goto", [](AsyncWebServerRequest* request, JsonVariant& json) {
            String book = json["book"] | "";
            int percent = json["percent"] | -1;
            if (book.length() == 0 || percent < 0 || percent > 100) {
                request->send(400, "application/json", "{\"error\":\"invalid 'book' or 'percent'\"}");
                return;
            }
            GoToPercentStore::getInstance().setPending(book, percent);
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        });
    goToPercentHandler->setMethod(HTTP_POST);
    server->addHandler(goToPercentHandler);

    // API (v1.18.0): Table of contents — chapter titles built on-device from
    // headings already detected while parsing each chapter (see
    // EpubLoader::getChapterTitle), cached in ChapterTocStore once a book has
    // been opened on the device and the background scan has finished (see
    // BookIndexer). "ready":false with an empty "chapters" array means that
    // hasn't happened yet — same "not known yet" shape as the page-count
    // fields elsewhere in this API.
    //
    // "narrative" (added by the non-narrative chapter filtering work) is
    // read from the sibling ChapterNarrativeStore, classified from the OPF
    // <guide> (see EpubLoader::isChapterNarrative) — cover/toc/title-page/
    // copyright-page/etc. entries come back false so the web UI can grey
    // them out or skip them in "ir para capítulo", without touching the
    // chapter *indices* themselves (those still match the EPUB spine 1:1,
    // exactly as ProgressStore/PageCountStore/GoToPercentLogic expect). A
    // book indexed before this field existed has no ChapterNarrativeStore
    // entry yet, so every chapter defaults to true (narrative) — the same
    // "show everything" behaviour as before this feature, until the book is
    // reindexed. See docs/plans/2026-09-01-filtrar-capitulos-nao-narrativos-design.md.
    //
    // "guideType" is the sibling ChapterGuideTypeStore's raw OPF <guide>
    // type ("cover", "title-page", ...) for the same entry, "" when the
    // chapter is narrative or has no <guide> reference — the web UI uses it
    // to label a non-narrative entry that has no detected title (e.g. "Capa"
    // instead of the generic "Capítulo N") instead of deciding a Portuguese
    // label here, same "web decides the label" split as the rest of this
    // endpoint.
    server->on("/api/toc", HTTP_GET, [](AsyncWebServerRequest* request) {
        if (!request->hasParam("book")) {
            request->send(400, "application/json", "{\"error\":\"missing 'book'\"}");
            return;
        }
        String book = request->getParam("book")->value();
        std::vector<String> titles;
        bool ready = ChapterTocStore::getInstance().get(book, titles);
        std::vector<bool> narrative;
        ChapterNarrativeStore::getInstance().get(book, narrative); // ok if this stays empty
        std::vector<String> guideTypes;
        ChapterGuideTypeStore::getInstance().get(book, guideTypes); // ok if this stays empty

        AsyncResponseStream* response = request->beginResponseStream("application/json");
        response->printf("{\"book\":\"%s\",\"ready\":%s,\"chapters\":[", jsonEscape(book).c_str(),
                         ready ? "true" : "false");
        for (size_t i = 0; i < titles.size(); i++) {
            if (i > 0) response->print(",");
            bool isNarrative = (i < narrative.size()) ? narrative[i] : true;
            String guideType = (i < guideTypes.size()) ? guideTypes[i] : "";
            response->printf("{\"index\":%d,\"title\":\"%s\",\"narrative\":%s,\"guideType\":\"%s\"}", (int)i,
                             jsonEscape(titles[i]).c_str(), isNarrative ? "true" : "false",
                             jsonEscape(guideType).c_str());
        }
        response->print("]}");
        request->send(response);
    });

    // API (v1.18.0): Go to an exact chapter. Body: {"book":"...","chapter":N}.
    // Unlike /api/reader/goto (percent), this needs no background scan on
    // open: the index comes straight from the /api/toc list this UI already
    // fetched, so AppReader can jump to it immediately the next time the
    // book opens (see GoToChapterStore, AppReader::applyChapterJump).
    // Validated against ChapterTocStore's own chapter count, same as the
    // percent endpoint validates its range — a request for a book with no
    // index yet, or a chapter past the end, is rejected rather than queued.
    AsyncCallbackJsonWebHandler* goToChapterHandler = new AsyncCallbackJsonWebHandler(
        "/api/reader/goto-chapter", [](AsyncWebServerRequest* request, JsonVariant& json) {
            String book = json["book"] | "";
            int chapter = json["chapter"] | -1;
            if (book.length() == 0 || chapter < 0) {
                request->send(400, "application/json", "{\"error\":\"invalid 'book' or 'chapter'\"}");
                return;
            }
            std::vector<String> titles;
            if (!ChapterTocStore::getInstance().get(book, titles) || chapter >= (int)titles.size()) {
                request->send(404, "application/json",
                              "{\"error\":\"no chapter index for this book yet, or chapter out of range\"}");
                return;
            }
            GoToChapterStore::getInstance().setPending(book, chapter);
            request->send(200, "application/json", "{\"status\":\"ok\"}");
        });
    goToChapterHandler->setMethod(HTTP_POST);
    server->addHandler(goToChapterHandler);

    // API (v1.8.0): Export library state — reading progress, original-name
    // metadata and the manual order. No .epub files: those go through /send.
    //
    // Keyed by original filename throughout, so importing on a device that
    // truncated the names differently still matches the right books.
    //
    // Como todos os endpoints desde a v1.9.0, é aberto: quem chegar à porta 80
    // pode exportar o estado da biblioteca.
    server->on("/api/library/export", HTTP_GET, [](AsyncWebServerRequest *request) {
        std::map<String, String> metadata;
        loadBookMetadata(metadata);

        std::vector<String> order;
        loadBookOrder(order);

        // Sized from the actual entry counts rather than a fixed buffer — the
        // same lesson as the /api/books truncation fixed in v1.2.0.
        size_t capacity = 1024
                        + ProgressStore::getInstance().count() * 224
                        + metadata.size() * 160
                        + order.size() * 96;
        DynamicJsonDocument doc(capacity);

        JsonObject header = doc.createNestedObject("book32");
        header["schema"] = PROGRESS_SCHEMA_CURRENT;
        header["version"] = SYSTEM_VERSION;

        ProgressStore::getInstance().fillExportJson(doc.createNestedObject("progress"));

        JsonObject meta = doc.createNestedObject("meta");
        for (const auto& kv : metadata) {
            // key = original name, value = original name, so the bundle stays
            // readable on a device that truncates differently.
            meta[kv.second] = kv.second;
        }

        JsonArray arr = doc.createNestedArray("order");
        for (const String& filename : order) arr.add(getOriginalFilename(filename));

        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->addHeader("Content-Disposition", "attachment; filename=\"book32-state.json\"");
        serializeJson(doc, *response);
        request->send(response);
    });

    // API (v1.8.0): Import library state.
    //
    // Streamed to a temp file instead of AsyncCallbackJsonWebHandler: that
    // handler buffers the whole body in RAM before the callback runs, and a
    // library-sized bundle risks exhausting the heap mid-request.
    server->on("/api/library/import", HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (g_importState.owner != nullptr && g_importState.owner != request) {
                request->send(409, "application/json",
                              "{\"status\":\"error\",\"message\":\"import em curso\"}");
                return;
            }
            if (g_importState.tooBig) {
                g_importState.reset();
                request->send(413, "application/json",
                              "{\"status\":\"error\",\"message\":\"bundle acima de 64 KB\"}");
                return;
            }
            if (!g_importState.received) {
                g_importState.reset();
                request->send(400, "application/json",
                              "{\"status\":\"error\",\"message\":\"nenhum ficheiro recebido\"}");
                return;
            }

            ImportOutcome outcome = applyImportBundle(IMPORT_TMP_PATH);
            g_importState.reset();
            EbookFS.remove(IMPORT_TMP_PATH);

            if (!outcome.ok) {
                AsyncResponseStream *r = request->beginResponseStream("application/json");
                DynamicJsonDocument doc(256);
                doc["status"] = "error";
                doc["message"] = outcome.error;
                serializeJson(doc, *r);
                request->send(r);
                return;
            }

            AsyncResponseStream *response = request->beginResponseStream("application/json");
            DynamicJsonDocument doc(256);
            doc["status"] = "ok";
            doc["merged"] = outcome.report.merged;
            doc["added"] = outcome.report.added;
            doc["pending"] = outcome.report.pending;
            doc["skipped"] = outcome.report.skipped;
            doc["orderApplied"] = outcome.orderApplied;
            doc["metaAdded"] = outcome.metaAdded;
            serializeJson(doc, *response);
            request->send(response);
        },
        [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (index == 0) {
                if (g_importState.owner != nullptr && g_importState.owner != request) return;
                g_importState.reset();
                g_importState.owner = request;

                request->onDisconnect([request]() {
                    if (g_importState.owner == request) {
                        if (g_importState.file) g_importState.file.close();
                        EbookFS.remove(IMPORT_TMP_PATH);
                        g_importState.reset();
                    }
                });

                EbookFS.remove(IMPORT_TMP_PATH);
                g_importState.file = EbookFS.open(IMPORT_TMP_PATH, FILE_WRITE);
            }

            if (g_importState.owner != request) return;

            g_importState.size += len;
            if (g_importState.size > IMPORT_MAX_BYTES) {
                g_importState.tooBig = true;
                if (g_importState.file) g_importState.file.close();
                EbookFS.remove(IMPORT_TMP_PATH);
                return;
            }

            if (g_importState.file && len) g_importState.file.write(data, len);

            if (final) {
                if (g_importState.file) g_importState.file.close();
                g_importState.received = !g_importState.tooBig;
            }
        });

    // API: Sleep Settings - GET
    server->on("/api/settings/sleep", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(512);

        SleepSettings s = SettingsStore::getInstance().loadSleep();
        doc["sleepTimeout"] = s.timeout;
        doc["sleepMessage"] = s.message;

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: Sleep Settings - POST
    AsyncCallbackJsonWebHandler* sleepSettingsHandler = new AsyncCallbackJsonWebHandler("/api/settings/sleep",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            // Merge, so posting only one key doesn't blank the other.
            // Transacção pela mesma razão do handler do leitor, fechada antes
            // de avisar o BatteryMgr: assim nunca se detêm dois bloqueios ao
            // mesmo tempo e não há ordem de aquisição para respeitar.
            bool saved;
            {
                SettingsStore::Transaction tx;
                SettingsStore& store = SettingsStore::getInstance();
                SleepSettings s = store.loadSleep();

                if (json.containsKey("sleepTimeout")) {
                    s.timeout = json["sleepTimeout"].as<int>();
                }
                if (json.containsKey("sleepMessage")) {
                    s.message = json["sleepMessage"].as<String>();
                }

                saved = store.saveSleep(s);
            }

            if (saved) {
                // Notify BatteryMgr to reload settings
                BatteryMgr::getInstance().loadSleepSettings();
                request->send(200, "application/json", "{\"status\":\"ok\"}");
            } else {
                request->send(500, "application/json", "{\"status\":\"error\",\"message\":\"Failed to save\"}");
            }
        }
    );
    server->addHandler(sleepSettingsHandler);

    // API: Switch to app by name
    server->on("/api/app/switch", HTTP_GET, [](AsyncWebServerRequest *request) {
        if (!request->hasParam("name")) {
            request->send(400, "application/json", "{\"error\":\"App name required\"}");
            return;
        }

        String appName = request->getParam("name")->value();
        AppMgr& appMgr = AppMgr::getInstance();

        int appIndex = -1;
        int idx = 0;
        for (auto* app : appMgr.getApps()) {
            if (appName.equalsIgnoreCase(app->getName())) {
                appIndex = idx;
                break;
            }
            idx++;
        }

        if (appIndex >= 0) {
            // Só agenda: a troca é executada por update(), no loop principal.
            WebMgr::getInstance()._pendingAppSwitch = appIndex;
            request->send(200, "application/json", "{\"status\":\"ok\"}");
            Serial.printf("App switch scheduled: %s\n", appName.c_str());
        } else {
            request->send(404, "application/json", "{\"error\":\"App not found\"}");
        }
    });

    // === WIFI / HOTSPOT API ENDPOINTS ===

    // API: WiFi status - station connection + hotspot (AP) state
    server->on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(512);

        bool sta = WiFi.status() == WL_CONNECTED;
        doc["sta_connected"] = sta;
        doc["sta_ssid"] = sta ? WiFi.SSID() : String("");
        doc["sta_ip"] = sta ? WiFi.localIP().toString() : String("");
        doc["rssi"] = sta ? WiFi.RSSI() : 0;

        wifi_mode_t mode = WiFi.getMode();
        bool ap = (mode == WIFI_AP || mode == WIFI_AP_STA);
        doc["ap_active"] = ap;
        doc["ap_ssid"] = ap ? WiFi.softAPSSID() : String("");
        doc["ap_ip"] = ap ? WiFi.softAPIP().toString() : String("");

        serializeJson(doc, *response);
        request->send(response);
    });

    // API: WiFi scan - async so async_tcp keeps running. First call kicks off a
    // scan and returns 202; the client polls until results are ready.
    server->on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) {
            request->send(202, "application/json", "{\"status\":\"scanning\"}");
            return;
        }
        if (n == WIFI_SCAN_FAILED) {
            WiFi.scanNetworks(true);  // start async scan
            request->send(202, "application/json", "{\"status\":\"scanning\"}");
            return;
        }

        // n >= 0: results available
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        DynamicJsonDocument doc(4096);
        JsonArray arr = doc.createNestedArray("networks");
        for (int i = 0; i < n && i < 20; i++) {
            JsonObject net = arr.createNestedObject();
            net["ssid"] = WiFi.SSID(i);
            net["rssi"] = WiFi.RSSI(i);
            net["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        }
        serializeJson(doc, *response);
        request->send(response);
        WiFi.scanDelete();
    });

    // API: WiFi connect - join a network. Credentials persist to NVS so the
    // device reconnects automatically on the next boot.
    AsyncCallbackJsonWebHandler* wifiConnectHandler = new AsyncCallbackJsonWebHandler("/api/wifi/connect",
        [](AsyncWebServerRequest *request, JsonVariant &json) {
            String ssid = json["ssid"].as<String>();
            String password = json["password"] | "";

            if (ssid.length() == 0) {
                request->send(400, "application/json", "{\"error\":\"SSID is required\"}");
                return;
            }

            // Keep the AP up (AP_STA) so the phone stays connected to the page
            // while the station connection is attempted.
            wifi_mode_t mode = WiFi.getMode();
            if (mode == WIFI_AP) WiFi.mode(WIFI_AP_STA);
            else if (mode == WIFI_OFF) WiFi.mode(WIFI_STA);

            Serial.printf("WiFi connect requested via web: %s\n", ssid.c_str());
            WiFi.begin(ssid.c_str(), password.c_str());

            request->send(200, "application/json", "{\"status\":\"connecting\"}");
        }
    );
    server->addHandler(wifiConnectHandler);

    // Static Files - serve from SystemFS first (where OTA filesystem updates go)
    // Fall back to EbookFS if not found
    if (SystemFS.exists("/index.html")) {
        Serial.println("Serving web UI from SystemFS");
        server->serveStatic("/", SystemFS, "/").setDefaultFile("index.html");
    } else if (EbookFS.exists("/index.html")) {
        Serial.println("Serving web UI from EbookFS");
        server->serveStatic("/", EbookFS, "/").setDefaultFile("index.html");
    } else {
        Serial.println("WARNING: No index.html found on either filesystem!");
    }
}
