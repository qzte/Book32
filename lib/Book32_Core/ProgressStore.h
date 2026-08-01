#pragma once
// Book32 v1.8.0 — single owner of /reader_progress.json (EbookFS).
//
// Before this, READER_PROGRESS_PATH was declared in both AppReader.cpp and
// WebMgr.cpp and each side opened and rewrote the file its own way. Import and
// export would have added a third writer. Everything now goes through here.
//
// Three fixes come with the move:
//   * Capacity is derived from the file size instead of a fixed 4096. A library
//     past that size used to truncate on read, and the next save serialised the
//     *already truncated* document over the file, silently losing every other
//     book's position.
//   * Writes are atomic (.tmp + rename), so a power cut mid-write no longer
//     takes the whole library's progress with it.
//   * `updatedAt = millis()` is replaced by the monotonic `seq` counter, which
//     survives reboots. The device has no RTC.
//
// Books are keyed by *original* filename (see BookMeta.h), not by path, so the
// state is portable between devices that truncated the names differently.

#include <Arduino.h>
#include <ArduinoJson.h>
#include <map>
#include <vector>
#include "ProgressMergeLogic.h"
#include "Lock.h"

struct ImportReport {
    bool ok = false;
    int merged = 0;    // local entry existed and the imported one was ahead
    int added = 0;     // book was unknown here
    int pending = 0;   // added but the .epub is not on the device yet
    int skipped = 0;   // local entry was equal or ahead
    String error;      // set when ok == false
};

class ProgressStore {
public:
    static ProgressStore& getInstance();

    // Loads and migrates on first call; cheap afterwards.
    void begin();

    bool get(const String& originalName, BookProgress& out);
    // Bumps `seq`, stores and persists.
    void set(const String& originalName, const BookProgress& progress);
    void remove(const String& originalName);

    void setLast(const String& originalName, bool resumeOnBoot);
    void setResumeOnBoot(bool resume);
    String lastBook();
    bool resumeOnBoot();

    // Drops entries whose .epub is gone (unless pending) and clears `pending`
    // for entries whose file has since arrived. `presentOriginalNames` is the
    // library as enumerated from flash. Writes only if something changed.
    void reconcile(const std::vector<String>& presentOriginalNames);

    void clearAll();

    // Entry count, for callers sizing a JSON document.
    size_t count();

    // Export: fills `dest` with { "<original name>": {chapter, nodeIndex,
    // charOffset, globalPage} }. `pending` and `seq` are device-local and left
    // out on purpose.
    void fillExportJson(JsonObject dest);

    // Import: merges `src` (same shape as fillExportJson) using the
    // further-ahead-page rule. One atomic write for the whole bundle.
    ImportReport applyImportedJson(JsonObjectConst src);

private:
    ProgressStore() {}
    bool load();
    bool save();

    // Serializa todo o acesso ao estado abaixo. O leitor grava a posição a
    // partir do loop principal enquanto os endpoints /api/reader/progress,
    // /api/library/* e /api/books/delete mexem no mesmo mapa a partir da
    // tarefa do servidor. Ver Lock.h.
    Book32Mutex _mutex;

    std::map<String, BookProgress> _books;
    String _lastBook;
    bool _resumeOnBoot = false;
    unsigned long _seq = 0;
    bool _loaded = false;
};
