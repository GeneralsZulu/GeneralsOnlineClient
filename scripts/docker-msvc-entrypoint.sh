#!/bin/bash
#
# In-container entrypoint for the MSVC-2022-under-Wine image
# (resources/dockerbuild-msvc). Mounted over /entrypoint.sh by
# scripts/docker-build-msvc.sh rather than baked into the image, so the build
# recipe can change without a 3 GB image rebuild.
#
# It is a superset of resources/dockerbuild-msvc/entrypoint.sh:
#
#   * the MSVC toolset version is detected instead of pinned (the image ships
#     whatever MSVC Build Tools were current the day it was built; a stale pin
#     fails configure with "not a full path to an existing compiler tool"),
#   * the vcpkg toolchain is wired up when /build/vcpkg is mounted, which the
#     GeneralsOnline-based tree (Zulu GO) requires: openssl, protobuf, zlib and
#     stb come from vcpkg while curl, sentry and GNS are FetchContent,
#   * a persistent FetchContent cache is used when /build/deps is mounted,
#   * extra -D arguments can be injected by the caller (EXTRA_CMAKE_DEFS),
#   * the build directory can be chosen by the caller (BUILD_SUBDIR), so two
#     configurations of the same preset get their own cmake cache.
#
# Inputs (all environment):
#   PRESET            cmake preset name (default msvc-wine)
#   BUILD_SUBDIR      build dir relative to the repo root (default build/$PRESET)
#   FORCE_CMAKE       "true" to re-run the configure step
#   MAKE_TARGET       ninja target(s) (default z_generals)
#   USE_VCPKG         "true"/"false"; autodetected from CMakeLists.txt when empty
#   EXTRA_CMAKE_DEFS  newline-separated extra cmake arguments, e.g.
#                     -DZULU_GO_SELFHOST_BASE=... or -DRTS_DEBUG_LOGGING=ON
set -euo pipefail
cd /build/cnc

# Use a fresh Wine prefix owned by the current user
export WINEPREFIX="/tmp/wineprefix"
mkdir -p "$WINEPREFIX"

# Initialize Wine prefix
wineboot --init 2>/dev/null || true

# MSVC paths - replicate what msvcenv.sh sets
BASE="Z:\\build\\tools\\msvc"
# Detect the installed MSVC toolset rather than pinning it. Rebuilding the
# Docker image picks up whatever MSVC Build Tools ship that day, and a stale
# pin fails configure with "not a full path to an existing compiler tool".
MSVCVER="$(ls -1 /build/tools/msvc/VC/Tools/MSVC/ 2>/dev/null | sort -V | tail -1)"
if [ -z "$MSVCVER" ]; then
	echo "ERROR: no MSVC toolset found under /build/tools/msvc/VC/Tools/MSVC/" >&2
	exit 1
fi
echo "Using MSVC toolset ${MSVCVER}"
SDKVER="10.0.26100.0"
ARCH="x86"

MSVCDIR="${BASE}\\VC\\Tools\\MSVC\\${MSVCVER}"
SDKBASE="${BASE}\\Windows Kits\\10"
SDKINCLUDE="${SDKBASE}\\Include\\${SDKVER}"
SDKLIB="${SDKBASE}\\Lib\\${SDKVER}"

# Real Windows executables (not the Unix wrapper scripts)
CL_WIN="${MSVCDIR}\\bin\\Hostx64\\${ARCH}\\cl.exe"
LINK_WIN="${MSVCDIR}\\bin\\Hostx64\\${ARCH}\\link.exe"

# Environment for cl.exe to find headers and libraries
export INCLUDE="${MSVCDIR}\\atlmfc\\include;${MSVCDIR}\\include;${SDKINCLUDE}\\shared;${SDKINCLUDE}\\ucrt;${SDKINCLUDE}\\um;${SDKINCLUDE}\\winrt"
export LIB="${MSVCDIR}\\atlmfc\\lib\\${ARCH};${MSVCDIR}\\lib\\${ARCH};${SDKLIB}\\ucrt\\${ARCH};${SDKLIB}\\um\\${ARCH}"
export LIBPATH="${LIB}"

# Wine needs the Hostx64/x64 dir in PATH for DLLs (vcruntime140.dll etc.)
export WINEPATH="${MSVCDIR}\\bin\\Hostx64\\${ARCH};${MSVCDIR}\\bin\\Hostx64\\x64;${SDKBASE}\\bin\\${SDKVER}\\x64"
export WINEDLLOVERRIDES="vcruntime140=n;vcruntime140_1=n"

# One build directory per configuration, not per preset: Zulu GO builds the
# same msvc-wine preset twice (shipping vs RTS_DEBUG_LOGGING/RTS_DEBUG_CRASHING)
# and a cmake cache only remembers the variables a preset actually names, so a
# shared directory would leak the debug defines into the shipping build.
PRESET="${PRESET:-msvc-wine}"
BUILD_SUBDIR="${BUILD_SUBDIR:-build/${PRESET}}"
BUILD_DIR="/build/cnc/${BUILD_SUBDIR}"
#
# Symlink "Windows Kits" to a path without spaces so Wine doesn't generate
# 8.3 short names (e.g. WIND~2DP) that can't be resolved on Linux filesystems.
WINKITS="/build/tools/msvc/Windows Kits"
WINKITS_LINK="/build/tools/msvc/WindowsKits"
if [ -d "$WINKITS" ] && [ ! -e "$WINKITS_LINK" ]; then
	ln -s "$WINKITS" "$WINKITS_LINK"
fi

RC_COMPILER="Z:/build/tools/msvc/WindowsKits/10/bin/${SDKVER}/x64/rc.exe"
RC_INCLUDE="Z:/build/tools/msvc/WindowsKits/10/Include/${SDKVER}"
RC_FLAGS="-I \"${RC_INCLUDE}/um\" -I \"${RC_INCLUDE}/shared\""

# The GeneralsOnline tree refuses to configure Zero Hour without the vcpkg
# toolchain (it checks VCPKG_TOOLCHAIN). Detect that from the worktree's
# CMakeLists.txt and, when a prebuilt vcpkg tree is mounted at /build/vcpkg,
# point the Windows cmake.exe at it in classic (non-manifest) mode so nothing
# tries to run a vcpkg binary under wine. USE_VCPKG=true/false overrides.
EXTRA_CMAKE_ARGS=()
if [ -z "${USE_VCPKG:-}" ]; then
	if grep -q "VCPKG_TOOLCHAIN" /build/cnc/CMakeLists.txt 2>/dev/null; then
		USE_VCPKG=true
	else
		USE_VCPKG=false
	fi
fi
if [ "${USE_VCPKG}" = "true" ]; then
	if [ ! -d /build/vcpkg/installed/x86-windows/share/openssl ] \
		|| [ ! -d /build/vcpkg/installed/x86-windows/share/protobuf ]; then
		echo "ERROR: this worktree needs the vcpkg toolchain but /build/vcpkg has no" >&2
		echo "       installed x86-windows openssl/protobuf." >&2
		echo "       Point ZULU_VCPKG_DIR at a prebuilt tree (see scripts/docker-build-msvc.sh)." >&2
		exit 1
	fi
	echo "Using vcpkg toolchain (classic mode) from /build/vcpkg"
	EXTRA_CMAKE_ARGS+=(
		-DCMAKE_TOOLCHAIN_FILE="Z:/build/vcpkg/scripts/buildsystems/vcpkg.cmake"
		-DVCPKG_TARGET_TRIPLET=x86-windows
		-DVCPKG_MANIFEST_MODE=OFF
		-DVCPKG_INSTALLED_DIR="Z:/build/vcpkg/installed"
		-DVCPKG_APPLOCAL_DEPS=OFF
		# cmake.exe under wine dies (libuv: AssignProcessToJobObject invalid
		# handle) if a configure step execute_process()es a Unix ELF. CMake's
		# Windows search path includes /bin, which wine maps onto Z:, so
		# curl's find_package(Perl) found /usr/bin/perl and ran it. Perl only
		# gates curl's man pages; pkg-config would hit the same trap.
		-DCMAKE_DISABLE_FIND_PACKAGE_Perl=TRUE
		-DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=TRUE
		-DCURL_USE_PKGCONFIG=OFF
	)
fi
# Extra -D args from the caller (EXTRA_CMAKE_DEFS, one per line), for things
# like -DZULU_GO_SELFHOST_BASE=..., the client keys, or the debug defines that
# stand in for a "releaselog" preset on this toolchain.
if [ -n "${EXTRA_CMAKE_DEFS:-}" ]; then
	while IFS= read -r line; do
		[ -z "$line" ] && continue
		EXTRA_CMAKE_ARGS+=("$line")
	done <<< "${EXTRA_CMAKE_DEFS}"
fi

# Persistent FetchContent cache (gamespy, curl, sentry, GNS, ...) so a clean
# build dir or image rebuild does not re-clone and rebuild everything. The
# caller gives each build configuration its own cache directory: the fetched
# projects are built with the parent's compile definitions, so two
# configurations must not share one.
if [ -d /build/deps ]; then
	echo "Using FetchContent cache at /build/deps"
	EXTRA_CMAKE_ARGS+=(-DFETCHCONTENT_BASE_DIR="Z:/build/deps")
fi

# Configure if needed
if [ "${FORCE_CMAKE:-}" = "true" ] || [ ! -f "${BUILD_DIR}/build.ninja" ]; then
	rm -f "${BUILD_DIR}/CMakeCache.txt"

	# Predefining CMAKE_SYSTEM makes CMake skip CMakeDetermineSystem.cmake, the
	# only place CMAKE_TOOLCHAIN_FILE is ever read, so it must not be set when
	# the vcpkg toolchain is in use (vcpkg chainloads its windows.cmake, which
	# sets CMAKE_SYSTEM_NAME/CROSSCOMPILING itself). Kept for legacy worktrees.
	SYSTEM_ARGS=()
	if [ "${USE_VCPKG}" != "true" ]; then
		SYSTEM_ARGS+=(-DCMAKE_SYSTEM="Windows")
	fi

	wine /build/tools/cmake/bin/cmake.exe \
		--preset ${PRESET} \
		"${SYSTEM_ARGS[@]}" \
		-DCMAKE_SYSTEM_NAME="Windows" \
		-DCMAKE_SIZEOF_VOID_P=4 \
		-DCMAKE_MAKE_PROGRAM="Z:/build/tools/ninja.exe" \
		-DCMAKE_C_COMPILER="${CL_WIN}" \
		-DCMAKE_CXX_COMPILER="${CL_WIN}" \
		-DCMAKE_LINKER="${LINK_WIN}" \
		-DCMAKE_C_COMPILER_ID=MSVC \
		-DCMAKE_CXX_COMPILER_ID=MSVC \
		-DCMAKE_C_COMPILER_VERSION=19.50.35726 \
		-DCMAKE_CXX_COMPILER_VERSION=19.50.35726 \
		-DMSVC_VERSION=1950 \
		-DMSVC=1 \
		-DCMAKE_C_STANDARD_COMPUTED_DEFAULT=17 \
		-DCMAKE_C_EXTENSIONS_COMPUTED_DEFAULT=OFF \
		-DCMAKE_CXX_STANDARD_COMPUTED_DEFAULT=20 \
		-DCMAKE_CXX_EXTENSIONS_COMPUTED_DEFAULT=OFF \
		-DCMAKE_SUPPRESS_REGENERATION=ON \
		-DCMAKE_C_COMPILER_WORKS=1 \
		-DCMAKE_CXX_COMPILER_WORKS=1 \
		-DGIT_EXECUTABLE="Z:/build/tools/git/git.exe" \
		-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
		-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
		-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
		-DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY \
		-DCMAKE_RC_COMPILER="${RC_COMPILER}" \
		-DCMAKE_RC_FLAGS="${RC_FLAGS}" \
		"${EXTRA_CMAKE_ARGS[@]}" \
		-B "${BUILD_DIR}"
fi

# Fix PCH paths: CMake generates Unix paths for /FI, /Yc, /Fp flags.
# MSVC under Wine needs Z: drive prefix.
echo "Fixing PCH paths for Wine..."
# The same applies to /build/deps (FetchContent cache) and /build/vcpkg, so
# match any /build/... path rather than just /build/cnc/.
sed -i \
	-e 's|/Yc/build/|/YcZ:/build/|g' \
	-e 's|/Yu/build/|/YuZ:/build/|g' \
	-e 's|/Fp/build/|/FpZ:/build/|g' \
	-e 's|/FI/build/|/FIZ:/build/|g' \
	-e 's| -c /build/| -c Z:/build/|g' \
	-e 's| -c \\build\\| -c Z:\\build\\|g' \
	-e 's| -I \\build\\| -I Z:\\build\\|g' \
	-e 's| -I /build/| -I Z:/build/|g' \
	-e 's|-LIBPATH:\\build\\|-LIBPATH:Z:\\build\\|g' \
	-e 's|-LIBPATH:/build/|-LIBPATH:Z:/build/|g' \
	"${BUILD_DIR}/build.ninja"

# Remove the CMake regeneration rule so Ninja doesn't overwrite our fixes
sed -i '/^build build.ninja:/,/^$/d' "${BUILD_DIR}/build.ninja"

FIXED=$(grep -c 'Z:/build/' "${BUILD_DIR}/build.ninja" || true)
echo "Fixed paths: ${FIXED} occurrences with Z: prefix"

# Build - pass MSVC environment into the Windows cmd session
cd "${BUILD_DIR}"
wine cmd /c "set TMP=Z:\build\tmp& set TEMP=Z:\build\tmp& set INCLUDE=${INCLUDE}& set LIB=${LIB}& set LIBPATH=${LIBPATH}& set PATH=${WINEPATH};%PATH%& Z:\build\tools\ninja.exe ${MAKE_TARGET:-z_generals}"

# CMake's TARGET_RUNTIME_DLLS post-build step stages DLLs of imported targets
# that carry an IMPORTED_LOCATION (GNS, libcurl, protobuf, abseil) but not
# zlib1.dll (FindZLIB imports only the .lib) nor OpenSSL (linked by libcurl and
# GNS, not the exe). vcpkg's own applocal step is PowerShell and off under
# wine, so copy the vcpkg runtime DLLs next to the exe here. libprotoc.dll is
# protoc-only and skipped.
if [ "${USE_VCPKG}" = "true" ]; then
	OUT_DIR="${BUILD_DIR}/GeneralsMD"
	if [ -d "${OUT_DIR}" ]; then
		for dll in /build/vcpkg/installed/x86-windows/bin/*.dll; do
			case "$(basename "$dll")" in libprotoc.dll) continue ;; esac
			if [ ! -e "${OUT_DIR}/$(basename "$dll")" ]; then
				cp "$dll" "${OUT_DIR}/" && echo "Staged $(basename "$dll") from vcpkg"
			fi
		done
	fi
fi
