# Resume From Replay

## What it is

A dead match (a crash, a drop, a desync vote) is continued from the host's
recording instead of being played again. The host arms the resume in the
lobby, every client receives the same replay file, and on start everyone
replays it in lockstep at full speed up to a handoff point ten seconds
before the recording ended. The last ten seconds run at normal speed, a
ten second countdown freezes the game, then live control is handed back
and a fresh recording of the resumed match begins.

Works in the LAN lobby and the online (GeneralsOnline) lobby.

## How to use it

1. Do not start anything else first. The source is the host's last
   recording (`00000000.rep`), and any new start overwrites it, including a
   fifteen second abort.
2. The player who hosted the dead match hosts again. Only that player can
   resume: the recorded host is in slot 1 and the recording is theirs.
3. Everyone who was in the match joins under the same name. Slots, factions,
   colors, start positions and teams do not have to be right: the arm fixes
   them. AI opponents are re-created at their recorded difficulty.
4. The host presses **RESUME** (or types `/resume`).
   - LAN: the lobby is reordered locally and the arm goes out in the game
     options; the replay is pushed to the guests when the game starts.
   - Online: the client walks the service through what differs from the
     recording, in order, each step from the previous one's completion:
     map, starting cash, superweapon limit, then the whole slot table in one
     `HOST_ACTION_ALIGN_SLOTS` request (humans moved, AI and empty slots
     re-created), then the replay upload, then the arm. The arm has to be
     last because the service disarms on any of those changes.
5. Guests see "Resume armed by the host ..." from the lobby state itself,
   the button turns into **RESUME ARMED**, and online each guest downloads
   the replay and reports in chat whether its copy matches.
6. The host presses Start. Online the host cannot start until every human
   holds the replay; if the lobby drifted since arming, Start says so and
   asks for Resume again.

**CANCEL RESUME** (the same button while armed, or `/resume off`) makes the
next start a fresh game. Changing the map, starting cash or superweapon
limit disarms as well.

## What can refuse, and what it tells you

Every refusal names the problem and who fixes it, in the lobby chat:

| Refusal | Fix |
|---|---|
| the last replay is too short / unreadable / from another version | nothing in the lobby helps; the host's last recording is what resumes |
| the host's copy of the map differs from the one the match was played on | host reinstalls that map |
| a recorded player is not in the lobby, or the count differs | get exactly those players in, press Resume again |
| only X, the host of the recorded game, can resume it | X hosts |
| the service refused the map / cash / limit change | host sets it by hand, presses Resume again |
| the lobby could not be arranged like the recording (reason) | everyone takes the slot they had, host presses Resume again |
| the replay upload failed | press Resume again; the service caps the held replay size |
| a guest's download failed or does not match | host presses Cancel Resume, then Resume |
| in game: the replay ends before the handoff | that client leaves; everyone else validated the same file, so this is a torn copy |

All of it is also written to ReleaseLog (`Resume: ...` lines) and uploaded
with the match telemetry.

## Code surface

- `GeneralsMD/Code/GameEngine/Source/GameNetwork/ResumeFromReplay.cpp` ,
  the shared checks: source copy and header, roster, slot-by-slot match,
  handoff computation, the game start validation.
- `GeneralsMD/.../Menus/WOLGameSetupMenu.cpp`: online arming
  (`wolTryArmResumeFromReplay`, `wolResumeArmStep`), the guest download,
  the button (`wolUpdateResumeButton`).
- `GeneralsMD/.../Menus/LanGameOptionsMenu.cpp`: LAN arming
  (`tryArmResumeFromReplay`, `reorderLobbyForResume`), the button.
- `GeneralsMD/.../GeneralsOnline/OnlineServices_LobbyInterface.cpp` ,
  `UpdateCurrentLobby_AlignSlots`, upload and download of the source,
  `UpdateCurrentLobby_ArmResume`.
- Services: `LobbyController.cs` `HOST_ACTION_ALIGN_SLOTS` (23),
  `LobbyManager.cs` `Lobby.AlignSlots` (refuses the whole request unless
  all eight slots are named, every human placed once, the owner in slot 0);
  `UpdateMap` / `UpdateStartingCash` / `UpdateLimitSuperweapons` disarm only
  on a real change.
- `GeneralsMD/Code/GameEngine/Source/Common/Recorder.cpp`: the catch-up
  playback, lead-in, freeze and handoff.
- Layouts: `LanGameOptionsMenu.wnd:ButtonResumeFromReplay`,
  `GameSpyGameOptionsMenu.wnd:ButtonResume` (bottom row, between Randomize
  and Buddies).

## Concerns

- **Version lock.** The replay must come from the same build (version, exe
  CRC, INI CRC); a resume across builds is refused up front.
- **One source file.** `Resume.rep` in the replay directory is the only
  source; the host's `00000000.rep` is copied there on every arm.
- **Catch-up CPU.** The fast-forward runs as fast as the slowest machine
  simulates; lockstep still waits for every peer each frame.
