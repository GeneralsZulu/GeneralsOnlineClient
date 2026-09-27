#!/usr/bin/env bash
#
# Build script for compiling Generals/Zero Hour on Linux using Docker + Wine + MSVC 2022
#
# This script builds Windows executables using a Docker container with Wine and MSVC Build Tools.
# The resulting binaries are identical to native Windows VS2022 builds.
#
# It is the counterpart of scripts/docker-build.sh (VC6) for the msvc-wine
# preset, and it is what the `zulugo-*` Makefile targets drive: the Zulu GO
# product is the GeneralsOnline-based client, which needs the vcpkg toolchain
# and a handful of build-time -D defines that the VC6 pipeline never had.
#
# Usage:
#   ./scripts/docker-build-msvc.sh                        # Full build of Zero Hour (z_generals)
#   ./scripts/docker-build-msvc.sh --target z_generals    # Build one cmake target
#   ./scripts/docker-build-msvc.sh --clean                # Clean build directory
#   ./scripts/docker-build-msvc.sh --cmake                # Force CMake reconfiguration
#   ./scripts/docker-build-msvc.sh --interactive          # Enter container shell
#
# Environment:
#   PRESET             cmake preset (default msvc-wine)
#   BUILD_SUBDIR       build dir relative to the repo root. Defaults to
#                      build/docker-$PRESET, matching docker-build.sh. Give
#                      each *configuration* its own: a cmake cache only
#                      remembers the variables a preset names, so configuring
#                      the shipping build on top of a cache left by the
#                      logging build silently keeps RTS_DEBUG_LOGGING on.
#   EXTRA_CMAKE_DEFS   newline-separated extra cmake arguments, forwarded to
#                      the configure step, e.g.
#                        -DZULU_GO_SELFHOST_BASE=https://example.org/go
#                        -DRTS_DEBUG_LOGGING=ON
#   ZULU_VCPKG_DIR     prebuilt vcpkg tree to mount read-only at /build/vcpkg.
#                      See "vcpkg" below.
#   USE_VCPKG          true/false override; autodetected from CMakeLists.txt.
#   DEPS_CACHE_DIR     FetchContent cache (curl, sentry, GNS, gamespy), mounted
#                      at /build/deps. Default $BUILD_SUBDIR-deps, so each
#                      configuration keeps its own and neither re-clones.
#   IMAGE_NAME         docker image tag (default generals-msvc-build)
#   DOCKER_USER        uid:gid the build runs as (default: the caller's)
#
# vcpkg
# -----
# The GeneralsOnline tree requires the vcpkg toolchain (its CMakeLists.txt
# checks VCPKG_TOOLCHAIN): openssl, protobuf, zlib and stb come from vcpkg,
# while curl, sentry and GNS are FetchContent. Building vcpkg's x86-windows
# packages under wine takes hours, so a tree is built once and reused; this
# script only mounts it. Point ZULU_VCPKG_DIR at it, or drop it at one of the
# default locations below. It is mounted read-only, so concurrent builds
# cannot corrupt it.
#
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DOCKER_DIR="$PROJECT_DIR/resources/dockerbuild-msvc"
IMAGE_NAME="${IMAGE_NAME:-generals-msvc-build}"
PRESET="${PRESET:-msvc-wine}"
BUILD_SUBDIR="${BUILD_SUBDIR:-build/docker-$PRESET}"
BUILD_DIR="$PROJECT_DIR/$BUILD_SUBDIR"

# The entrypoint is mounted rather than taken from the image so this recipe
# can change without a 3 GB image rebuild (the image bakes in an older copy at
# /entrypoint.sh that pins the MSVC toolset and knows nothing about vcpkg).
ENTRYPOINT="$SCRIPT_DIR/docker-msvc-entrypoint.sh"

# Candidate locations for the prebuilt vcpkg tree, first match wins.
#   1. whatever the caller asked for
#   2. in-repo, for a self-contained checkout
#   3. the tree go-lab/vcpkg-install.sh builds, next to this worktree
VCPKG_CANDIDATES=(
	"${ZULU_VCPKG_DIR:-}"
	"$PROJECT_DIR/build/vcpkg"
	"$PROJECT_DIR/../go-lab/vcpkg"
)

MAKE_TARGET=""
FORCE_CMAKE="false"
INTERACTIVE="false"
CLEAN="false"

while [[ $# -gt 0 ]]; do
	case "$1" in
	--target)
		MAKE_TARGET="$2"
		shift 2
		;;
	--clean)
		CLEAN="true"
		shift
		;;
	--cmake)
		FORCE_CMAKE="true"
		shift
		;;
	--interactive)
		INTERACTIVE="true"
		shift
		;;
	*)
		echo "Unknown option: $1"
		exit 1
		;;
	esac
done

# Serialize invocations that share a build directory, the same way
# scripts/docker-build.sh does and for the same reason: --clean does an
# rm -rf, and a second build started in the same directory while the first is
# still compiling deletes the running build's tree out from under it. The
# failure looks like a broken dependency fetch ("Cannot open include file"),
# not like what it is. Different build directories still run concurrently, so
# the shipping and the logging configuration never wait on each other -- but
# `make -j` building the client and the launcher out of one directory does.
if command -v flock >/dev/null 2>&1; then
	mkdir -p "$(dirname "$BUILD_DIR")"
	exec 9>"$BUILD_DIR.lock"
	if ! flock -n 9; then
		echo "Another build is using $BUILD_SUBDIR. Waiting for it to finish..."
		flock 9
		echo "Previous build finished; continuing."
	fi
else
	echo "WARNING: flock not found; do not run two builds of $BUILD_SUBDIR at once." >&2
fi

if [ "$CLEAN" = "true" ]; then
	echo "Cleaning build directory: $BUILD_SUBDIR"
	rm -rf "$BUILD_DIR"
	# The FetchContent cache is deliberately kept: it is network clones plus a
	# real compile, and nothing in it depends on the build directory.
	exit 0
fi

# Does this worktree need vcpkg? Same test the entrypoint makes, done here too
# because the mount has to be decided before the container starts.
if [ -z "${USE_VCPKG:-}" ]; then
	if grep -q "VCPKG_TOOLCHAIN" "$PROJECT_DIR/CMakeLists.txt" 2>/dev/null; then
		USE_VCPKG=true
	else
		USE_VCPKG=false
	fi
fi

MOUNTS=()
if [ "$USE_VCPKG" = "true" ]; then
	VCPKG_DIR=""
	for candidate in "${VCPKG_CANDIDATES[@]}"; do
		[ -n "$candidate" ] || continue
		if [ -d "$candidate/installed/x86-windows/share/openssl" ]; then
			VCPKG_DIR="$(cd "$candidate" && pwd)"
			break
		fi
	done
	if [ -z "$VCPKG_DIR" ]; then
		echo "ERROR: this tree needs the vcpkg toolchain (CMakeLists.txt checks" >&2
		echo "       VCPKG_TOOLCHAIN) but no prebuilt vcpkg tree was found." >&2
		echo "" >&2
		echo "       Looked for installed/x86-windows/share/openssl under:" >&2
		for candidate in "${VCPKG_CANDIDATES[@]}"; do
			[ -n "$candidate" ] || continue
			echo "         $candidate" >&2
		done
		echo "" >&2
		echo "       Set ZULU_VCPKG_DIR=/path/to/vcpkg to point at an existing" >&2
		echo "       tree, or build one with go-lab/vcpkg-install.sh (hours:" >&2
		echo "       it compiles openssl and protobuf for x86-windows under" >&2
		echo "       wine). USE_VCPKG=false skips this for older worktrees." >&2
		exit 1
	fi
	echo "Using vcpkg tree: $VCPKG_DIR"
	MOUNTS+=(-v "$VCPKG_DIR:/build/vcpkg:ro")
fi

# One FetchContent cache per build configuration: the fetched projects are
# built with the parent build's compile definitions, so the logging and the
# shipping configuration must not share one.
DEPS_CACHE_DIR="${DEPS_CACHE_DIR:-$BUILD_DIR-deps}"
mkdir -p "$DEPS_CACHE_DIR"
# docker -v needs an absolute path; a relative one would be read as the name
# of a docker volume and silently give the build an empty cache.
DEPS_CACHE_DIR="$(cd "$DEPS_CACHE_DIR" && pwd)"
MOUNTS+=(-v "$DEPS_CACHE_DIR:/build/deps")

# Build Docker image (this takes a while the first time - downloads MSVC)
echo "Building Docker image (first run downloads ~3GB of MSVC tools)..."
docker build \
	--build-arg UID="$(id -u)" \
	--build-arg GID="$(id -g)" \
	-t "$IMAGE_NAME" \
	"$DOCKER_DIR"

DOCKER_ARGS=(
	--rm
	# The image's last USER directive is root (it has to be, to COPY the
	# entrypoint in), so without this every build artifact lands root-owned
	# and the packaging steps that run as the user cannot stage next to them.
	-u "${DOCKER_USER:-$(id -u):$(id -g)}"
	-v "$PROJECT_DIR:/build/cnc"
	-v "$ENTRYPOINT:/entrypoint.sh:ro"
	"${MOUNTS[@]}"
	-e "PRESET=$PRESET"
	-e "BUILD_SUBDIR=$BUILD_SUBDIR"
	-e "FORCE_CMAKE=$FORCE_CMAKE"
	-e "MAKE_TARGET=${MAKE_TARGET}"
	-e "USE_VCPKG=${USE_VCPKG}"
	-e "EXTRA_CMAKE_DEFS=${EXTRA_CMAKE_DEFS:-}"
	# Same pass-through set as docker-build.sh, for the cmake modules that
	# read them (buildvariant, zuluclientkey, cncstatsclientkey, ...). The
	# Makefile prefers -D defines via EXTRA_CMAKE_DEFS; these keep a plain
	# command-line build working.
	-e "ZULU_VERSION_MAJOR=${ZULU_VERSION_MAJOR:-}"
	-e "ZULU_VERSION_MINOR=${ZULU_VERSION_MINOR:-}"
	-e "ZULU_VERSION_BUILDNUM=${ZULU_VERSION_BUILDNUM:-}"
	-e "ZULU_BUILD_VARIANT=${ZULU_BUILD_VARIANT:-}"
	-e "ZULU_CLIENT_KEY=${ZULU_CLIENT_KEY:-}"
	-e "CNCSTATS_ZULU_CLIENT_KEY=${CNCSTATS_ZULU_CLIENT_KEY:-}"
	-e "ZULU_DISCORD_WEBHOOK_URL=${ZULU_DISCORD_WEBHOOK_URL:-}"
)

if [ "$INTERACTIVE" = "true" ]; then
	echo "Entering interactive shell..."
	docker run -it "${DOCKER_ARGS[@]}" --entrypoint /bin/bash "$IMAGE_NAME"
else
	echo "Building... (preset $PRESET, build dir $BUILD_SUBDIR)"
	docker run "${DOCKER_ARGS[@]}" --entrypoint /entrypoint.sh "$IMAGE_NAME"
fi
