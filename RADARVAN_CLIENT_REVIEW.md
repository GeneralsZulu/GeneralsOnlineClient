# Radarvan client review

Review of how this client talks to radarvan (and the cncstats map store that
shares the same HTTP code), looking for crashes, network problems and
improvements.

**Method.** Static read of `GeneralsMD/Code/GameEngine/Source/Common/StatsUploader.cpp`
(all of it), its call sites, and the matching radarvan routes in `../radarvan`
(read only, nothing changed there). Nothing was built or run, so every item is
from reading code. Items marked *plausible* depend on runtime behaviour I did
not reproduce.

Paths below are relative to the repo root. `SU` means
`GeneralsMD/Code/GameEngine/Source/Common/StatsUploader.cpp`.

## Status

Changed since this review was written (not yet compiled or run):

- **Item 1, timeouts:** `openHttpRequest` now bounds every request (connect
  5 s, send 10 s, receive 15 s, one connect attempt). Uploads and map
  downloads widen send/receive to 30 s; the load-screen intel keeps 4/4/6 s.
  The calls themselves are **still on the main thread**: a dead server now
  costs a bounded stall instead of an open-ended one. Moving map vote, team
  balance and the lobby map upload onto workers is still open.
- **Item 2, team numbering:** fixed. The roster builder moved to
  `RadarvanIntelStartForGame` in `StatsUploader.cpp` and sends the two sides as
  teams 1 and 2 in ascending lobby-team order.
- **Load-screen intel online:** `NGMPGame::launchGame` now starts the worker
  and `GameSpyLoadScreen` shows the result, sharing the panel code with the
  LAN screen. Online, the general's portrait is only replaced when a request
  is actually in flight. (The lobby `/intel` chat command already worked
  online.)

Everything else below is unchanged.

## Summary

| # | Finding | Kind | Severity |
|---|---------|------|----------|
| 1 | Lobby HTTP calls block the main thread with no timeouts | Network / freeze | High |
| 2 | Load-screen intel mislabels or drops teams unless they are exactly Team 1 vs Team 2 | Wrong result | High |
| 3 | Stack buffer overflow from server-supplied player names | Crash | Medium |
| 4 | Detached workers are not waited for at engine shutdown | Crash at exit (plausible) | Medium |
| 5 | Uploads are never retried and die with the process | Data loss | Medium |
| 6 | Map-vote install downloads no sidecars | Wrong result | Medium |
| 7 | `map_filename` from the server is used as a path unsanitised | Latent file-write hole | Medium (dormant) |
| 8 | Intel timeouts are looser than they look | Network | Low |
| 9 | Match-report polling is fixed-rate and retries final errors | Network / load | Low |
| 10 | A new connection and TLS handshake per request | Efficiency | Low |
| 11 | Replays over 5 MB are uploaded in full, then rejected | Network | Low |
| 12 | `balance_teams` is called for rosters the server always answers empty | UX | Low |
| 13 | Name encoding is inconsistent between endpoints | Wrong result | Low |
| 14 | API key goes to any non-cncstats host, including plain HTTP | Hardening | Low |
| 15 | Hand-rolled JSON scanner fragility; silent body truncation | Robustness | Low |
| 16 | `docs/telemetry-uploads.md` has drifted | Docs | Low |

## High

### 1. Lobby HTTP calls block the main thread with no timeouts

Only `intelHttpJsonStatus` (`SU:2770`) sets WinINet timeouts. Every other
request goes through `openHttpRequest` with WinINet defaults, which are tens of
seconds per phase with connect retries. These run on the main thread:

- **Map vote** — `handleMapVoteClick` in
  `GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/LanGameOptionsMenu.cpp:1554`
  and its twin in `WOLGameSetupMenu.cpp:1833`. One click chains
  `ChooseMapFromServer` (radarvan) → `InstallMapByCRC` → blocking
  `DownloadAndInstallMap` (up to 7 GETs, 16 MiB cap each) →
  `UploadAllMapAssetsIfMissing` (a list GET plus up to 7 POSTs).
- **Map select confirm** — `WOLMapSelectMenu.cpp:631` runs
  `UploadAllMapAssetsIfMissing` inline.
- **Team balance** — `RandomAssign.cpp:642` calls `BalanceTeamsFromServer`.
- **Map list** — `Core/GameEngine/Source/GameClient/MapUtil.cpp:1660` and
  `:1793` call `FetchMapMatchCountsIfStale` when the map dialog opens.

The code already knows the cost: the comment at `SU:4275` says the lobby thread
also services the LAN heartbeat, "so a slow download freezes the UI and can get
the peer dropped from the game". That was fixed for the joiner's download
(`MapDownloadStart`/`MapDownloadPoll`) but the host paths above still block.
If radarvan or cncstats is slow or unreachable, the host freezes and may be
dropped.

**Suggested fix.**
- Set connect/send/receive timeouts inside `openHttpRequest` so every caller is
  bounded (short for small JSON calls, longer for file transfers).
- Move map vote and team balance onto a worker with a poll, the same shape as
  the `/intel` worker. Use `MapDownloadStart` for the vote install, and hand
  the lobby map upload to the existing telemetry worker.
- Fetch match counts in the background on lobby entry and let rows fill in when
  the data arrives.

### 2. Load-screen intel is wrong unless the teams are exactly 1 and 2

`startLanBattlefieldIntel`
(`Core/GameEngine/Source/GameNetwork/LANAPICallbacks.cpp:208`) sends each
player's team as `getTeamNumber() + 1`, so a lobby that picked Team 2 vs Team 4
sends teams 2 and 4, and `localTeam` is 2 or 4.

Radarvan labels teams A/B by ascending team id and returns `favored_team` as
that id (`radarvan/api_types/predictions.py`). `composeIntel` (`SU:3025`,
`SU:3049`) assumes A is team 1 and B is team 2:

- Teams 2 vs 4, local player on team 2: team A is the local team, but
  `localTeam == 2` selects `labA = "ENEMY"`, `labB = "YOUR SQUAD"`. The records
  and duo lines are shown under the wrong side.
- Local player on team 3 or 4: falls into the observer branch and shows
  "Odds favor Team N" instead of the player's own odds.

Only the 1v1 path is safe, because it synthesises teams 1 and 2.

**Suggested fix.** Renumber the two teams to 1 and 2 (in ascending order of the
real team id) before building the roster, or read `team_a`/`team_b` from the
reply and compare against those.

## Medium

### 3. Stack buffer overflow from server-supplied names

`appendTeamBlock` (`SU:2951`):

```cpp
char l2[192];
sprintf(l2, "  %s: %s & %s %+d%%", tag, pair.a.str(), pair.b.str(), pctSigned(pair.delta));
```

`pair.a` and `pair.b` are `player_a`/`player_b` strings from the synergy reply,
with no length limit. Two names totalling about 170 bytes overflow the stack on
the worker thread. Real names are short, so this needs a hostile or corrupted
reply, but it is remote data into an unbounded `sprintf`.

**Suggested fix.** `_snprintf` with explicit termination, here and for the
other `sprintf` calls that format remote or path data.

### 4. Detached workers are not waited for at shutdown (plausible)

`GameEngine::~GameEngine` waits only for the `/intel` worker
(`GeneralsMD/Code/GameEngine/Source/Common/GameEngine.cpp:343`), with the
comment that it "uses the game allocator". Four other workers use the same
allocator and are detached with no handle kept:

- load-screen intel (`SU:3266`)
- match report (`SU:3866`), which can keep polling for 150 s or more
- match telemetry (`SU:4198`, `SU:4257`), which also touches `TheFileSystem`,
  `OptionPreferences`, `TheVersion` and `ReleaseLog`
- map download (`SU:4407`)

A player who quits from the score screen leaves the report worker, and usually
the telemetry worker, running while the engine tears down. That can crash on
exit, which would also produce a false crash report on the next launch.

**Suggested fix.** Keep the handles, bump the generation counters at shutdown
so workers stop early, and wait briefly, the same way
`RadarvanMapSummaryShutdown` does.

### 5. Uploads are never retried and die with the process

`telemetryThreadProc` (`SU:4017`) makes one attempt per channel. Any timeout or
5xx loses that match's replay. If the player exits before the worker finishes,
the upload is simply cut off (the comment at `SU:3943` accepts this). The replay
file is then overwritten by the next game.

**Suggested fix.** Spool the tagged replay to a pending folder before
uploading, delete it on 2xx/409/413/422, and retry leftovers on the next launch
(the crash-upload launch check is a ready-made hook). Add one or two retries
with backoff for timeouts and 5xx.

### 6. Map-vote install downloads no sidecars

`ChooseMapFromServer` leaves `contentsMask` at 0 because radarvan does not send
`map_contents_mask` (`ChooseMapResult` has no such field). The comment at
`SU:1758` says that in this case the caller "asks the CDN for every sidecar
kind". It does not: both vote handlers pass `res.contentsMask` straight to
`InstallMapByCRC`, and `DownloadAndInstallMap` (`SU:2093`) skips every sidecar
whose bit is 0. The joiner path passes `MAP_SIDECARS_ALL` (0x7E) instead.

A host who gets a map through the vote therefore has no `map.ini`, `.str` or
preview for it, while other players may have them.

**Suggested fix.** Treat a zero mask as `MAP_SIDECARS_ALL` in `InstallMapByCRC`.

### 7. `map_filename` from the server becomes a path unsanitised (dormant)

`InstallMapByCRC`
(`GeneralsMD/Code/GameEngine/Source/GameNetwork/GUIUtil.cpp:186`) uses
`serverMapFileName` verbatim as the install path, while the fallback name goes
through `sanitizeMapNameForPath`. Radarvan does not send `map_filename` today,
so this is dead code. If it ever did, or a reply were tampered with, a value
such as `..\..\x.map` would write outside the maps directory. The CRC check does
not help, because the CRC comes from the same reply.

Related: sidecars are written without any integrity check (`SU:2104`,
`SU:4484`), and the comments note the cncstats store has no upload auth.

**Suggested fix.** Drop the `map_filename` branch, or force the result under
the user maps directory and sanitise each component.

## Low

### 8. Intel timeouts are looser than they look

- `INTERNET_OPTION_CONNECT_TIMEOUT` does not cover DNS, and applies per attempt
  and per address. Setting `INTERNET_OPTION_CONNECT_RETRIES` to 1 tightens it.
- The load-screen worker makes three sequential calls, each allowed about
  14 s, so the worst case is over 40 s, not 6.
- A 6 s receive timeout on `predict` may be tight for a cold server, which
  shows as the "recon down" fallback. A single overall deadline for the worker
  would be clearer than per-call limits.

### 9. Match-report polling

`reportThreadProc` (`SU:3789`) polls `narrative/<id>` every 3 s for up to 150 s
from every client in the match, about 50 requests each.

- It retries on any failure, including 401/403, which cannot recover.
- `waited` counts only sleep time, so real wall time can be well over
  `maxWaitSec` when requests are slow.
- The client's own `upload_replay` reply already says whether the match was
  accepted. Passing that outcome to the report worker would let it skip the
  wait entirely on 413/422 or a failed upload.

**Suggested fix.** Back off (3, 5, 8, 13 s…), stop on 401/403, and bound by
wall-clock time.

### 10. A new connection per request

`openHttpRequest` calls `InternetOpenA` and `InternetConnectA` for every
request, and `closeHttpRequest` tears both down. The three intel calls, the
report polls and the per-file log uploads each pay a fresh TLS handshake.
Keeping one session and one connect handle per worker would cut latency and
make the intel more likely to land before the load screen ends.

### 11. Oversized replays

Radarvan rejects bodies over 5 MB (`MAX_REPLAY_UPLOAD_BYTES` in
`radarvan/routes/files.py`). The client sends the whole file and then gets 413.
Check the size in `StartMatchTelemetryUpload` and skip with a log line.

### 12. `balance_teams` on small rosters

The server returns `{}` for fewer than 4 players. The client calls it for 2 or
more (`RandomAssign.cpp:639`) and reports "Balance-teams response was empty."
Require 4 names before calling, or show a clearer message.

Also, `splitCommaList` (`SU:1238`) strips spaces inside names and splits on
commas, so a name containing either will not match its slot.

### 13. Name and text encoding

- Map vote sends names via `AsciiString::translate` (`LanGameOptionsMenu.cpp:1541`,
  `WOLGameSetupMenu.cpp:1819`), while every other radarvan call uses
  `RadarvanPlayerName` (UTF-8). Non-ASCII names will not match their votes.
- Replies are UTF-8 but are shown through `translate()` (load-screen blurb,
  `Core/GameEngine/Source/GameClient/GUI/LoadScreen.cpp:1974`) or `%s` chat
  formats (`chosen_map`), so non-ASCII text is garbled. `utf8ToUnicode` already
  exists for this.
- `X-Map-Name` is sent in the system code page.

### 14. API key scope

`openHttpRequest` (`SU:191`) attaches `X-API-Key` to every host whose name does
not contain "cncstats". That includes hosts given by `-replayUrl` style
overrides, plain `http://` URLs, and redirect targets. Suggest an explicit
host-to-key table and no key over plain HTTP except to localhost. The key is
extractable from the binary regardless, so the server should keep treating it
as low trust.

### 15. Parsing robustness

- `jsonValueStart` (`SU:1558`) matches a key at any nesting depth and does not
  understand `\"` inside values, so an escaped quote can desynchronise it.
  `jsonGetString` does not decode `\uXXXX`. This is fine against today's
  replies but brittle.
- `RadarvanIntelStart` (`SU:3240`) truncates the JSON body at 4096 bytes with
  `strncpy` and sends it anyway. `RadarvanMapSummaryStart` checks the length
  first; the intel path should do the same.
- Several readers stop at a fixed cap and parse whatever arrived
  (`BalanceTeamsFromServer` 16 KB, `ChooseMapFromServer` 32 KB, intel 256 KB).
  Only the match-count fetch detects truncation.
- A `chosen_map` of `null` ("no eligible map") surfaces as "Map vote response
  missing chosen_map". A dedicated message would be friendlier.

### 16. Docs drift

`docs/telemetry-uploads.md` says the host uploads the replay, that
`StatsUploader` is about 700 lines, and that uploads run "from the host only".
In the code every client with 2+ humans uploads (`Recorder.cpp:1330`), which
radarvan expects and de-duplicates, and the file is about 4500 lines.

## Worth confirming

- `RadarvanIntelStart` has a single call site, in the LAN path. If online
  (GeneralsOnline) lobbies are meant to show load-screen intel too, they
  currently do not start the worker.

## Suggested order of work

1. Timeouts in `openHttpRequest` (item 1, small change, removes the worst
   freezes).
2. Team renumbering for intel (item 2) and the `_snprintf` fix (item 3).
3. Zero-mask sidecar fix and `map_filename` removal (items 6, 7).
4. Worker shutdown handling (item 4), then upload spool and retry (item 5).
5. Move map vote and balance off the main thread (rest of item 1).
