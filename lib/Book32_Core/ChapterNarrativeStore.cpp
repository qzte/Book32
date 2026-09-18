#include "ChapterNarrativeStore.h"
#include "Book32FS.h"

static const char* CHAPTER_NARRATIVE_PATH = "/chapter_narrative.json";

// Uma entrada por capítulo é um único bit no JSON (0/1); bem mais leve que o
// tecto do ChapterTocStore, que guarda strings inteiras.
static const size_t CHAPTER_NARRATIVE_MAX_CAPACITY = 65536; // 64 KB

ChapterNarrativeStore& ChapterNarrativeStore::getInstance() {
    static ChapterNarrativeStore instance;
    return instance;
}

PerBookListStore<bool>& ChapterNarrativeStore::store() {
    // Construído à primeira utilização, como o próprio singleton: os
    // parâmetros de capacidade são os que este store já usava.
    static PerBookListStore<bool> s(SystemFS, CHAPTER_NARRATIVE_PATH, "ChapterNarrativeStore",
                                    CHAPTER_NARRATIVE_MAX_CAPACITY,
                                    /*readSlack=*/512, /*writeBase=*/512,
                                    /*writePerEntry=*/8);
    return s;
}

bool ChapterNarrativeStore::get(const String& originalName, std::vector<bool>& out) {
    return store().get(originalName, out);
}

void ChapterNarrativeStore::set(const String& originalName, const std::vector<bool>& narrative) {
    store().set(originalName, narrative);
}

void ChapterNarrativeStore::reconcile(const std::vector<String>& presentOriginalNames) {
    store().reconcile(presentOriginalNames);
}
