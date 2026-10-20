add_executable(z_generals WIN32)

# Use a binary name that doesn't conflict with original game.
if("${CMAKE_SYSTEM}" MATCHES "Windows")
    set_target_properties(z_generals PROPERTIES OUTPUT_NAME "GeneralsOnlineZH${RTS_BUILD_OUTPUT_SUFFIX}")
else()
    set_target_properties(z_generals PROPERTIES OUTPUT_NAME "GeneralsOnlineZH${RTS_BUILD_OUTPUT_SUFFIX}")
endif()

target_link_libraries(z_generals PRIVATE
    binkstub
    comctl32
    core_debug
    core_profile_legacy
    d3d8
    d3dx8
    dinput8
    dxguid
    imm32
    milesstub
    vfw32
    winmm
    z_gameengine
    z_gameenginedevice
    zi_always
)

# TODO Originally referred to build host and user, replace with git info perhaps?
#
# Compare-and-write, same as BuildVersion.h below. An unconditional file(WRITE)
# bumps this header's mtime on every configure, and because WinMain.cpp declares
# an OBJECT_DEPENDS on it (see below), every `--cmake` pass forced a WinMain.cpp
# recompile plus a full relink of the ~7.5 MB generalszh.exe even when nothing
# had changed. Every Make-driven build passes `--cmake` (make installer,
# installer-dev, zulu-exe, zulu-exe-log, zulu-launcher), so that was every build.
set(_zulu_generated_version_content
"#pragma once

#define VERSION_LOCALBUILDNUM 0
#define VERSION_BUILDUSER \"\"
#define VERSION_BUILDLOC \"\"
")
set(_zulu_generated_version_existing "")
if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/GeneratedVersion.h")
    file(READ "${CMAKE_CURRENT_BINARY_DIR}/GeneratedVersion.h" _zulu_generated_version_existing)
endif()
if(NOT _zulu_generated_version_existing STREQUAL _zulu_generated_version_content)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/GeneratedVersion.h "${_zulu_generated_version_content}")
endif()

# `make installer` overrides these via -DZULU_VERSION_* (parsed out of
# APPVERSION in installer/Zulu.nsi by the top-level Makefile) so the binary's
# TheVersion matches the installer version, which in turn flows into every
# replay header recorded by that binary.
#
# The defaults matter because the recorded version string is what
# deriveReplayEpochFromHeader() maps to a replay determinism epoch on
# playback. The old retail-style default (1.4.601) maps to REPLAY_EPOCH_V130,
# so a plain `cmake` build recorded a replay under the CURRENT gates and then
# replayed it with the newer ones switched off -- guaranteed divergence
# against its own recordings. Defaulting to the current release version
# instead makes a bare cmake build self-consistent and identical to what
# `make installer` produces from the same tree.
#
# Note that in a GENERALS_ONLINE build WinMain passes GENERALS_ONLINE_VERSION
# as the build number, so only MAJOR and MINOR reach the recorded version
# string (it renders as "1.6.<GENERALS_ONLINE_VERSION>"). MINOR is therefore
# the component that decides the epoch; BUILDNUM is kept in step with
# APPVERSION for the non-GO path and for RTS.RC's VERSIONINFO.
#
# Keep this in step with APPVERSION in installer/Zulu.nsi, and re-check it
# whenever a new epoch is added to RecorderClass::ReplayEpoch.
if(NOT DEFINED ZULU_VERSION_MAJOR)
    set(ZULU_VERSION_MAJOR 1)
endif()
if(NOT DEFINED ZULU_VERSION_MINOR)
    set(ZULU_VERSION_MINOR 6)
endif()
if(NOT DEFINED ZULU_VERSION_BUILDNUM)
    set(ZULU_VERSION_BUILDNUM 3)
endif()

# Which application icon the exe carries. Zulu GO installs into a subfolder of
# someone else's Zero Hour install and sits on the desktop next to the stock
# GeneralsOnline shortcut, so it gets the GeneralsZulu insignia to tell the two
# apart; every other build keeps the retail icon. The exe is the only place
# this has to be set -- the shortcuts, the Add/Remove entry, the taskbar and
# alt-tab all take their icon from it. RC resolves the name against RTS.RC's
# own directory, which is where both .ico files live.
#
# Defined only for the Zulu GO build: RTS.RC falls back to the literal
# "GENERALS.ICO" when it is absent, so the VC6 Zulu build compiles exactly the
# resource script it always did.
set(_zulu_app_icon_define "")
if(DEFINED ZULU_GO_SELFHOST_BASE AND NOT ZULU_GO_SELFHOST_BASE STREQUAL "")
    set(_zulu_app_icon_define "#define ZULU_APP_ICON_FILE \"ZuluGO.ico\"\n")
endif()

# VERSION_STRING is consumed by RTS.RC's VERSIONINFO block (and any C/C++
# code that wants a string form). Kept alongside the numeric macros so both
# the game and the launcher read versions from one source of truth.
# Compare-and-write so the file's mtime only bumps when the version actually
# changes; otherwise every `--cmake` reconfigure would force a WinMain.cpp
# recompile via OBJECT_DEPENDS below.
set(_zulu_build_version_content
"#pragma once

#define VERSION_MAJOR ${ZULU_VERSION_MAJOR}
#define VERSION_MINOR ${ZULU_VERSION_MINOR}
#define VERSION_BUILDNUM ${ZULU_VERSION_BUILDNUM}
#define VERSION_STRING \"${ZULU_VERSION_MAJOR}.${ZULU_VERSION_MINOR}.${ZULU_VERSION_BUILDNUM}\"
${_zulu_app_icon_define}")
set(_zulu_build_version_existing "")
if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.h")
    file(READ "${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.h" _zulu_build_version_existing)
endif()
if(NOT _zulu_build_version_existing STREQUAL _zulu_build_version_content)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.h "${_zulu_build_version_content}")
endif()

if(MSVC)
    target_link_options(z_generals PRIVATE "/NODEFAULTLIB:libci.lib")
endif()

target_include_directories(z_generals PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}
)

target_sources(z_generals PRIVATE
    VCRuntimeCheck.cpp
    WinMain.cpp
    WinMain.h
)

# Stamp the file version of the runtime this toolset ships, the minimum VCRuntimeCheck.cpp accepts
if(MSVC AND NOT IS_VS6_BUILD)
    # cl.exe lives in VC/Tools/MSVC/<ver>/bin/Host<arch>/<arch>
    get_filename_component(_go_vc_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_go_vc_dir "${_go_vc_dir}/../../../../../.." ABSOLUTE)
    set(_go_redist_version_file "${_go_vc_dir}/Auxiliary/Build/Microsoft.VCRedistVersion.default.txt")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_go_redist_arch x64)
    else()
        set(_go_redist_arch x86)
    endif()

    set(_go_msvcp "")
    if(EXISTS "${_go_redist_version_file}")
        file(STRINGS "${_go_redist_version_file}" _go_redist_dir_version LIMIT_COUNT 1)
        string(STRIP "${_go_redist_dir_version}" _go_redist_dir_version)
        file(GLOB _go_msvcp "${_go_vc_dir}/Redist/MSVC/${_go_redist_dir_version}/${_go_redist_arch}/Microsoft.VC*.CRT/msvcp140.dll")
    endif()

    set(_go_msvcp_version "")
    if(_go_msvcp)
        list(GET _go_msvcp 0 _go_msvcp)
        execute_process(
            COMMAND powershell -NoProfile -NonInteractive -Command "(Get-Item -LiteralPath '${_go_msvcp}').VersionInfo.FileVersion"
            OUTPUT_VARIABLE _go_msvcp_version
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    endif()

    if(_go_msvcp_version MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)")
        message(STATUS "VC++ runtime check requires ${CMAKE_MATCH_1}.${CMAKE_MATCH_2}.${CMAKE_MATCH_3} or newer")
        set_source_files_properties(VCRuntimeCheck.cpp PROPERTIES COMPILE_DEFINITIONS
            "GO_VCREDIST_MAJOR=${CMAKE_MATCH_1};GO_VCREDIST_MINOR=${CMAKE_MATCH_2};GO_VCREDIST_BUILD=${CMAKE_MATCH_3}")
    else()
        message(WARNING "Could not read the toolset's msvcp140.dll version, the VC++ runtime check falls back to the compiler version")
    endif()

    # New STL features live in satellite DLLs older redistributables lack, and a missing import stops the
    # loader before any game code runs. Delay-load them so VCRuntimeCheck.cpp can detect and install instead.
    # LNK4199 is the harmless "no imports found" warning for satellites the exe doesn't use (yet).
    target_link_options(z_generals PRIVATE
        "/DELAYLOAD:msvcp140_atomic_wait.dll"
        "/DELAYLOAD:msvcp140_1.dll"
        "/DELAYLOAD:msvcp140_2.dll"
        "/DELAYLOAD:msvcp140_codecvt_ids.dll"
        "/IGNORE:4199"
    )
    target_link_libraries(z_generals PRIVATE delayimp)
endif()

if(WIN32)
	add_custom_command(TARGET z_generals POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/../GameEngine/Source/GameNetwork/GeneralsOnline/Vendor/DiscordRPC/discord-rpc.dll"
			"$<TARGET_FILE_DIR:z_generals>/discord-rpc.dll"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_CURRENT_SOURCE_DIR}/../GameEngine/Source/GameNetwork/GeneralsOnline/Vendor/DiscordRPC/LICENSE.txt"
			"$<TARGET_FILE_DIR:z_generals>/discord-rpc-LICENSE.txt"
	)

	if(EXISTS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/sentry-native/crashpad_handler.exe")
		# sentry's crashpad backend is an out-of-process watcher: it needs its
		# own executable next to the game, at the default handler path sentry
		# looks for (see sentry_options_set_handler_path in sentry.h).
		add_custom_command(TARGET z_generals POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/sentry-native/crashpad_handler.exe"
				"$<TARGET_FILE_DIR:z_generals>/crashpad_handler.exe"
		)
	endif()
endif()
# WinMain.cpp #include's BuildVersion.h and GeneratedVersion.h. VC6's cl.exe
# rejects /showIncludes, so the top-level CMakeLists.txt disables ninja's
# header-dependency scanning. Without this explicit OBJECT_DEPENDS, bumping
# APPVERSION regenerates BuildVersion.h but ninja never recompiles
# WinMain.cpp, so TheVersion->setVersion() keeps reporting whatever VERSION_*
# macros were live the last time WinMain.obj was actually built.
set_source_files_properties(WinMain.cpp PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.h;${CMAKE_CURRENT_BINARY_DIR}/GeneratedVersion.h")

# RC files optional for MinGW builds
# Icon: LoadIcon() handles missing resource gracefully (uses default system icon)
# Manifest: DPI awareness metadata (nice to have but not essential)
if(MSVC)
    # VS2005 and later adds default manifest, we need to turn it off to prevent conflict with custom manifest
    if(NOT IS_VS6_BUILD)
        target_link_options(z_generals PRIVATE
            "/MANIFEST:NO"
        )
    endif()

    target_sources(z_generals PRIVATE
        RTS.RC
    )

    # RTS.RC #include's BuildVersion.h; same ninja header-tracking problem
    # as WinMain.cpp above. Without this, bumping APPVERSION leaves the .res
    # unmodified and the exe ships with the stale FILEVERSION. The launcher
    # avoids this entirely because its .rc.in goes through configure_file().
    # The icons are listed for the same reason: the .res embeds whichever one
    # ZULU_APP_ICON_FILE names, and nothing else tells ninja that redrawing an
    # .ico should rebuild it.
    set_source_files_properties(RTS.RC PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/BuildVersion.h;${CMAKE_CURRENT_SOURCE_DIR}/Generals.ico;${CMAKE_CURRENT_SOURCE_DIR}/ZuluGO.ico")
endif()

# Strip debug symbols to separate file for MinGW Release builds
# This creates generalszh${RTS_BUILD_OUTPUT_SUFFIX}.exe.debug (similar to MSVC .pdb files)
if(MINGW AND COMMAND add_debug_strip_target)
    add_debug_strip_target(z_generals)
endif()
