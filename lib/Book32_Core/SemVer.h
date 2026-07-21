#pragma once
// Book32 v1.4.1 — Semantic Versioning (Major.Minor.Patch) comparison for OTA.
//
// Rationale: GitHubMgr::checkUpdate() previously flagged an update whenever the
// release tag merely *differed* from SYSTEM_VERSION:
//     if (latestV.length() > 0 && latestV != currentV) info.available = true;
// That reports a downgrade as an available update, so republishing or fixing an
// older tag would silently roll the device back. It also used
//     latestV.replace("v", "")
// which strips every 'v' in the string, not just the tag prefix.
//
// This header compares the numeric Major.Minor.Patch core, ignoring any
// pre-release/build suffix, and fails closed: anything unparseable is never
// treated as newer, so a malformed tag can never trigger an OTA.
//
// Pure template — works with Arduino String and std::string alike. The return
// type of semverStripPrefix() matches the input type. Host-testable:
// tools/tests/test_semver.cpp.

#include <cstddef>
#include <cctype>

// Strip a single leading 'v' or 'V' tag prefix (e.g. "v1.4.0" -> "1.4.0").
// Unlike String::replace("v", ""), inner characters are left untouched.
template <typename S>
S semverStripPrefix(const S& v) {
    if (v.length() > 0 && (v[0] == 'v' || v[0] == 'V')) {
        return v.substring(1);
    }
    return v;
}

// std::string has no substring(); provide the host-test overload.
#ifdef _GLIBCXX_STRING
inline std::string semverStripPrefix(const std::string& v) {
    if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) return v.substr(1);
    return v;
}
#endif

// Parse Major.Minor.Patch into out[3]. Missing components default to 0, so
// "1.4" parses as 1.4.0. Any suffix after the patch number (e.g. "-rc1") is
// ignored. Returns false if no leading digit is present at all.
template <typename S>
bool semverParse(const S& raw, long out[3]) {
    out[0] = out[1] = out[2] = 0;

    const S v = semverStripPrefix(raw);
    const size_t n = v.length();
    if (n == 0 || !isdigit((unsigned char)v[0])) return false;

    size_t i = 0;
    for (int field = 0; field < 3 && i < n; field++) {
        if (!isdigit((unsigned char)v[i])) break;
        long value = 0;
        while (i < n && isdigit((unsigned char)v[i])) {
            value = value * 10 + (v[i] - '0');
            if (value > 100000L) return false;  // implausible; fail closed
            i++;
        }
        out[field] = value;
        if (i < n && v[i] == '.') i++;  // consume separator, else stop
        else break;
    }
    return true;
}

// True only when `candidate` is strictly greater than `current`.
// Equal versions and downgrades both return false.
template <typename S>
bool semverIsNewer(const S& candidate, const S& current) {
    long a[3], b[3];
    if (!semverParse(candidate, a)) return false;
    if (!semverParse(current, b)) return false;
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) return a[i] > b[i];
    }
    return false;
}
