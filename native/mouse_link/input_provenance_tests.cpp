#include "input_provenance.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace mouse_link;
void check(InputEvidence value, const char* origin, bool tag, bool lower = false) {
    if (std::strcmp(value.origin, origin) || value.tag_match != tag || value.lower_integrity != lower)
        throw std::runtime_error("provenance evidence mismatch");
}
int main() {
    try {
        check(hook_evidence(1, 0), "os_injected", false);
        check(hook_evidence(3, kMovementTag), "os_injected", true, true);
        check(hook_evidence(2, 0), "os_injected", false, true);
        check(hook_evidence(0, kMovementTag), "unconfirmed", true);
        check(hook_evidence(0, 0), "unconfirmed", false);
        check(raw_evidence(false, 0), "unconfirmed", false); // Includes real touchpad input.
        check(raw_evidence(true, 0), "device_associated", false); // Could be virtual.
        check(raw_evidence(true, kMovementTag), "device_associated", true);
        check(raw_evidence(false, kFireTag), "unconfirmed", true);
        check(hook_evidence(1, 123, 123), "os_injected", true);
        check(hook_evidence(1, 124, 123), "os_injected", false);
        std::cout << "PASS 11 cases: flags, tags, null devices and virtual-device ambiguity\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
