cmake_minimum_required(VERSION 3.26)

project(cod_native LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 89)
endif()

option(NATIVE_ENABLE_VIGEM "Enable ViGEmClient output for cod_native_runtime" OFF)
option(NATIVE_TEST_ENABLE_OFFLINE_BENCHMARKS
    "Build and register AimLab, PID, scenario, score, and trace tests"
    OFF)
set(ViGEmClient_DLL "" CACHE FILEPATH "Path to ViGEmClient.dll copied beside cod_native_runtime")
set(SDL2_DLL "" CACHE FILEPATH "Path to SDL2.dll copied beside cod_native_runtime")
set(TensorRT_ROOT "" CACHE PATH "Path to the TensorRT Windows SDK root")

if(NOT TensorRT_ROOT)
    if(DEFINED ENV{TensorRT_ROOT})
        set(TensorRT_ROOT "$ENV{TensorRT_ROOT}")
    elseif(DEFINED ENV{TENSORRT_ROOT})
        set(TensorRT_ROOT "$ENV{TENSORRT_ROOT}")
    else()
        set(TensorRT_ROOT "D:/env/TensorRT-10.15.1.29")
    endif()
endif()

if(NOT ViGEmClient_DLL)
    if(DEFINED ENV{VIGEM_CLIENT_DLL})
        set(ViGEmClient_DLL "$ENV{VIGEM_CLIENT_DLL}" CACHE FILEPATH "Path to ViGEmClient.dll" FORCE)
    else()
        set(_default_vigem_client_dll "D:/env/python/Lib/site-packages/vgamepad/win/vigem/client/x64/ViGEmClient.dll")
        if(EXISTS "${_default_vigem_client_dll}")
            set(ViGEmClient_DLL "${_default_vigem_client_dll}" CACHE FILEPATH "Path to ViGEmClient.dll" FORCE)
        endif()
    endif()
endif()

if(NOT SDL2_DLL)
    if(DEFINED ENV{SDL2_DLL})
        set(SDL2_DLL "$ENV{SDL2_DLL}" CACHE FILEPATH "Path to SDL2.dll" FORCE)
    else()
        set(_default_sdl2_dll "D:/env/python/Lib/site-packages/pygame/SDL2.dll")
        if(EXISTS "${_default_sdl2_dll}")
            set(SDL2_DLL "${_default_sdl2_dll}" CACHE FILEPATH "Path to SDL2.dll" FORCE)
        endif()
    endif()
endif()

file(TO_CMAKE_PATH "${TensorRT_ROOT}" TensorRT_ROOT)
set(TensorRT_INCLUDE_DIR "${TensorRT_ROOT}/include")
set(TensorRT_LIB_DIR "${TensorRT_ROOT}/lib")
set(TensorRT_BIN_DIR "${TensorRT_ROOT}/bin")

if(NOT EXISTS "${TensorRT_INCLUDE_DIR}/NvInfer.h")
    message(FATAL_ERROR "NvInfer.h not found. Pass -DTensorRT_ROOT=<TensorRT SDK root>.")
endif()

find_package(CUDAToolkit REQUIRED)
find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
find_package(pybind11 CONFIG REQUIRED)

set(TensorRT_LIBS
    "${TensorRT_LIB_DIR}/nvinfer_10.lib"
    "${TensorRT_LIB_DIR}/nvinfer_plugin_10.lib")
foreach(_lib IN LISTS TensorRT_LIBS)
    if(NOT EXISTS "${_lib}")
        message(FATAL_ERROR "Required TensorRT library missing: ${_lib}")
    endif()
endforeach()

enable_testing()

function(cod_native_defaults target)
    target_include_directories(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}/vision_native/include
        ${PROJECT_SOURCE_DIR}
        ${PROJECT_SOURCE_DIR}/controller_native)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(${target} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)
endfunction()

# Explicit hardware gate: requires a CUDA-capable D3D11 adapter, never ViGEm
# or a live game. Not part of portable/CPU-only CTest runs.
add_executable(cod_native_cuda_d3d11_interop_tests EXCLUDE_FROM_ALL
    vision_native/src/cuda_d3d11_interop_tests.cpp)
cod_native_defaults(cod_native_cuda_d3d11_interop_tests)
target_link_libraries(cod_native_cuda_d3d11_interop_tests PRIVATE CUDA::cudart d3d11 dxgi)

function(cod_native_add_offline_test target test_name)
    add_executable(${target} ${ARGN})
    cod_native_defaults(${target})
    if(NATIVE_TEST_ENABLE_OFFLINE_BENCHMARKS)
        add_test(NAME ${test_name} COMMAND ${target})
    else()
        set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
endfunction()

add_subdirectory(vision_native)

add_library(fusion_shared STATIC
    shared_fusion/fusion_channel.cpp
    shared_fusion/fusion_channel.h)
target_include_directories(fusion_shared PUBLIC ${PROJECT_SOURCE_DIR})
target_compile_options(fusion_shared PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8>)

include(controller_native/controller_targets.cmake)

include(runtime_app/runtime_targets.cmake)

add_executable(vision_native_smoke vision_native/src/trt_smoke_main.cpp)
add_executable(cod_native_ds4_smoke EXCLUDE_FROM_ALL
    controller_native/ds4_output_smoke.cpp
    controller_native/virtual_gamepad.cpp
    controller_native/sdl_gamepad_reader.cpp
    controller_native/io_recovery_policy.cpp)
cod_native_defaults(cod_native_ds4_smoke)
foreach(_ds4_dll IN ITEMS "${ViGEmClient_DLL}" "${SDL2_DLL}")
    if(EXISTS "${_ds4_dll}")
        add_custom_command(TARGET cod_native_ds4_smoke POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${_ds4_dll}" "$<TARGET_FILE_DIR:cod_native_ds4_smoke>"
            VERBATIM)
    endif()
endforeach()
target_link_libraries(vision_native_smoke PRIVATE vision_native_core)
add_executable(vision_native_debug vision_native/src/vision_debug_main.cpp)
target_link_libraries(vision_native_debug PRIVATE vision_native_core)
add_executable(vision_native_bgra_benchmark EXCLUDE_FROM_ALL
    vision_native/src/bgra_benchmark.cpp)
target_link_libraries(vision_native_bgra_benchmark PRIVATE vision_native_core)

add_executable(vision_native_color_readback_benchmark EXCLUDE_FROM_ALL
    vision_native/src/color_readback_benchmark.cpp vision_native/src/color_readback.cpp)
cod_native_defaults(vision_native_color_readback_benchmark)
target_include_directories(vision_native_color_readback_benchmark PRIVATE ${PROJECT_SOURCE_DIR}/vision_native/src)
target_link_libraries(vision_native_color_readback_benchmark PRIVATE CUDA::cudart)

add_executable(cod_native_scheduler_benchmark EXCLUDE_FROM_ALL
    runtime_app/scheduler_benchmark.cpp
    runtime_app/runtime_timing.cpp)
cod_native_defaults(cod_native_scheduler_benchmark)
target_link_libraries(cod_native_scheduler_benchmark PRIVATE winmm)

include(cmake/ProductTests.cmake)

include(cmake/OfflineBenchmarks.cmake)

include(overlay_canvas/fusion_targets.cmake)

pybind11_add_module(vision_native_cpp vision_native/src/vision_native_module.cpp)
set_target_properties(vision_native_cpp PROPERTIES OUTPUT_NAME "vision_native_cpp")
target_link_libraries(vision_native_cpp PRIVATE vision_native_core)

# Opt-in, game-independent GPU contention benchmarks. No controller or output device.
add_executable(vision_graphics_load EXCLUDE_FROM_ALL
    vision_native/src/vision_graphics_load.cpp runtime_app/runtime_timing.cpp)
cod_native_defaults(vision_graphics_load)
target_link_libraries(vision_graphics_load PRIVATE CUDA::cudart d3d11 dxgi d3dcompiler winmm)

add_executable(vision_contention_benchmark EXCLUDE_FROM_ALL
    vision_native/src/vision_contention_benchmark.cpp runtime_app/runtime_timing.cpp)
cod_native_defaults(vision_contention_benchmark)
target_link_libraries(vision_contention_benchmark PRIVATE vision_native_core winmm)

include(mouse_native/mouse_targets.cmake)

set(_runtime_dlls nvinfer_10.dll nvinfer_plugin_10.dll)
foreach(_target vision_native_smoke vision_native_debug vision_native_bgra_benchmark cod_native_runtime cod_native_mouse_runtime vision_native_cpp vision_contention_benchmark)
    foreach(_dll IN LISTS _runtime_dlls)
        add_custom_command(TARGET ${_target} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${TensorRT_BIN_DIR}/${_dll}"
                "$<TARGET_FILE_DIR:${_target}>/${_dll}"
            VERBATIM)
    endforeach()
endforeach()
