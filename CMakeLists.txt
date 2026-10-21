cmake_minimum_required(VERSION 3.24)

project(EEEsSA VERSION 0.1.0 LANGUAGES CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(EEE_BUILD_TESTS "Build the test suite" ON)
option(EEE_BACKEND_OPENGL "Build the OpenGL backend (macOS)" ON)
option(EEE_BACKEND_METAL "Build the Metal backend (macOS)" ON)
option(EEE_BACKEND_VULKAN "Build the Vulkan backend placeholder" OFF)

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
function(eee_apply_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wall>
        $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
        $<$<COMPILE_LANGUAGE:CXX>:-Wpedantic>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-unused-parameter>
        $<$<COMPILE_LANGUAGE:OBJCXX>:-Wall>
        $<$<COMPILE_LANGUAGE:OBJCXX>:-Wextra>
        $<$<COMPILE_LANGUAGE:OBJCXX>:-Wno-unused-parameter>)
endfunction()

function(eee_enable_arc target)
    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)
endfunction()

# ---------------------------------------------------------------------------
# core: math, types, input snapshot, logging. No engine dependencies.
# ---------------------------------------------------------------------------
add_library(eee_core STATIC
    src/core/src/Log.cpp)
target_include_directories(eee_core PUBLIC src/core/include)
target_compile_features(eee_core PUBLIC cxx_std_20)
eee_apply_warnings(eee_core)

# ---------------------------------------------------------------------------
# scene: the data contract between logic and rendering. Header-only.
# ---------------------------------------------------------------------------
add_library(eee_scene INTERFACE)
target_include_directories(eee_scene INTERFACE src/scene/include)
target_link_libraries(eee_scene INTERFACE eee_core)

# ---------------------------------------------------------------------------
# logic: gameplay simulation. Deliberately does NOT link eee_render/eee_platform.
# ---------------------------------------------------------------------------
add_library(eee_logic STATIC
    src/logic/src/World.cpp
    src/logic/src/RpgGame.cpp
    src/logic/src/Classes.cpp
    src/logic/src/Items.cpp
    src/logic/src/Zones.cpp)
target_include_directories(eee_logic PUBLIC src/logic/include)
target_link_libraries(eee_logic PUBLIC eee_core eee_scene)
eee_apply_warnings(eee_logic)

# ---------------------------------------------------------------------------
# platform: window + input. Graphics-API agnostic.
# ---------------------------------------------------------------------------
add_library(eee_platform STATIC
    src/platform/src/Audio.cpp
    src/platform/src/Window.cpp
    src/platform/src/NullWindow.cpp)
target_include_directories(eee_platform PUBLIC src/platform/include)
target_link_libraries(eee_platform PUBLIC eee_core)
eee_apply_warnings(eee_platform)
eee_enable_arc(eee_platform)

if(APPLE)
    target_sources(eee_platform PRIVATE src/platform/macos/CocoaWindow.mm src/platform/macos/AudioMac.mm)
    target_link_libraries(eee_platform PRIVATE "-framework Cocoa")
endif()

# ---------------------------------------------------------------------------
# render: backend-agnostic renderer + device registry.
# Deliberately does NOT link eee_logic — only the scene contract.
# ---------------------------------------------------------------------------
add_library(eee_render STATIC
    src/render/src/Renderer.cpp
    src/render/src/DebugText.cpp
    src/render/src/DeviceRegistry.cpp)
target_include_directories(eee_render PUBLIC src/render/include)
target_link_libraries(eee_render PUBLIC eee_core eee_scene eee_platform)
eee_apply_warnings(eee_render)

# ---------------------------------------------------------------------------
# net: protocol + transport (no gameplay dependency).
# ---------------------------------------------------------------------------
add_library(eee_net STATIC
    src/net/src/Protocol.cpp
    src/net/src/TcpTransport.cpp)
target_include_directories(eee_net PUBLIC src/net/include)
target_link_libraries(eee_net PUBLIC eee_core)
eee_apply_warnings(eee_net)

# ---------------------------------------------------------------------------
# session: fixed-tick simulation driver (single-player now, lockstep later).
# ---------------------------------------------------------------------------
add_library(eee_session STATIC
    src/session/src/SessionDriver.cpp
    src/session/src/LockstepSession.cpp
    src/session/src/AuthoritativeClient.cpp)
target_include_directories(eee_session PUBLIC src/session/include)
target_link_libraries(eee_session PUBLIC eee_core eee_logic eee_net)
eee_apply_warnings(eee_session)

# ---------------------------------------------------------------------------
# backends: concrete graphics APIs implementing IRenderDevice.
# ---------------------------------------------------------------------------
add_library(eee_backends STATIC
    src/backends/registry/Backends.cpp
    src/backends/null/NullDevice.cpp)
target_include_directories(eee_backends PUBLIC src/backends/include)
target_link_libraries(eee_backends PUBLIC eee_render)
eee_apply_warnings(eee_backends)
eee_enable_arc(eee_backends)

set(EEE_HAS_OPENGL 0)
if(EEE_BACKEND_OPENGL)
    if(APPLE)
        target_sources(eee_backends PRIVATE src/backends/opengl/OpenGLDevice.mm)
        target_link_libraries(eee_backends PRIVATE "-framework Cocoa" "-framework OpenGL")
        set(EEE_HAS_OPENGL 1)
    else()
        message(STATUS "OpenGL backend skipped: currently implemented for macOS")
    endif()
endif()

set(EEE_HAS_METAL 0)
if(EEE_BACKEND_METAL)
    if(APPLE)
        target_sources(eee_backends PRIVATE src/backends/metal/MetalDevice.mm)
        target_link_libraries(eee_backends PRIVATE
            "-framework Cocoa" "-framework Metal" "-framework QuartzCore")
        set(EEE_HAS_METAL 1)
    else()
        message(STATUS "Metal backend skipped: Metal is macOS only")
    endif()
endif()

set(EEE_HAS_VULKAN 0)
if(EEE_BACKEND_VULKAN)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        target_sources(eee_backends PRIVATE src/backends/vulkan/VulkanDevice.cpp)
        target_link_libraries(eee_backends PRIVATE Vulkan::Vulkan)
        set(EEE_HAS_VULKAN 1)
        message(STATUS "Vulkan backend: placeholder registered (not implemented yet)")
    else()
        message(STATUS "Vulkan backend skipped: no Vulkan SDK found")
    endif()
endif()

target_compile_definitions(eee_backends PUBLIC
    EEE_HAS_OPENGL=${EEE_HAS_OPENGL}
    EEE_HAS_METAL=${EEE_HAS_METAL}
    EEE_HAS_VULKAN=${EEE_HAS_VULKAN})

# ---------------------------------------------------------------------------
# Dedicated relay server (no window, no GPU, no simulation).
# ---------------------------------------------------------------------------
add_executable(eee_server src/server/main.cpp)
target_link_libraries(eee_server PRIVATE eee_net eee_logic eee_core)
eee_apply_warnings(eee_server)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_executable(eee_demo src/app/main.cpp)
target_link_libraries(eee_demo PRIVATE
    eee_session
    eee_backends)
eee_apply_warnings(eee_demo)

# ---------------------------------------------------------------------------
# Tests: logic unit tests + renderer tests against a recording mock device.
# Neither target needs a window or a GPU.
# ---------------------------------------------------------------------------
if(EEE_BUILD_TESTS)
    enable_testing()

    add_executable(eee_tests src/tests/logic_tests.cpp)
    target_link_libraries(eee_tests PRIVATE eee_logic)
    eee_apply_warnings(eee_tests)

    # Render-layer tests: text overlay only, no device or window required.
    add_executable(eee_render_tests src/tests/render_tests.cpp)
    target_link_libraries(eee_render_tests PRIVATE eee_render)
    eee_apply_warnings(eee_render_tests)

    add_executable(eee_session_tests src/tests/session_tests.cpp)
    target_link_libraries(eee_session_tests PRIVATE eee_session)
    eee_apply_warnings(eee_session_tests)

    add_executable(eee_net_tests src/tests/net_tests.cpp)
    target_link_libraries(eee_net_tests PRIVATE eee_net)
    eee_apply_warnings(eee_net_tests)

    add_test(NAME logic COMMAND eee_tests)
    add_test(NAME render COMMAND eee_render_tests)
    add_test(NAME session COMMAND eee_session_tests)
    add_test(NAME net COMMAND eee_net_tests)
    set_tests_properties(session net PROPERTIES SKIP_RETURN_CODE 77)

    add_test(NAME coop_smoke
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/coop_smoke.sh $<TARGET_FILE:eee_demo>)
    set_tests_properties(coop_smoke PROPERTIES TIMEOUT 30 SKIP_RETURN_CODE 77)

    add_test(NAME dedicated_smoke
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/dedicated_smoke.sh
                $<TARGET_FILE:eee_demo> $<TARGET_FILE:eee_server>)
    set_tests_properties(dedicated_smoke PROPERTIES TIMEOUT 40 SKIP_RETURN_CODE 77)
    add_test(NAME headless_smoke COMMAND eee_demo --backend=null --headless --frames=30)
    add_test(NAME gameplay_smoke COMMAND eee_demo --backend=null --headless --frames=60 --fps --class=warrior)
    add_test(NAME fps_overlay_smoke COMMAND eee_demo --backend=null --headless --frames=30 --fps)
    add_test(NAME vsync_off_smoke COMMAND eee_demo --backend=null --headless --frames=30 --no-vsync)
endif()
