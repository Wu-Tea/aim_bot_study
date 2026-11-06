#include "test_support/native_test_registry.h"
void register_target_pipeline_integration_tests(native_test::Registry&);
int main(int argc,char** argv) {
    native_test::Registry registry;
    register_target_pipeline_integration_tests(registry);
    return native_test::run(registry,argc,argv,"PersonCueControllerTests");
}
