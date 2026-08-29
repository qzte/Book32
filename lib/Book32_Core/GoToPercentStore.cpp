#include "GoToPercentStore.h"

GoToPercentStore& GoToPercentStore::getInstance() {
    static GoToPercentStore instance;
    return instance;
}

void GoToPercentStore::setPending(const String& originalName, int percent) {
    Book32Guard guard(_mutex);
    int clamped = percent;
    if (clamped < 0) clamped = 0;
    if (clamped > 100) clamped = 100;
    _book = originalName;
    _percent = clamped;
    _hasPending = originalName.length() > 0;
}

bool GoToPercentStore::takePendingFor(const String& originalName, int& outPercent) {
    Book32Guard guard(_mutex);
    if (!_hasPending || _book != originalName) return false;
    outPercent = _percent;
    _hasPending = false;
    _book = "";
    return true;
}
