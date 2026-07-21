#pragma once
// Book32 v1.4.1 — filename validation for filesystem-facing HTTP parameters.
//
// Rationale (security): /api/books/delete built its path as "/" + the raw
// `name` query parameter with no validation, so a request such as
//   DELETE /api/books/delete?name=../spiffs/index.html
// could escape the ebooks root and remove system files. Uploads sanitised the
// name by stripping directory components, but the delete path did not.
//
// The rule here is deliberately strict (allow-list, fail closed): a valid book
// name is a single filesystem entry, with no path separators, no parent
// references, no control characters, a known extension, and a length that fits
// LittleFS's name limit.
//
// Pure template — works with Arduino String and std::string alike (both expose
// length() and operator[]). Host-testable: tools/tests/test_safe_name.cpp.

#include <cstddef>
#include <cstring>
#include "FileExt.h"

// LittleFS default LFS_NAME_MAX is 255, but the upload handler already
// truncates to 28 characters plus a possible "_NN" dedup suffix. 63 leaves
// headroom without permitting absurd names.
#ifndef BOOK32_MAX_NAME_LEN
#define BOOK32_MAX_NAME_LEN 63
#endif

template <typename S>
bool isSafeBookName(const S& name) {
    const size_t n = name.length();
    if (n == 0 || n > BOOK32_MAX_NAME_LEN) return false;

    for (size_t i = 0; i < n; i++) {
        const unsigned char c = (unsigned char)name[i];
        // Reject path separators, control characters and DEL.
        if (c == '/' || c == '\\') return false;
        if (c < 0x20 || c == 0x7F) return false;
        // Reject any ".." sequence anywhere, not just a leading one.
        if (c == '.' && i + 1 < n && name[i + 1] == '.') return false;
    }

    // A lone "." is not a file.
    if (n == 1 && name[0] == '.') return false;

    // Allow-list of extensions this device stores on EbookFS.
    return hasExtensionCI(name, ".epub") || hasExtensionCI(name, ".ttf");
}
