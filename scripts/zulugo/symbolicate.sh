#!/usr/bin/env bash
# Symbolicate a Zulu GO crash report stored by cncstats.
#
#   scripts/zulugo/symbolicate.sh <crash dir> [symbols dir]
#
# <crash dir> is one crash-YYYYMMDD/<player>-HHMMSS-<id>/ directory as the
# server stores it (pull it with /get_logs?seed=crash-YYYYMMDD, or scp it off
# the logs volume). It holds summary.txt, event.json and, with the breakpad
# backend, a .dmp attachment.
#
# Symbols are the PDB and exe of the build that crashed. `make
# zulugo-installer-release` uploads them to gs://<bucket>/symbols/zulugo/v<ver>/
# and keeps a copy in build/installer-go-tmp/symbols/. Without a symbols dir
# the script reads the release from summary.txt and fetches that version from
# the bucket into ~/.cache/zulugo-symbols/<ver>/.
#
# What it does, in order of how much it gives you:
#   1. summary.txt frames (module+offset): resolved with llvm-symbolizer
#      against the PDB. Works on Linux; LLVM's native PDB reader gives
#      function names, line numbers when the PDB has them.
#   2. a minidump: walked with minidump-stackwalk (rust-minidump) against
#      breakpad .sym files produced from the PDB by dump_syms. Every thread,
#      registers, the crashing frame. Both tools: `cargo install dump_syms
#      minidump-stackwalk`. Without them the dump is left for WinDbg
#      (File > Open Crash Dump, .sympath pointing at the symbols dir).
set -euo pipefail

crash_dir=${1:?usage: symbolicate.sh <crash dir> [symbols dir]}
symbols_dir=${2:-}
bucket=${GCS_BUCKET:-zulu-installer}
exe_name=ZuluGO.exe
pdb_name=GeneralsOnlineZH.pdb   # the name the exe's debug directory carries; must not be renamed

[ -d "$crash_dir" ] || { echo "not a directory: $crash_dir" >&2; exit 1; }
summary="$crash_dir/summary.txt"
[ -s "$summary" ] || { echo "no summary.txt in $crash_dir" >&2; exit 1; }

release=$(sed -n 's/^release:[[:space:]]*//p' "$summary" | head -n1)
version=${release#zulugo@}
echo "crash:    $crash_dir"
echo "release:  ${release:-?}"

if [ -z "$symbols_dir" ]; then
	[ -n "$version" ] && [ "$version" != "$release" ] || {
		echo "summary.txt carries no zulugo@<version> release; pass the symbols dir explicitly" >&2; exit 1; }
	symbols_dir="$HOME/.cache/zulugo-symbols/$version"
	if [ ! -s "$symbols_dir/$pdb_name" ]; then
		mkdir -p "$symbols_dir"
		echo "fetching symbols for $version from gs://$bucket/symbols/zulugo/v$version/"
		gcloud storage cp "gs://$bucket/symbols/zulugo/v$version/*" "$symbols_dir/"
	fi
fi
[ -s "$symbols_dir/$pdb_name" ] || { echo "no $pdb_name in $symbols_dir" >&2; exit 1; }
echo "symbols:  $symbols_dir"
echo

# 1. The frames in summary.txt. Lines look like
#      #0  ZuluGO.exe+0x112345 (0x00512345)
#    and only the game's own module can be resolved here; system DLLs are
#    printed as they are.
if grep -q '^    #' "$summary" && command -v llvm-symbolizer >/dev/null; then
	echo "== frames (from summary.txt) =="
	grep '^    #' "$summary" | while read -r num where rest; do
		mod=${where%%+*}
		off=${where#*+}
		if [ "$mod" = "$exe_name" ] || [ "$mod" = "GeneralsOnlineZH.exe" ]; then
			sym=$( { llvm-symbolizer --obj="$symbols_dir/$exe_name" --relative-address "$off" 2>/dev/null || true; } | head -n2 | paste -sd' ' - || true)
			printf '  %-4s %-36s %s\n' "$num" "$where" "${sym:-?}"
		else
			printf '  %-4s %s %s\n' "$num" "$where" "$rest"
		fi
	done
	echo
fi

# 2. A minidump, if the report carries one.
dmp=$(ls "$crash_dir"/*.dmp 2>/dev/null | head -n1 || true)
if [ -n "$dmp" ]; then
	if command -v minidump-stackwalk >/dev/null && command -v dump_syms >/dev/null; then
		sym_root="$symbols_dir/breakpad"
		if [ ! -d "$sym_root" ]; then
			echo "building breakpad symbols from $pdb_name (once per version)"
			tmp=$(mktemp)
			dump_syms "$symbols_dir/$pdb_name" > "$tmp"
			# layout minidump-stackwalk expects: <root>/<module>/<debug id>/<module>.sym
			read -r _ _ _ debug_id module < <(head -n1 "$tmp")
			mkdir -p "$sym_root/$module/$debug_id"
			mv "$tmp" "$sym_root/$module/$debug_id/${module%.pdb}.sym"
		fi
		echo "== minidump: $(basename "$dmp") =="
		minidump-stackwalk --symbols-path "$sym_root" "$dmp"
	else
		echo "== minidump: $(basename "$dmp") =="
		echo "  minidump-stackwalk / dump_syms not installed (cargo install dump_syms minidump-stackwalk)."
		echo "  Open it in WinDbg with .sympath $symbols_dir, or !analyze -v."
	fi
fi
