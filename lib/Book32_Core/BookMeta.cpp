#include "BookMeta.h"
#include "Book32FS.h"
#include <ArduinoJson.h>

static const char* BOOKS_META_PATH = "/books_meta.json";

// Capacity derived from the file, not a fixed 4096: a large library used to
// overflow the fixed document, and a save on top of a truncated document threw
// away everyone else's entries.
static size_t metaCapacityFor(size_t fileSize) {
    size_t cap = fileSize * 2 + 1024;
    if (cap > 24576) cap = 24576;
    return cap;
}

static bool openMetaForRead(File& file) {
    if (SystemFS.exists(BOOKS_META_PATH)) {
        file = SystemFS.open(BOOKS_META_PATH, FILE_READ);
    } else if (EbookFS.exists(BOOKS_META_PATH)) {
        // Older firmware wrote the metadata to the ebook partition.
        file = EbookFS.open(BOOKS_META_PATH, FILE_READ);
    }
    return (bool)file;
}

void loadBookMetadata(std::map<String, String>& metadata) {
    metadata.clear();

    File file;
    if (!openMetaForRead(file)) return;

    DynamicJsonDocument doc(metaCapacityFor(file.size()));
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error || !doc.is<JsonObject>()) return;

    JsonObject obj = doc.as<JsonObject>();
    for (JsonPair pair : obj) {
        metadata[String(pair.key().c_str())] = pair.value().as<String>();
    }
}

String getOriginalFilename(const String& truncatedName) {
    File file;
    if (!openMetaForRead(file)) return truncatedName;

    DynamicJsonDocument doc(metaCapacityFor(file.size()));
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error) return truncatedName;

    if (doc.containsKey(truncatedName)) {
        return doc[truncatedName].as<String>();
    }
    return truncatedName;
}

String findFilenameForOriginal(const String& originalName) {
    std::map<String, String> metadata;
    loadBookMetadata(metadata);

    for (const auto& kv : metadata) {
        if (kv.second == originalName) {
            if (EbookFS.exists("/" + kv.first)) return kv.first;
        }
    }

    // No metadata entry: books that were never truncated are stored under the
    // original name itself.
    if (EbookFS.exists("/" + originalName)) return originalName;
    return "";
}

void saveBookMetadata(const String& truncatedName, const String& originalName) {
    DynamicJsonDocument doc(24576);

    File metaFile = SystemFS.open(BOOKS_META_PATH, FILE_READ);
    if (metaFile) {
        DeserializationError error = deserializeJson(doc, metaFile);
        metaFile.close();
        if (error) {
            Serial.println("Failed to parse metadata, creating new");
            doc.clear();
        }
    }

    doc[truncatedName] = originalName;

    metaFile = SystemFS.open(BOOKS_META_PATH, FILE_WRITE);
    if (metaFile) {
        serializeJson(doc, metaFile);
        metaFile.close();
        Serial.printf("Saved metadata: %s -> %s\n", truncatedName.c_str(), originalName.c_str());
    } else {
        Serial.println("Failed to save metadata");
    }
}

void removeBookMetadata(const String& truncatedName) {
    File metaFile = SystemFS.open(BOOKS_META_PATH, FILE_READ);
    if (!metaFile) return;

    DynamicJsonDocument doc(metaCapacityFor(metaFile.size()));
    DeserializationError error = deserializeJson(doc, metaFile);
    metaFile.close();
    if (error) return;

    doc.remove(truncatedName);

    metaFile = SystemFS.open(BOOKS_META_PATH, FILE_WRITE);
    if (metaFile) {
        serializeJson(doc, metaFile);
        metaFile.close();
    }
}
