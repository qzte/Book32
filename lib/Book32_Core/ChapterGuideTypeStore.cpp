#include "ChapterGuideTypeStore.h"
#include "Book32FS.h"

static const char* CHAPTER_GUIDE_TYPE_PATH = "/chapter_guide_type.json";

// Tipos do <guide> são palavras curtas ("cover", "title-page", ...), bem
// mais leves que os títulos do ChapterTocStore, mas ainda strings — tecto a
// meio caminho entre o do ChapterNarrativeStore (bools) e o do
// ChapterTocStore.
static const size_t CHAPTER_GUIDE_TYPE_MAX_CAPACITY = 65536; // 64 KB

ChapterGuideTypeStore& ChapterGuideTypeStore::getInstance() {
    static ChapterGuideTypeStore instance;
    return instance;
}

PerBookListStore<String>& ChapterGuideTypeStore::store() {
    // Construído à primeira utilização, como o próprio singleton: os
    // parâmetros de capacidade são os que este store já usava.
    static PerBookListStore<String> s(SystemFS, CHAPTER_GUIDE_TYPE_PATH, "ChapterGuideTypeStore",
                                      CHAPTER_GUIDE_TYPE_MAX_CAPACITY,
                                      /*readSlack=*/512, /*writeBase=*/512,
                                      /*writePerEntry=*/24);
    return s;
}

bool ChapterGuideTypeStore::get(const String& originalName, std::vector<String>& out) {
    return store().get(originalName, out);
}

void ChapterGuideTypeStore::set(const String& originalName, const std::vector<String>& guideTypes) {
    store().set(originalName, guideTypes);
}

void ChapterGuideTypeStore::reconcile(const std::vector<String>& presentOriginalNames) {
    store().reconcile(presentOriginalNames);
}
