cmake_minimum_required(VERSION 3.25)

# Match the game's ZULU_VERSION_* defaults so a plain `cmake ..` build
# outside the Make pipeline still produces a consistent pair of binaries.
# The top-level Makefile passes /D overrides parsed from APPVERSION in
# installer/Zulu.nsi.
if(NOT DEFINED ZULU_VERSION_MAJOR)
    set(ZULU_VERSION_MAJOR 1)
endif()
if(NOT DEFINED ZULU_VERSION_MINOR)
    set(ZULU_VERSION_MINOR 4)
endif()
if(NOT DEFINED ZULU_VERSION_BUILDNUM)
    set(ZULU_VERSION_BUILDNUM 601)
endif()

# configure_file uses forward slashes; RC files happily accept those even
# on Windows. The default points at the game's icon in the source tree so the
# launcher shortcut looks identical to a direct-to-game shortcut; a product
# whose game exe carries its own icon passes that one instead (ICON below).
set(ZULU_LAUNCHER_ICON_DEFAULT "${CMAKE_SOURCE_DIR}/GeneralsMD/Code/Main/Generals.ico")
set(ZULU_LAUNCHER_MANIFEST     "${CMAKE_CURRENT_SOURCE_DIR}/ZuluLauncher.manifest")

# ---------------------------------------------------------------------------
# zulu_add_launcher(<target> <KEY value>...)
#
# One source file, one launcher per product. Everything a product differs in
# is baked into a per-target LauncherProduct.h and a per-target version
# resource; ZuluLauncher.cpp itself has no product strings left in it.
#
# Products:
#   z_launcher     -> ZuluLauncher.exe    (the original; values below are
#                                          exactly what used to be hardcoded)
#   z_go_launcher  -> ZuluGOLauncher.exe  (Zulu GO, installs side by side in
#                                          a ZuluGO subfolder of Zero Hour)
# ---------------------------------------------------------------------------
function(zulu_add_launcher target)
    set(_one_value
        OUTPUT_NAME
        ICON
        APP_NAME
        GAME_EXE_NAME
        LAUNCHER_EXE_NAME
        URL_RELEASE
        URL_DEV
        URL_PREFIX
        INSTALLER_LEAF_RELEASE
        INSTALLER_LEAF_DEV
        HTTP_USER_AGENT
        MOD_BIG_NAME
        INSTALL_TO_PARENT
        FILE_DESCRIPTION)
    cmake_parse_arguments(P "" "${_one_value}" "" ${ARGN})

    # Names the configure_file templates expand. Local to this call, so two
    # invocations cannot leak values into each other.
    set(ZULU_PRODUCT_APP_NAME                "${P_APP_NAME}")
    set(ZULU_PRODUCT_GAME_EXE_NAME           "${P_GAME_EXE_NAME}")
    set(ZULU_PRODUCT_LAUNCHER_EXE_NAME       "${P_LAUNCHER_EXE_NAME}")
    set(ZULU_PRODUCT_LATEST_JSON_URL_RELEASE "${P_URL_RELEASE}")
    set(ZULU_PRODUCT_LATEST_JSON_URL_DEV     "${P_URL_DEV}")
    set(ZULU_PRODUCT_ALLOWED_URL_PREFIX      "${P_URL_PREFIX}")
    set(ZULU_PRODUCT_INSTALLER_LEAF_RELEASE  "${P_INSTALLER_LEAF_RELEASE}")
    set(ZULU_PRODUCT_INSTALLER_LEAF_DEV      "${P_INSTALLER_LEAF_DEV}")
    set(ZULU_PRODUCT_HTTP_USER_AGENT         "${P_HTTP_USER_AGENT}")
    set(ZULU_PRODUCT_MOD_BIG_NAME            "${P_MOD_BIG_NAME}")
    set(ZULU_PRODUCT_INSTALL_TO_PARENT       "${P_INSTALL_TO_PARENT}")

    set(ZULU_LAUNCHER_ICON "${ZULU_LAUNCHER_ICON_DEFAULT}")
    if(P_ICON)
        set(ZULU_LAUNCHER_ICON "${P_ICON}")
    endif()

    set(ZULU_LAUNCHER_FILE_DESCRIPTION  "${P_FILE_DESCRIPTION}")
    set(ZULU_LAUNCHER_INTERNAL_NAME     "${P_OUTPUT_NAME}")
    set(ZULU_LAUNCHER_ORIGINAL_FILENAME "${P_OUTPUT_NAME}.exe")
    set(ZULU_LAUNCHER_PRODUCT_NAME      "${P_APP_NAME}")

    # Per-target generated dir: the two products' headers have the same file
    # name and must not collide, and each target puts only its own on the
    # include path.
    set(_gen "${CMAKE_CURRENT_BINARY_DIR}/${target}_generated")
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/LauncherProduct.h.in
        ${_gen}/LauncherProduct.h
        @ONLY
    )
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/ZuluLauncher.rc.in
        ${_gen}/${P_OUTPUT_NAME}.rc
        @ONLY
    )

    # Standalone WIN32 binary; intentionally does NOT link any of the engine
    # libraries, so it stays tiny and starts before the game does its heavy
    # startup work (registry probes, BIG mounting, DirectX init, ...).
    add_executable(${target} WIN32)

    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${P_OUTPUT_NAME}")

    target_sources(${target} PRIVATE
        ZuluLauncher.cpp
    )

    # BuildVariant.h (cmake/buildvariant.cmake) baked at configure time tells
    # the launcher whether it's a dev or release build, which decides the
    # manifest URL and the update-comparison gate. LauncherProduct.h is the
    # per-product half of the same idea.
    set_property(SOURCE ZuluLauncher.cpp APPEND PROPERTY OBJECT_DEPENDS
        "${ZULU_BUILD_VARIANT_GENERATED_DIR}/BuildVariant.h"
        "${_gen}/LauncherProduct.h")

    if(MSVC)
        # VS2005+ injects a default manifest; we ship our own, so turn it off
        # to avoid duplicate-manifest link warnings (same pattern as the game).
        if(NOT IS_VS6_BUILD)
            target_link_options(${target} PRIVATE "/MANIFEST:NO")
        endif()

        target_sources(${target} PRIVATE
            ${_gen}/${P_OUTPUT_NAME}.rc
        )
    endif()

    # wininet: HTTPS GET for latest.json and the installer download.
    # version: GetFileVersionInfo / VerQueryValue for the installed-version read.
    # shell32: ShellExecuteEx for handing off to the elevation-requesting installer,
    #          plus SHGetKnownFolderPath (resolved at runtime) for the user data dir.
    # comdlg32: GetOpenFileName for the Replay Theater replay picker.
    # ole32: CoTaskMemFree, for the SHGetKnownFolderPath result.
    target_link_libraries(${target} PRIVATE
        wininet
        version
        shell32
        comdlg32
        ole32
        advapi32
        user32
        kernel32
    )

    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${_gen}
        ${ZULU_BUILD_VARIANT_GENERATED_DIR}
    )

    if(MINGW AND COMMAND add_debug_strip_target)
        add_debug_strip_target(${target})
    endif()
endfunction()

# --- Zulu -------------------------------------------------------------------
# These are the values the launcher used to hardcode. Changing any of them
# changes the shipping Zulu launcher.
zulu_add_launcher(z_launcher
    OUTPUT_NAME            "ZuluLauncher"
    APP_NAME               "Zulu"
    GAME_EXE_NAME          "generalszh_zulu.exe"
    # Must match LAUNCHERNAME in installer/Zulu.nsi: the installer renames the
    # running launcher to "<this>.old" so it can write the new one, and the
    # launcher sweeps that leftover on the next start.
    LAUNCHER_EXE_NAME      "ZuluLauncher.exe"
    URL_RELEASE            "https://storage.googleapis.com/zulu-installer/latest.json"
    URL_DEV                "https://storage.googleapis.com/zulu-installer/latest-dev.json"
    URL_PREFIX             "https://storage.googleapis.com/zulu-installer/"
    INSTALLER_LEAF_RELEASE "Zulu_Setup_update.exe"
    INSTALLER_LEAF_DEV     "Zulu_Setup_Dev_update.exe"
    HTTP_USER_AGENT        "ZuluLauncher"
    # Empty: Zulu sits in the Zero Hour folder and the game finds Zulu.big by
    # itself, so the launcher adds no -mod of its own.
    MOD_BIG_NAME           ""
    # 0: Zulu's launcher sits in the install root, so NSIS /D= is our own dir.
    INSTALL_TO_PARENT      0
    FILE_DESCRIPTION       "Zulu Launcher"
)

# --- Zulu GO ----------------------------------------------------------------
# Side-by-side test build (installer/ZuluGO.nsi). Installs into
# <ZeroHour>\ZuluGO, so both product differences below are load-bearing:
#   MOD_BIG_NAME      the game will not find the archive on its own down there
#   INSTALL_TO_PARENT /D= is the Zero Hour folder, one level above us
zulu_add_launcher(z_go_launcher
    OUTPUT_NAME            "ZuluGOLauncher"
    # The same insignia the Zulu GO client carries, so the brief moment the
    # launcher owns the taskbar button looks like the game it is starting.
    ICON                   "${CMAKE_SOURCE_DIR}/GeneralsMD/Code/Main/ZuluGO.ico"
    APP_NAME               "Zulu GO"
    GAME_EXE_NAME          "ZuluGO.exe"
    LAUNCHER_EXE_NAME      "ZuluGOLauncher.exe"
    URL_RELEASE            "https://storage.googleapis.com/zulu-installer/latest-go.json"
    URL_DEV                "https://storage.googleapis.com/zulu-installer/latest-go-dev.json"
    URL_PREFIX             "https://storage.googleapis.com/zulu-installer/"
    INSTALLER_LEAF_RELEASE "ZuluGO_Setup_update.exe"
    INSTALLER_LEAF_DEV     "ZuluGO_Setup_Dev_update.exe"
    HTTP_USER_AGENT        "ZuluGOLauncher"
    MOD_BIG_NAME           "Zulu.modbig"
    INSTALL_TO_PARENT      1
    FILE_DESCRIPTION       "Zulu GO Launcher"
)
