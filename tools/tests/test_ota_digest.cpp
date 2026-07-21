// Book32 v1.6.0 — host test for SHA-256 digest parsing and comparison.
// Build: g++ -std=c++17 -I lib/Book32_Core tools/tests/test_ota_digest.cpp
#include <cassert>
#include <cstdio>
#include <string>
#include "OtaDigest.h"

int main() {
    using std::string;

    // Release notes carry one "SHA256 (file) = hex" line per asset, as emitted
    // by `shasum -a 256` / the release workflow. Surrounding prose is ignored.
    const string notes =
        "## What's new\n"
        "- Fixed a thing\n"
        "\n"
        "### Checksums\n"
        "SHA256 (firmware.bin) = "
        "aaaabbbbccccddddeeeeffff0000111122223333444455556666777788889999\n"
        "SHA256 (littlefs.bin) = "
        "1111111122222222333333334444444455555555666666667777777788888888\n";

    string got;

    // Exact asset lookup.
    assert(extractSha256(notes, "firmware.bin", got));
    assert(got == "aaaabbbbccccddddeeeeffff0000111122223333444455556666777788889999");

    assert(extractSha256(notes, "littlefs.bin", got));
    assert(got == "1111111122222222333333334444444455555555666666667777777788888888");

    // An asset with no checksum line must fail, not fall through to another.
    assert(!extractSha256(notes, "other.bin", got));

    // Empty notes: nothing to find.
    assert(!extractSha256(string(""), "firmware.bin", got));

    // Case-insensitive hex and a lowercase "sha256" label are both accepted.
    const string lower =
        "sha256 (firmware.bin) = "
        "AAAABBBBCCCCDDDDEEEEFFFF0000111122223333444455556666777788889999\n";
    assert(extractSha256(lower, "firmware.bin", got));

    // Truncated or malformed digests are rejected (must be exactly 64 hex).
    const string shortHash = "SHA256 (firmware.bin) = abcd1234\n";
    assert(!extractSha256(shortHash, "firmware.bin", got));

    const string nonHex =
        "SHA256 (firmware.bin) = "
        "zzzzbbbbccccddddeeeeffff0000111122223333444455556666777788889999\n";
    assert(!extractSha256(nonHex, "firmware.bin", got));

    // A partial name must not match a different asset ("firmware.bin" must not
    // be satisfied by a line for "old_firmware.bin").
    const string prefixed =
        "SHA256 (old_firmware.bin) = "
        "aaaabbbbccccddddeeeeffff0000111122223333444455556666777788889999\n";
    assert(!extractSha256(prefixed, "firmware.bin", got));

    // Digest comparison is case-insensitive on both sides.
    assert(sha256Equal(string("ABCDEF00"), string("abcdef00")));
    assert(!sha256Equal(string("abcdef00"), string("abcdef01")));
    assert(!sha256Equal(string("abcdef00"), string("abcdef0")));

    printf("test_ota_digest: all tests passed.\n");
    return 0;
}
