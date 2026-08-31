#include "GoToChapterStore.h"

GoToChapterStore& GoToChapterStore::getInstance() {
    static GoToChapterStore instance;
    return instance;
}

void GoToChapterStore::setPending(const String& originalName, int chapterIndex) {
    Book32Guard guard(_mutex);
    _book = originalName;
    _chapterIndex = (chapterIndex < 0) ? 0 : chapterIndex;
    _hasPending = originalName.length() > 0;
}

bool GoToChapterStore::takePendingFor(const String& originalName, int& outChapterIndex) {
    Book32Guard guard(_mutex);
    if (!_hasPending || _book != originalName) return false;
    outChapterIndex = _chapterIndex;
    _hasPending = false;
    _book = "";
    return true;
}
