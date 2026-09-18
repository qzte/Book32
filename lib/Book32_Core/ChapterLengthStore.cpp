#include "ChapterLengthStore.h"
#include "Book32FS.h"

static const char* CHAPTER_LENGTH_PATH = "/chapter_length.json";

// Uma entrada por capítulo é um inteiro (comprimento em caracteres), mais
// pesado que o bit do ChapterNarrativeStore mas mais leve que as strings do
// ChapterTocStore.
static const size_t CHAPTER_LENGTH_MAX_CAPACITY = 65536; // 64 KB

ChapterLengthStore& ChapterLengthStore::getInstance() {
    static ChapterLengthStore instance;
    return instance;
}

PerBookListStore<long>& ChapterLengthStore::store() {
    // Construído à primeira utilização, como o próprio singleton: os
    // parâmetros de capacidade são os que este store já usava.
    static PerBookListStore<long> s(SystemFS, CHAPTER_LENGTH_PATH, "ChapterLengthStore",
                                    CHAPTER_LENGTH_MAX_CAPACITY,
                                    /*readSlack=*/512, /*writeBase=*/512,
                                    /*writePerEntry=*/16);
    return s;
}

bool ChapterLengthStore::get(const String& originalName, std::vector<long>& out) {
    return store().get(originalName, out);
}

void ChapterLengthStore::set(const String& originalName, const std::vector<long>& lengths) {
    store().set(originalName, lengths);
}

void ChapterLengthStore::reconcile(const std::vector<String>& presentOriginalNames) {
    store().reconcile(presentOriginalNames);
}
