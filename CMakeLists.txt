cmake_minimum_required(VERSION 3.20)
project(MetaphorHeadTracking VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "MetaphorHeadTracking targets Windows x64 only.")
endif()

add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# --- MinHook (provides the `minhook` target the core hook manager needs) ---
add_subdirectory(third_party/minhook)

# --- cameraunlock-core C++ static libs ---
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_HOOKS ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_DISCOVERY ON CACHE BOOL "" FORCE)
add_subdirectory(cameraunlock-core/cpp)

# --- The mod itself: a DLL renamed to .asi by the packager ---
add_library(MetaphorHeadTracking SHARED
    src/MetaphorHeadTracking/dllmain.cpp
    src/MetaphorHeadTracking/mod.cpp
    src/MetaphorHeadTracking/present_hook.cpp
    src/MetaphorHeadTracking/camera_hook.cpp
    src/MetaphorHeadTracking/build_profiles.cpp
    src/MetaphorHeadTracking/config.cpp
    src/legacy_config/legacy_config.cpp
)

target_include_directories(MetaphorHeadTracking PRIVATE
    src/MetaphorHeadTracking
    src
    cameraunlock-core/cpp/include
    third_party/minhook/include
)

target_link_libraries(MetaphorHeadTracking PRIVATE
    cameraunlock
    cameraunlock_hooks
    cameraunlock_discovery
    minhook
    d3d11
    dxgi
    ws2_32
)

if(MSVC)
    target_compile_options(MetaphorHeadTracking PRIVATE
        /W4 /EHa
        $<$<CONFIG:Release>:/O2>
    )
endif()

set_target_properties(MetaphorHeadTracking PROPERTIES
    OUTPUT_NAME "MetaphorHeadTracking"
    SUFFIX ".asi"
)

# --- Tests (opt-in; off by default so build/package stay untouched) ---
option(METAPHOR_BUILD_TESTS "Build native unit tests" OFF)
if(METAPHOR_BUILD_TESTS)
    enable_testing()
    add_executable(packet_parser_tests tests/packet_parser_tests.cpp)
    target_include_directories(packet_parser_tests PRIVATE cameraunlock-core/cpp/include)
    target_link_libraries(packet_parser_tests PRIVATE cameraunlock)
    add_test(NAME packet_parser_tests COMMAND packet_parser_tests)

    # The config differential test. The oracle is the dev pre-release's reader with the core
    # sources it compiled, copied verbatim into tests/config_differential/oracle, and that build's
    # hotkey registration over the chord guards of its pin. It is two libraries, each renaming the
    # namespaces its own way, so neither collides with the other or with the current core.
    set(METAPHOR_ORACLE_DIR ${CMAKE_SOURCE_DIR}/tests/config_differential/oracle)
    add_library(metaphor_config_oracle STATIC
        ${METAPHOR_ORACLE_DIR}/core/src/config/ini_reader.cpp
        ${METAPHOR_ORACLE_DIR}/core/src/logging/file_log.cpp
        tests/config_differential/oracle_config_adapter.cpp
    )
    target_include_directories(metaphor_config_oracle PRIVATE
        ${METAPHOR_ORACLE_DIR}/src
        ${METAPHOR_ORACLE_DIR}/core/include
        ${CMAKE_SOURCE_DIR}/tests/config_differential)
    target_compile_definitions(metaphor_config_oracle PRIVATE
        cameraunlock=mrf_oracle_core
        metaphor=mrf_oracle)

    add_library(metaphor_hotkeys_oracle STATIC
        tests/config_differential/oracle_hotkeys_adapter.cpp
    )
    target_include_directories(metaphor_hotkeys_oracle PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/config_differential/oracle_fake
        ${METAPHOR_ORACLE_DIR}/core/include
        ${CMAKE_SOURCE_DIR}/tests/config_differential)
    target_compile_definitions(metaphor_hotkeys_oracle PRIVATE
        cameraunlock=mrf_oracle_hk_core)

    add_executable(config_differential_tests
        tests/config_differential/differential_tests.cpp
        src/MetaphorHeadTracking/config.cpp
        src/legacy_config/legacy_config.cpp
    )
    target_include_directories(config_differential_tests PRIVATE
        src/MetaphorHeadTracking
        src
        cameraunlock-core/cpp/include
        tests/config_differential)
    target_compile_definitions(config_differential_tests PRIVATE
        METAPHOR_DIFFERENTIAL_DATA="${CMAKE_SOURCE_DIR}/tests/config_differential/data"
        METAPHOR_COMMITTED_CONFIG="${CMAKE_SOURCE_DIR}/config/CameraUnlock.ini")
    target_link_libraries(config_differential_tests PRIVATE
        cameraunlock metaphor_config_oracle metaphor_hotkeys_oracle)
    if(MSVC)
        target_compile_options(config_differential_tests PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME config_differential_tests COMMAND config_differential_tests)

    # Core's canonical config lint over the committed file and every file the differential test
    # migrated, which it writes beside itself under migrated.
    add_test(NAME config_lint COMMAND node
        ${CMAKE_SOURCE_DIR}/tests/config_differential/lint-migrated.mjs
        $<TARGET_FILE_DIR:config_differential_tests>/migrated)
    set_tests_properties(config_lint PROPERTIES DEPENDS config_differential_tests)

    # The committed config against the table, a first start, the toggles' saves and the legacy
    # file's ANSI path. `--render-config <path>` makes it write the committed file instead
    # (pixi run render-config).
    add_executable(config_tests
        tests/config_tests.cpp
        src/MetaphorHeadTracking/config.cpp
        src/legacy_config/legacy_config.cpp
    )
    target_include_directories(config_tests PRIVATE
        src/MetaphorHeadTracking
        src
        cameraunlock-core/cpp/include)
    target_compile_definitions(config_tests PRIVATE
        METAPHOR_COMMITTED_CONFIG="${CMAKE_SOURCE_DIR}/config/CameraUnlock.ini")
    target_link_libraries(config_tests PRIVATE cameraunlock)
    if(MSVC)
        target_compile_options(config_tests PRIVATE /W4 /permissive-)
    endif()
    add_test(NAME config_tests COMMAND config_tests)
endif()
