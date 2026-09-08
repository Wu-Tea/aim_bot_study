#include "mouse_native/mouse_interception_transport.h"
#include <Windows.h>
#include <iostream>
int main() {
    auto driver = mouse_native::make_interception_driver();
    if (!driver->open()) return 1;
    auto dll = GetModuleHandleW(L"interception.dll");
    const auto filter = reinterpret_cast<unsigned short (*)(int)>(GetProcAddress(dll, "fixture_filter"));
    const auto queries = reinterpret_cast<int (*)()>(GetProcAddress(dll, "fixture_query_count"));
    const auto reject = reinterpret_cast<void (*)(int)>(GetProcAddress(dll, "fixture_reject_setter"));
    if (!filter || !queries || !reject) return 2;
    if (!driver->capture(13, true) || filter(13) != 0xffff || filter(12) || filter(14) || queries()) return 3;
    if (!driver->capture(13, false) || filter(13) || queries()) return 4;
    reject(1);
    if (driver->capture(13, true) || filter(13)) return 5;
    reject(0);
    driver->close();
    std::cout << "Selected-device setter and failure propagation verified without GET_FILTER.\n";
    return 0;
}
