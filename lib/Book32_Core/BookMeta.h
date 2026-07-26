#pragma once
// Book32 v1.8.0 — single owner of /books_meta.json (truncated filename ->
// original long filename).
//
// Was previously implemented twice: three functions in WebMgr.cpp and a static
// loadBookMetadata() in AppReader.cpp. Moved to Core because ProgressStore now
// keys reading progress by *original* name, and Core must not depend on the
// web layer.

#include <Arduino.h>
#include <map>

// Uploads truncate filenames to 28 chars and may add an anti-collision suffix.
// Returns the original long name, or `truncatedName` unchanged when there is no
// metadata entry — which is what makes "original name" a safe key for books
// that predate the metadata file.
String getOriginalFilename(const String& truncatedName);

// Reverse lookup, needed by the state import: original long name -> filename
// as stored on this device. Returns "" when no local file matches.
String findFilenameForOriginal(const String& originalName);

void saveBookMetadata(const String& truncatedName, const String& originalName);
void removeBookMetadata(const String& truncatedName);

// Whole map in one read, for callers that resolve many names at once
// (AppReader::scanBooks, the state export).
void loadBookMetadata(std::map<String, String>& metadata);
