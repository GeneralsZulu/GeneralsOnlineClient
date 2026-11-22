# Headless Replay Mode

## What it is

Headless replay mode simulates one or more `.rep` files with no GUI
and reports the outcome in machine-parseable form. There are two
outputs:

- **stdout result lines**, printed by `VictoryConditions::update` on
  the frame where only one alliance remains. These say who won.
- **A result log** (`-resultLog <path>`), one JSON line per replay,
  written by the batch replay driver. These say whether the replay
  played through cleanly.

The stdout result lines exist only in the Zero Hour (`GeneralsMD`)
tree. `Generals/` does not print them.

## How to run

```
generalszh.exe -headless -replay <file.rep>
```

- `-replay` can be repeated and accepts wildcards. As the comment in
  `CommandLine.cpp` says, the file must be in the replay folder or a
  subfolder of it.
- `-jobs N` runs the replays in `N` parallel worker processes.
- `-resultLog <path>` appends one JSON line per replay to `<path>`
  (see "Result log" below).
- `-exportStats` additionally exports stats for replays that finish
  cleanly.

## stdout result lines

These lines are printed only when `TheGlobalData->m_headless` is set
and the replay is multiplayer (`VictoryConditions::update` returns
early when `!TheRecorder->isMultiplayer()`). They are printed once, on
the frame where no more than one alliance remains
(`m_endFrame = TheGameLogic->getFrame()`), and stdout is flushed
afterwards.

Victory, one line:

```
[HEADLESS RESULT] outcome=victory end_frame=<frame> winner_index=<player index> winner_side=<side> winner_name=<internal name>
```

Then one line for every other player slot that is not null, in slot
order (index 0 up to `MAX_PLAYER_COUNT`):

```
[HEADLESS RESULT] <role> index=<player index> side=<side> name=<internal name>
```

No undefeated player (everyone eliminated):

```
[HEADLESS RESULT] outcome=draw end_frame=<frame>
```

**A draw prints only the draw line. No per-player `ally` or `defeated`
lines follow it.**

Worked example of a victory (values are illustrative):

```
[HEADLESS RESULT] outcome=victory end_frame=18240 winner_index=2 winner_side=America winner_name=player1
[HEADLESS RESULT] defeated index=3 side=China name=player2
[HEADLESS RESULT] ally index=4 side=GLA name=player3
```

Keys:

- `outcome`: `victory` or `draw`.
- `end_frame`: the logic frame on which the single remaining alliance
  was detected.
- `winner_index` and `index`: the player index.
- `winner_side` and `side`: `Player::getSide()`, for example the
  faction side string.
- `winner_name` and `name`: the internal player name from
  `TheNameKeyGenerator->keyToName(getPlayerNameKey())`. This is not
  the lobby nickname. It is typically `player0`, `player1`, and so on,
  so the output is plain ASCII.
- `<role>`: `ally` if the player is allied with the winner
  (`areAllies`), otherwise `defeated`.

## Result log (`-resultLog <path>`)

`-resultLog <path>` appends one JSON object per replay to `<path>`.
The parser (`parseResultLog`) takes the next argument as the path. With
no argument it does nothing.

- The file is opened in append mode and closed after each line. It is
  never truncated, so delete it yourself between batches.
- It is written only on the headless batch path
  (`-headless -replay ...`). The non-headless replay loop does not
  write it.
- With `-jobs N`, the parent passes `-resultLog "<path>"` to every
  worker. All workers append to the same file. Lines are whole in
  practice, but their order is not guaranteed.
- If the file cannot be opened, `[resultLog] ERROR: could not open
  <path> for append` is printed to stdout and that line is skipped.

Line format, keys in this order:

```
{"file":"<replay path>","verdict":"<verdict>","framesPlayed":<uint>,"framesExpected":<uint>,"epoch":<int>,"statsExported":<true|false>,"staleRefs":<uint>}
```

| Field | Meaning |
| --- | --- |
| `file` | The replay path as given, JSON escaped. Backslashes, quotes, `\n`, `\r` and `\t` are escaped. Nothing else is escaped. |
| `verdict` | One of the values listed below. |
| `framesPlayed` | `TheGameLogic->getFrame()` when playback stopped. |
| `framesExpected` | `TheRecorder->getPlaybackFrameCount()`, the recorded length. |
| `epoch` | The `RecorderClass::ReplayEpoch` value: 0 retail, 1 v121, 2 v128, 3 v130, 4 v155 (current). It is `-1` on `STARTED` and `CANT_OPEN` lines. |
| `statsExported` | `true` only when `-exportStats` is set and the verdict is `OK` or `OK_RETAIL_UNCHECKED`. |
| `staleRefs` | `getPlaybackStaleObjectRefs()`, the number of replayed commands whose object IDs did not resolve. Zero is the strongest fidelity signal for retail-epoch replays. |

### Verdict values

- `STARTED`: written before simulation begins, as a crash marker. Its
  numeric fields are all 0, `statsExported` is false and `epoch` is
  -1. If `STARTED` is the last line for a file, the process crashed,
  usually on a hard desync. A finished run appends a later line that
  supersedes it, so every file normally has two lines.
- `OK`: the run completed and the checksum was verified. It reached the recorded length with no CRC mismatch.
- `OK_RETAIL_UNCHECKED`: same as `OK`, but for a retail-epoch replay.
  Retail checksums cannot be verified, so weigh `staleRefs` instead.
- `DESYNC`: a CRC mismatch was seen. Playback stopped there.
- `INCOMPLETE`: there was no mismatch, but
  `framesPlayed + LOGICFRAMES_PER_SECOND < framesExpected`, which
  allows one second of slack. This only applies when
  `framesExpected != 0`.
- `CANT_OPEN`: the replay could not be opened. Its numeric fields are
  0, `statsExported` is false and `epoch` is -1.

**`statsExported` is never true for `DESYNC` or `INCOMPLETE`.** It is
true only when `-exportStats` is set and the verdict is `OK` or
`OK_RETAIL_UNCHECKED`.

**`INCOMPLETE` never applies when a desync occurred.** A desynced
replay is always `DESYNC`, even if it stopped early.

Example of a `STARTED` line followed by the final line that replaces it:

```
{"file":"Replays\\match1.rep","verdict":"STARTED","framesPlayed":0,"framesExpected":0,"epoch":-1,"statsExported":false,"staleRefs":0}
{"file":"Replays\\match1.rep","verdict":"OK","framesPlayed":18240,"framesExpected":18240,"epoch":4,"statsExported":false,"staleRefs":0}
```

## Why

Two adjacent use cases:

- **Replay-based regression testing.** When AI changes go in, the
  easiest sanity check is "did this match still end the way the
  recording says it did?" Running the replay headless and reading the
  stdout result lines and the result log lets CI assert the winner
  and the verdict without any GUI scraping.
- **Tournament archive validation.** Batch-validating uploaded
  replays gives the leaderboard a way to fail fast on truncated or
  corrupted recordings.

## Code surface

- `GeneralsMD/Code/GameEngine/Source/GameLogic/ScriptEngine/VictoryConditions.cpp`:
  the stdout result lines, in `VictoryConditions::update`.
- `Core/GameEngine/Source/Common/ReplaySimulation.cpp`:
  `appendResultLogEntry`, `simulateReplaysInThisProcess` and
  `simulateReplaysInWorkerProcesses`.
- `GeneralsMD/Code/GameEngine/Source/Common/CommandLine.cpp`:
  `parseResultLog` and the `-resultLog` entry, plus the `-replay`
  and `-jobs` flags.
- `GeneralsMD/Code/GameEngine/Include/Common/Recorder.h`:
  the `REPLAY_EPOCH_*` enum and `getPlaybackStaleObjectRefs`.

## Concerns

- **AI-only setup gap.** As of 2026-05-12 the headless replay path
  is the only end-to-end automation hook; there's no way to set up
  and start an AI-only skirmish from the command line. The
  current workaround is "record manually once, then replay
  headlessly in CI." See memory `project_headless_replay.md`.
- **Mismatch / save / replay.** None. Neither the stdout lines nor the
  result log are written to the `.rep` file or to save state.
- **Output format stability.** Consumers should key on the
  `[HEADLESS RESULT]` prefix plus `outcome=`. New fields go after the
  existing key=value pairs. Treat the JSON keys of the result log, and
  their order, as a stable contract too.
- **Draws and missing lines.** A draw prints `outcome=draw`. A replay
  that ends before one alliance remains (for example a truncated
  replay, or players leaving with several alliances still alive)
  prints no result line at all.
