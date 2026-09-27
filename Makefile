# Top-level convenience targets. The CMake/Ninja build is driven by
# scripts/docker-build.sh; this Makefile only wraps the steps that turn the
# build output plus the assets/ tree into a shippable installer.
#
# Common invocations:
#   make installer           build Zulu.big, build the exe, package Zulu_Setup.exe
#   make installer-release   build the installer and upload it to GCS as a
#                            versioned, publicly-downloadable object
#   make zulu-big            just (re)pack assets/ into build/installer-tmp/Zulu.big
#   make replay-bigs         rebuild the per-release data archives (ReplayData/)
#                            that Replay Theater mounts to play back old replays
#   make zulu-exe            build the shipping Release exe
#   make zulu-exe-log        build a Release exe with DEBUG_LOGGING + DEBUG_CRASHING
#                            (no debug CRT; same DLL deps as the shipping exe)
#   make clean-installer     remove the staged tmp dir and the built setup exe
#   make sign-status         show whether Authenticode signing is configured
#
# Those targets all build the Zulu product (VC6, retail Zero Hour). The
# second product, Zulu GO (MSVC 2022, the GeneralsOnline client), has its own
# set of targets named zulugo-*; see the "Zulu GO" section at the bottom of
# this file. The two share only scripts/sign.sh and the GCS bucket.
#
# Every PE that ships (installer, uninstaller, game exe, launcher) is signed
# with Azure Artifact Signing when ~/.config/zulu/signing.env is present;
# see scripts/sign.sh. `make installer-release` refuses to run unsigned.

BIG          ?= big
NSIS         ?= makensis
DOCKER_BUILD ?= ./scripts/docker-build.sh
GCLOUD       ?= gcloud
GCS_BUCKET   ?= zulu-installer
# Authenticode signer (Azure Artifact Signing via jsign). Given to NSIS as an
# absolute path because !finalize runs from makensis's cwd, not the .nsi dir.
# The script is a no-op when signing is not configured; see scripts/sign.sh.
SIGN         ?= $(abspath scripts/sign.sh)

ASSETS_DIR := assets
TMP_DIR    := build/installer-tmp

# scripts/docker-build.sh keys its build directory off the cmake preset:
# build/docker-<preset>. The shipping exe and the launcher use the default
# `vc6` preset; the logging exe uses `vc6-releaselog`, which switches
# RTS_DEBUG_LOGGING/RTS_DEBUG_CRASHING on. They deliberately no longer share a
# cmake cache: a `vc6` configure on top of a `vc6-releaselog` cache kept the
# DEBUG_* flags (cmake caches are sticky for variables a preset does not name),
# so `make installer` silently produced a logging-enabled "release" binary.
# Separate dirs also keep both configurations' ninja state warm, so alternating
# `make installer` and `make installer-dev` rebuilds nothing.
PRESET_DEFAULT := vc6
PRESET_LOG     := vc6-releaselog
BUILD_DIR      := build/docker-$(PRESET_DEFAULT)
BUILD_DIR_LOG  := build/docker-$(PRESET_LOG)

BIG_NAME      := Zulu.big
EXE_NAME      := generalszh_zulu.exe
EXE_LOG_NAME  := generalszh_zulu_log.exe
LAUNCHER_NAME := ZuluLauncher.exe
SOURCE_EXE      := $(BUILD_DIR)/GeneralsMD/generalszh.exe
SOURCE_EXE_LOG  := $(BUILD_DIR_LOG)/GeneralsMD/generalszh.exe
SOURCE_LAUNCHER := $(BUILD_DIR)/launcher/ZuluLauncher.exe

# Per-release data archives for old-replay playback. Reconstructed from git on
# demand rather than checked in: they are ~95% identical to one another, so
# storing them would add ~73 MB of near-duplicate binaries to the repo.
REPLAY_DATA_CSV := installer/replay_data_versions.csv
REPLAY_DATA_DIR := $(TMP_DIR)/ReplayData
REPLAY_DATA_STAMP := $(TMP_DIR)/.replay-data-built

NSI               := installer/Zulu.nsi
INSTALLER_OUT     := installer/Zulu_Setup.exe
INSTALLER_OUT_DEV := installer/Zulu_Setup_Dev.exe
LATEST_JSON       := $(TMP_DIR)/latest.json
LATEST_DEV_JSON   := $(TMP_DIR)/latest-dev.json
# Hash of the game exe a dev installer carries, stashed while it is still
# staged so the manifest can publish it. See the installer-dev recipe.
DEV_EXE_SHA_FILE  := $(TMP_DIR)/dev-exe-sha256.txt

# Dev artifact lives at a fixed name in the same GCS bucket so the dev
# launcher can always pull the latest dev build from a stable URL; the
# manifest sidecar (latest-dev.json) carries the SHA256 the launcher
# uses to decide whether to prompt for an update.
DEV_OBJECT_NAME       := Zulu-Installer-Dev.exe
DEV_GCS_URI            = gs://$(GCS_BUCKET)/$(DEV_OBJECT_NAME)
DEV_PUBLIC_URL         = https://storage.googleapis.com/$(GCS_BUCKET)/$(DEV_OBJECT_NAME)
LATEST_DEV_GCS_URI     = gs://$(GCS_BUCKET)/latest-dev.json
LATEST_DEV_PUBLIC_URL  = https://storage.googleapis.com/$(GCS_BUCKET)/latest-dev.json

# Single source of truth for the release version is APPVERSION inside the
# NSI script. Keep this Make-side parser tolerant of whitespace/quoting so
# the user can edit the .nsi without having to also touch the Makefile.
APPVERSION   := $(shell sed -n 's/^!define[[:space:]]\+APPVERSION[[:space:]]\+"\([^"]*\)".*/\1/p' $(NSI))
RELEASE_NAME      = Zulu_Setup_v$(APPVERSION).exe
GCS_URI           = gs://$(GCS_BUCKET)/$(RELEASE_NAME)
PUBLIC_URL        = https://storage.googleapis.com/$(GCS_BUCKET)/$(RELEASE_NAME)
LATEST_GCS_URI    = gs://$(GCS_BUCKET)/latest.json
LATEST_PUBLIC_URL = https://storage.googleapis.com/$(GCS_BUCKET)/latest.json

# Decompose APPVERSION ("major.minor.build") for the cmake build so the
# binary's TheVersion (and therefore every replay it records) reports the
# same version as the installer. Empty trailing components default to 0
# so APPVERSION="1.1" still parses as 1.1.0. Forwarded to docker-build.sh
# as ZULU_VERSION_* env vars; cmake is force-reconfigured below so the
# new values are baked into BuildVersion.h on every Make-driven build.
ZULU_VERSION_MAJOR    := $(or $(word 1,$(subst ., ,$(APPVERSION))),0)
ZULU_VERSION_MINOR    := $(or $(word 2,$(subst ., ,$(APPVERSION))),0)
ZULU_VERSION_BUILDNUM := $(or $(word 3,$(subst ., ,$(APPVERSION))),0)

TMP_BIG      := $(TMP_DIR)/$(BIG_NAME)
TMP_EXE      := $(TMP_DIR)/$(EXE_NAME)
TMP_EXE_LOG  := $(TMP_DIR)/$(EXE_LOG_NAME)
TMP_LAUNCHER := $(TMP_DIR)/$(LAUNCHER_NAME)

ASSET_FILES := $(shell find $(ASSETS_DIR) -type f 2>/dev/null)

# Empty-archive seed bytes for `big add` to work against. The Go BIGF library
# refuses to open a missing or zero-byte file, so we hand-roll a 16-byte
# header: magic "BIGF", little-endian archive_size = 16, big-endian count = 0,
# big-endian first-data offset = 16.
EMPTY_BIG_BYTES := 'BIGF\x10\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x10'

.PHONY: installer installer-release installer-dev zulu-big replay-bigs zulu-exe zulu-exe-log zulu-launcher clean-installer sign-status

sign-status:
	@$(SIGN) --status

# Target-specific variables that propagate down the prereq chain so the
# docker-build-* recipes pick the right Discord webhook secret and bake
# the right build-variant tag into the binaries. Plain `make zulu-exe`
# leaves these empty: the discord fetch is skipped and the cmake
# buildvariant module defaults the tag to "dev".
installer:         DISCORD_WEBHOOK_SECRET := discord_webhook
installer:         ZULU_BUILD_VARIANT     := release
installer-release: DISCORD_WEBHOOK_SECRET := discord_webhook
installer-release: ZULU_BUILD_VARIANT     := release
# A release must never ship unsigned by accident: with this set, sign.sh
# fails the build instead of skipping when signing is not configured.
installer-release: export ZULU_SIGN_REQUIRED := 1
installer-dev:     DISCORD_WEBHOOK_SECRET := debug_discord_webhook
installer-dev:     ZULU_BUILD_VARIANT     := dev

installer: $(INSTALLER_OUT)

zulu-big: $(TMP_BIG)

replay-bigs: $(REPLAY_DATA_STAMP)

zulu-exe: $(TMP_EXE)

zulu-exe-log: $(TMP_EXE_LOG)

zulu-launcher: $(TMP_LAUNCHER)

# Stage the BIG by packing every file in assets/ in one pass, mapping each
# relative path to a backslash-separated archive path (Data/INI/Foo.ini ->
# Data\INI\Foo.ini). Rebuilt from scratch each time so removed assets do not
# linger inside the archive. The old per-file `big add` loop rewrote the
# archive once per file, which took ~12 minutes once the community patch INI
# tree (2000+ files) moved into assets/ (and its expanded command line
# overflowed ARG_MAX).
$(TMP_BIG): $(ASSET_FILES) | $(TMP_DIR)
	@rm -f $@
	@python3 scripts/pack_big.py $(ASSETS_DIR) $@

# Stage every historical Zulu.big next to a copy of the version map, so an
# installed client can mount the data a given replay was recorded against
# (see installer/replay_data_versions.csv for why that is necessary). Each
# archive is rebuilt from the assets tree its release shipped, straight out
# of git history, so this depends only on the map and the packer -- not on
# the current working tree.
$(REPLAY_DATA_STAMP): $(REPLAY_DATA_CSV) scripts/build_replay_bigs.py scripts/pack_big.py | $(TMP_DIR)
	@rm -rf "$(REPLAY_DATA_DIR)"
	@python3 scripts/build_replay_bigs.py "$(REPLAY_DATA_CSV)" "$(REPLAY_DATA_DIR)"
	@cp "$(REPLAY_DATA_CSV)" "$(REPLAY_DATA_DIR)/versions.csv"
	@touch $@

# Drive the docker build for the Zero Hour exe and copy the result into the
# tmp dir under its installer name. We always invoke docker-build.sh so the
# build system itself decides whether anything is stale; pinning to a single
# Make timestamp would lie about the underlying source tree.
#
# --cmake forces a configure pass so the ZULU_VERSION_* env vars below
# are picked up into BuildVersion.h. Without --cmake, ninja keeps the
# previous configure's BuildVersion.h and the version doesn't update.
#
# DISCORD_WEBHOOK_SECRET, when set by a parent target (installer /
# installer-release / installer-dev), names the GCP Secret Manager secret
# to fetch and bake into the binary as ZULU_DISCORD_WEBHOOK_URL. Empty
# (the default for plain `make zulu-exe`) skips the fetch entirely so dev
# builds keep working without gcloud credentials. The secret must be
# non-empty when requested; an empty payload aborts the build so a release
# never silently ships with the feature disabled.
.PHONY: docker-build-z_generals
docker-build-z_generals:
	@if [ -n "$(DISCORD_WEBHOOK_SECRET)" ] && [ -z "$$ZULU_DISCORD_WEBHOOK_URL" ]; then \
		echo "[discord] fetching secret '$(DISCORD_WEBHOOK_SECRET)' from GCP Secret Manager..."; \
		ZULU_DISCORD_WEBHOOK_URL=$$($(GCLOUD) secrets versions access latest --secret=$(DISCORD_WEBHOOK_SECRET)) \
			|| { echo "ERROR: gcloud failed to read secret '$(DISCORD_WEBHOOK_SECRET)' (see error above)."; exit 1; }; \
		if [ -z "$$ZULU_DISCORD_WEBHOOK_URL" ]; then \
			echo "ERROR: secret '$(DISCORD_WEBHOOK_SECRET)' is empty."; \
			exit 1; \
		fi; \
		export ZULU_DISCORD_WEBHOOK_URL; \
		echo "[discord] webhook url loaded ($${#ZULU_DISCORD_WEBHOOK_URL} bytes)"; \
	fi; \
	ZULU_VERSION_MAJOR=$(ZULU_VERSION_MAJOR) \
	ZULU_VERSION_MINOR=$(ZULU_VERSION_MINOR) \
	ZULU_VERSION_BUILDNUM=$(ZULU_VERSION_BUILDNUM) \
	ZULU_BUILD_VARIANT=$(ZULU_BUILD_VARIANT) \
	$(DOCKER_BUILD) --cmake --target z_generals

# Sign the staged copy, not the build output, so the docker build dir stays
# pristine and a re-run never double-signs.
$(TMP_EXE): docker-build-z_generals | $(TMP_DIR)
	cp "$(SOURCE_EXE)" "$@"
	$(SIGN) "$@"

# Logging variant: same Release build (no debug CRT, same DLL deps as the
# shipping exe), but with DEBUG_LOGGING + DEBUG_CRASHING compiled in. Writes
# DebugLogFile.txt next to the running .exe at runtime. Built via the
# vc6-releaselog cmake preset, which gets its own build dir ($(BUILD_DIR_LOG))
# so it neither leaks its DEBUG_* flags into the shipping build nor invalidates
# that build's ninja state.
#
# Mirrors docker-build-z_generals's discord-secret fetch so installer-dev
# (which depends on this recipe to get the log-enabled exe) can bake in
# the debug discord webhook the same way installer bakes in the prod one.
.PHONY: docker-build-z_generals-log
docker-build-z_generals-log:
	@if [ -n "$(DISCORD_WEBHOOK_SECRET)" ] && [ -z "$$ZULU_DISCORD_WEBHOOK_URL" ]; then \
		echo "[discord] fetching secret '$(DISCORD_WEBHOOK_SECRET)' from GCP Secret Manager..."; \
		ZULU_DISCORD_WEBHOOK_URL=$$($(GCLOUD) secrets versions access latest --secret=$(DISCORD_WEBHOOK_SECRET)) \
			|| { echo "ERROR: gcloud failed to read secret '$(DISCORD_WEBHOOK_SECRET)' (see error above)."; exit 1; }; \
		if [ -z "$$ZULU_DISCORD_WEBHOOK_URL" ]; then \
			echo "ERROR: secret '$(DISCORD_WEBHOOK_SECRET)' is empty."; \
			exit 1; \
		fi; \
		export ZULU_DISCORD_WEBHOOK_URL; \
		echo "[discord] webhook url loaded ($${#ZULU_DISCORD_WEBHOOK_URL} bytes)"; \
	fi; \
	PRESET=$(PRESET_LOG) \
	ZULU_VERSION_MAJOR=$(ZULU_VERSION_MAJOR) \
	ZULU_VERSION_MINOR=$(ZULU_VERSION_MINOR) \
	ZULU_VERSION_BUILDNUM=$(ZULU_VERSION_BUILDNUM) \
	ZULU_BUILD_VARIANT=$(ZULU_BUILD_VARIANT) \
	$(DOCKER_BUILD) --cmake --target z_generals

$(TMP_EXE_LOG): docker-build-z_generals-log | $(TMP_DIR)
	cp "$(SOURCE_EXE_LOG)" "$@"
	$(SIGN) "$@"
	@echo
	@echo "Logging exe ready: $@"
	@echo "Ship this to the rejoiner VM; DebugLogFile.txt will land next to the .exe."

# Launcher build, mirroring the game-exe rule. Same ZULU_VERSION_* env
# vars so the launcher's embedded VS_VERSION_INFO matches the game's.
.PHONY: docker-build-z_launcher
docker-build-z_launcher:
	ZULU_VERSION_MAJOR=$(ZULU_VERSION_MAJOR) \
	ZULU_VERSION_MINOR=$(ZULU_VERSION_MINOR) \
	ZULU_VERSION_BUILDNUM=$(ZULU_VERSION_BUILDNUM) \
	ZULU_BUILD_VARIANT=$(ZULU_BUILD_VARIANT) \
	$(DOCKER_BUILD) --cmake --target z_launcher

$(TMP_LAUNCHER): docker-build-z_launcher | $(TMP_DIR)
	cp "$(SOURCE_LAUNCHER)" "$@"
	$(SIGN) "$@"

# Hand the staged paths to NSIS via /D overrides (paths are relative to the
# .nsi file's directory, i.e. installer/). After packaging, drop the staged
# big/exe/launcher so they don't sit around taking disk; keep $(TMP_DIR)
# itself in case something else stashes things there.
$(INSTALLER_OUT): $(TMP_BIG) $(TMP_EXE) $(TMP_LAUNCHER) $(REPLAY_DATA_STAMP) $(NSI)
	$(NSIS) \
		-DBIG_SOURCE="../$(TMP_BIG)" \
		-DEXE_SOURCE="../$(TMP_EXE)" \
		-DLAUNCHER_SOURCE="../$(TMP_LAUNCHER)" \
		-DREPLAYDATA_SOURCE="../$(REPLAY_DATA_DIR)" \
		-DSIGN_CMD="$(SIGN)" \
		$(NSI)
	@rm -f "$(TMP_BIG)" "$(TMP_EXE)" "$(TMP_LAUNCHER)"

$(TMP_DIR):
	@mkdir -p $@

clean-installer:
	rm -rf "$(TMP_DIR)" "$(INSTALLER_OUT)"

# Build a dev-tagged installer and publish it to GCS under a stable name
# (Zulu-Installer-Dev.exe + latest-dev.json) so dev launchers always know
# where to look. Uses the vc6-releaselog preset so dev builds ship with
# DEBUG_LOGGING + DEBUG_CRASHING on by default - that's the whole reason
# you'd cut a dev build.
#
# We rename the NSIS output to $(INSTALLER_OUT_DEV) so a later `make
# installer` doesn't see a fresh installer/Zulu_Setup.exe on disk and
# skip the build.
installer-dev: $(TMP_BIG) $(TMP_EXE_LOG) $(TMP_LAUNCHER) $(REPLAY_DATA_STAMP) $(NSI) | $(TMP_DIR)
	@test -n "$(APPVERSION)" || { \
		echo "ERROR: could not parse APPVERSION from $(NSI)"; exit 1; }
	$(NSIS) \
		-DBIG_SOURCE="../$(TMP_BIG)" \
		-DEXE_SOURCE="../$(TMP_EXE_LOG)" \
		-DLAUNCHER_SOURCE="../$(TMP_LAUNCHER)" \
		-DREPLAYDATA_SOURCE="../$(REPLAY_DATA_DIR)" \
		-DSIGN_CMD="$(SIGN)" \
		$(NSI)
	@# The dev update gate compares the game exe on disk against the game exe
	@# this installer carries, so the manifest needs that hash -- take it while
	@# the staged exe still exists (it is dropped once the manifest is written).
	@sha256sum "$(TMP_EXE_LOG)" | cut -d' ' -f1 > "$(DEV_EXE_SHA_FILE)"
	@rm -f "$(TMP_BIG)" "$(TMP_EXE_LOG)" "$(TMP_LAUNCHER)"
	mv "$(INSTALLER_OUT)" "$(INSTALLER_OUT_DEV)"
	$(GCLOUD) storage cp "$(INSTALLER_OUT_DEV)" "$(DEV_GCS_URI)"
	@$(GCLOUD) storage objects update "$(DEV_GCS_URI)" \
		--cache-control="no-cache, max-age=0" \
		|| echo "[note] could not set cache-control on $(DEV_OBJECT_NAME)."
	@$(GCLOUD) storage objects update "$(DEV_GCS_URI)" \
		--add-acl-grant=entity=AllUsers,role=READER \
		|| echo "[note] per-object ACL grant failed for $(DEV_OBJECT_NAME); rely on bucket-level allUsers grant."
	@# "sha256" is the installer's own hash; "exe_sha256" is the hash of the
	@# game exe inside it. The launcher's dev gate compares exe_sha256 against
	@# the installed generalszh_zulu.exe. Comparing against "sha256" instead is
	@# what made every dev launch think an update was waiting: it hashed the
	@# game exe and compared it to the hash of a completely different file.
	@SIZE=$$(stat -c%s "$(INSTALLER_OUT_DEV)"); \
	SHA=$$(sha256sum "$(INSTALLER_OUT_DEV)" | cut -d' ' -f1); \
	EXESHA=$$(cat "$(DEV_EXE_SHA_FILE)"); \
	printf '%s\n' \
	    '{' \
	    '  "version": "$(APPVERSION)",' \
	    '  "url": "$(DEV_PUBLIC_URL)",' \
	    "  \"size\": $$SIZE," \
	    "  \"sha256\": \"$$SHA\"," \
	    "  \"exe_sha256\": \"$$EXESHA\"" \
	    '}' > "$(LATEST_DEV_JSON)"
	$(GCLOUD) storage cp "$(LATEST_DEV_JSON)" "$(LATEST_DEV_GCS_URI)"
	@$(GCLOUD) storage objects update "$(LATEST_DEV_GCS_URI)" \
		--content-type=application/json \
		--cache-control="no-cache, max-age=0" \
		|| echo "[note] could not set metadata on latest-dev.json."
	@$(GCLOUD) storage objects update "$(LATEST_DEV_GCS_URI)" \
		--add-acl-grant=entity=AllUsers,role=READER \
		|| echo "[note] per-object ACL grant failed for latest-dev.json; rely on bucket-level allUsers grant."
	@rm -f "$(LATEST_DEV_JSON)" "$(DEV_EXE_SHA_FILE)"
	@echo
	@echo "Uploaded dev installer: $(DEV_GCS_URI)"
	@echo "Public URL:             $(DEV_PUBLIC_URL)"
	@echo "Update manifest:        $(LATEST_DEV_GCS_URI)"
	@echo "Manifest URL:           $(LATEST_DEV_PUBLIC_URL)"

# Build the installer (via the regular pipeline) and publish it to GCS under
# a version-stamped object name so each release has a stable shareable URL.
# The ACL grant marks the uploaded object world-readable; if the bucket has
# uniform bucket-level access enabled the per-object grant will fail, in
# which case allUsers must be granted Storage Object Viewer at the bucket
# level for the public URL to work.
installer-release: $(INSTALLER_OUT) | $(TMP_DIR)
	@test -n "$(APPVERSION)" || { \
		echo "ERROR: could not parse APPVERSION from $(NSI)"; exit 1; }
	$(GCLOUD) storage cp "$(INSTALLER_OUT)" "$(GCS_URI)"
	@$(GCLOUD) storage objects update "$(GCS_URI)" \
		--add-acl-grant=entity=AllUsers,role=READER \
		|| echo "[note] per-object ACL grant failed; if the bucket uses uniform bucket-level access, ensure allUsers is granted Storage Object Viewer at the bucket level."
	@# Manifest the launcher reads on every cold start. Cache-Control is
	@# overridden because GCS's default for public objects (max-age=3600)
	@# would mask freshly published releases for up to an hour. The launcher
	@# additionally sets INTERNET_FLAG_RELOAD client-side as belt-and-braces.
	@SIZE=$$(stat -c%s "$(INSTALLER_OUT)"); \
	SHA=$$(sha256sum "$(INSTALLER_OUT)" | cut -d' ' -f1); \
	printf '%s\n' \
	    '{' \
	    '  "version": "$(APPVERSION)",' \
	    '  "url": "$(PUBLIC_URL)",' \
	    "  \"size\": $$SIZE," \
	    "  \"sha256\": \"$$SHA\"" \
	    '}' > "$(LATEST_JSON)"
	$(GCLOUD) storage cp "$(LATEST_JSON)" "$(LATEST_GCS_URI)"
	@$(GCLOUD) storage objects update "$(LATEST_GCS_URI)" \
		--content-type=application/json \
		--cache-control="no-cache, max-age=0" \
		|| echo "[note] could not set metadata on latest.json (uniform bucket-level access?)."
	@$(GCLOUD) storage objects update "$(LATEST_GCS_URI)" \
		--add-acl-grant=entity=AllUsers,role=READER \
		|| echo "[note] per-object ACL grant failed for latest.json; rely on bucket-level allUsers grant."
	@rm -f "$(LATEST_JSON)"
	@echo
	@echo "Uploaded installer:  $(GCS_URI)"
	@echo "Public installer URL: $(PUBLIC_URL)"
	@echo "Update manifest:     $(LATEST_GCS_URI)"
	@echo "Manifest URL:        $(LATEST_PUBLIC_URL)"

# ===========================================================================
# Zulu GO
# ===========================================================================
#
# Second product, shipped from this same tree: the Zulu feature set built on
# the GeneralsOnline client instead of on retail Zero Hour. It shares nothing
# with the Zulu targets above except the signing script and the GCS bucket:
#
#   different compiler    MSVC 2022 under Wine (the `msvc-wine` preset with
#                         the vcpkg toolchain), not VC6
#   different installer   installer/ZuluGO.nsi, which drops one self-contained
#                         folder inside an existing GeneralsOnline install
#   different payload     ZuluGO.exe + ZuluGOLauncher.exe + Zulu.modbig +
#                         cacert.pem + README.txt + the support DLLs + the
#                         MSVC runtime (VC6 needed no runtime; MSVC 2022 does)
#   different version     APPVERSION out of installer/ZuluGO.nsi
#   different channels    latest-go.json / latest-go-dev.json
#
#   make zulugo-installer          build the exe, the launcher and
#                                  ZuluGO_Setup.exe
#   make zulugo-installer-release  build it and publish the release channel
#   make zulugo-installer-dev      build the logging variant and publish the
#                                  dev channel
#   make zulugo-exe                build just the shipping game exe
#   make zulugo-exe-log            build it with DEBUG_LOGGING + DEBUG_CRASHING
#   make zulugo-launcher           build just ZuluGOLauncher.exe
#   make zulugo-launcher-dev       ... from the logging build directory
#   make zulugo-modbig             just (re)pack assets/ into Zulu.modbig
#   make zulugo-payload            stage the full installer payload and stop
#   make zulugo-payload-dev        ... the dev channel's payload
#   make clean-zulugo-installer    remove the GO staging dir and its installers
#
# Everything lands under build/installer-go-tmp, NOT the build/installer-tmp
# the Zulu targets use: both pipelines delete their staged files when they are
# done packaging, so a shared directory would have each one pulling the
# other's payload out from under it.

DOCKER_BUILD_MSVC ?= ./scripts/docker-build-msvc.sh

GO_NSI := installer/ZuluGO.nsi
# Same single-source-of-truth trick as APPVERSION above, and deliberately
# NOT derived from it: the two products version independently. ZuluGO.nsi
# wraps its !define in an !ifndef block, so unlike Zulu.nsi the define is
# indented -- hence the leading-whitespace tolerance and "first match wins".
GO_APPVERSION := $(shell sed -n 's/^[[:space:]]*!define[[:space:]]\+APPVERSION[[:space:]]\+"\([^"]*\)".*/\1/p' $(GO_NSI) 2>/dev/null | head -n1)
GO_VERSION_MAJOR    := $(or $(word 1,$(subst ., ,$(GO_APPVERSION))),0)
GO_VERSION_MINOR    := $(or $(word 2,$(subst ., ,$(GO_APPVERSION))),0)
GO_VERSION_BUILDNUM := $(or $(word 3,$(subst ., ,$(GO_APPVERSION))),0)

# scripts/docker-build-msvc.sh takes the build directory from BUILD_SUBDIR
# (defaulting to build/docker-<preset>). Both Zulu GO configurations use the
# same `msvc-wine` preset -- there is no "releaselog" preset for this
# toolchain, the two debug defines are passed as -D instead -- so unlike the
# VC6 side the preset name cannot key the directory, and the log build gets
# an explicit one. The reason is the same one spelled out for PRESET_LOG
# above: a cmake cache only remembers the variables a preset names, so
# configuring the shipping build in a directory the logging build configured
# would keep RTS_DEBUG_LOGGING/RTS_DEBUG_CRASHING switched on and quietly
# ship a logging-enabled "release" client.
#
# Each build directory also gets its own FetchContent cache: curl, sentry,
# GNS and gamespy are built with the parent build's compile definitions, so
# the two configurations must not share one. The cache survives a --clean,
# which is the point -- those are network clones plus a real compile.
GO_PRESET             := msvc-wine
GO_BUILD_SUBDIR       ?= build/docker-$(GO_PRESET)
GO_BUILD_SUBDIR_LOG   ?= build/docker-$(GO_PRESET)-log
GO_DEPS_CACHE         ?= $(abspath $(GO_BUILD_SUBDIR)-deps)
GO_DEPS_CACHE_LOG     ?= $(abspath $(GO_BUILD_SUBDIR_LOG)-deps)

# The GeneralsOnline client target is still called z_generals; its output is
# named GeneralsOnlineZH.exe and ships as ZuluGO.exe. z_go_launcher is the one
# launcher source built a second time for this product, with the Zulu GO
# product strings (see launcher/LauncherProduct.h.in); override if that
# target is ever renamed.
GO_GAME_TARGET        := z_generals
GO_LAUNCHER_TARGET    ?= z_go_launcher
GO_EXE_NAME           := ZuluGO.exe
GO_LAUNCHER_NAME      := ZuluGOLauncher.exe
GO_BIG_NAME           := Zulu.modbig
GO_SOURCE_EXE          := $(GO_BUILD_SUBDIR)/GeneralsMD/GeneralsOnlineZH.exe
GO_SOURCE_EXE_LOG      := $(GO_BUILD_SUBDIR_LOG)/GeneralsMD/GeneralsOnlineZH.exe
GO_SOURCE_LAUNCHER     := $(GO_BUILD_SUBDIR)/launcher/$(GO_LAUNCHER_NAME)
GO_SOURCE_LAUNCHER_LOG := $(GO_BUILD_SUBDIR_LOG)/launcher/$(GO_LAUNCHER_NAME)

# Build-time configuration baked into the client. The base URL is the one
# knob a tester is likely to want to move (a laptop, a staging host), so it
# is a plain variable: `make zulugo-installer GO_SELFHOST_BASE=http://...`.
# The two keys are secrets and are fetched from GCP Secret Manager at build
# time, exactly like the Discord webhook the Zulu targets bake in.
GO_SELFHOST_BASE       ?= https://cncstats.computersrfun.org/go
GO_CLIENT_KEY_SECRET   := zuluclientkey
GO_CNCSTATS_KEY_SECRET := cncstats_zuluclientkey
# Which Discord channel a Randomize roll posts the lobby map into. Same two
# secrets the Zulu targets use: the live channel for a release, the debug one
# for the dev channel. A build with neither simply does not post.
GO_DISCORD_WEBHOOK_SECRET =

# Its own staging tree, with a payload directory per channel: the installer
# demands the game exe be named ZuluGO.exe, so the shipping build and the
# logging build cannot be told apart by file name the way the VC6 side's
# generalszh_zulu.exe / generalszh_zulu_log.exe are.
GO_TMP_DIR           := build/installer-go-tmp
GO_PAYLOAD           := $(GO_TMP_DIR)/payload
GO_PAYLOAD_DEV       := $(GO_TMP_DIR)/payload-dev
GO_PAYLOAD_STAMP     := $(GO_TMP_DIR)/.payload-staged
GO_PAYLOAD_STAMP_DEV := $(GO_TMP_DIR)/.payload-dev-staged
GO_TMP_BIG           := $(GO_TMP_DIR)/$(GO_BIG_NAME)

# Payload files that are not build output. Vendored under scripts/zulugo/ so
# a release does not depend on a hand-assembled directory; point them
# elsewhere if they ever move.
#   cacert.pem  CA bundle libcurl is handed explicitly (HTTPRequest.cpp opens
#               "cacert.pem" next to the exe); without it every HTTPS call in
#               the client fails on a machine with no usable system store.
#   README.txt  installed next to the exe, shown by the finish page.
GO_CACERT   ?= scripts/zulugo/cacert.pem
GO_README   ?= scripts/zulugo/README.txt
GO_VCREDIST := scripts/stage-vcredist.sh

GO_INSTALLER_OUT     := $(GO_TMP_DIR)/ZuluGO_Setup.exe
GO_INSTALLER_OUT_DEV := $(GO_TMP_DIR)/ZuluGO_Setup_Dev.exe
GO_LATEST_JSON       := $(GO_TMP_DIR)/latest-go.json
GO_LATEST_DEV_JSON   := $(GO_TMP_DIR)/latest-go-dev.json
GO_DEV_EXE_SHA_FILE  := $(GO_TMP_DIR)/dev-exe-sha256.txt

# Same bucket as Zulu, distinct object names. The GO launcher reads
# latest-go.json / latest-go-dev.json (launcher/LauncherProduct.h.in), so
# these names are part of the product's contract and must not drift.
GO_DEV_OBJECT_NAME       := ZuluGO-Installer-Dev.exe
GO_DEV_GCS_URI            = gs://$(GCS_BUCKET)/$(GO_DEV_OBJECT_NAME)
GO_DEV_PUBLIC_URL         = https://storage.googleapis.com/$(GCS_BUCKET)/$(GO_DEV_OBJECT_NAME)
GO_LATEST_DEV_GCS_URI     = gs://$(GCS_BUCKET)/latest-go-dev.json
GO_LATEST_DEV_PUBLIC_URL  = https://storage.googleapis.com/$(GCS_BUCKET)/latest-go-dev.json
GO_RELEASE_NAME           = ZuluGO_Setup_v$(GO_APPVERSION).exe
GO_GCS_URI                = gs://$(GCS_BUCKET)/$(GO_RELEASE_NAME)
GO_PUBLIC_URL             = https://storage.googleapis.com/$(GCS_BUCKET)/$(GO_RELEASE_NAME)
GO_LATEST_GCS_URI         = gs://$(GCS_BUCKET)/latest-go.json
GO_LATEST_PUBLIC_URL      = https://storage.googleapis.com/$(GCS_BUCKET)/latest-go.json

.PHONY: zulugo-installer zulugo-installer-release zulugo-installer-dev \
        zulugo-exe zulugo-exe-log zulugo-launcher zulugo-launcher-dev \
        zulugo-modbig zulugo-payload zulugo-payload-dev clean-zulugo-installer

# Target-specific variables, propagated down the prereq chain the same way the
# Zulu ones are: the build variant decides which manifest URL the launcher
# compiles in and which tag the client reports, and GO_SECRETS_REQUIRED turns
# a missing client key from a warning into a hard failure. A bare
# `make zulugo-exe` sets neither, so it builds as "dev" and keeps working on a
# machine with no gcloud credentials.
zulugo-installer:         ZULU_BUILD_VARIANT  := release
zulugo-installer:         GO_DISCORD_WEBHOOK_SECRET := discord_webhook
zulugo-installer:         GO_SECRETS_REQUIRED := 1
zulugo-installer-release: ZULU_BUILD_VARIANT  := release
zulugo-installer-release: GO_DISCORD_WEBHOOK_SECRET := discord_webhook
zulugo-installer-release: GO_SECRETS_REQUIRED := 1
# A release must never ship unsigned by accident: with this set, sign.sh
# fails the build instead of skipping when signing is not configured.
zulugo-installer-release: export ZULU_SIGN_REQUIRED := 1
zulugo-installer-dev:     ZULU_BUILD_VARIANT  := dev
zulugo-installer-dev:     GO_DISCORD_WEBHOOK_SECRET := debug_discord_webhook
zulugo-installer-dev:     GO_SECRETS_REQUIRED := 1

# One docker build of the msvc-wine tree.
#   $(1) build subdir   $(2) FetchContent cache   $(3) extra -D args
#   $(4) ninja target
#
# The two client keys are fetched here rather than inside the build script so
# the container never needs gcloud or GCP credentials -- the same split the
# Discord webhook uses. They are handed to cmake as -D defines together with
# the service base URL, because ZULU_GO_SELFHOST_BASE is only read as a cmake
# variable (GeneralsMD/Code/GameEngine/CMakeLists.txt) and mixing the two
# mechanisms would be one more thing to remember.
#
# The whole recipe is one @-prefixed shell command so the secrets never reach
# the terminal or a build log.
define go_build
	@set -e; \
	if [ -z "$$ZULU_CLIENT_KEY" ]; then \
		echo "[keys] fetching secret '$(GO_CLIENT_KEY_SECRET)' from GCP Secret Manager..."; \
		ZULU_CLIENT_KEY=$$($(GCLOUD) secrets versions access latest --secret=$(GO_CLIENT_KEY_SECRET) 2>/dev/null || true); \
	fi; \
	if [ -z "$$CNCSTATS_ZULU_CLIENT_KEY" ]; then \
		echo "[keys] fetching secret '$(GO_CNCSTATS_KEY_SECRET)' from GCP Secret Manager..."; \
		CNCSTATS_ZULU_CLIENT_KEY=$$($(GCLOUD) secrets versions access latest --secret=$(GO_CNCSTATS_KEY_SECRET) 2>/dev/null || true); \
	fi; \
	if [ -n "$(GO_DISCORD_WEBHOOK_SECRET)" ] && [ -z "$$ZULU_DISCORD_WEBHOOK_URL" ]; then \
		echo "[discord] fetching secret '$(GO_DISCORD_WEBHOOK_SECRET)' from GCP Secret Manager..."; \
		ZULU_DISCORD_WEBHOOK_URL=$$($(GCLOUD) secrets versions access latest --secret=$(GO_DISCORD_WEBHOOK_SECRET) 2>/dev/null || true); \
	fi; \
	if [ -n "$(GO_SECRETS_REQUIRED)" ]; then \
		[ -n "$(GO_SELFHOST_BASE)" ] || { \
			echo "ERROR: GO_SELFHOST_BASE is empty; the client would fall back to the public service."; exit 1; }; \
		[ -n "$$ZULU_CLIENT_KEY" ] || { \
			echo "ERROR: could not read secret '$(GO_CLIENT_KEY_SECRET)' (gcloud auth? empty payload?)."; exit 1; }; \
		[ -n "$$CNCSTATS_ZULU_CLIENT_KEY" ] || { \
			echo "ERROR: could not read secret '$(GO_CNCSTATS_KEY_SECRET)' (gcloud auth? empty payload?)."; exit 1; }; \
		echo "[keys] both client keys loaded"; \
	fi; \
	DISCORD_DEF=; \
	if [ -n "$$ZULU_DISCORD_WEBHOOK_URL" ]; then \
		DISCORD_DEF="-DZULU_DISCORD_WEBHOOK_URL=$$ZULU_DISCORD_WEBHOOK_URL"; \
		echo "[discord] webhook url loaded ($${#ZULU_DISCORD_WEBHOOK_URL} bytes)"; \
	fi; \
	EXTRA_CMAKE_DEFS=$$(printf '%s\n' \
		"-DZULU_GO_SELFHOST_BASE=$(GO_SELFHOST_BASE)" \
		"-DZULU_CLIENT_KEY=$$ZULU_CLIENT_KEY" \
		"-DCNCSTATS_ZULU_CLIENT_KEY=$$CNCSTATS_ZULU_CLIENT_KEY" \
		$${DISCORD_DEF:+"$$DISCORD_DEF"} \
		$(3)); \
	export EXTRA_CMAKE_DEFS; \
	PRESET=$(GO_PRESET) \
	BUILD_SUBDIR=$(1) \
	DEPS_CACHE_DIR=$(2) \
	ZULU_VERSION_MAJOR=$(GO_VERSION_MAJOR) \
	ZULU_VERSION_MINOR=$(GO_VERSION_MINOR) \
	ZULU_VERSION_BUILDNUM=$(GO_VERSION_BUILDNUM) \
	ZULU_BUILD_VARIANT=$(ZULU_BUILD_VARIANT) \
	$(DOCKER_BUILD_MSVC) --cmake --target $(4)
endef

# --cmake on every invocation, for the reason given on docker-build-z_generals:
# without a configure pass the ZULU_VERSION_* values, the build variant and the
# baked-in keys keep whatever the previous configure left behind.
zulugo-exe:
	$(call go_build,$(GO_BUILD_SUBDIR),$(GO_DEPS_CACHE),,$(GO_GAME_TARGET))
	@echo "Zulu GO client: $(GO_SOURCE_EXE)"

# The logging variant, and the only difference between the two channels'
# clients: a Release build (no debug CRT, same DLL dependencies) with
# DEBUG_LOGGING and DEBUG_CRASHING compiled in, so a dev tester's crash
# leaves a log behind. On the VC6 side this is the vc6-releaselog preset;
# here it is two -D defines into a build directory of its own.
zulugo-exe-log:
	$(call go_build,$(GO_BUILD_SUBDIR_LOG),$(GO_DEPS_CACHE_LOG),"-DRTS_DEBUG_LOGGING=ON" "-DRTS_DEBUG_CRASHING=ON",$(GO_GAME_TARGET))
	@echo "Zulu GO client (logging): $(GO_SOURCE_EXE_LOG)"

# Built in the same directory as the client it ships with, so the build
# variant baked into the launcher (which picks latest-go.json vs
# latest-go-dev.json) always matches the one baked into the client.
zulugo-launcher:
	$(call go_build,$(GO_BUILD_SUBDIR),$(GO_DEPS_CACHE),,$(GO_LAUNCHER_TARGET))
	@echo "Zulu GO launcher: $(GO_SOURCE_LAUNCHER)"

zulugo-launcher-dev:
	$(call go_build,$(GO_BUILD_SUBDIR_LOG),$(GO_DEPS_CACHE_LOG),"-DRTS_DEBUG_LOGGING=ON" "-DRTS_DEBUG_CRASHING=ON",$(GO_LAUNCHER_TARGET))
	@echo "Zulu GO launcher (dev): $(GO_SOURCE_LAUNCHER_LOG)"

zulugo-modbig: $(GO_TMP_BIG)

zulugo-payload: $(GO_PAYLOAD_STAMP)

zulugo-payload-dev: $(GO_PAYLOAD_STAMP_DEV)

zulugo-installer: $(GO_INSTALLER_OUT)

# Same packer and the same assets/ tree as Zulu.big; only the name differs.
# ZuluGO installs into a subfolder of someone else's game directory and is
# loaded with -mod, and the engine will not accept a mod archive whose
# extension is .big while a .big of the same name may already be mounted --
# hence .modbig.
$(GO_TMP_BIG): $(ASSET_FILES) | $(GO_TMP_DIR)
	@rm -f $@
	@python3 scripts/pack_big.py $(ASSETS_DIR) $@

# Stage one complete payload directory for makensis.
#   $(1) payload dir   $(2) client exe   $(3) launcher exe   $(4) build subdir
#
# Wiped and rebuilt every time rather than copied over, so a file dropped
# from the client build cannot survive into the installer as a stale copy.
# The staged copies are what gets signed -- never the build output, so the
# build directory stays pristine and a re-run never double-signs.
define go_stage_payload
	@rm -rf "$(1)"
	@mkdir -p "$(1)"
	cp "$(2)" "$(1)/$(GO_EXE_NAME)"
	cp "$(3)" "$(1)/$(GO_LAUNCHER_NAME)"
	@set -e; n=0; \
	for dll in "$(4)"/GeneralsMD/*.dll; do \
		[ -e "$$dll" ] || continue; \
		case "$$(basename "$$dll")" in \
			binkw32.dll|mss32.dll) continue ;; \
		esac; \
		cp "$$dll" "$(1)/"; \
		n=$$((n + 1)); \
	done; \
	[ "$$n" -gt 0 ] || { echo "ERROR: no support DLLs in $(4)/GeneralsMD -- did the build run?"; exit 1; }; \
	echo "[payload] staged $$n support DLL(s) from $(4)/GeneralsMD"
	$(GO_VCREDIST) "$(1)"
	cp "$(GO_TMP_BIG)" "$(1)/$(GO_BIG_NAME)"
	cp "$(GO_CACERT)" "$(1)/cacert.pem"
	cp "$(GO_README)" "$(1)/README.txt"
	$(SIGN) "$(1)/$(GO_EXE_NAME)" "$(1)/$(GO_LAUNCHER_NAME)"
endef

# binkw32.dll and mss32.dll are excluded above on purpose: cmake copies those
# two next to the exe so the build can be run in place, but they are the
# retail Bink and Miles redistributables that every Zero Hour install already
# has. Shipping them would both bloat the installer and put someone else's
# binaries in our package.

$(GO_PAYLOAD_STAMP): $(GO_TMP_BIG) $(GO_CACERT) $(GO_README) $(GO_VCREDIST) zulugo-exe zulugo-launcher | $(GO_TMP_DIR)
	$(call go_stage_payload,$(GO_PAYLOAD),$(GO_SOURCE_EXE),$(GO_SOURCE_LAUNCHER),$(GO_BUILD_SUBDIR))
	@touch $@

$(GO_PAYLOAD_STAMP_DEV): $(GO_TMP_BIG) $(GO_CACERT) $(GO_README) $(GO_VCREDIST) zulugo-exe-log zulugo-launcher-dev | $(GO_TMP_DIR)
	$(call go_stage_payload,$(GO_PAYLOAD_DEV),$(GO_SOURCE_EXE_LOG),$(GO_SOURCE_LAUNCHER_LOG),$(GO_BUILD_SUBDIR_LOG))
	@touch $@

# ZuluGO.nsi takes a staged payload directory (and the launcher's path
# separately, since it need not come from the same toolchain as the client)
# rather than a file at a time. Paths are absolute: makensis resolves
# relative ones against the .nsi's own directory. -WX because the script is
# written to compile warning-free and a warning there means a missing file.
#
# The payload is dropped after packaging so ~90 MB of staged binaries do not
# sit around; $(GO_TMP_DIR) itself stays.
$(GO_INSTALLER_OUT): $(GO_PAYLOAD_STAMP) $(GO_NSI)
	@test -n "$(GO_APPVERSION)" || { \
		echo "ERROR: could not parse APPVERSION from $(GO_NSI)"; exit 1; }
	$(NSIS) -V3 -WX \
		-DPAYLOAD_DIR="$(abspath $(GO_PAYLOAD))" \
		-DLAUNCHER_SOURCE="$(abspath $(GO_PAYLOAD))/$(GO_LAUNCHER_NAME)" \
		-DOUTFILE="$(abspath $(GO_INSTALLER_OUT))" \
		-DAPPVERSION="$(GO_APPVERSION)" \
		-DBIGNAME="$(GO_BIG_NAME)" \
		-DSIGN_CMD="$(SIGN)" \
		$(GO_NSI)
	@rm -rf "$(GO_PAYLOAD)" "$(GO_PAYLOAD_STAMP)"

$(GO_TMP_DIR):
	@mkdir -p $@

clean-zulugo-installer:
	rm -rf "$(GO_TMP_DIR)"

# Publish the release channel: a version-stamped, immutable object plus the
# manifest the launcher polls. Mirrors installer-release, including the ACL
# handling -- the per-object grant marks the upload world-readable and fails
# harmlessly if the bucket uses uniform bucket-level access, in which case
# allUsers needs Storage Object Viewer at the bucket level.
zulugo-installer-release: $(GO_INSTALLER_OUT) | $(GO_TMP_DIR)
	@test -n "$(GO_APPVERSION)" || { \
		echo "ERROR: could not parse APPVERSION from $(GO_NSI)"; exit 1; }
	$(GCLOUD) storage cp "$(GO_INSTALLER_OUT)" "$(GO_GCS_URI)"
	@$(GCLOUD) storage objects update "$(GO_GCS_URI)" \
		--add-acl-grant=entity=AllUsers,role=READER \
		|| echo "[note] per-object ACL grant failed; if the bucket uses uniform bucket-level access, ensure allUsers is granted Storage Object Viewer at the bucket level."
	@# Cache-Control is overridden on the manifest only: the installer object
	@# is immutable (its name carries the version), while a cached manifest
	@# would hide a fresh release for up to GCS's default hour.
	@SIZE=$$(stat -c%s "$(GO_INSTALLER_OUT)"); \
	SHA=$$(sha256sum "$(GO_INSTALLER_OUT)" | cut -d' ' -f1); \
	printf '%s\n' \
	    '{' \
	    '  "version": "$(GO_APPVERSION)",' \
	    '  "url": "$(GO_PUBLIC_URL)",' \
	    "  \"size\": $$SIZE," \
	    "  \"sha256\": \"$$SHA\"" \
	    '}' > "$(GO_LATEST_JSON)"
	$(GCLOUD) storage cp "$(GO_LATEST_JSON)" "$(GO_LATEST_GCS_URI)"
	@$(GCLOUD) storage objects update "$(GO_LATEST_GCS_URI)" \
		--content-type=application/json \
		--cache-control="no-cache, max-age=0" \
		|| echo "[note] could not set metadata on latest-go.json (uniform bucket-level access?)."
	@$(GCLOUD) storage objects update "$(GO_LATEST_GCS_URI)" \
		--add-acl-grant=entity=AllUsers,role=READER \
		|| echo "[note] per-object ACL grant failed for latest-go.json; rely on bucket-level allUsers grant."
	@rm -f "$(GO_LATEST_JSON)"
	@echo
	@echo "Uploaded installer:   $(GO_GCS_URI)"
	@echo "Public installer URL: $(GO_PUBLIC_URL)"
	@echo "Update manifest:      $(GO_LATEST_GCS_URI)"
	@echo "Manifest URL:         $(GO_LATEST_PUBLIC_URL)"

# Publish the dev channel: a fixed object name so a dev launcher always has a
# stable URL, no-cache on both objects because both are overwritten in place.
#
# The NSIS output is renamed to $(GO_INSTALLER_OUT_DEV) so a later
# `make zulugo-installer` does not see a fresh ZuluGO_Setup.exe on disk and
# skip the build.
zulugo-installer-dev: $(GO_PAYLOAD_STAMP_DEV) $(GO_NSI) | $(GO_TMP_DIR)
	@test -n "$(GO_APPVERSION)" || { \
		echo "ERROR: could not parse APPVERSION from $(GO_NSI)"; exit 1; }
	$(NSIS) -V3 -WX \
		-DPAYLOAD_DIR="$(abspath $(GO_PAYLOAD_DEV))" \
		-DLAUNCHER_SOURCE="$(abspath $(GO_PAYLOAD_DEV))/$(GO_LAUNCHER_NAME)" \
		-DOUTFILE="$(abspath $(GO_INSTALLER_OUT_DEV))" \
		-DAPPVERSION="$(GO_APPVERSION)" \
		-DBIGNAME="$(GO_BIG_NAME)" \
		-DSIGN_CMD="$(SIGN)" \
		$(GO_NSI)
	@# The dev update gate compares the game exe on disk against the game exe
	@# this installer carries, so the manifest needs that hash -- take it while
	@# the staged exe still exists (the payload goes away on the next line).
	@sha256sum "$(GO_PAYLOAD_DEV)/$(GO_EXE_NAME)" | cut -d' ' -f1 > "$(GO_DEV_EXE_SHA_FILE)"
	@rm -rf "$(GO_PAYLOAD_DEV)" "$(GO_PAYLOAD_STAMP_DEV)"
	$(GCLOUD) storage cp "$(GO_INSTALLER_OUT_DEV)" "$(GO_DEV_GCS_URI)"
	@$(GCLOUD) storage objects update "$(GO_DEV_GCS_URI)" \
		--cache-control="no-cache, max-age=0" \
		|| echo "[note] could not set cache-control on $(GO_DEV_OBJECT_NAME)."
	@$(GCLOUD) storage objects update "$(GO_DEV_GCS_URI)" \
		--add-acl-grant=entity=AllUsers,role=READER \
		|| echo "[note] per-object ACL grant failed for $(GO_DEV_OBJECT_NAME); rely on bucket-level allUsers grant."
	@# "sha256" is the installer's own hash; "exe_sha256" is the hash of the
	@# client inside it, which is what the launcher's dev gate compares against
	@# the installed ZuluGO.exe. Comparing against "sha256" instead is what made
	@# every dev launch on the Zulu side think an update was waiting.
	@SIZE=$$(stat -c%s "$(GO_INSTALLER_OUT_DEV)"); \
	SHA=$$(sha256sum "$(GO_INSTALLER_OUT_DEV)" | cut -d' ' -f1); \
	EXESHA=$$(cat "$(GO_DEV_EXE_SHA_FILE)"); \
	printf '%s\n' \
	    '{' \
	    '  "version": "$(GO_APPVERSION)",' \
	    '  "url": "$(GO_DEV_PUBLIC_URL)",' \
	    "  \"size\": $$SIZE," \
	    "  \"sha256\": \"$$SHA\"," \
	    "  \"exe_sha256\": \"$$EXESHA\"" \
	    '}' > "$(GO_LATEST_DEV_JSON)"
	$(GCLOUD) storage cp "$(GO_LATEST_DEV_JSON)" "$(GO_LATEST_DEV_GCS_URI)"
	@$(GCLOUD) storage objects update "$(GO_LATEST_DEV_GCS_URI)" \
		--content-type=application/json \
		--cache-control="no-cache, max-age=0" \
		|| echo "[note] could not set metadata on latest-go-dev.json."
	@$(GCLOUD) storage objects update "$(GO_LATEST_DEV_GCS_URI)" \
		--add-acl-grant=entity=AllUsers,role=READER \
		|| echo "[note] per-object ACL grant failed for latest-go-dev.json; rely on bucket-level allUsers grant."
	@rm -f "$(GO_LATEST_DEV_JSON)" "$(GO_DEV_EXE_SHA_FILE)"
	@echo
	@echo "Uploaded dev installer: $(GO_DEV_GCS_URI)"
	@echo "Public URL:             $(GO_DEV_PUBLIC_URL)"
	@echo "Update manifest:        $(GO_LATEST_DEV_GCS_URI)"
	@echo "Manifest URL:           $(GO_LATEST_DEV_PUBLIC_URL)"
