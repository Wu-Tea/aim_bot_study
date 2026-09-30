add_library(vision_native_core STATIC
    src/build_family.cpp
    src/color_readback.cpp
    src/dxgi_capture.cpp
    src/qpc_steady_clock.cpp
    src/present_interval_contract.cpp
    src/engine_io.cpp
    src/preprocess.cu
    src/resize_contract.cpp
    src/target_selector.cpp
    src/tensorrt_engine.cpp
    src/tensorrt_inspector.cpp
    src/vision_engine.cpp)
target_include_directories(vision_native_core PUBLIC
    include
    ${PROJECT_SOURCE_DIR}
    PRIVATE "${TensorRT_INCLUDE_DIR}")
target_compile_definitions(vision_native_core PUBLIC VISION_BUILD_FAMILY_MODERN=1)
target_compile_definitions(vision_native_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(vision_native_core PROPERTIES
    CUDA_SEPARABLE_COMPILATION OFF
    CUDA_ARCHITECTURES "${CMAKE_CUDA_ARCHITECTURES}")
target_link_libraries(vision_native_core PUBLIC
    CUDA::cudart
    ${TensorRT_LIBS}
    d3d11
    dxgi)
