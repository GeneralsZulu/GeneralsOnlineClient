#!/usr/bin/env bash
# Sign a ZuluGO.exe so the online service accepts it without a crcfiles slot.
#
#   scripts/zulugo/sign-build.sh --keygen KEY.pem      make a key pair; send us KEY.pub.pem
#   scripts/zulugo/sign-build.sh EXE dev|release [KEY.pem]
#
# Writes EXE.buildsig ("<variant> <base64 sig>" over "zulugo-build:<variant>:<crc32>"),
# which the client sends with its version check. The private key never leaves
# this machine and never goes in the exe; the service only holds public keys
# (signkeys/prod for release, signkeys/dev for dev builds). Re-run after every
# build, and after Authenticode signing, since both change the exe's bytes.
# The key is the first of: KEY.pem, $ZULUGO_BUILD_KEY, ~/.config/zulu/zulugo-build-key.pem,
# then the gcloud secret $ZULUGO_BUILD_KEY_SECRET (default zulugo_build_key, the
# prod key; fetched to a temp file that is removed on exit). Exits 3 if none is found.
# Needs only openssl, gzip and coreutils (plus gcloud for the secret), so it runs
# in Git Bash on Windows.
set -euo pipefail

if [ "${1:-}" = "--keygen" ]; then
	key="${2:?usage: $0 --keygen KEY.pem}"
	[ ! -e "$key" ] || { echo "refusing to overwrite $key" >&2; exit 1; }
	(umask 077; openssl ecparam -name prime256v1 -genkey -noout -out "$key")
	openssl ec -in "$key" -pubout -out "${key%.pem}.pub.pem" 2>/dev/null
	echo "private key: $key (keep it)"
	echo "public key:  ${key%.pem}.pub.pem (send this one)"
	exit 0
fi

exe="${1:?usage: $0 EXE dev|release [KEY.pem]}"
variant="${2:?usage: $0 EXE dev|release [KEY.pem]}"
key="${3:-${ZULUGO_BUILD_KEY:-}}"
case "$variant" in dev|release) ;; *) echo "variant must be dev or release" >&2; exit 1 ;; esac
if [ -z "$key" ] && [ -r "$HOME/.config/zulu/zulugo-build-key.pem" ]; then
	key="$HOME/.config/zulu/zulugo-build-key.pem"
fi
if [ -z "$key" ] && command -v "${GCLOUD:-gcloud}" >/dev/null 2>&1; then
	tmp=$(umask 077; mktemp)
	trap 'rm -f "$tmp"' EXIT
	if "${GCLOUD:-gcloud}" secrets versions access latest --secret="${ZULUGO_BUILD_KEY_SECRET:-zulugo_build_key}" > "$tmp" 2>/dev/null && [ -s "$tmp" ]; then
		key="$tmp"
	fi
fi
[ -n "$key" ] && [ -r "$key" ] || { echo "no build signing key (set ZULUGO_BUILD_KEY)" >&2; exit 3; }

# the gzip trailer carries the input's CRC32, little endian
crc=$(gzip -c "$exe" | tail -c 8 | head -c 4 | od -An -tu4 | tr -d ' \n')
sig=$(printf 'zulugo-build:%s:%s' "$variant" "$crc" | openssl dgst -sha256 -sign "$key" | base64 | tr -d '\n')
printf '%s %s\n' "$variant" "$sig" > "$exe.buildsig"
echo "[buildsig] $exe.buildsig: $variant build, crc32 $crc"
