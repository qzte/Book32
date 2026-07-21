// Book32 v1.2.2 — host test for case-insensitive extension matching.
// Build: g++ -std=c++17 -I lib/Book32_Core tools/tests/test_file_ext.cpp
#include <cassert>
#include <string>
#include "FileExt.h"

int main() {
    using std::string;
    // Exact lowercase match
    assert(hasExtensionCI(string("book.epub"), ".epub"));
    // Uppercase / mixed-case extensions must match
    assert(hasExtensionCI(string("Book.EPUB"), ".epub"));
    assert(hasExtensionCI(string("Font.Ttf"), ".ttf"));
    // Non-matching extension
    assert(!hasExtensionCI(string("book.pdf"), ".epub"));
    // Name shorter than extension
    assert(!hasExtensionCI(string("a"), ".epub"));
    // Extension substring in the middle must not match
    assert(!hasExtensionCI(string("book.epub.bak"), ".epub"));
    // Empty name
    assert(!hasExtensionCI(string(""), ".epub"));

    printf("All 7 tests passed.\n");
    return 0;
}
