# Included by native/CMakeLists.txt; source paths are relative to native/.

cod_native_add_offline_test(cod_native_aimlab_benchmark_tests NativeAimlabBenchmarkTests
    controller_native/aimlab_benchmark_tests.cpp
    controller_native/aimlab_benchmark.cpp)
target_link_libraries(cod_native_aimlab_benchmark_tests PRIVATE vision_native_core)
cod_native_add_offline_test(cod_native_sustained_aimlab_scenario_tests NativeSustainedAimlabScenarioTests
    controller_native/sustained_aimlab_scenario_tests.cpp
    controller_native/sustained_aimlab_scenario.cpp)
cod_native_add_offline_test(cod_native_sustained_aimlab_score_tests NativeSustainedAimlabScoreTests
    controller_native/sustained_aimlab_score_tests.cpp
    controller_native/sustained_aimlab_score.cpp)
cod_native_add_offline_test(cod_native_sustained_aimlab_simulator_tests NativeSustainedAimlabSimulatorTests
    controller_native/sustained_aimlab_simulator_tests.cpp
    controller_native/sustained_aimlab_simulator.cpp
    controller_native/sustained_aimlab_score.cpp
    controller_native/sustained_aimlab_scenario.cpp)
cod_native_add_offline_test(cod_native_sustained_aimlab_trace_tests NativeSustainedAimlabTraceTests
    controller_native/sustained_aimlab_trace_tests.cpp
    controller_native/sustained_aimlab_trace.cpp
    controller_native/sustained_aimlab_scenario.cpp)
cod_native_add_offline_test(cod_native_benchmark_physical_input_tests NativeBenchmarkPhysicalInputTests
    controller_native/native_benchmark_physical_input_tests.cpp)
cod_native_add_offline_test(cod_native_benchmark_controller_adapter_tests NativeBenchmarkControllerAdapterTests
    controller_native/native_benchmark_controller_adapter_tests.cpp
    controller_native/native_benchmark_controller_adapter.cpp)
target_link_libraries(cod_native_benchmark_controller_adapter_tests PRIVATE
    controller_native_core vision_native_core gdi32 windowsapp)
cod_native_add_offline_test(cod_native_pid_benchmark_controller_tests NativePidBenchmarkControllerTests
    controller_native/pid_benchmark_controller_tests.cpp)
add_executable(cod_native_aimlab_benchmark
    controller_native/cod_native_aimlab_benchmark.cpp
    controller_native/aimlab_benchmark.cpp)
cod_native_defaults(cod_native_aimlab_benchmark)
target_link_libraries(cod_native_aimlab_benchmark PRIVATE vision_native_core)

add_executable(cod_native_sustained_aimlab_benchmark
    controller_native/cod_native_sustained_aimlab_benchmark.cpp
    controller_native/native_benchmark_controller_adapter.cpp
    controller_native/sustained_aimlab_trace.cpp
    controller_native/sustained_aimlab_simulator.cpp
    controller_native/sustained_aimlab_score.cpp
    controller_native/sustained_aimlab_scenario.cpp)
cod_native_defaults(cod_native_sustained_aimlab_benchmark)
target_link_libraries(cod_native_sustained_aimlab_benchmark PRIVATE
    controller_native_core vision_native_core gdi32 windowsapp)

add_executable(cod_native_oscillation_scan EXCLUDE_FROM_ALL
    controller_native/oscillation_scan.cpp)
cod_native_defaults(cod_native_oscillation_scan)
target_link_libraries(cod_native_oscillation_scan PRIVATE controller_native_core)

if(NATIVE_TEST_ENABLE_OFFLINE_BENCHMARKS)
    add_test(NAME NativeSustainedAimlabLeftStrafeOff
        COMMAND cod_native_sustained_aimlab_benchmark
            --config ${PROJECT_SOURCE_DIR}/../config.native.example.toml
            --duration-ms 1575 --seed 2026072301
            --profile pure --cohort ads --smoke --left-strafe off)
    add_test(NAME NativeSustainedAimlabLeftStrafeBoth
        COMMAND cod_native_sustained_aimlab_benchmark
            --config ${PROJECT_SOURCE_DIR}/../config.native.example.toml
            --duration-ms 1575 --seed 2026072301
            --profile pure --cohort ads --smoke --left-strafe both)
    add_test(NAME NativeSustainedAimlabBodyLockFreshLtPerTarget
        COMMAND cod_native_sustained_aimlab_benchmark
            --config ${PROJECT_SOURCE_DIR}/../config.native.example.toml
            --duration-ms 6000 --seed 2026082703
            --profile pure --cohort bodylock --vision-hz 100
            --smoke --left-strafe off)
    add_test(NAME NativeSustainedAimlabEightKhzCounterfactual
        COMMAND cod_native_sustained_aimlab_benchmark
            --config ${PROJECT_SOURCE_DIR}/../config.native.example.toml
            --duration-ms 1575 --seed 2026082703
            --controller-tick-hz 8000 --vision-hz 200
            --profile pure --cohort ads --target-motion stationary
            --smoke --left-strafe off)
    add_test(NAME NativeSustainedAimlabLeftStrafeRejectsInvalid
        COMMAND cod_native_sustained_aimlab_benchmark
            --config ${PROJECT_SOURCE_DIR}/../config.native.example.toml
            --duration-ms 100 --seed 2026072301
            --profile pure --cohort ads --left-strafe invalid)
    set_tests_properties(
        NativeSustainedAimlabLeftStrafeRejectsInvalid
        PROPERTIES WILL_FAIL TRUE)
    add_test(NAME NativeRuntimeRejectsRetiredAiCadenceCli
        COMMAND cod_native_runtime
            --config ${PROJECT_SOURCE_DIR}/../config.native.example.toml
            --dump-effective-config --ai-proposal-mode fixed)
    add_test(NAME NativeSustainedAimlabRejectsRetiredAiCadence
        COMMAND cod_native_sustained_aimlab_benchmark
            --config ${PROJECT_SOURCE_DIR}/../config.native.example.toml
            --duration-ms 100 --seed 2026072301
            --profile pure --cohort ads
            --ai-proposal-mode fixed --ai-proposal-hz 250)
    set_tests_properties(
        NativeRuntimeRejectsRetiredAiCadenceCli
        NativeSustainedAimlabRejectsRetiredAiCadence
        PROPERTIES WILL_FAIL TRUE)
else()
    set_target_properties(
        cod_native_aimlab_benchmark
        cod_native_sustained_aimlab_benchmark
        PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
