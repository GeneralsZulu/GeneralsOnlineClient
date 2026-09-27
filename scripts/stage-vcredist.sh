#!/usr/bin/env bash
#
# Copy the Visual C++ runtime DLLs that the Zulu GO client needs into a
# payload directory.
#
#   scripts/stage-vcredist.sh <dest-dir>
#
# Why this exists: the msvc-wine build links against the DLL runtime
# (CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL in the preset), so the client
# will not start on a machine without a matching "Microsoft Visual C++
# Redistributable". Zulu never had this problem -- VC6's runtime is ancient
# and universally present -- but Zulu GO is built with MSVC 2022 and has to
# carry its runtime next to the exe.
#
# The files come out of the build image itself (the x86 redist folder that
# ships with the same MSVC Build Tools that compiled the exe), so the runtime
# always matches the toolset rather than whatever happens to sit on the
# packaging machine.
#
# Environment:
#   IMAGE_NAME            build image to take the DLLs from
#                         (default generals-msvc-build)
#   ZULU_VCREDIST_DIR     copy from this local directory instead of the image;
#                         useful for packaging without docker.
set -euo pipefail

DEST="${1:-}"
[ -n "$DEST" ] || { echo "usage: $(basename "$0") <dest-dir>" >&2; exit 1; }
[ -d "$DEST" ] || { echo "ERROR: no such directory: $DEST" >&2; exit 1; }
DEST="$(cd "$DEST" && pwd)"

IMAGE_NAME="${IMAGE_NAME:-generals-msvc-build}"

# The x86 CRT, minus vccorlib140.dll: that one is only needed by C++/CX
# (/ZW) code, which nothing here builds.
DLLS=(
	concrt140.dll
	msvcp140.dll
	msvcp140_1.dll
	msvcp140_2.dll
	msvcp140_atomic_wait.dll
	msvcp140_codecvt_ids.dll
	vcruntime140.dll
	vcruntime140_threads.dll
)

if [ -n "${ZULU_VCREDIST_DIR:-}" ]; then
	echo "[vcredist] copying from $ZULU_VCREDIST_DIR"
	for dll in "${DLLS[@]}"; do
		[ -f "$ZULU_VCREDIST_DIR/$dll" ] \
			|| { echo "ERROR: $ZULU_VCREDIST_DIR is missing $dll" >&2; exit 1; }
		cp -- "$ZULU_VCREDIST_DIR/$dll" "$DEST/$dll"
	done
	echo "[vcredist] staged ${#DLLS[@]} runtime DLL(s) into $DEST"
	exit 0
fi

command -v docker >/dev/null 2>&1 \
	|| { echo "ERROR: docker not found; set ZULU_VCREDIST_DIR to a local copy of the x86 VC runtime." >&2; exit 1; }
docker image inspect "$IMAGE_NAME" >/dev/null 2>&1 \
	|| { echo "ERROR: docker image '$IMAGE_NAME' not found; build it first (scripts/docker-build-msvc.sh does)." >&2; exit 1; }

# Resolve the redist folder inside the image rather than pinning a toolset
# version: the image is rebuilt with whatever MSVC Build Tools are current.
docker run --rm \
	-u "$(id -u):$(id -g)" \
	-v "$DEST:/out" \
	-e "DLLS=${DLLS[*]}" \
	--entrypoint bash \
	"$IMAGE_NAME" -c '
set -euo pipefail
src="$(ls -d /build/tools/msvc/VC/Redist/MSVC/*/x86/Microsoft.VC*.CRT 2>/dev/null | sort -V | tail -1)"
if [ -z "$src" ]; then
	echo "ERROR: no x86 Microsoft.VC*.CRT redist folder in the image" >&2
	exit 1
fi
echo "[vcredist] source: $src"
for dll in $DLLS; do
	[ -f "$src/$dll" ] || { echo "ERROR: $src is missing $dll" >&2; exit 1; }
	cp -- "$src/$dll" "/out/$dll"
done
'
echo "[vcredist] staged ${#DLLS[@]} runtime DLL(s) into $DEST"
