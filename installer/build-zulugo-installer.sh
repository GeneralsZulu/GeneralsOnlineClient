#!/usr/bin/env bash
#
# Build ZuluGO_Setup.exe -- the installer for the side-by-side ZuluGO test
# build (see installer/ZuluGO.nsi for what it produces on the target machine).
#
# Two steps, both here so that rebuilding after a new client build is one
# command and no hand work:
#   1. stage a clean payload directory from wherever the built client lives
#   2. run makensis over it
#
# Usage:
#   installer/build-zulugo-installer.sh [PAYLOAD_SRC]
#
#   PAYLOAD_SRC   directory holding the built client: ZuluGO.exe,
#                 ZuluGOLauncher.exe, the support DLLs, Zulu.big, cacert.pem,
#                 README.txt. Defaults to $ZULUGO_PAYLOAD_SRC, then to
#                 build/zulugo-payload under the repo. Anything else in there
#                 (notably the old Window\Menus folder -- those .wnd files ship
#                 inside Zulu.big now) is left behind by the staging step.
#
# Environment:
#   ZULUGO_PAYLOAD_SRC  same as the positional argument
#   ZULUGO_LAUNCHER     path to ZuluGOLauncher.exe when it is not built into
#                       PAYLOAD_SRC (it comes from a different toolchain than
#                       the client, so it does not always land there)
#   APPVERSION          version stamped into the exe and Add/Remove Programs
#                       (default 2.0.0)
#   SIGN_CMD            absolute path to scripts/sign.sh to Authenticode-sign
#                       the installer and uninstaller; unset means unsigned
#   MAKENSIS            makensis binary (default: makensis on PATH)
#   OUT_DIR             where staging and the finished exe go
#                       (default build/zulugo-installer under the repo)

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd -- "${SCRIPT_DIR}/.." && pwd)"

PAYLOAD_SRC="${1:-${ZULUGO_PAYLOAD_SRC:-${REPO_ROOT}/build/zulugo-payload}}"
OUT_DIR="${OUT_DIR:-${REPO_ROOT}/build/zulugo-installer}"
APPVERSION="${APPVERSION:-2.0.0}"
MAKENSIS="${MAKENSIS:-makensis}"

NSI="${SCRIPT_DIR}/ZuluGO.nsi"
STAGE="${OUT_DIR}/payload"
OUT_EXE="${OUT_DIR}/ZuluGO_Setup.exe"

# Kept in step with the names ZuluGO.nsi expects. The DLLs are copied by
# wildcard on both sides, so a new support DLL in a later client build needs no
# edit in either file.
BIG_NAME="${ZULUGO_BIG_NAME:-Zulu.modbig}"
LAUNCHER_NAME="ZuluGOLauncher.exe"
REQUIRED=( "ZuluGO.exe" "${BIG_NAME}" "cacert.pem" "README.txt" )

die() { printf 'build-zulugo-installer: %s\n' "$*" >&2; exit 1; }

[ -d "${PAYLOAD_SRC}" ] || die "payload source not found: ${PAYLOAD_SRC}
Pass it as an argument or set ZULUGO_PAYLOAD_SRC."
command -v "${MAKENSIS}" >/dev/null 2>&1 || die "makensis not found (set MAKENSIS)"

for f in "${REQUIRED[@]}"; do
    [ -f "${PAYLOAD_SRC}/${f}" ] || die "payload source is missing ${f}: ${PAYLOAD_SRC}"
done

# The launcher is kept out of REQUIRED so it can be pointed somewhere else:
# it is built by a different toolchain from the client and does not always
# land in the same output directory. Checked separately and loudly either
# way -- every shortcut this installer creates targets the launcher, so a
# package built without one installs shortcuts that do nothing.
LAUNCHER_SRC="${ZULUGO_LAUNCHER:-${PAYLOAD_SRC}/${LAUNCHER_NAME}}"
[ -f "${LAUNCHER_SRC}" ] || die "launcher not found: ${LAUNCHER_SRC}
The Start Menu and desktop shortcuts both target ${LAUNCHER_NAME}, so the
package cannot be built without it. Build it, drop it in ${PAYLOAD_SRC},
or point ZULUGO_LAUNCHER at it."

# --- stage ------------------------------------------------------------------
# Wiped and rebuilt every time rather than copied over, so a file dropped from
# the client build cannot survive in the installer as a stale copy.
rm -rf -- "${STAGE}"
# build/ is created by the dockerised VC6 build and ends up owned by root on
# this machine, so say what to do rather than dying on a bare mkdir error.
mkdir -p -- "${STAGE}" 2>/dev/null || die "cannot create ${STAGE}
The repo's build/ directory is often owned by root (the dockerised VC6 build
creates it). Either chown it, or set OUT_DIR to somewhere writable."

for f in "${REQUIRED[@]}"; do
    cp -- "${PAYLOAD_SRC}/${f}" "${STAGE}/${f}"
done

# Staged under its canonical name whatever it was called at the source, so
# the listing below and the installer agree on one name.
cp -- "${LAUNCHER_SRC}" "${STAGE}/${LAUNCHER_NAME}"

# Copy DLLs individually rather than with a glob that could expand to nothing
# under `set -u`/nullglob differences, and count them so an empty set is loud.
dll_count=0
while IFS= read -r -d '' dll; do
    cp -- "${dll}" "${STAGE}/"
    dll_count=$((dll_count + 1))
done < <(find "${PAYLOAD_SRC}" -maxdepth 1 -type f -iname '*.dll' -print0)
[ "${dll_count}" -gt 0 ] || die "payload source has no DLLs: ${PAYLOAD_SRC}"

# The .wnd files that used to ship loose under Window\Menus live inside
# ${BIG_NAME} now. Staging copies a named list plus top-level DLLs, so the
# folder is dropped here; ZuluGO.nsi packs a named file list and never uses
# File /r either, so it could not reach the game folder in any case. Say so
# rather than dropping it silently -- a payload that still has one means the
# packaging step upstream has not caught up.
if [ -e "${PAYLOAD_SRC}/Window" ]; then
    echo "note: ignoring ${PAYLOAD_SRC}/Window -- those .wnd files ship inside ${BIG_NAME} now" >&2
fi
[ -e "${STAGE}/Window" ] && die "staging leaked ${STAGE}/Window"

echo "staged ${STAGE}:"
( cd -- "${STAGE}" && ls -1 ) | sed 's/^/  /'
echo "  (${dll_count} DLLs)"

# --- compile ----------------------------------------------------------------
NSIS_ARGS=(
    -V3
    -WX                      # any warning fails the build
    "-DPAYLOAD_DIR=${STAGE}"
    "-DOUTFILE=${OUT_EXE}"
    "-DAPPVERSION=${APPVERSION}"
    "-DBIGNAME=${BIG_NAME}"
    "-DLAUNCHER_SOURCE=${STAGE}/${LAUNCHER_NAME}"
)
if [ -n "${SIGN_CMD:-}" ]; then
    NSIS_ARGS+=( "-DSIGN_CMD=${SIGN_CMD}" )
fi

"${MAKENSIS}" "${NSIS_ARGS[@]}" "${NSI}"

[ -f "${OUT_EXE}" ] || die "makensis reported success but ${OUT_EXE} is missing"

echo
echo "built ${OUT_EXE}"
ls -lh -- "${OUT_EXE}" | awk '{print "  " $5}'
