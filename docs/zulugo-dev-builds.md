# Zulu GO: building, signing and running a dev client

The online service only lets in an exe it knows: either one whose CRC is in a
`crcfiles` slot on the server (every release so far), or one carrying a valid
**build signature**. A build you made yourself needs the signature.

## 1. One-time setup

Make a signing key pair and send the **public** half to Bill. The private key
never leaves your machine, so nothing secret changes hands.

```
scripts/zulugo/sign-build.sh --keygen ~/.config/zulu/zulugo-build-key.pem
# send ~/.config/zulu/zulugo-build-key.pub.pem
```

Bill installs it on the server (see "Server" below). Until then the service
turns your build away with "cannot connect to online services".

## 2. Build

Linux, Docker (the path the releases use):

```
make zulugo-exe        # -> build/docker-msvc-wine/GeneralsMD/GeneralsOnlineZH.exe
make zulugo-modbig     # -> build/installer-go-tmp/Zulu.modbig (only if assets/ changed)
```

The first run builds the Docker image and vcpkg, which takes a while; later
runs are incremental. Without gcloud access the client keys are left out, so
stats and replay uploads are rejected. Set `ZULU_CLIENT_KEY` and
`CNCSTATS_ZULU_CLIENT_KEY` in the environment if you have them.

Windows, Visual Studio 2022 (untested for Zulu GO): configure with
`-DZULU_GO_SELFHOST_BASE=https://cncstats.computersrfun.org/go` (without it the
client talks to the public GeneralsOnline service instead) and build
`z_generals`.

Any build that is not one of the `zulugo-installer*` release targets is a
`dev` build: its uploads carry `X-Zulu-Build: dev-<hash>`, which radarvan
records as `is_dev`.

## 3. Sign

Sign the exact exe you will run, after every build:

```
scripts/zulugo/sign-build.sh path/to/ZuluGO.exe dev
```

This writes `ZuluGO.exe.buildsig` next to it. The signature only fits that
exe, so a rebuild needs a re-sign. Needs `openssl`, `gzip` and coreutils (Git
Bash on Windows has them).

## 4. Run

Copy the exe over `ZuluGO\ZuluGO.exe` in a Zulu GO install (keep the
original), put the `.buildsig` next to it, and start it from the Zero Hour
folder:

```
ZuluGO\ZuluGO.exe -mod "C:\full\path\to\ZuluGO\Zulu.modbig"
```

Start the exe directly, not ZuluGOLauncher: the launcher sees an exe it did
not install and offers to replace it. After the version check you get a
"Dev Build" notice, then online works as normal.

- Lobbies only accept clients with the same exe and INI CRC, so a dev build
  only plays with copies of itself. To test with someone, send them both files.
- The online version numbers (`GENERALS_ONLINE_VERSION`, net and services
  versions) must still match the server's.

## Server (Bill)

Public keys sit next to `crcfiles`, re-read on every version check:

- `/opt/gosvc/signkeys/prod/*.pem`: may sign `release` and `dev` builds.
  Holds only the prod key.
- `/opt/gosvc/signkeys/dev/*.pem`: may sign `dev` builds only.

Deleting a `.pem` revokes it immediately. `gosvc-swap.sh` carries the folder
across deploys. The tree is `gosvc`-owned and 750, so use `sudo` and absolute
paths:

```
sudo install -D -o gosvc -g gosvc -m 644 brendan.pub.pem /opt/gosvc/signkeys/dev/brendan.pem
```

## Release builds and the prod key

`make zulugo-installer*` signs the staged `ZuluGO.exe` after Authenticode and
ships the `.buildsig` in the installer. A `release` build fails without a key;
others are left unsigned with a note.

- Private key: only in GCP Secret Manager, secret `zulugo_build_key`
  (project cncstats). The script fetches it to a temp file and deletes it.
  Set `ZULUGO_BUILD_KEY` to use a local key instead.
- Public key: `scripts/zulugo/keys/prod.pub.pem` in this repo, also in secret
  `zulugo_build_key_pub`. It is what goes in `signkeys/prod/`.

The `crcfiles` slots keep working, so releases from before signing still get in.

Limit: the exe CRC is reported by the client itself, as it always was. Signing
keeps out builds nobody vouched for; it does not stop a modified client that
lies about its CRC.
