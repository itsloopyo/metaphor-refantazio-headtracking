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
)

target_include_directories(MetaphorHeadTracking PRIVATE
    src/MetaphorHeadTracking
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
endif()
