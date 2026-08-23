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

    // v1.11.0: Ed25519 signature lines use the same "LABEL (asset) = hex"
    // shape as SHA256, just a different label and a 64-byte (128 hex char)
    // value instead of a 32-byte one.
    // firmwareSig is bytes 0x00..0x3f in order; littlefsSig is 64 repeats of
    // 0x11 — distinct fixtures, so a mix-up between the two lines would fail
    // the equality assertions below.
    const string firmwareSig =
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
        "202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f";
    const string littlefsSig(BOOK32_ED25519_SIG_HEX_LEN, '1');
    assert(firmwareSig.length() == (size_t)BOOK32_ED25519_SIG_HEX_LEN);
    assert(littlefsSig.length() == (size_t)BOOK32_ED25519_SIG_HEX_LEN);

    const string signed_notes =
        "### Checksums\n"
        "SHA256 (firmware.bin) = "
        "aaaabbbbccccddddeeeeffff0000111122223333444455556666777788889999\n"
        "ED25519 (firmware.bin) = " + firmwareSig + "\n"
        "ED25519 (littlefs.bin) = " + littlefsSig + "\n";

    string sig;
    assert(extractEd25519Signature(signed_notes, "firmware.bin", sig));
    assert(sig.length() == (size_t)BOOK32_ED25519_SIG_HEX_LEN);
    assert(sig == firmwareSig);

    assert(extractEd25519Signature(signed_notes, "littlefs.bin", sig));
    assert(sig == littlefsSig);

    // No signature line for this asset: must fail, not fall through to
    // firmware.bin's.
    assert(!extractEd25519Signature(signed_notes, "other.bin", sig));

    // A SHA-256 line must never satisfy an Ed25519 lookup, even though both
    // share the "LABEL (asset) = hex" shape.
    assert(!extractEd25519Signature(notes, "firmware.bin", sig));

    // Wrong length (a SHA-256-sized value under the ED25519 label) is
    // rejected, not silently accepted at the wrong size.
    const string shortSig =
        "ED25519 (firmware.bin) = "
        "aaaabbbbccccddddeeeeffff0000111122223333444455556666777788889999\n";
    assert(!extractEd25519Signature(shortSig, "firmware.bin", sig));

    // hexDecode(): the inverse of the hex text this file parses out.
    uint8_t decoded[BOOK32_ED25519_SIG_LEN];
    assert(hexDecode(sig, (size_t)BOOK32_ED25519_SIG_HEX_LEN, decoded));
    assert(decoded[0] == 0x11 && decoded[1] == 0x11 && decoded[BOOK32_ED25519_SIG_LEN - 1] == 0x11);

    // Wrong length or non-hex input must fail closed, leaving `decoded`
    // whatever it was (callers must check the return value).
    assert(!hexDecode(string("abcd"), (size_t)BOOK32_ED25519_SIG_HEX_LEN, decoded));
    string nonHexSig = sig;
    nonHexSig[0] = 'z';
    assert(!hexDecode(nonHexSig, (size_t)BOOK32_ED25519_SIG_HEX_LEN, decoded));

    printf("test_ota_digest: all tests passed.\n");
    return 0;
}
