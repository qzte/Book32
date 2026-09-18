// Host test for the pure update-check classification.
// Build: g++ -std=c++17 -I ../../lib/Book32_Core -o test_update_check test_update_check.cpp &&
// ./test_update_check
#include "UpdateCheckLogic.h"
#include <cassert>
#include <cstdio>
#include <set>
#include <string>

using std::string;

// A successful 200 with a newer release, as a starting point to vary.
static UpdateCheckInputs ok() {
    UpdateCheckInputs in;
    in.wifiConnected = true;
    in.httpCode = 200;
    in.responseParsed = true;
    in.tagPresent = true;
    in.newerThanCurrent = true;
    return in;
}

static void testHeardBack() {
    assert(classifyUpdateCheck(ok()) == UpdateCheckStatus::UpdateAvailable);

    UpdateCheckInputs same = ok();
    same.newerThanCurrent = false;
    assert(classifyUpdateCheck(same) == UpdateCheckStatus::UpToDate);

    // These two are the only statuses the device may present as an answer.
    assert(updateCheckCompleted(UpdateCheckStatus::UpdateAvailable));
    assert(updateCheckCompleted(UpdateCheckStatus::UpToDate));
}

static void testCouldNotCheck() {
    // No WiFi: the request was never made, whatever else is set.
    {
        UpdateCheckInputs in = ok();
        in.wifiConnected = false;
        assert(classifyUpdateCheck(in) == UpdateCheckStatus::Offline);
    }
    // Offline wins even over a 200 left over from a previous attempt.
    {
        UpdateCheckInputs in;
        in.wifiConnected = false;
        in.httpCode = 200;
        in.responseParsed = true;
        in.tagPresent = true;
        assert(classifyUpdateCheck(in) == UpdateCheckStatus::Offline);
    }
    {
        UpdateCheckInputs in = ok();
        in.httpCode = 404;
        assert(classifyUpdateCheck(in) == UpdateCheckStatus::NoRelease);
    }
    {
        UpdateCheckInputs in = ok();
        in.httpCode = 403;
        assert(classifyUpdateCheck(in) == UpdateCheckStatus::RateLimited);
    }
    // Any other status, and HTTPClient's negative transport errors.
    for (int code : {500, 502, 301, 401, 0, -1, -11}) {
        UpdateCheckInputs in = ok();
        in.httpCode = code;
        assert(classifyUpdateCheck(in) == UpdateCheckStatus::HttpError);
    }
    // A 200 we could not read.
    {
        UpdateCheckInputs in = ok();
        in.responseParsed = false;
        assert(classifyUpdateCheck(in) == UpdateCheckStatus::BadResponse);
    }
    // The regression this change is about: a parsed 200 with no tag_name made
    // semverIsNewer() return false, and the device reported "up to date"
    // without ever having a version to compare against.
    {
        UpdateCheckInputs in = ok();
        in.tagPresent = false;
        in.newerThanCurrent = false;
        assert(classifyUpdateCheck(in) == UpdateCheckStatus::BadResponse);
    }

    // None of the failures may be presented as an answer.
    for (UpdateCheckStatus s :
         {UpdateCheckStatus::Offline, UpdateCheckStatus::NoRelease, UpdateCheckStatus::RateLimited,
          UpdateCheckStatus::HttpError, UpdateCheckStatus::BadResponse}) {
        assert(!updateCheckCompleted(s));
    }
}

static void testKeys() {
    const UpdateCheckStatus all[] = {
        UpdateCheckStatus::UpdateAvailable, UpdateCheckStatus::UpToDate,    UpdateCheckStatus::Offline,
        UpdateCheckStatus::NoRelease,       UpdateCheckStatus::RateLimited, UpdateCheckStatus::HttpError,
        UpdateCheckStatus::BadResponse,
    };

    // Every status has a key, and no two share one: the web UI switches on
    // them, so a collision would silently merge two outcomes into one message.
    std::set<string> seen;
    for (UpdateCheckStatus s : all) {
        string key = updateCheckStatusKey(s);
        assert(!key.empty());
        assert(seen.insert(key).second);
    }
    assert(seen.size() == 7);

    // The two the UI keys off by name.
    assert(string(updateCheckStatusKey(UpdateCheckStatus::UpdateAvailable)) == "update_available");
    assert(string(updateCheckStatusKey(UpdateCheckStatus::UpToDate)) == "up_to_date");
}

int main() {
    testHeardBack();
    testCouldNotCheck();
    testKeys();
    printf("test_update_check: OK\n");
    return 0;
}
