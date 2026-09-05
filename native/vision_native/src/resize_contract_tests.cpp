#include "vision_native/resize_contract.h"
#include "vision_native/capture_geometry.h"
#include "test_support/native_test_registry.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition) {
    if (!condition) throw std::runtime_error("resize contract assertion failed");
}

void test_production_contract() {
    const auto contract =
        vision_native::validate_resize_contract(480, 416, 480, 416, 480, 416, true);
    require(contract.isotropic);
    require(std::abs(contract.scale_x - 1.0f) < 0.0001f);
    require(std::abs(contract.scale_y - 1.0f) < 0.0001f);
}

void test_exploration_contracts_are_uniform() {
    const auto balanced =
        vision_native::validate_resize_contract(640, 512, 480, 384, 480, 384, true);
    require(balanced.isotropic);
    require(std::abs(balanced.scale_x - (4.0f / 3.0f)) < 0.0001f);

    const auto near =
        vision_native::validate_resize_contract(640, 440, 512, 352, 512, 352, true);
    require(near.isotropic);
    require(std::abs(near.scale_x - 1.25f) < 0.0001f);

    const auto latency =
        vision_native::validate_resize_contract(630, 462, 480, 352, 480, 352, true);
    require(latency.isotropic);
    require(std::abs(latency.scale_x - 1.3125f) < 0.0001f);
}

void test_engine_shape_mismatch_is_rejected() {
    bool failed = false;
    try {
        (void)vision_native::validate_resize_contract(
            640, 512, 512, 352, 480, 384, true);
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        failed = message.find("480x384") != std::string::npos &&
            message.find("512x352") != std::string::npos;
    }
    require(failed);
}

void test_anisotropic_resize_requires_explicit_opt_out() {
    bool failed = false;
    try {
        (void)vision_native::validate_resize_contract(
            640, 512, 512, 352, 512, 352, true);
    } catch (const std::runtime_error& error) {
        failed = std::string(error.what()).find("anisotropic") != std::string::npos;
    }
    require(failed);

    const auto allowed =
        vision_native::validate_resize_contract(640, 512, 512, 352, 512, 352, false);
    require(!allowed.isotropic);
    require(allowed.scale_x != allowed.scale_y);
}

}  // namespace

void register_resize_contract_tests(native_test::Registry& registry) {
    registry.add_case("BaseContracts", "capture_geometry_refreshes_center_and_position", [] {
        auto geometry = vision_native::centered_capture_geometry(480, 416, 0, 0, 2560, 1440);
        require(geometry.roi_left == 1040 && geometry.roi_top == 512);
        geometry = vision_native::centered_capture_geometry(480, 416, -1920, 180, 1920, 1080);
        require(geometry.roi_left == 720 && geometry.roi_top == 332);
        require(geometry.output_left == -1920 && geometry.output_top == 180);
        bool rejected = false;
        try { (void)vision_native::centered_capture_geometry(480, 416, 0, 0, 320, 240); }
        catch (const std::runtime_error&) { rejected = true; }
        require(rejected);
    });
    registry.add_case("BaseContracts", "pointer_only_acquire_has_no_image_observation", [] {
        require(!vision_native::dxgi_has_new_desktop_image(0, 0));
        require(!vision_native::dxgi_has_new_desktop_image(0, 1));
        require(!vision_native::dxgi_has_new_desktop_image(100, 0));
        require(vision_native::dxgi_has_new_desktop_image(100, 1));
        require(vision_native::dxgi_has_new_desktop_image(200, 4));
    });
    registry.add_case("BaseContracts", "production_resize_contract", test_production_contract);
    registry.add_case("BaseContracts", "exploration_resize_is_uniform", test_exploration_contracts_are_uniform);
    registry.add_case("BaseContracts", "engine_shape_mismatch_is_rejected", test_engine_shape_mismatch_is_rejected);
    registry.add_case("BaseContracts", "anisotropic_resize_requires_opt_out", test_anisotropic_resize_requires_explicit_opt_out);
}
