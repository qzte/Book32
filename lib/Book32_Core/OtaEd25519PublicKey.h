#pragma once
// Book32 v1.11.0 — public half of the OTA signing keypair.
//
// The matching private key never leaves the OTA_ED25519_PRIVATE_KEY GitHub
// Actions secret; release.yml uses it to sign the SHA-256 digest of each
// published asset (see docs/plans/2026-08-23-ota-ed25519-signing-design.md).
// This key is meant to be long-lived — rotating it means every device already
// in the field needs a USB reflash to trust the new key, since the whole
// point is that OTA cannot silently swap it out from under them.

#include <cstdint>

inline constexpr uint8_t BOOK32_OTA_ED25519_PUBLIC_KEY[32] = {
    0xce, 0xd7, 0xa3, 0xfa, 0xa7, 0xe4, 0x74, 0xc8,
    0x52, 0x6a, 0x35, 0x0c, 0x43, 0xf0, 0x63, 0x6f,
    0xe1, 0x6f, 0xc1, 0x4c, 0x3e, 0x86, 0x5f, 0x0a,
    0x58, 0xdc, 0x0f, 0x3d, 0xd1, 0x2a, 0xfb, 0x3c,
};
