#include "mouse_native/mouse_relay_client.h"

#include <iostream>
#include <stdexcept>
#include <type_traits>

namespace {

void require_true(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void test_shared_abi_is_fixed_width_and_trivial() {
    static_assert(sizeof(COD_MOUSE_RELAY_TOKEN) == 16);
    static_assert(sizeof(COD_MOUSE_BEGIN_REQUEST) == 16);
    static_assert(sizeof(COD_MOUSE_BEGIN_REPLY) == 24);
    static_assert(sizeof(COD_MOUSE_TOKEN_REQUEST) == 24);
    static_assert(sizeof(COD_MOUSE_SOURCE_PACKET) == 40);
    static_assert(sizeof(COD_MOUSE_FINAL_REPORT) == 48);
    static_assert(sizeof(COD_MOUSE_CALIBRATION_REPORT) == 32);
    static_assert(std::is_trivially_copyable_v<COD_MOUSE_FINAL_REPORT>);
    require_true(
        IOCTL_COD_MOUSE_SUBMIT_FINAL != IOCTL_COD_MOUSE_SUBMIT_CALIBRATION,
        "controller final output and calibration probes must have distinct IOCTLs");
}

void test_unopened_client_is_safe_and_non_arming() {
    mouse_native::MouseRelayClient client;
    require_true(!client.is_open() && !client.has_lease() && !client.armed(),
        "default relay client must be inert");
    require_true(!client.begin(1), "unopened client cannot begin a lease");
    require_true(!client.arm(), "unopened client cannot arm interception");
    require_true(client.disarm(), "unopened disarm must be a safe no-op");
    client.close();
    require_true(!client.is_open(), "repeated close must remain safe");
}

}  // namespace

int main() {
    try {
        test_shared_abi_is_fixed_width_and_trivial();
        test_unopened_client_is_safe_and_non_arming();
        std::cout << "[MouseRelayClientTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[MouseRelayClientTests][FAIL] " << error.what() << '\n';
        return 1;
    }
}
