// Book32 v1.4.1 — host test for Semantic Versioning comparison used by OTA.
// Build: g++ -std=c++17 -I lib/Book32_Core tools/tests/test_semver.cpp
#include <cassert>
#include <cstdio>
#include <string>
#include "SemVer.h"

int main() {
    using std::string;

    // Prefix stripping: only a leading 'v' is removed, never inner ones.
    assert(semverStripPrefix(string("v1.4.0")) == "1.4.0");
    assert(semverStripPrefix(string("1.4.0")) == "1.4.0");
    assert(semverStripPrefix(string("v1.4.0-preview")) == "1.4.0-preview");
    assert(semverStripPrefix(string("v1.4.0v")) == "1.4.0v");

    // Strictly newer versions.
    assert(semverIsNewer(string("1.4.1"), string("1.4.0")));
    assert(semverIsNewer(string("1.5.0"), string("1.4.9")));
    assert(semverIsNewer(string("2.0.0"), string("1.9.9")));
    assert(semverIsNewer(string("v1.4.1"), string("1.4.0")));

    // Equal versions are not newer.
    assert(!semverIsNewer(string("1.4.0"), string("1.4.0")));
    assert(!semverIsNewer(string("v1.4.0"), string("1.4.0")));

    // Downgrades must NOT be reported as updates (the v1.4.0 bug).
    assert(!semverIsNewer(string("1.3.9"), string("1.4.0")));
    assert(!semverIsNewer(string("1.4.0"), string("1.4.1")));
    assert(!semverIsNewer(string("0.9.0"), string("1.0.0")));

    // Missing components default to zero: "1.4" == "1.4.0".
    assert(!semverIsNewer(string("1.4"), string("1.4.0")));
    assert(semverIsNewer(string("1.4.1"), string("1.4")));

    // Numeric comparison, not lexicographic: 1.10.0 > 1.9.0.
    assert(semverIsNewer(string("1.10.0"), string("1.9.0")));
    assert(!semverIsNewer(string("1.9.0"), string("1.10.0")));

    // Pre-release suffix is ignored for ordering of the numeric core.
    assert(semverIsNewer(string("1.5.0-rc1"), string("1.4.0")));

    // Malformed input is never "newer" (fail closed — no accidental OTA).
    assert(!semverIsNewer(string(""), string("1.4.0")));
    assert(!semverIsNewer(string("garbage"), string("1.4.0")));

    printf("test_semver: all tests passed.\n");
    return 0;
}
