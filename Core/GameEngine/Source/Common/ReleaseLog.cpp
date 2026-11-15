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
#include "Common/AsyncDiagnosticLog.h"

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
static char s_userDataDir[512] = "";

// Protect path initialization only; no file operations take this lock.
static std::recursive_mutex s_releaseLogMutex;

// Paths are fixed on the producer; files are opened/rotated only by the worker.
struct DiagnosticFiles {
    std::mutex mutex;
    std::string path[2], previous[2];
    FILE* file[2] = {nullptr, nullptr};
    bool attempted[2] = {false, false};
};
static DiagnosticFiles& diagnosticFiles()
{
    static DiagnosticFiles* files = new DiagnosticFiles;
    return *files;
}
static AsyncDiagnosticLog& diagnosticWriter()
{
    // Process lifetime: do not join a potentially stuck disk operation during
    // static destruction, or race late logging from other background workers.
    // Orderly exit and upload paths use the bounded flush barrier below.
    static AsyncDiagnosticLog* writer = new AsyncDiagnosticLog([](const AsyncDiagnosticLog::Batch& batch) {
        DiagnosticFiles& files = diagnosticFiles();
        bool touched[2] = {false, false};
        for (const auto& entry : batch) {
            const unsigned channel = entry.channel;
            if (!files.attempted[channel]) {
                std::string path, previous;
                {
                    std::lock_guard<std::mutex> lock(files.mutex);
                    path = files.path[channel];
                    previous = files.previous[channel];
                }
                if (path.empty()) continue;
                files.attempted[channel] = true;
                std::remove(previous.c_str());
                std::rename(path.c_str(), previous.c_str());
                files.file[channel] = std::fopen(path.c_str(), "w");
            }
            if (files.file[channel]) {
                std::fwrite(entry.text.data(), 1, entry.text.size(), files.file[channel]);
                touched[channel] = true;
            }
        }
        for (unsigned channel = 0; channel < 2; ++channel)
            if (touched[channel]) std::fflush(files.file[channel]);
    });
    return *writer;
}

void DiagnosticLogSubmit(unsigned channel, const char* path, const char* previous,
    const char* text, bool verbose, unsigned rateKey)
{
    if (channel >= 2 || !path || !*path) return;
    {
        DiagnosticFiles& files = diagnosticFiles();
        std::lock_guard<std::mutex> lock(files.mutex);
        if (files.path[channel].empty()) {
            files.path[channel] = path;
            files.previous[channel] = previous;
        }
    }
    diagnosticWriter().submit(channel, text, verbose, rateKey);
}

bool DiagnosticLogFlush(unsigned timeoutMs)
{
    return diagnosticWriter().flush(timeoutMs);
}

// Full path, resolved lazily because TheGlobalData doesn't exist during very
// early startup. Returns NULL while the user data dir is still unknown.
static const char *releaseLogPath()
{
	std::lock_guard<std::recursive_mutex> lock(s_releaseLogMutex);
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
	strcpy(s_userDataDir, dir.str());
	return s_releaseLogPath;
}

const char *ReleaseLogGetUserDataDir()
{
	releaseLogPath();	// cache it now, while TheGlobalData may still exist
	return (s_userDataDir[0] != '\0') ? s_userDataDir : NULL;
}

static void releaseLogV(unsigned rateKey, const char* fmt, va_list ap)
{
    const char* path = releaseLogPath();
    if (!path) return;
    va_list copy;
    va_copy(copy, ap);
    const int length = vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);
    if (length < 0) return;
    const size_t capacity = (static_cast<size_t>(length) < AsyncDiagnosticLog::MAX_MESSAGE_BYTES - 64)
        ? static_cast<size_t>(length) : AsyncDiagnosticLog::MAX_MESSAGE_BYTES - 64;
    std::vector<char> buffer(capacity + 1);
    vsnprintf(buffer.data(), buffer.size(), fmt, ap);
    time_t now = time(nullptr);
    struct tm local = {};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "[%H:%M:%S] ", &local);
    std::string text = timestamp;
    text += buffer.data();
    if (static_cast<size_t>(length) > capacity) text += " [log message truncated]";
    text += '\n';
    DiagnosticLogSubmit(0, path, s_releaseLogPrevPath, text.c_str(), false, rateKey);
}

void ReleaseLog(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    releaseLogV(0, fmt, ap);
    va_end(ap);
}

void ReleaseLogRateLimited(unsigned key, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    releaseLogV(key, fmt, ap);
    va_end(ap);
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

// Queue one entry per section rather than thousands of separately formatted
// records. Large diagnostic sections are bounded by MAX_MESSAGE_BYTES.
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
