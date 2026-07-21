#pragma once
// Book32 v1.6.0 — SHA-256 digest parsing for OTA payload verification.
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
// SCOPE — read this before trusting it:
// This detects *corruption*, not *tampering*. The digest is fetched from the
// same GitHub API response that supplies the download URL, so an attacker able
// to forge that response controls both the binary and its expected hash. It is
// therefore NOT a defence against an active man-in-the-middle. Closing that gap
// requires either pinning GitHub's CA (checkUpdate() currently does not call
// setCACert(), so TLS is unauthenticated) or signing the binary with a key
// embedded in the firmware. See docs/plans/2026-07-21-ota-integrity-design.md.
//
// Pure string handling — no Arduino dependency, host-testable:
// tools/tests/test_ota_digest.cpp.

#include <cstddef>
#include <cctype>

#define BOOK32_SHA256_HEX_LEN 64

namespace book32_digest_detail {

inline bool isHexDigit(char c) {
    const unsigned char u = (unsigned char)c;
    return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'f') || (u >= 'A' && u <= 'F');
}

inline char lowerAscii(char c) {
    return (char)tolower((unsigned char)c);
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

// Find the SHA-256 digest published for `assetName` in `notes`.
// On success writes 64 hex characters to `out` and returns true.
// Returns false when the line is absent, the asset name does not match
// exactly, or the digest is not exactly 64 hex characters (fail closed — a
// malformed digest must never be treated as "no check required").
template <typename S>
bool extractSha256(const S& notes, const char* assetName, S& out) {
    using namespace book32_digest_detail;

    const size_t n = notes.length();
    size_t i = 0;

    while (i < n) {
        // Anchor on the "SHA256" label at the start of a line (allowing
        // leading whitespace), so prose mentioning the word doesn't match.
        size_t lineStart = i;
        while (lineStart < n && (notes[lineStart] == ' ' || notes[lineStart] == '\t')) {
            lineStart++;
        }

        if (matchesAt(notes, lineStart, "sha256")) {
            size_t p = lineStart + 6;
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

                            if (p - hexStart == BOOK32_SHA256_HEX_LEN) {
                                out = sliceOf(notes, hexStart, p);
                                return true;
                            }
                            // Wrong length: fail closed rather than continuing,
                            // so a truncated digest can't be silently skipped.
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
