#pragma once
// Book32 — pure classification of one "check for updates" attempt.
//
// No Arduino, no HTTPClient, no ArduinoJson: only plain types, so every
// decision here is host-testable (tools/tests/test_update_check.cpp). Same
// pattern as SemVer.h and ProgressMergeLogic.h.
//
// Why this exists: GitHubMgr::checkUpdate() used to collapse every possible
// outcome into a single `available` bool. No WiFi, a refused connection, a
// 403 rate limit, a 404 with no releases published, a response too big to
// parse — all of them came back the same as a release that really is not
// newer, and every caller then told the user "you are up to date". That is
// the one answer the device had no basis to give: it never heard from
// GitHub at all.
//
// It matters more here than it would elsewhere. The OTA design accepts that
// the TLS connection is not certificate-verified (see
// docs/plans/2026-07-21-ota-integrity-design.md — Ed25519 signatures protect
// what gets flashed, deliberately instead of pinning a CA that could expire
// and strand the device). What signatures cannot protect against is an
// answer that never arrives: anyone able to interfere with the connection
// can keep the device on an old version simply by breaking the check. Saying
// "up to date" then is exactly the wrong thing to show. Saying "could not
// check" is honest, and it is something the user can act on.

// The outcome of one attempt, in the order the caller should reason about
// them: the first two mean the device got an answer, the rest mean it did
// not.
enum class UpdateCheckStatus {
    UpdateAvailable, // heard from GitHub: a newer release exists
    UpToDate,        // heard from GitHub: nothing newer
    Offline,         // no WiFi, the request was never made
    NoRelease,       // 404 — the repository publishes no releases
    RateLimited,     // 403 — GitHub's unauthenticated API quota
    HttpError,       // any other status, or a transport failure (code <= 0)
    BadResponse      // a 200 whose body could not be parsed, or carries no tag
};

struct UpdateCheckInputs {
    bool wifiConnected = false;
    // HTTPClient's GET() return: a positive value is an HTTP status, zero or
    // negative is one of its transport errors (connection refused, timeout).
    int httpCode = 0;
    // The three below only mean anything for a 200; they are ignored otherwise.
    bool responseParsed = false;
    bool tagPresent = false; // the release carries a non-empty tag_name
    bool newerThanCurrent = false;
};

inline UpdateCheckStatus classifyUpdateCheck(const UpdateCheckInputs& in) {
    if (!in.wifiConnected) return UpdateCheckStatus::Offline;

    if (in.httpCode == 200) {
        // A body that did not parse, or a release with no tag to compare
        // against, is not "nothing newer" — it is an answer we could not
        // read. Before this, an empty tag_name made semverIsNewer() return
        // false and the device reported being up to date.
        if (!in.responseParsed || !in.tagPresent) return UpdateCheckStatus::BadResponse;
        return in.newerThanCurrent ? UpdateCheckStatus::UpdateAvailable : UpdateCheckStatus::UpToDate;
    }

    if (in.httpCode == 404) return UpdateCheckStatus::NoRelease;
    if (in.httpCode == 403) return UpdateCheckStatus::RateLimited;
    return UpdateCheckStatus::HttpError;
}

// True only when the device actually heard back from GitHub, and so has a
// basis for what it tells the user. Everything else is "could not check",
// which is never the same as "nothing newer".
inline bool updateCheckCompleted(UpdateCheckStatus status) {
    return status == UpdateCheckStatus::UpdateAvailable || status == UpdateCheckStatus::UpToDate;
}

// Stable key for the HTTP API (GET /api/check_update) and the serial log.
// The web UI switches on these, so they are part of the interface: adding a
// status is fine, renaming one is not.
inline const char* updateCheckStatusKey(UpdateCheckStatus status) {
    switch (status) {
        case UpdateCheckStatus::UpdateAvailable:
            return "update_available";
        case UpdateCheckStatus::UpToDate:
            return "up_to_date";
        case UpdateCheckStatus::Offline:
            return "offline";
        case UpdateCheckStatus::NoRelease:
            return "no_release";
        case UpdateCheckStatus::RateLimited:
            return "rate_limited";
        case UpdateCheckStatus::HttpError:
            return "http_error";
        case UpdateCheckStatus::BadResponse:
            return "bad_response";
    }
    return "http_error"; // unreachable; fail towards "could not check"
}
