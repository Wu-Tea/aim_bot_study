#include <Windows.h>
namespace { unsigned short filters[21]{}; int queries = 0; bool reject = false; }
extern "C" {
__declspec(dllexport) void* interception_create_context() { return filters; }
__declspec(dllexport) void interception_destroy_context(void*) {}
__declspec(dllexport) void interception_set_filter(void*, int (*select)(int), unsigned short value) {
    if (reject) { SetLastError(ERROR_ACCESS_DENIED); return; }
    for (int id = 1; id <= 20; ++id) if (select(id)) filters[id] = value;
}
// Reproduce the observed broken readback, without loading any system driver.
__declspec(dllexport) unsigned short interception_get_filter(void*, int) { ++queries; return 0; }
__declspec(dllexport) int interception_wait_with_timeout(void*, unsigned long) { return 0; }
__declspec(dllexport) int interception_receive(void*, int, void*, unsigned int) { return 0; }
__declspec(dllexport) int interception_send(void*, int, const void*, unsigned int) { return 0; }
__declspec(dllexport) unsigned int interception_get_hardware_id(void*, int, void*, unsigned int) { return 0; }
__declspec(dllexport) unsigned short fixture_filter(int id) { return filters[id]; }
__declspec(dllexport) int fixture_query_count() { return queries; }
__declspec(dllexport) void fixture_reject_setter(int value) { reject = value != 0; }
}
