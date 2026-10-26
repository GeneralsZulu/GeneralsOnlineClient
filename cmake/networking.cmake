find_package(OpenSSL REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

set(SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "Build Sentry as a static library" FORCE)
set(SENTRY_BUILD_TESTS OFF CACHE BOOL "Do not build Sentry tests" FORCE)
set(SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "Do not build Sentry examples" FORCE)
set(SENTRY_ENABLE_INSTALL OFF CACHE BOOL "Do not install Sentry" FORCE)
set(SENTRY_TRANSPORT winhttp CACHE STRING "Use WinHTTP for Sentry transport" FORCE)
# breakpad rather than inproc: inproc captures the crashing thread's frames
# only, breakpad writes a real minidump (every thread, registers, stack memory)
# and attaches it to the report, with no external handler process to ship.
set(SENTRY_BACKEND breakpad CACHE STRING "Use the in-process breakpad backend for Sentry crash capture" FORCE)

FetchContent_Declare(
    sentry_native
    GIT_REPOSITORY https://github.com/getsentry/sentry-native.git
    GIT_TAG        0.9.0
)
FetchContent_MakeAvailable(sentry_native)

set(CURL_USE_OPENSSL ON CACHE BOOL "Build libcurl with OpenSSL" FORCE)
set(CURL_DISABLE_WEBSOCKETS OFF CACHE BOOL "Enable libcurl WebSocket support" FORCE)
set(BUILD_CURL_EXE OFF CACHE BOOL "Do not build the curl command-line tool" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "Do not build third-party tests" FORCE)
set(BUILD_SHARED_LIBS ON CACHE BOOL "Build libcurl as a shared library" FORCE)
set(BUILD_STATIC_LIBS OFF CACHE BOOL "Do not build a static libcurl" FORCE)

FetchContent_Declare(
    curl
    GIT_REPOSITORY https://github.com/curl/curl.git
    GIT_TAG        curl-8_11_0
)
FetchContent_MakeAvailable(curl)

set(BUILD_STATIC_LIB OFF CACHE BOOL "Do not build the static GameNetworkingSockets library" FORCE)
set(BUILD_SHARED_LIB ON CACHE BOOL "Build the shared GameNetworkingSockets library" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Do not build GameNetworkingSockets examples" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Do not build GameNetworkingSockets tests" FORCE)
set(BUILD_TOOLS OFF CACHE BOOL "Do not build GameNetworkingSockets tools" FORCE)
set(ENABLE_ICE ON CACHE BOOL "Enable GameNetworkingSockets NAT traversal" FORCE)
set(USE_STEAMWEBRTC ON CACHE BOOL "Enable GameNetworkingSockets WebRTC ICE support" FORCE)
set(USE_CRYPTO OpenSSL CACHE STRING "Use OpenSSL for GameNetworkingSockets crypto" FORCE)
set(Protobuf_USE_STATIC_LIBS OFF CACHE BOOL "Use shared protobuf when available" FORCE)

FetchContent_Declare(
    gamenetworkingsockets
    # GeneralsOnline's fork: v1.6.1 plus their ICE fixes (STUN/TURN failover,
    # async DNS, no shutdown hangs). Same pin as the vcpkg registry upstream uses.
    GIT_REPOSITORY https://github.com/tintinhamans/GNS.git
    GIT_TAG        d2837fac6aa4329f96887b71929e6799e9bcc1ac
    GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_Populate(gamenetworkingsockets)

# The WebRTC wrapper bundled with GNS predates vcpkg's protobuf package and
# otherwise adds its vendored Abseil targets a second time.
set(_gns_steamwebrtc_cmake
    "${gamenetworkingsockets_SOURCE_DIR}/src/external/steamwebrtc/CMakeLists.txt")
file(READ "${_gns_steamwebrtc_cmake}" _gns_steamwebrtc_contents)
string(REPLACE
    "set(absl_FOUND OFF)"
    "find_package(absl CONFIG REQUIRED)"
    _gns_steamwebrtc_contents
    "${_gns_steamwebrtc_contents}")
file(WRITE "${_gns_steamwebrtc_cmake}" "${_gns_steamwebrtc_contents}")

# The fork's native ICE client never refreshes TURN permissions, so every relayed
# link dies 300 s after it connects. Skip if a previous configure already applied it;
# --ignore-whitespace because the build checks GNS out with CRLF endings.
# The docker build runs Windows CMake under wine, where git is only reachable as
# GIT_EXECUTABLE.
find_package(Git REQUIRED)
set(_gns_turn_patch "${CMAKE_CURRENT_LIST_DIR}/patches/gns-turn-permission-refresh.patch")
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace --reverse --check "${_gns_turn_patch}"
    WORKING_DIRECTORY "${gamenetworkingsockets_SOURCE_DIR}"
    RESULT_VARIABLE _gns_turn_patch_applied
    OUTPUT_QUIET ERROR_QUIET)
if(NOT _gns_turn_patch_applied EQUAL 0)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace "${_gns_turn_patch}"
        WORKING_DIRECTORY "${gamenetworkingsockets_SOURCE_DIR}"
        RESULT_VARIABLE _gns_turn_patch_result
        ERROR_VARIABLE _gns_turn_patch_error)
    if(NOT _gns_turn_patch_result EQUAL 0)
        message(FATAL_ERROR "Could not apply ${_gns_turn_patch} (${_gns_turn_patch_result}): ${_gns_turn_patch_error}")
    endif()
endif()

add_subdirectory(
    "${gamenetworkingsockets_SOURCE_DIR}"
    "${gamenetworkingsockets_BINARY_DIR}"
)
