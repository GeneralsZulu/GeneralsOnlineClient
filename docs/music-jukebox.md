# Music jukebox (player's own mp3s)

Drop mp3s in

```
<My Documents>\Command and Conquer Generals Zero Hour Data\Music\
```

(beside `Replays\`) and the game plays those, shuffled, instead of the
stock faction playlists. Empty or missing folder: nothing changes.

Covers matches only. The menus (the shell map) and the score screen keep their
stock faction tracks. Shift+M skips to the next song, the same key retail
already binds to `DEMO_MUSIC_NEXT_TRACK`.

## Why

Stock music is a per-faction script chain in `SkirmishScripts.scb`:

```
"USA Play Music Track 2"
  if TIMER_EXPIRED "USA Music Track 2 Start" or COUNTER "USA Music Track" == 2
  -> MUSIC_SET_TRACK "Game_USA_02"
  -> SET_MILLISECOND_TIMER "USA Music Track 3 Start" = 220   <- the song length, by hand
  -> SET_COUNTER "USA Music Track" = 3
```

Music streams open with an infinite loop count
(`MilesAudioManager::playStream`), so nothing but that hand-typed timer
ends a song. Adding one track meant a `MusicTrack` block in `Music.ini`
(inside retail `INIZH.big`), a new link in the scb chain, and typing the
duration. Three fixed-length chains, one per faction.

## How

`MusicJukebox` (`Core/GameEngine/.../Common/MusicJukebox.{h,cpp}`) is just
the playlist: scan, shuffle, advance, wrap, drop. Playback hooks are five
small ones in `MilesAudioManager`:

| Site | Behaviour when the jukebox is active |
| --- | --- |
| `playAudioEvent`, `AT_Music` | A song already playing: drop the request. Otherwise play the playlist's file under the requested event's `MusicTrack` info. |
| `stopAudioEvent`, `AHSV_StopTheMusic`/`Fade` | Ignored, so the script chain cannot cut a song short. |
| `playStream` | Loop count 1 instead of infinite, so the song ends. |
| `update` | Song ended: advance and play the next. Music we don't own playing: fade it out. Left the game or music switched off: release the song, and advance anyway so it does not start over next time. |
| `nextMusicTrack` / `prevMusicTrack` | Step the playlist instead of the INI track list, so the stock skip keys work. |

Active when the playlist is non-empty, there is an audio device,
`AudioAffect_Music` is on, and `TheGameLogic->isInGame()` without
`isInShellGame()`. The stock script chain keeps running and keeps asking for
its tracks and silences; all of it is ignored.

The load screen is excluded with `isLoadingMap()`, which brackets the whole of
`startNewGame`, so the faction load jingle (`Load_USA` and friends) still
plays while the map loads. That jingle is an ordinary music track with an
infinite loop count, so if it is still playing once the match is up, `update`
fades it out and starts the playlist.

No `Music.ini` entry, no scb edit and no configured length: the file path
is substituted at the device layer, and a song ends when its stream ends.

Order is Fisher-Yates over the **audio** random stream (seeded from the clock
in `InitRandom`, so it differs per launch). It is reshuffled on each wrap and
again at every game teardown (`reset`, reached from `GameLogic::clearGameData`),
so each game opens on a fresh order. Neither a wrap nor a new game opens with
the song that just played.

The playlist advances only when a song's stream ends, or when a skip key or the
end of a game moves it. Nothing advances it on a timer: that is the whole point
of not configuring lengths. A song that gets cut short still counts as played,
so quitting a match part way through a song does not replay it next time.

The folder is scanned once per launch, so mp3s added mid-session show up
the next time the game starts.

## Multiplayer / replays

No effect. The hooks sit below `AudioManager::addAudioEvent`, so no
logical-audio bookkeeping is skipped, and music only ever consumes the
audio random stream, never the logic one. Two players with different
Music folders stay in sync.

The stock chain's `MUSIC_TRACK_HAS_COMPLETED` conditions do read
client-local audio state and so fire at different times for a player with
a Music folder, but that was already true for any player who turned music
off, those scripts only set counters and timers, and `ScriptEngine::crc`
is empty.

## Limits

- Miles' mp3 decoder is from 2003. Ordinary CBR 44.1 kHz stereo is safe;
  VBR, 48 kHz or large ID3v2 / album-art headers may fail to open. A file
  that will not open is logged and dropped from the playlist for the rest
  of the session (`MUSICJUKEBOX:` lines in the release log).
- One frame of silence between songs, since the next one starts from the
  audio update after the stream ends.
- One folder for everyone. No per-faction subfolders, no ordering file,
  no in-game track display, mp3 only.

## Self-test

```
GeneralsOnlineZH.exe -headless -musicjukeboxtest
```

Checks the playlist order, wrap and drop handling, prints
`[MUSICJUKEBOX TEST]` lines plus what the real folder holds, and exits
with the failure count.
