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

// FILE: MusicJukebox.cpp ///////////////////////////////////////////////////////////////////////////
// See MusicJukebox.h.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/MusicJukebox.h"

#include "Common/AudioRandomValue.h"
#include "Common/FileSystem.h"
#include "Common/GlobalData.h"
#include "Common/LocalFileSystem.h"
#include "Common/ReleaseLog.h"

//-------------------------------------------------------------------------------------------------
MusicJukebox::MusicJukebox()
	: m_index(0)
	, m_scanned(FALSE)
{
}

//-------------------------------------------------------------------------------------------------
void MusicJukebox::scanOnce()
{
	if (m_scanned) {
		return;
	}

	// Called from the audio update, which can run before the subsystems we need. Stay unscanned
	// and try again rather than deciding the folder is empty.
	if (TheGlobalData == nullptr || TheLocalFileSystem == nullptr) {
		return;
	}

	AsciiString dir = TheGlobalData->getPath_UserData();
	if (dir.isEmpty()) {
		return;
	}
	dir.concat(MUSIC_JUKEBOX_FOLDER);

	m_scanned = TRUE;

	// Make the folder the first time, so a player who has never heard of this can see where the
	// mp3s go without being told.
	CreateDirectory(dir.str(), nullptr);

	// Deliberately the local file system only: these are the player's own loose files, and a
	// shipped .big must never switch the jukebox on.
	FilenameList files;
	TheLocalFileSystem->getFileListInDirectory(AsciiString::TheEmptyString, dir, "*.mp3", files, FALSE);

	FilenameListIter it;
	for (it = files.begin(); it != files.end(); ++it) {
		m_tracks.push_back(*it);
	}

	m_index = 0;
	if (!m_tracks.empty()) {
		shuffle(AsciiString::TheEmptyString);
		ReleaseLog("MUSICJUKEBOX: %d mp3s in %s; playing those instead of the stock tracks",
			(Int)m_tracks.size(), dir.str());
	}
}

//-------------------------------------------------------------------------------------------------
const AsciiString& MusicJukebox::currentTrack() const
{
	if (m_index < 0 || m_index >= (Int)m_tracks.size()) {
		return AsciiString::TheEmptyString;
	}

	return m_tracks[m_index];
}

//-------------------------------------------------------------------------------------------------
void MusicJukebox::advance()
{
	if (m_tracks.empty()) {
		return;
	}

	const AsciiString justPlayed = currentTrack();

	++m_index;
	if (m_index >= (Int)m_tracks.size()) {
		shuffle(justPlayed);
		m_index = 0;
	}
}

//-------------------------------------------------------------------------------------------------
AsciiString MusicJukebox::currentTrackLeafName() const
{
	const AsciiString& path = currentTrack();
	const char *leaf = path.reverseFind('\\');

	return leaf != nullptr ? AsciiString(leaf + 1) : path;
}

//-------------------------------------------------------------------------------------------------
void MusicJukebox::retreat()
{
	if (m_tracks.empty()) {
		return;
	}

	if (m_index <= 0) {
		m_index = (Int)m_tracks.size() - 1;
	} else {
		--m_index;
	}
}

//-------------------------------------------------------------------------------------------------
void MusicJukebox::restart()
{
	if (m_tracks.empty()) {
		return;
	}

	// Copy first: shuffle() reorders m_tracks, which currentTrack() points into.
	const AsciiString justPlayed = currentTrack();
	shuffle(justPlayed);
	m_index = 0;
}

//-------------------------------------------------------------------------------------------------
void MusicJukebox::dropCurrentTrack()
{
	if (m_tracks.empty()) {
		return;
	}

	if (m_index < 0 || m_index >= (Int)m_tracks.size()) {
		m_index = 0;
		return;
	}

	ReleaseLog("MUSICJUKEBOX: dropping '%s', it would not open", m_tracks[m_index].str());

	m_tracks.erase(m_tracks.begin() + m_index);
	if (m_index >= (Int)m_tracks.size()) {
		m_index = 0;
	}
}

//-------------------------------------------------------------------------------------------------
void MusicJukebox::friend_setTracks(const std::vector<AsciiString>& tracks)
{
	m_tracks = tracks;
	m_index = 0;
	m_scanned = TRUE;
}

//-------------------------------------------------------------------------------------------------
/** Fisher-Yates over the audio random stream, which is client side only: music must never touch
	* the logic random values that feed the game CRC. avoidFirst keeps a wrap from replaying the
	* song that just finished. */
//-------------------------------------------------------------------------------------------------
void MusicJukebox::shuffle(const AsciiString& avoidFirst)
{
	const Int count = (Int)m_tracks.size();
	AsciiString tmp;
	Int i;

	for (i = count - 1; i > 0; --i) {
		const Int j = GameAudioRandomValue(0, i);
		if (j != i) {
			tmp = m_tracks[i];
			m_tracks[i] = m_tracks[j];
			m_tracks[j] = tmp;
		}
	}

	if (count > 1 && !avoidFirst.isEmpty() && m_tracks[0] == avoidFirst) {
		tmp = m_tracks[0];
		m_tracks[0] = m_tracks[count - 1];
		m_tracks[count - 1] = tmp;
	}
}

//-----------------------------------------------------------------------------
// Self-test (run with -headless -musicjukeboxtest).
//-----------------------------------------------------------------------------

static Bool s_selfTestRequested = FALSE;
void requestMusicJukeboxSelfTest() { s_selfTestRequested = TRUE; }
Bool isMusicJukeboxSelfTestRequested() { return s_selfTestRequested; }

static Int s_selfTestFailures = 0;

static void selfTestCheck(Bool ok, const char *what)
{
	if (!ok) {
		++s_selfTestFailures;
	}
	fprintf(stdout, "[MUSICJUKEBOX TEST] %s %s\n", ok ? "ok  " : "FAIL", what);
}

static void selfTestFillPlaylist(MusicJukebox& jukebox, Int count)
{
	std::vector<AsciiString> tracks;
	Int i;
	for (i = 0; i < count; ++i) {
		AsciiString name;
		name.format("C:\\Music\\song%d.mp3", i);
		tracks.push_back(name);
	}
	jukebox.friend_setTracks(tracks);
}

// Index of a "song<n>.mp3" path within a playlist of the given size, or -1.
static Int selfTestTrackIndex(const AsciiString& path, Int count)
{
	Int i;
	for (i = 0; i < count; ++i) {
		AsciiString name;
		name.format("C:\\Music\\song%d.mp3", i);
		if (name == path) {
			return i;
		}
	}
	return -1;
}

Int runMusicJukeboxSelfTest()
{
	s_selfTestFailures = 0;

	const Int COUNT = 8;
	const Int CYCLES = 16;
	MusicJukebox jukebox;
	Int i, cycle, step;

	// 1. Each cycle plays every song exactly once, and advance() never hands out a blank.
	selfTestFillPlaylist(jukebox, COUNT);
	std::vector<Int> playsPerSong(COUNT, 0);
	std::vector<Int> firstOfCycle;
	std::vector<Int> lastOfCycle;
	Bool everyStepNamedASong = TRUE;
	Bool everyCycleComplete = TRUE;
	for (cycle = 0; cycle < CYCLES; ++cycle) {
		std::vector<Int> thisCycle(COUNT, 0);
		for (step = 0; step < COUNT; ++step) {
			const Int which = selfTestTrackIndex(jukebox.currentTrack(), COUNT);
			if (which < 0) {
				everyStepNamedASong = FALSE;
			} else {
				++thisCycle[which];
				++playsPerSong[which];
				if (step == 0) {
					firstOfCycle.push_back(which);
				}
				if (step == COUNT - 1) {
					lastOfCycle.push_back(which);
				}
			}
			jukebox.advance();
		}
		for (i = 0; i < COUNT; ++i) {
			if (thisCycle[i] != 1) {
				everyCycleComplete = FALSE;
			}
		}
	}
	selfTestCheck(everyStepNamedASong, "playlist: every step names a song");
	selfTestCheck(everyCycleComplete, "playlist: each pass plays every song exactly once");

	Bool allSongsUsed = TRUE;
	for (i = 0; i < COUNT; ++i) {
		if (playsPerSong[i] != CYCLES) {
			allSongsUsed = FALSE;
		}
	}
	selfTestCheck(allSongsUsed, "playlist: 16 passes play every song 16 times");

	// 2. The order is actually shuffled, and a wrap never replays the song that just ended.
	Bool orderVaries = FALSE;
	Bool noRepeatAcrossWrap = TRUE;
	for (i = 1; i < (Int)firstOfCycle.size(); ++i) {
		if (firstOfCycle[i] != firstOfCycle[0]) {
			orderVaries = TRUE;
		}
		if (firstOfCycle[i] == lastOfCycle[i - 1]) {
			noRepeatAcrossWrap = FALSE;
		}
	}
	selfTestCheck(orderVaries, "shuffle: the first song of a pass varies between passes");
	selfTestCheck(noRepeatAcrossWrap, "shuffle: a wrap does not replay the song that just ended");

	// 3. A file that will not open is dropped, and draining the playlist empties it cleanly.
	selfTestFillPlaylist(jukebox, 3);
	const AsciiString doomed = jukebox.currentTrack();
	jukebox.dropCurrentTrack();
	Bool stillPlayable = !jukebox.isEmpty() && !jukebox.currentTrack().isEmpty();
	Bool droppedForGood = TRUE;
	for (i = 0; i < 16; ++i) {
		if (jukebox.currentTrack() == doomed) {
			droppedForGood = FALSE;
		}
		jukebox.advance();
	}
	selfTestCheck(stillPlayable, "drop: the remaining songs still play");
	selfTestCheck(droppedForGood, "drop: the dropped song never comes back");

	jukebox.dropCurrentTrack();
	jukebox.dropCurrentTrack();
	selfTestCheck(jukebox.isEmpty(), "drop: dropping every song empties the playlist");
	selfTestCheck(jukebox.currentTrack().isEmpty(), "drop: an empty playlist names no song");
	jukebox.advance();	// must not crash or resurrect anything
	selfTestCheck(jukebox.isEmpty(), "drop: advancing an empty playlist is harmless");

	// 4. A new game reshuffles and starts the order over, and does not reopen with the song that
	//    was cut off when the last game ended.
	selfTestFillPlaylist(jukebox, COUNT);
	// Section 1 already proves a pass plays every song once, so only the restart itself is checked.
	std::vector<Int> firstAfterRestart;
	Bool restartAvoidsCutOffSong = TRUE;
	for (cycle = 0; cycle < CYCLES; ++cycle) {
		for (step = 0; step < 3; ++step) {
			jukebox.advance();	// stop part way through, the way quitting a match does
		}
		const AsciiString cutOff = jukebox.currentTrack();
		jukebox.restart();
		if (jukebox.currentTrack() == cutOff) {
			restartAvoidsCutOffSong = FALSE;
		}
		firstAfterRestart.push_back(selfTestTrackIndex(jukebox.currentTrack(), COUNT));
	}
	Bool restartReorders = FALSE;
	for (i = 1; i < (Int)firstAfterRestart.size(); ++i) {
		if (firstAfterRestart[i] != firstAfterRestart[0]) {
			restartReorders = TRUE;
		}
	}
	selfTestCheck(restartAvoidsCutOffSong, "restart: a new game does not reopen with the song it cut off");
	selfTestCheck(restartReorders, "restart: the opening song varies between games");

	// 5. The skip keys step through the playlist, and stepping back wraps.
	selfTestFillPlaylist(jukebox, 4);
	const AsciiString skipFirst = jukebox.currentTrack();
	jukebox.advance();
	const Bool stepsForward = jukebox.currentTrack() != skipFirst;
	jukebox.retreat();
	const Bool stepsBack = jukebox.currentTrack() == skipFirst;
	jukebox.retreat();
	const Bool wrapsBack = jukebox.currentTrack() != skipFirst && !jukebox.currentTrack().isEmpty();
	selfTestCheck(stepsForward, "skip: forward moves to another song");
	selfTestCheck(stepsBack, "skip: back returns to the song before it");
	selfTestCheck(wrapsBack, "skip: back from the first song wraps to the end");

	// 6. The on-screen track message gets a file name, not a 200 character path.
	std::vector<AsciiString> onePath;
	onePath.push_back(AsciiString("C:\\Users\\me\\CnC Data\\Music\\My Song.mp3"));
	jukebox.friend_setTracks(onePath);
	selfTestCheck(jukebox.currentTrackLeafName() == "My Song.mp3", "name: the track message shows just the file name");

	// 7. Not a check: report what the player's real folder holds.
	MusicJukebox real;
	real.scanOnce();
	fprintf(stdout, "[MUSICJUKEBOX TEST] note %s folder: %s\n",
		MUSIC_JUKEBOX_FOLDER, real.isEmpty() ? "empty or missing, stock music in use" : real.currentTrack().str());

	fprintf(stdout, "[MUSICJUKEBOX TEST] failures=%d\n", s_selfTestFailures);
	return s_selfTestFailures;
}
