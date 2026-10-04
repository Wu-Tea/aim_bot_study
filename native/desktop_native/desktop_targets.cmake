# Native desktop/config/process adapter. Win32 only; no scripting runtime.
add_library(desktop_native_session STATIC desktop_native/desktop_session.cpp)
cod_native_defaults(desktop_native_session)
target_link_libraries(desktop_native_session PUBLIC controller_native_core)
add_executable(cod_native_assistant desktop_native/desktop_main.cpp)
cod_native_defaults(cod_native_assistant)
target_link_libraries(cod_native_assistant PRIVATE desktop_native_session user32 shell32)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/desktop-ui-fixture")
configure_file("${PROJECT_SOURCE_DIR}/../config.native.example.toml"
    "${CMAKE_CURRENT_BINARY_DIR}/desktop-ui-fixture/config.toml" COPYONLY)
add_test(NAME FeatureDesktopWindow COMMAND cod_native_assistant --action ui-check
    --project "${CMAKE_CURRENT_BINARY_DIR}/desktop-ui-fixture")
set_tests_properties(FeatureDesktopWindow PROPERTIES LABELS "functional;desktop")
