#pragma once
#include <LittleFS.h>

// System Filesystem (Partition: spiffs)
// Stores: Web UI, System Config, Metadata
#define SystemFS LittleFS

// Ebook Filesystem (Partition: ebooks)
// Stores: EPUB files, Cover thumbnails
// Declared as an external object, initialized in WebMgr
extern fs::LittleFSFS EbookFS;
