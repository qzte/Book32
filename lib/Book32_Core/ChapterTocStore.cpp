#include "ChapterTocStore.h"
#include "Book32FS.h"

static const char* CHAPTER_TOC_PATH = "/chapter_toc.json";

// Ao contrário do BookTitleStore (um título por livro), aqui cada livro tem
// um array inteiro de títulos, por isso o tecto tem de ser bem maior para
// caber uma biblioteca inteira já indexada.
static const size_t CHAPTER_TOC_MAX_CAPACITY = 131072; // 128 KB

ChapterTocStore& ChapterTocStore::getInstance() {
    static ChapterTocStore instance;
    return instance;
}

PerBookListStore<String>& ChapterTocStore::store() {
    // Construído à primeira utilização, como o próprio singleton: os
    // parâmetros de capacidade são os que este store já usava.
    static PerBookListStore<String> s(SystemFS, CHAPTER_TOC_PATH, "ChapterTocStore", CHAPTER_TOC_MAX_CAPACITY,
                                      /*readSlack=*/1024, /*writeBase=*/1024,
                                      /*writePerEntry=*/96);
    return s;
}

bool ChapterTocStore::get(const String& originalName, std::vector<String>& out) {
    return store().get(originalName, out);
}

void ChapterTocStore::set(const String& originalName, const std::vector<String>& titles) {
    store().set(originalName, titles);
}

void ChapterTocStore::reconcile(const std::vector<String>& presentOriginalNames) {
    store().reconcile(presentOriginalNames);
}
