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

// ReleaseLog.cpp - see ReleaseLog.h for rationale.

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/ReleaseLog.h"

#include "Common/GlobalData.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <mutex>
#include <string>

// Log file name. Lives in the per-user data dir (same root as Replays\), not
// the working directory: unelevated installs can have a read-only game dir,
// which silently disabled the log while a stale copy kept being re-uploaded.
static const char *const RELEASE_LOG_FILE_NAME = "ReleaseLog.txt";
static const char *const RELEASE_LOG_PREV_FILE_NAME = "ReleaseLog.prev.txt";
static char s_releaseLogPath[512] = "";
static char s_releaseLogPrevPath[512] = "";

// Background workers (the network self-test, the telemetry uploader) write
// here too, so the lazy open and every write are serialized.
// Recursive: the fatal hooks (OnlineServices_Init.cpp) log from inside a CRT
// invalid-parameter report, which vfprintf can raise while this lock is held.
static std::recursive_mutex s_releaseLogMutex;

// Full path, resolved lazily because TheGlobalData doesn't exist during very
// early startup. Returns NULL while the user data dir is still unknown.
static const char *releaseLogPath()
{
	if (s_releaseLogPath[0] != '\0')
		return s_releaseLogPath;
	if (TheGlobalData == NULL)
		return NULL;
	const AsciiString &dir = TheGlobalData->getPath_UserData();
	if (dir.isEmpty()
	    || dir.getLength() + (Int)strlen(RELEASE_LOG_FILE_NAME) + 1 > (Int)sizeof(s_releaseLogPath))
		return NULL;
	strcpy(s_releaseLogPath, dir.str());
	strcat(s_releaseLogPath, RELEASE_LOG_FILE_NAME);
	strcpy(s_releaseLogPrevPath, dir.str());
	strcat(s_releaseLogPrevPath, RELEASE_LOG_PREV_FILE_NAME);
	return s_releaseLogPath;
}

// Opened lazily on first write, truncating once per app session so a single
// file accumulates every match played in that session. fflush after each line
// so it survives a crash.
static FILE *s_releaseLogFile = NULL;
static Bool  s_releaseLogOpenFailed = FALSE;

void ReleaseLog(const char *fmt, ...)
{
	std::lock_guard<std::recursive_mutex> lock(s_releaseLogMutex);
	if (s_releaseLogFile == NULL)
	{
		if (s_releaseLogOpenFailed)
			return;
		const char *path = releaseLogPath();
		if (path == NULL)
			return; // user data dir not known yet; try again on the next call
		// Keep the previous session's log as ReleaseLog.prev.txt. It is what
		// a crash or a "the game just froze, I restarted" report needs, and
		// the end-of-match upload ships it once per session.
		remove(s_releaseLogPrevPath);
		rename(path, s_releaseLogPrevPath);
		s_releaseLogFile = fopen(path, "w");
		if (s_releaseLogFile == NULL)
		{
			// Don't retry every call if the file can't be opened.
			s_releaseLogOpenFailed = TRUE;
			return;
		}
	}

	time_t now = time(NULL);
	struct tm *lt = localtime(&now);
	if (lt != NULL)
		fprintf(s_releaseLogFile, "[%02d:%02d:%02d] ", lt->tm_hour, lt->tm_min, lt->tm_sec);

	va_list ap;
	va_start(ap, fmt);
	vfprintf(s_releaseLogFile, fmt, ap);
	va_end(ap);

	fputc('\n', s_releaseLogFile);
	fflush(s_releaseLogFile);
}

const char *ReleaseGetLogFileName()
{
	const char *path = releaseLogPath();
	return path != NULL ? path : RELEASE_LOG_FILE_NAME;
}

const char *ReleaseGetPrevLogFileName()
{
	const char *path = releaseLogPath();
	return path != NULL ? s_releaseLogPrevPath : RELEASE_LOG_PREV_FILE_NAME;
}

// ---------------------------------------------------------------------------
// Tier 2: overall game-CRC ring buffer
// ---------------------------------------------------------------------------

// ~1024 samples. At a per-frame CRC interval and 30 fps that is ~34 seconds of
// history, which is plenty to see the run-up to a divergence. 8 bytes/entry.
static const Int RELEASE_CRC_RING_SIZE = 1024;

struct ReleaseFrameCRC
{
	UnsignedInt frame;
	UnsignedInt crc;
};

static ReleaseFrameCRC s_crcRing[RELEASE_CRC_RING_SIZE];
static Int s_crcRingNext  = 0;	// index of next slot to write
static Int s_crcRingCount = 0;	// total records written (may exceed ring size)

void ReleaseLogRecordFrameCRC(UnsignedInt frame, UnsignedInt crc)
{
	s_crcRing[s_crcRingNext].frame = frame;
	s_crcRing[s_crcRingNext].crc   = crc;
	++s_crcRingNext;
	if (s_crcRingNext == RELEASE_CRC_RING_SIZE)
		s_crcRingNext = 0;
	++s_crcRingCount;
}

// ---------------------------------------------------------------------------
// Tier 3 hook (dormant by default): per-subsystem CRC components
// ---------------------------------------------------------------------------

// Enabled by default: per-subsystem CRC capture is effectively free in release
// (getCRC runs only every ~100 frames and the reads are O(1); the ring is
// in-memory and only spills to disk on a desync). See ReleaseLog.h.
Bool TheReleaseLogSubsystemCRCs = TRUE;

// Larger ring: ~6 components per frame, so this holds ~1300 frames when enabled.
// Only touched when the flag is on, so it costs nothing in a normal release run.
static const Int RELEASE_SUB_RING_SIZE = 8192;

struct ReleaseSubCRC
{
	UnsignedInt frame;
	const char *tag;	// static-lifetime string literal supplied by caller
	UnsignedInt crc;
};

static ReleaseSubCRC s_subRing[RELEASE_SUB_RING_SIZE];
static Int s_subRingNext  = 0;
static Int s_subRingCount = 0;

void ReleaseLogResetCRCHistory()
{
	s_crcRingNext  = 0;
	s_crcRingCount = 0;
	s_subRingNext  = 0;
	s_subRingCount = 0;
}

void ReleaseLogRecordSubsystemCRC(UnsignedInt frame, const char *tag, UnsignedInt crc)
{
	if (!TheReleaseLogSubsystemCRCs)
		return;
	s_subRing[s_subRingNext].frame = frame;
	s_subRing[s_subRingNext].tag   = tag;
	s_subRing[s_subRingNext].crc   = crc;
	++s_subRingNext;
	if (s_subRingNext == RELEASE_SUB_RING_SIZE)
		s_subRingNext = 0;
	++s_subRingCount;
}

// One write per section rather than one per record: up to 9216 locked
// fprintf+fflush calls in a single frame was a visible hitch.
void ReleaseLogDumpCRCHistory()
{
	// Overall CRC history, oldest first.
	Int have  = (s_crcRingCount < RELEASE_CRC_RING_SIZE) ? s_crcRingCount : RELEASE_CRC_RING_SIZE;
	Int start = (s_crcRingCount < RELEASE_CRC_RING_SIZE) ? 0 : s_crcRingNext;
	std::string text;
	text.reserve(32 * (size_t)have);
	char line[96];
	Int k;
	for (k = 0; k < have; ++k)
	{
		const ReleaseFrameCRC &e = s_crcRing[(start + k) % RELEASE_CRC_RING_SIZE];
		snprintf(line, sizeof(line), "\n  frame %u CRC=%8.8X", e.frame, e.crc);
		text += line;
	}
	ReleaseLog("--- CRC history (last %d recorded CRCs) ---%s", have, text.c_str());

	// Per-subsystem history, only if the Tier-3 capture was enabled.
	if (TheReleaseLogSubsystemCRCs && s_subRingCount > 0)
	{
		Int subHave  = (s_subRingCount < RELEASE_SUB_RING_SIZE) ? s_subRingCount : RELEASE_SUB_RING_SIZE;
		Int subStart = (s_subRingCount < RELEASE_SUB_RING_SIZE) ? 0 : s_subRingNext;
		text.clear();
		text.reserve(40 * (size_t)subHave);
		for (k = 0; k < subHave; ++k)
		{
			const ReleaseSubCRC &e = s_subRing[(subStart + k) % RELEASE_SUB_RING_SIZE];
			snprintf(line, sizeof(line), "\n  frame %u %s CRC=%8.8X", e.frame, e.tag ? e.tag : "(null)", e.crc);
			text += line;
		}
		ReleaseLog("--- per-subsystem CRC history (last %d records) ---%s", subHave, text.c_str());
	}
}
