#!/usr/bin/env bash
#
# Build script for compiling Generals/Zero Hour on Linux using Docker + Wine + MSVC 2022
#
# This script builds Windows executables using a Docker container with Wine and MSVC Build Tools.
# The resulting binaries are identical to native Windows VS2022 builds.
#
# Usage:
#   ./scripts/docker-build-msvc.sh                        # Full build of Zero Hour (z_generals)
#   ./scripts/docker-build-msvc.sh --clean                # Clean build directory
#   ./scripts/docker-build-msvc.sh --cmake                # Force CMake reconfiguration
#   ./scripts/docker-build-msvc.sh --interactive          # Enter container shell
#

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &>/dev/null && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
DOCKER_DIR="$PROJECT_DIR/resources/dockerbuild-msvc"
IMAGE_NAME="generals-msvc-build"
PRESET="msvc-wine"

MAKE_TARGET=""
FORCE_CMAKE="false"
INTERACTIVE="false"

while [[ $# -gt 0 ]]; do
	case "$1" in
	--target)
		MAKE_TARGET="$2"
		shift 2
		;;
	--clean)
		echo "Cleaning build directory..."
		rm -rf "$PROJECT_DIR/build/$PRESET"
		exit 0
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

# Pull ZULU_CLIENT_KEY from GCP Secret Manager when it isn't already set,
# so cmake/zuluclientkey.cmake stops failing on machines that have gcloud
# configured. Stays silent if gcloud is unavailable or the fetch fails;
# the cmake step will then surface its existing error.
if [[ -z "${ZULU_CLIENT_KEY:-}" ]] && command -v gcloud &>/dev/null; then
	if fetched_key=$(gcloud secrets versions access latest --secret=zuluclientkey 2>/dev/null); then
		export ZULU_CLIENT_KEY="$fetched_key"
		echo "[zuluclientkey] fetched from GCP Secret Manager"
	fi
fi

# Build Docker image (this takes a while the first time - downloads MSVC)
echo "Building Docker image (first run downloads ~3GB of MSVC tools)..."
docker build \
	--build-arg UID="$(id -u)" \
	--build-arg GID="$(id -g)" \
	-t "$IMAGE_NAME" \
	"$DOCKER_DIR"

DOCKER_ARGS=(
	--rm
	-v "$PROJECT_DIR:/build/cnc"
	-e "PRESET=$PRESET"
	-e "FORCE_CMAKE=$FORCE_CMAKE"
	-e "MAKE_TARGET=${MAKE_TARGET}"
	-e "ZULU_CLIENT_KEY=${ZULU_CLIENT_KEY:-}"
	-e "ZULU_DISCORD_WEBHOOK_URL=${ZULU_DISCORD_WEBHOOK_URL:-}"
	-e "ZULU_VERSION_MAJOR=${ZULU_VERSION_MAJOR:-}"
	-e "ZULU_VERSION_MINOR=${ZULU_VERSION_MINOR:-}"
	-e "ZULU_VERSION_BUILDNUM=${ZULU_VERSION_BUILDNUM:-}"
)

if [ "$INTERACTIVE" = "true" ]; then
	echo "Entering interactive shell..."
	docker run -it "${DOCKER_ARGS[@]}" "$IMAGE_NAME" /bin/bash
else
	echo "Building..."
	docker run "${DOCKER_ARGS[@]}" "$IMAGE_NAME"
fi
