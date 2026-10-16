# Included by native/CMakeLists.txt; source paths are relative to native/.

add_library(fusion_overlay_core STATIC
    overlay_canvas/fusion_overlay_contract.cpp
    overlay_canvas/fusion_overlay_contract.h
    overlay_canvas/overlay_window_policy.h)
cod_native_defaults(fusion_overlay_core)
target_include_directories(fusion_overlay_core PUBLIC
    ${PROJECT_SOURCE_DIR}/overlay_canvas)

add_library(fusion_capture_support STATIC
    vision_native/src/dxgi_capture.cpp
    vision_native/src/qpc_steady_clock.cpp)
cod_native_defaults(fusion_capture_support)
target_include_directories(fusion_capture_support PUBLIC
    ${PROJECT_SOURCE_DIR}/vision_native/include)
target_link_libraries(fusion_capture_support PUBLIC d3d11 dxgi)

add_executable(fusion_canvas
    overlay_canvas/fusion_canvas.cpp
    overlay_canvas/capture_isolation_guard.cpp
    overlay_canvas/capture_isolation_guard.h)
cod_native_defaults(fusion_canvas)
target_link_libraries(fusion_canvas PRIVATE
    fusion_shared fusion_overlay_core fusion_capture_support
    d3d11 d2d1 dxgi dcomp dwrite Shcore dwmapi)
