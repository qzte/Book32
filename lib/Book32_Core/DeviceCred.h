#pragma once
// Book32 v1.5.0 — per-device credential derived from the WiFi MAC address.
//
// Rationale (security): v1.4.x brought up the management SoftAP with
// WiFi.softAP(AP_SSID) and no password, and served the whole HTTP API with no
// authentication. Anyone within radio range could upload and delete books,
// read the home network's SSID, and trigger an OTA update.
//
// The credential is derived from the last three bytes of the MAC so that it is
// stable across reboots (the value printed on the e-ink screen stays valid)
// and needs no persisted state or configuration UI.
//
// v1.9.0: the HTTP Basic Auth layer was removed, so this value is now *only*
// the SoftAP WPA2 passphrase.
//
// Threat model — be honest about what this does and does not achieve:
//   * It keeps the hotspot from being an open AP.
//   * It does NOT protect the HTTP API. Once a client is on the same network
//     (hotspot or home LAN) every endpoint is reachable with no credential:
//     uploading and deleting books, changing settings, joining a WiFi network
//     and triggering OTA.
//   * It does NOT stop a determined attacker. The MAC is observable over the
//     air, so the passphrase is derivable by someone who knows this scheme.
//
// Pure function over a caller-supplied MAC — no Arduino dependency, so it is
// host-testable: tools/tests/test_device_cred.cpp.

#include <cstdint>
#include <cstdio>
#include <cstddef>

// "book" + 6 hex digits + NUL.
#define BOOK32_CRED_LEN 11

// Write "book<XXYYZZ>" (uppercase hex of mac[3..5]) into out.
// out must be at least BOOK32_CRED_LEN bytes.
inline void deriveDevicePassword(const uint8_t mac[6], char* out, size_t outLen) {
    if (!out || outLen < BOOK32_CRED_LEN) {
        if (out && outLen > 0) out[0] = '\0';
        return;
    }
    snprintf(out, outLen, "book%02X%02X%02X", mac[3], mac[4], mac[5]);
}
