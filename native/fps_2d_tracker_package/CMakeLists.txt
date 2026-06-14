cmake_minimum_required(VERSION 3.16)
project(fps_2d_tracker LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(fps_tracker
    src/projection_model.cpp
    src/stick_projector.cpp
    src/recoil_visual_model.cpp
    src/ego_motion_buffer.cpp
    src/kalman_cv2d.cpp
    src/association.cpp
    src/target_tracker.cpp
)

target_include_directories(fps_tracker PUBLIC include)

target_compile_options(fps_tracker PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

add_executable(synthetic_replay examples/synthetic_replay.cpp)
target_link_libraries(synthetic_replay PRIVATE fps_tracker)

add_executable(tracker_invariants tests/tracker_invariants.cpp)
target_link_libraries(tracker_invariants PRIVATE fps_tracker)
