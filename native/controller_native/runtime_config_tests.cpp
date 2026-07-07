#include "runtime_config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace {

void require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

void test_vision_gpu_service_defaults_are_enabled() {
    const controller_native::RuntimeConfig config =
        controller_native::load_runtime_config(std::filesystem::path{});
    require(config.vision.gpu_service_enabled);
    require(config.vision.gpu_service_active_fps == 100);
    require(config.vision.gpu_service_idle_fps == 20);
    require(config.vision.gpu_service_keepwarm_when_idle);
    require(config.vision.gpu_service_repeat_last_on_no_update);
}

void test_vision_gpu_service_config_values_parse() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cod_native_runtime_config_test.toml";
    {
        std::ofstream output(path);
        output << "[runtime.vision]\n"
               << "gpu_service_enabled = true\n"
               << "gpu_service_active_fps = 120\n"
               << "gpu_service_idle_fps = 15\n"
               << "gpu_service_keepwarm_when_idle = false\n"
               << "gpu_service_repeat_last_on_no_update = false\n";
    }

    const controller_native::RuntimeConfig config =
        controller_native::load_runtime_config(path);
    std::filesystem::remove(path);

    require(config.vision.gpu_service_enabled);
    require(config.vision.gpu_service_active_fps == 120);
    require(config.vision.gpu_service_idle_fps == 15);
    require(!config.vision.gpu_service_keepwarm_when_idle);
    require(!config.vision.gpu_service_repeat_last_on_no_update);
}

void test_vision_gpu_service_can_be_disabled() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "cod_native_runtime_config_disable_test.toml";
    {
        std::ofstream output(path);
        output << "[runtime.vision]\n"
               << "gpu_service_enabled = false\n";
    }

    const controller_native::RuntimeConfig config =
        controller_native::load_runtime_config(path);
    std::filesystem::remove(path);

    require(!config.vision.gpu_service_enabled);
}

} // namespace

int main() {
    test_vision_gpu_service_defaults_are_enabled();
    test_vision_gpu_service_config_values_parse();
    test_vision_gpu_service_can_be_disabled();
    return 0;
}
