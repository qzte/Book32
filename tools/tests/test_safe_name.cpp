// Book32 v1.4.1 — host test for filename sanitisation (path-traversal guard).
// Build: g++ -std=c++17 -I lib/Book32_Core tools/tests/test_safe_name.cpp
#include <cassert>
#include <cstdio>
#include <string>
#include "SafeName.h"

int main() {
    using std::string;

    // Plain names are accepted.
    assert(isSafeBookName(string("book.epub")));
    assert(isSafeBookName(string("Meu Livro.EPUB")));
    assert(isSafeBookName(string("font.ttf")));

    // Path separators must be rejected (traversal into other directories).
    assert(!isSafeBookName(string("/etc/passwd")));
    assert(!isSafeBookName(string("covers/a.epub")));
    assert(!isSafeBookName(string("dir\\a.epub")));

    // Parent-directory references must be rejected.
    assert(!isSafeBookName(string("..")));
    assert(!isSafeBookName(string("../spiffs/index.html")));
    assert(!isSafeBookName(string("a..b.epub")));

    // Empty or dot-only names are rejected.
    assert(!isSafeBookName(string("")));
    assert(!isSafeBookName(string(".")));

    // Control characters and NUL-ish bytes are rejected.
    assert(!isSafeBookName(string("bad\nname.epub")));
    assert(!isSafeBookName(string("bad\x01name.epub")));

    // Only known extensions are accepted.
    assert(!isSafeBookName(string("index.html")));
    assert(!isSafeBookName(string("noextension")));

    // Overlong names are rejected (LittleFS name limit).
    assert(!isSafeBookName(string(std::string(64, 'a') + ".epub")));

    printf("test_safe_name: all tests passed.\n");
    return 0;
}
