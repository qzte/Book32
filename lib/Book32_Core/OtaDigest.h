#pragma once
// Book32 v1.6.0 — SHA-256 digest parsing for OTA payload verification.
// Extended in v1.11.0 with Ed25519 signature parsing (see below).
//
// Rationale: performFirmwareUpdate() and performFilesystemUpdate() streamed
// bytes straight into Update.write() with no integrity check. Combined with
// the truncation bugs fixed in v1.4.1 (chunked responses reporting length -1,
// and stalled connections), a partial or corrupted download could be written
// to the app partition and brick the device.
//
// The release workflow publishes one line per asset in the release body:
//
//     SHA256 (firmware.bin) = <64 hex digits>
//     SHA256 (littlefs.bin) = <64 hex digits>
//
// which is exactly the `shasum -a 256 --tag` format, so it stays readable to a
// human editing the release notes by hand.
//
// SHA-256 alone detects *corruption*, not *tampering*: the digest is fetched
// from the same GitHub API response that supplies the download URL, so an
// attacker able to forge that response controls both the binary and its
// expected hash. v1.11.0 closes that gap with a second, independent line:
//
//     ED25519 (firmware.bin) = <128 hex digits>
//     ED25519 (littlefs.bin) = <128 hex digits>
//
// a signature (by the release workflow, using a private key that never
// leaves the OTA_ED25519_PRIVATE_KEY GitHub Actions secret) over the raw
// 32-byte SHA-256 digest of the asset. The public key is embedded in the
// firmware (lib/Book32_Core/OtaEd25519PublicKey.h) and never changes over
// the air. Forging the GitHub API response no longer suffices — the
// attacker would also need the private key. See
// docs/plans/2026-08-23-ota-ed25519-signing-design.md.
//
// Signing the digest rather than the whole file is safe *because* SHA-256 is
// preimage-resistant: reusing a signed (digest, signature) pair under a
// different asset name would require finding a different file — malicious or
// not — that hashes to that same digest, which is exactly what SHA-256 is
// designed to make infeasible. No per-asset domain separation is needed on
// top of that.
//
// Pure string handling — no Arduino dependency, host-testable:
// tools/tests/test_ota_digest.cpp.

#include <cstddef>
#include <cstdint>
#include <cctype>

#define BOOK32_SHA256_HEX_LEN 64
#define BOOK32_ED25519_SIG_HEX_LEN 128
#define BOOK32_ED25519_SIG_LEN 64

namespace book32_digest_detail {

inline bool isHexDigit(char c) {
    const unsigned char u = (unsigned char)c;
    return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F');
}

inline char lowerAscii(char c) {
    return (char)tolower((unsigned char)c);
}

inline uint8_t hexNibble(char c) {
    c = lowerAscii(c);
    return (c >= 'a') ? (uint8_t)(c - 'a' + 10) : (uint8_t)(c - '0');
}

// Case-insensitive comparison of `hay[at..]` against the literal `needle`.
template <typename S>
bool matchesAt(const S& hay, size_t at, const char* needle) {
    const size_t n = hay.length();
    for (size_t i = 0; needle[i] != '\0'; i++) {
        if (at + i >= n) return false;
        if (lowerAscii(hay[at + i]) != lowerAscii(needle[i])) return false;
    }
    return true;
}

// Arduino String uses substring(); std::string uses substr(). This shim keeps
// the parser itself generic over both.
template <typename S>
inline S sliceOf(const S& s, size_t from, size_t to) {
    return s.substring(from, to);
}

}  // namespace book32_digest_detail

#ifdef _GLIBCXX_STRING
namespace book32_digest_detail {
inline std::string sliceOf(const std::string& s, size_t from, size_t to) {
    return s.substr(from, to - from);
}
}  // namespace book32_digest_detail
#endif

// Find the hex value published for `assetName` under a `LABEL (asset) = hex`
// line in `notes` (case-insensitive label, e.g. "sha256" or "ed25519"). On
// success writes exactly `expectedHexLen` hex characters to `out` and returns
// true. Returns false when the line is absent, the asset name does not match
// exactly, or the value is not exactly `expectedHexLen` hex characters (fail
// closed — a malformed value must never be treated as "no check required").
template <typename S>
bool extractHexField(const S& notes, const char* label, const char* assetName,
                     size_t expectedHexLen, S& out) {
    using namespace book32_digest_detail;

    size_t labelLen = 0;
    while (label[labelLen] != '\0') labelLen++;

    const size_t n = notes.length();
    size_t i = 0;

    while (i < n) {
        // Anchor on the label at the start of a line (allowing leading
        // whitespace), so prose mentioning the word doesn't match.
        size_t lineStart = i;
        while (lineStart < n && (notes[lineStart] == ' ' || notes[lineStart] == '\t')) {
            lineStart++;
        }

        if (matchesAt(notes, lineStart, label)) {
            size_t p = lineStart + labelLen;
            while (p < n && (notes[p] == ' ' || notes[p] == '\t')) p++;

            if (p < n && notes[p] == '(') {
                p++;
                // The asset name must match in full, up to the closing paren.
                size_t nameStart = p;
                while (p < n && notes[p] != ')' && notes[p] != '\n') p++;

                if (p < n && notes[p] == ')') {
                    const size_t nameLen = p - nameStart;
                    bool nameOk = true;
                    size_t k = 0;
                    for (; assetName[k] != '\0'; k++) {
                        if (k >= nameLen || notes[nameStart + k] != assetName[k]) {
                            nameOk = false;
                            break;
                        }
                    }
                    if (nameOk && k != nameLen) nameOk = false;  // extra chars

                    if (nameOk) {
                        p++;  // past ')'
                        while (p < n && (notes[p] == ' ' || notes[p] == '\t')) p++;
                        if (p < n && notes[p] == '=') {
                            p++;
                            while (p < n && (notes[p] == ' ' || notes[p] == '\t')) p++;

                            size_t hexStart = p;
                            while (p < n && isHexDigit(notes[p])) p++;

                            if (p - hexStart == expectedHexLen) {
                                out = sliceOf(notes, hexStart, p);
                                return true;
                            }
                            // Wrong length: fail closed rather than continuing,
                            // so a truncated value can't be silently skipped.
                            return false;
                        }
                    }
                }
            }
        }

        // Advance to the next line.
        while (i < n && notes[i] != '\n') i++;
        if (i < n) i++;
    }

    return false;
}

// Find the SHA-256 digest published for `assetName` in `notes`. See
// extractHexField() above for the matching rules.
template <typename S>
bool extractSha256(const S& notes, const char* assetName, S& out) {
    return extractHexField(notes, "sha256", assetName, BOOK32_SHA256_HEX_LEN, out);
}

// Find the Ed25519 signature (over the asset's raw SHA-256 digest) published
// for `assetName` in `notes`. See extractHexField() above for the matching
// rules.
template <typename S>
bool extractEd25519Signature(const S& notes, const char* assetName, S& out) {
    return extractHexField(notes, "ed25519", assetName, BOOK32_ED25519_SIG_HEX_LEN, out);
}

// Decode a `hexLen`-character hex string into `out[0..hexLen/2)`. Returns
// false — leaving `out` untouched — if the length is not exactly `hexLen` or
// any character is not a hex digit (fail closed).
template <typename S>
bool hexDecode(const S& hex, size_t hexLen, uint8_t* out) {
    using namespace book32_digest_detail;
    if (hex.length() != hexLen) return false;
    for (size_t i = 0; i < hexLen; i++) {
        if (!isHexDigit(hex[i])) return false;
    }
    for (size_t i = 0; i < hexLen / 2; i++) {
        out[i] = (uint8_t)((hexNibble(hex[i * 2]) << 4) | hexNibble(hex[i * 2 + 1]));
    }
    return true;
}

// Case-insensitive equality of two hex digests.
template <typename S>
bool sha256Equal(const S& a, const S& b) {
    using namespace book32_digest_detail;
    if (a.length() != b.length()) return false;
    for (size_t i = 0; i < a.length(); i++) {
        if (lowerAscii(a[i]) != lowerAscii(b[i])) return false;
    }
    return true;
}
