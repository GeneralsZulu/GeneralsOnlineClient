# Launcher Auto-Update

## What it is

`ZuluLauncher.exe` is a small WIN32 process that ships alongside
`generalszh.exe` and becomes the new start-menu / desktop shortcut
target. On every cold start it:

1. Reads the installed game's `VS_VERSION_INFO` to get the
   currently installed Zulu version.
2. Fetches `https://storage.googleapis.com/zulu-installer/latest.json`
   to see what's been published.
3. If the published version differs from what's installed — in
   **either** direction — prompts the user, downloads that installer
   to `%TEMP%`, runs it silently (`/S /D=<install dir>`), waits for
   it to finish, then continues into `launchGame()` with the original
   launcher argv intact.
4. If versions match, the fetch fails, or the user declines, hands
   off directly to `generalszh.exe` with the launcher's argv.

## Rolling back a bad release

`latest.json` is authoritative, not a high-water mark. To pull
everyone off a broken build, republish `latest.json` pointing
`version` and `url` at the previous installer. The next launch of
every client that already took the bad build sees a version
mismatch and offers the older installer, with the prompt reading
"Zulu has been rolled back to an earlier release." instead of "A
newer Zulu release is available."

The installer has no version gate of its own — it overwrites in
place and rewrites the `DisplayVersion` uninstall key — so a
downgrade install is the same operation as an upgrade.

Two things this costs us. A release-variant build made locally that
is ahead of `latest.json` now gets a downgrade prompt on every
launch instead of being silently left alone; decline it, or use a
dev build, which reads `latest-dev.json` and gates on the exe hash
(below). And a rollback only reaches players who actually run the
launcher, so it is not instant.

## Dev builds gate on a hash, not a version

Dev builds don't bump semver between rebuilds, so step 3's `>`
comparison would never fire for them. Dev launchers (`latest-dev.json`)
instead compare hashes: the manifest's **`exe_sha256`** against the
SHA-256 of the installed `generalszh_zulu.exe`.

It has to be `exe_sha256`, not `sha256`. Those are two different
things and mixing them up is a bug we have already shipped once:

| field | hash of |
|---|---|
| `sha256` | the installer, `Zulu-Installer-Dev.exe` |
| `exe_sha256` | the game exe that installer lays down |

The gate originally compared the installed game exe against `sha256`
— the installer's hash. Two different files, so it never matched and
every single dev launch announced a new build. `make installer-dev`
now publishes both, taking `exe_sha256` from the staged exe before
the staging directory is cleaned.

A manifest with no `exe_sha256` (published before the field existed)
leaves the gate closed rather than prompting forever; the next
`make installer-dev` republishes it and updates resume. Note that a
launcher installed *before* this fix still reads the old field, so it
will prompt one last time — accepting that update replaces the
launcher and stops the loop.

## Why

Zulu ships often. Without auto-update, every player needs to
manually visit a download page when they want the new client,
which means LAN sessions split between a host running v1.1.5 and
players still on v1.1.3 — the exeCRC check then refuses to let
them play together. The launcher closes that gap: hit play, get
the latest, join the lobby.

The argv preservation work (PR — `2d129251f`) fixes a subtle
problem from the original launcher commit. The first version
handed control to the NSIS installer's silent post-install `Exec`
line, which used the start-menu shortcut's hardcoded `LAUNCHARGS`
("-mod Zulu.big"). Any extra args the user passed on the
launcher command line were dropped. The fix has the calling
launcher wait for the elevated installer (`ShellExecuteEx` with
`SEE_MASK_NOCLOSEPROCESS`), then fall through to the normal
`launchGame` path with the launcher's original argv. NSIS's silent
post-install `Exec` is removed so it doesn't race the calling
launcher.

## Two products from one source

`ZuluGOLauncher.exe` is the same binary built a second time for the
Zulu GO side-by-side test build (`installer/ZuluGO.nsi`). Everything
that differs is a value, not a code path: `zulu_add_launcher()` in
`launcher/CMakeLists.txt` configures `launcher/LauncherProduct.h.in`
once per target, and `ZuluLauncher.cpp` reads the result. The Zulu
target's values are exactly the strings that used to be hardcoded in
the .cpp.

Two of those values change behaviour, and both exist because Zulu GO
installs into a `ZuluGO` **subfolder** of the Zero Hour folder with its
launcher inside that subfolder, where Zulu installs into the Zero Hour
folder itself:

| value | Zulu | Zulu GO |
|---|---|---|
| `MOD_BIG_NAME` | *(empty — no `-mod`)* | `Zulu.modbig` |
| `INSTALL_TO_PARENT` | `0` — `/D=` is our own dir | `1` — `/D=` is our parent |

`MOD_BIG_NAME` makes the launcher prepend
`-mod "<launcher dir>\Zulu.modbig"` to whatever argv it was handed.
It has to be absolute: `parseMod()` prefixes any `-mod` without a drive
letter or leading separator with the *user data* directory, so a
relative name is looked for under `Documents\...\Zero Hour Data` and
silently mounts nothing. Ours goes first so a caller-supplied `-mod`
still wins (`parseMod` overwrites, last one on the line takes effect).

`INSTALL_TO_PARENT` is the one that bites if it is wrong: the update
hand-off passes `/S /D=<dir>`, and passing our own directory from a
subfolder install nests another `ZuluGO` inside on every update.

Zulu GO keeps the `-replaytheater` early exit. It is not product
specific — it exists so that "watch a replay" never sits through an
install that would swap the data out mid-session — and the resolver
degrades cleanly on GO: `ZuluGO.nsi` ships no `ReplayData` folder, so
the theater reports "the replay data map is missing" instead of
guessing. Its `@default` branch does follow the product config, using
the absolute `-mod` rather than the bare `Zulu.big` that only works
when the archive sits next to the game.

## Zulu GO updates itself, and only itself

Two things are different about the Zulu GO channel, and both come from
the same fact: the GeneralsOnline service decides who may play online
by **CRC32 of the game exe**, not by a version number. Publishing a
build and moving that gate are one act, so a client holding the wrong
binary cannot play at all — the in-game message is the misleading
"cannot connect to online services".

**The release gate falls back to a hash.** Zulu GO republishes under an
unchanged `APPVERSION` more often than it bumps one, and the semver
comparison cannot see that. `make zulugo-installer-release` therefore
publishes `exe_sha256` in `latest-go.json` as well as in the dev
manifest, and the release gate reads: update when the version differs,
**or** when the version matches and the published game-exe hash does
not. The hash of the staged client is taken while the payload still
exists and parked in `build/installer-go-tmp/exe-sha256.txt` for the
publish step, because the rule that builds the installer deletes the
payload. Zulu's own `latest.json` does not carry the field, so the Zulu
gate is unchanged and stays semver-only.

**The game hands off to the launcher instead of running GO's patcher.**
When the service refuses a build, the stock client downloads
GeneralsOnline's patcher and runs it with `GetCurrentDirectory()` as its
working directory. For Zulu GO that directory is the Zero Hour root, one
level *above* the `ZuluGO` folder — so it would update the player's
stock GeneralsOnline install and leave the build that was actually
refused exactly as it was, while writing into an install this package
promises not to touch. `LaunchZuluGOUpdater()`
(`OnlineServices_Init.cpp`, built only when `ZULU_GO_SELFHOST_BASE` is
defined) replaces that path: it starts `ZuluGOLauncher.exe` from its own
folder with `-update` and quits the game.

`-update` tells the launcher two things. The player has already agreed,
so it installs without a second prompt; and if there is nothing newer to
install it must say so rather than silently restart the game into the
same refusal. The launcher also waits for the game exe to become
writable before starting the installer — the game that sent it is still
shutting down, and Windows holds an exclusive image lock on a running
exe, which is the same lock the launcher's own rename-aside dance in
`ZuluGO.nsi` exists to work around.

## Code surface

- `launcher/ZuluLauncher.cpp` — ~380 lines. Manifest, version
  compare, JSON parsing (minimal — only the `version` and `url`
  fields are read), WinHTTP download with progress, ShellExecuteEx
  wait-handoff, fallthrough to `CreateProcess(generalszh.exe, ...)`
  with original argv.
- `launcher/CMakeLists.txt` / `launcher/LauncherProduct.h.in` /
  `launcher/ZuluLauncher.rc.in` / `launcher/ZuluLauncher.manifest` —
  build, per-product config, version-resource, and manifest plumbing.
  The .rc.in and the .cpp are shared by both launcher targets.
- `installer/Zulu.nsi` — removes the silent post-install `Exec`
  and adds the manifest-publishing step.
- `Makefile` — `installer-release` target publishes the manifest
  with no-cache headers; downloads pinned to the
  `zulu-installer` GCS bucket as defense-in-depth.
- `GeneralsMD/Code/Main/RTS.RC` and the build glue around it pull
  `APPVERSION` from `installer/Zulu.nsi` so the EXE
  `VS_VERSION_INFO` and the installed-version check always agree.

## Concerns

- **Backwards compatibility.** Players upgrading from a pre-launcher
  build to a launcher-shipping build need to run the new installer
  once (shortcut still points at `generalszh.exe`). After the first
  install, the shortcut points at `ZuluLauncher.exe` and the
  manifest check kicks in.
- **Update channel risk.** All installs follow the same
  `latest.json` channel. There is no opt-out and no per-user
  channel selection. Dev builds protect themselves by having a
  version higher than the published manifest.
- **Mismatch / replay / save.** None — the launcher is a separate
  process and never touches game state.
- **Bucket lock-in.** The download URL is currently hardcoded to
  the `zulu-installer` GCS bucket. Moving providers would require
  a new launcher build to ship before the migration. Worth noting
  rather than urgent.
- **Failure-to-launch path.** If `latest.json` fetch fails (offline
  / DNS / 503), the launcher logs and falls through to launching
  the installed binary. The player isn't blocked.
