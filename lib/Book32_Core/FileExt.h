#pragma once
// Book32 v1.2.2 — case-insensitive file-extension matching.
//
// Rationale: AppReader accepted ".EPUB" (lowercased comparison) while
// WebMgr's /api/books only matched exact-case ".epub", so mixed-case
// uploads appeared on the e-ink library but not in the web UI (and were
// excluded from manual ordering). This helper unifies the rule.
//
// Pure template — works with Arduino String and std::string alike
// (both provide length() and operator[]). Host-testable:
// tools/tests/test_file_ext.cpp.

#include <cstddef>
#include <cstring>
#include <cctype>

template <typename S>
bool hasExtensionCI(const S& name, const char* ext) {
    const size_t nl = name.length();
    const size_t el = strlen(ext);
    if (nl < el) return false;
    for (size_t i = 0; i < el; i++) {
        const char a = name[nl - el + i];
        const char b = ext[i];
        if (tolower((unsigned char)a) != tolower((unsigned char)b)) return false;
    }
    return true;
}
