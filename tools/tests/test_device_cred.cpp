// Book32 v1.5.0 — host test for device credential derivation.
// Build: g++ -std=c++17 -I lib/Book32_Core tools/tests/test_device_cred.cpp
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include "DeviceCred.h"

int main() {
    // Known MAC -> known password. Uses the last three bytes, uppercase hex.
    const uint8_t mac[6] = {0x24, 0x6F, 0x28, 0x4F, 0x2A, 0x91};
    char buf[BOOK32_CRED_LEN];

    deriveDevicePassword(mac, buf, sizeof(buf));
    assert(std::string(buf) == "book4F2A91");

    // Deterministic: same MAC always yields the same password, so the value
    // shown on the e-ink screen stays valid across reboots.
    char buf2[BOOK32_CRED_LEN];
    deriveDevicePassword(mac, buf2, sizeof(buf2));
    assert(std::string(buf) == std::string(buf2));

    // Different MACs yield different passwords.
    const uint8_t mac2[6] = {0x24, 0x6F, 0x28, 0x00, 0x00, 0x01};
    deriveDevicePassword(mac2, buf2, sizeof(buf2));
    assert(std::string(buf2) == "book000001");
    assert(std::string(buf) != std::string(buf2));

    // Low bytes are zero-padded to two hex digits each.
    const uint8_t mac3[6] = {0, 0, 0, 0x0A, 0x0B, 0x0C};
    deriveDevicePassword(mac3, buf2, sizeof(buf2));
    assert(std::string(buf2) == "book0A0B0C");

    // WPA2 requires at least 8 characters; ours is always 10.
    assert(strlen(buf) >= 8);
    assert(strlen(buf) == BOOK32_CRED_LEN - 1);

    printf("test_device_cred: all tests passed.\n");
    return 0;
}
