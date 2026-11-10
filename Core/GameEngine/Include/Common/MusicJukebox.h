/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// FILE: MusicJukebox.h /////////////////////////////////////////////////////////////////////////////
// A shuffled playlist of the player's own mp3s, used in place of the stock faction music.
//
// The stock music is a per-faction script chain in SkirmishScripts.scb: a fixed number of
// MUSIC_SET_TRACK calls, each followed by a hand-typed timer holding that song's length. Adding
// a song means editing Music.ini, the scb chain and the timer. If the player's Music folder
// holds any mp3s, the audio device ignores that chain and plays this playlist instead, ending
// each song when the stream actually ends, so no lengths are configured anywhere.
//
// Playback hooks live in MilesAudioManager. This class is only the playlist.

#pragma once

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"
#include "Common/STLTypedefs.h"

// Folder holding the player's own music, under the user data directory beside Replays.
#define MUSIC_JUKEBOX_FOLDER "Music\\"

class MusicJukebox
{
public:
	MusicJukebox();

	// Scan the Music folder. Cheap to call repeatedly; only the first call that finds the
	// subsystems up does any work, so mp3s added while the game runs appear on the next launch.
	void scanOnce();

	Bool isEmpty() const { return m_tracks.empty(); }

	// Full path of the song that should be playing, or the empty string if there is none.
	const AsciiString& currentTrack() const;

	// Just the file name of that song, for the on-screen track message.
	AsciiString currentTrackLeafName() const;

	// Length of that song in ms from its Xing/Info header, or 0 if it has none (CBR files).
	Int currentTrackLengthMs() const;

	// Move to the next song, reshuffling when the playlist wraps.
	void advance();

	// Step back to the previous song. Does not reshuffle: going back should land on the song
	// that just played, not on a new order.
	void retreat();

	// Reshuffle and start the order again, for the start of a new game.
	void restart();

	// Forget the current song, for a file the audio device could not open.
	void dropCurrentTrack();

	// For the self-test: install a playlist without touching the disk.
	void friend_setTracks(const std::vector<AsciiString>& tracks);

private:
	void shuffle(const AsciiString& avoidFirst);

	std::vector<AsciiString> m_tracks;	///< full paths, in play order
	Int m_index;												///< index into m_tracks of the current song
	Bool m_scanned;
};

// -musicjukeboxtest: ask GameEngine::init to run the self-test below instead of starting the
// shell, then exit the process with its failure count as the code.
void requestMusicJukeboxSelfTest();
Bool isMusicJukeboxSelfTestRequested();

// Length in ms of an mp3 whose first bytes are given, from its Xing/Info frame count, or 0.
Int mp3LengthMsFromHeader(const UnsignedByte *buf, Int len);

// Exercise the playlist order, wrap and drop handling, and report what the real Music folder
// holds. Prints one "[MUSICJUKEBOX TEST] ..." line per check on stdout; returns the failure count.
Int runMusicJukeboxSelfTest();
