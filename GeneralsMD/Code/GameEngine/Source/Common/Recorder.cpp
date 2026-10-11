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

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/Recorder.h"
#include "Common/file.h"
#include "Common/FileSystem.h"
#include "Common/PlayerList.h"
#include "Common/Player.h"
#include "Common/GlobalData.h"
#include "Common/GameEngine.h"
#include "Common/FramePacer.h"
#if defined(GENERALS_ONLINE)
#include "GameNetwork/GeneralsOnline/NGMP_include.h"
#define OBS_LOG(...) NetworkLog(ELogVerbosity::LOG_RELEASE, __VA_ARGS__)
#else
#define OBS_LOG(...) DEBUG_LOG((__VA_ARGS__))
#endif
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/NetworkInterface.h"
#include "GameNetwork/ResumeFromReplay.h"
#include "GameClient/ClientInstance.h"
#include "GameClient/GameWindow.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/InGameUI.h"
#include "GameClient/Shell.h"
#include "GameClient/GameText.h"

#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/GameMessageParser.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/networkutil.h"
#include "GameLogic/GameLogic.h"
#include "Common/RandomValue.h"
#include "Common/CRCDebug.h"
#include "Common/OptionPreferences.h"
#include "Common/ReleaseLog.h"
#include "Common/StatsExporter.h"
#include "Common/StatsUploader.h"
#include "Common/version.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/GameInfo.h"
#include "GameNetwork/LANObserverStream.h"
#include "../NGMPGame.h"
#include "../OnlineServices_Init.h"

extern NGMPGame* TheNGMPGame;

// TheSuperHackers @feature bill-rich 15/09/2026 Resume-from-replay milestones go to the release log online.
#if defined(GENERALS_ONLINE)
// Both logs: the network log for GO's own tooling, the release log because that
// is the one the end-of-match upload ships (a failed resume used to leave no
// server-side trace at all).
#define RESUME_LOG(...) do { ReleaseLog(__VA_ARGS__); NetworkLog(ELogVerbosity::LOG_RELEASE, __VA_ARGS__); } while (0)
#else
#define RESUME_LOG(...) DEBUG_LOG((__VA_ARGS__))
#endif

// UTF-8 encode a UnicodeString onto an AsciiString. Used for the telemetry
// uploads, whose form fields and headers carry the player's lobby name; the
// servers expect UTF-8 rather than the engine's UTF-16.
static void appendUtf8(AsciiString& out, const UnicodeString& name)
{
	const WideChar *p = name.str();
	if (p == nullptr)
		return;
	for (; *p != L'\0'; ++p)
	{
		unsigned int c = static_cast<unsigned int>(*p);
		if (c < 0x80)
		{
			out.concat(static_cast<char>(c));
		}
		else if (c < 0x800)
		{
			out.concat(static_cast<char>(0xC0 | (c >> 6)));
			out.concat(static_cast<char>(0x80 | (c & 0x3F)));
		}
		else
		{
			out.concat(static_cast<char>(0xE0 | (c >> 12)));
			out.concat(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
			out.concat(static_cast<char>(0x80 | (c & 0x3F)));
		}
	}
}

constexpr const char s_genrep[] = "GENREP";
constexpr const UnsignedInt replayBufferBytes = 8192;

Int REPLAY_CRC_INTERVAL = 100;

const char* replayExtention = ".rep";
const char* lastReplayFileName = "00000000";	// a name the user is unlikely to ever type, but won't cause panic & confusion
const char* resumeReplayFileName = "Resume";	// the resume-from-replay source every client replays (see ResumeFromReplay)

// TheSuperHackers @tweak helmutbuhler 25/04/2025
// The replay header contains two time fields; startTime and endTime of type time_t.
// time_t is 32 bit wide on VC6, but on newer compilers it is 64 bit wide.
// In order to remain compatible we need to load and save time values with 32 bits.
// Note that this will overflow on January 18, 2038. @todo Upgrade to 64 bits when we break compatibility.
typedef int32_t replay_time_t;

static time_t startTime;
static const UnsignedInt startTimeOffset = 6;
static const UnsignedInt endTimeOffset = startTimeOffset + sizeof(replay_time_t);
static const UnsignedInt frameCountOffset = endTimeOffset + sizeof(replay_time_t);
static const UnsignedInt desyncOffset = frameCountOffset + sizeof(UnsignedInt);
static const UnsignedInt quitEarlyOffset = desyncOffset + sizeof(Bool);
static const UnsignedInt disconOffset = quitEarlyOffset + sizeof(Bool);

static void writeAtOffset(File* file, Int offset, const void* data, Int dataSize)
{
	UnsignedInt fileSize = file->size();
	DEBUG_ASSERTCRASH((UnsignedInt)(offset + dataSize) <= fileSize, ("writeAtOffset would exceed file size!"));
	if (file->seek(offset, File::seekMode::START) == offset)
	{
		file->write(data, dataSize);
	}
	MAYBE_UNUSED Int res = file->seek(fileSize, File::seekMode::START);
	(void)res;
	DEBUG_ASSERTCRASH(res == fileSize, ("Could not seek to end of file!"));
}

#if defined(RTS_DEBUG)
static FILE* openStatsLogFile()
{
	unsigned long bufSize = MAX_COMPUTERNAME_LENGTH + 1;
	char computerName[MAX_COMPUTERNAME_LENGTH + 1];
	if (!GetComputerName(computerName, &bufSize))
	{
		strcpy(computerName, "unknown");
	}
	AsciiString statsFile = TheGlobalData->m_baseStatsDir;
	statsFile.concat(computerName);
	statsFile.concat(".txt");
	return fopen(statsFile.str(), "a+");
}
#endif

RecorderClass::CRCInfo::CRCInfo() :
	m_sawCRCMismatch(FALSE),
	m_skippedOne(FALSE),
	m_localPlayer(0)
{}

RecorderClass::CRCInfo::CRCInfo(UnsignedInt localPlayer, Bool isMultiplayer)
{
	m_sawCRCMismatch = FALSE;
	m_skippedOne = !isMultiplayer;
	m_localPlayer = localPlayer;
}

void RecorderClass::CRCInfo::addCRC(UnsignedInt val)
{
	// TheSuperHackers @fix helmutbuhler 03/04/2025
	// In Multiplayer, the first MSG_LOGIC_CRC message somehow doesn't make it through the network.
	// Perhaps this happens because the network is not yet set up on frame 0.
	// So we also don't queue up the first local crc message, otherwise the crc
	// messages wouldn't match up anymore and we'd desync immediately during playback.
	if (!m_skippedOne)
	{
		m_skippedOne = TRUE;
		return;
	}

	m_data.push_back(val);
	//DEBUG_LOG(("CRCInfo::addCRC() - crc %8.8X pushes list to %d entries (full=%d)", val, m_data.size(), !m_data.empty()));
}

UnsignedInt RecorderClass::CRCInfo::readCRC()
{
	if (m_data.empty())
	{
		DEBUG_LOG(("CRCInfo::readCRC() - bailing, full=0, size=%d", m_data.size()));
		return 0;
	}

	UnsignedInt val = m_data.front();
	m_data.pop_front();
	//DEBUG_LOG(("CRCInfo::readCRC() - returning %8.8X, full=%d, size=%d", val, !m_data.empty(), m_data.size()));
	return val;
}

void RecorderClass::logGameStart(AsciiString options)
{
	if (!m_file)
		return;

	time(&startTime);
	replay_time_t tmp = (replay_time_t)startTime;
	writeAtOffset(m_file, startTimeOffset, &tmp, sizeof(tmp));

#if defined(RTS_DEBUG)
	if (TheNetwork && TheGlobalData->m_saveStats)
	{
		TheFileSystem->createDirectory(TheGlobalData->m_baseStatsDir);
		FILE *logFP = openStatsLogFile();
		if (!logFP)
		{
			TheWritableGlobalData->m_baseStatsDir = TheGlobalData->getPath_UserData();
			logFP = openStatsLogFile();
		}
		if (logFP)
		{
			struct tm *t2 = localtime(&startTime);
			fprintf(logFP, "\nGame start at %s\tOptions are %s\n", asctime(t2), options.str());
			fclose(logFP);
		}
	}
#endif
}

void RecorderClass::logPlayerDisconnect(UnicodeString player, Int slot)
{
	if (!m_file)
		return;

	DEBUG_ASSERTCRASH((slot >= 0) && (slot < MAX_SLOTS), ("Attempting to disconnect an invalid slot number"));
	if ((slot < 0) || (slot >= (MAX_SLOTS)))
	{
		return;
	}
	Bool flag = TRUE;
	Int playerSlotDisconOffset = disconOffset + slot * sizeof(Bool);
	writeAtOffset(m_file, playerSlotDisconOffset, &flag, sizeof(flag));

#if defined(RTS_DEBUG)
	if (TheGlobalData->m_saveStats)
	{
		FILE *logFP = openStatsLogFile();
		if (logFP)
		{
			time_t t;
			time(&t);
			struct tm *t2 = localtime(&t);
			fprintf(logFP, "\tPlayer %ls dropped at %s", player.str(), asctime(t2));
			fclose(logFP);
		}
	}
#endif
}

void RecorderClass::logCRCMismatch()
{
	if (!m_file)
		return;

	Bool flag = TRUE;
	writeAtOffset(m_file, desyncOffset, &flag, sizeof(flag));

#if defined(RTS_DEBUG)
	if (TheGlobalData->m_saveStats)
	{
		m_wasDesync = TRUE;
		FILE *logFP = openStatsLogFile();
		if (logFP)
		{
			time_t t;
			time(&t);
			struct tm *t2 = localtime(&t);
			fprintf(logFP, "\tCRC mismatch at %s", asctime(t2));
			fclose(logFP);
		}
	}
#endif
}

void RecorderClass::logGameEnd()
{
	if (!m_file)
		return;

	time_t t;
	time(&t);
	UnsignedInt frameCount = TheGameLogic->getFrame();
	replay_time_t tmp = (replay_time_t)t;
	writeAtOffset(m_file, endTimeOffset, &tmp, sizeof(tmp));
	writeAtOffset(m_file, frameCountOffset, &frameCount, sizeof(frameCount));

#if defined(RTS_DEBUG)
	if (TheNetwork && TheGlobalData->m_saveStats)
	{
		FILE *logFP = openStatsLogFile();
		if (logFP)
		{
			struct tm *t2 = localtime(&t);
			time_t duration = t - startTime;
			Int minutes = duration/60;
			Int seconds = duration%60;
			fprintf(logFP, "Game end at   %s(%d:%2.2d elapsed time)\n", asctime(t2), minutes, seconds);
			fclose(logFP);
		}
	}
#endif
}

void RecorderClass::cleanUpReplayFile()
{
#if defined(RTS_DEBUG)
	if (TheGlobalData->m_saveStats)
	{
		char fname[_MAX_PATH + 1];
		strlcpy(fname, TheGlobalData->m_baseStatsDir.str(), ARRAY_SIZE(fname));
		strlcat(fname, m_fileName.str(), ARRAY_SIZE(fname));
		DEBUG_LOG(("Saving replay to %s", fname));
		AsciiString oldFname;
		oldFname.format("%s%s", getReplayDir().str(), m_fileName.str());
		CopyFile(oldFname.str(), fname, TRUE);

#ifdef DEBUG_LOGGING
		const char* logFileName = DebugGetLogFileName();
		if (logFileName[0] == '\0')
			return;

		AsciiString debugFname = fname;
		debugFname.truncateBy(3);
		debugFname.concat("txt");
		UnsignedInt fileSize = 0;
		FILE* fp = fopen(logFileName, "rb");
		if (fp)
		{
			fseek(fp, 0, SEEK_END);
			fileSize = ftell(fp);
			fclose(fp);
			fp = nullptr;
			DEBUG_LOG(("Log file size was %d", fileSize));
		}

		const int MAX_DEBUG_SIZE = 65536;
		if (fileSize <= MAX_DEBUG_SIZE || TheGlobalData->m_saveAllStats)
		{
			DEBUG_LOG(("Using CopyFile to copy %s", logFileName));
			CopyFile(logFileName, debugFname.str(), TRUE);
		}
		else
		{
			DEBUG_LOG(("manual copy of %s", logFileName));
			FILE* ifp = fopen(logFileName, "rb");
			FILE* ofp = fopen(debugFname.str(), "wb");
			if (ifp && ofp)
			{
				fseek(ifp, fileSize - MAX_DEBUG_SIZE, SEEK_SET);
				char buf[4096];
				Int len;
				while ((len = fread(buf, 1, 4096, ifp)) > 0)
				{
					fwrite(buf, 1, len, ofp);
				}
				fclose(ofp);
				fclose(ifp);
				ifp = nullptr;
				ofp = nullptr;
			}
			else
			{
				if (ifp) fclose(ifp);
				if (ofp) fclose(ofp);
				ifp = nullptr;
				ofp = nullptr;
			}
		}
#endif // DEBUG_LOGGING
	}
#endif
}

/**
 * The recorder object.
 */
RecorderClass *TheRecorder = nullptr;

/**
 * Constructor
 */
RecorderClass::RecorderClass()
{
	m_originalGameMode = GAME_NONE;
	m_mode = RECORDERMODETYPE_RECORD;
	m_file = nullptr;
	m_resumeSourceFile = nullptr;
	m_fileName.clear();
	m_currentFilePosition = 0;
	m_doingAnalysis = FALSE;
	m_archiveReplays = FALSE;
	m_nextFrame = 0;
	m_wasDesync = FALSE;
	m_replayAIFeatureVersion = ZULU_AI_FEATURE_CURRENT;
	m_replayEpoch = REPLAY_EPOCH_CURRENT;
	m_replayLegacyUpgradeKeyDelta = 0;
	m_playbackStaleObjectRefs = 0;
	init(); // just for the heck of it.
}

/**
 * Destructor
 */
RecorderClass::~RecorderClass() {
}

//----------------------------------------------------------------------------------------------------------
// Zulu replay extension block helpers.
//----------------------------------------------------------------------------------------------------------
void RecorderClass::writeZuluReplayExtension()
{
	const char zuluMagic[4] = {'Z','U','L','U'};
	m_file->write(zuluMagic, sizeof(zuluMagic));
	UnsignedInt version = ZULU_AI_FEATURE_CURRENT;
	m_file->write(&version, sizeof(version));
	m_replayAIFeatureVersion = version;
}

void RecorderClass::readZuluReplayExtension()
{
	// Try to consume the extension block. Vanilla / pre-Zulu replays will
	// fail the magic check; rewind so the bytes we peeked at are interpreted
	// as the start of the command stream, as before.
	char magic[4] = {0,0,0,0};
	Int pos = m_file->position();
	Int got = m_file->read(magic, sizeof(magic));
	if (got == (Int)sizeof(magic)
		&& magic[0] == 'Z' && magic[1] == 'U' && magic[2] == 'L' && magic[3] == 'U')
	{
		UnsignedInt version = 0;
		m_file->read(&version, sizeof(version));
		m_replayAIFeatureVersion = version;
		DEBUG_LOG(("RecorderClass: Zulu replay extension version %u", version));
	}
	else
	{
		m_file->seek(pos, File::START);
		m_replayAIFeatureVersion = ZULU_AI_FEATURE_NONE;
		DEBUG_LOG(("RecorderClass: vanilla replay; Zulu AI features disabled for playback"));
	}
}

Bool RecorderClass::isAIFeatureEnabled(UnsignedInt featureVersion) const
{
	if (m_mode == RECORDERMODETYPE_PLAYBACK
		|| m_mode == RECORDERMODETYPE_SIMULATION_PLAYBACK
		|| m_mode == RECORDERMODETYPE_LIVE_OBSERVER
		|| m_mode == RECORDERMODETYPE_RESUME_CATCHUP)
	{
		return m_replayAIFeatureVersion >= featureVersion;
	}
	return TRUE;
}

//----------------------------------------------------------------------------------------------------------
// Replay determinism epoch.
//----------------------------------------------------------------------------------------------------------
Int RecorderClass::s_replayEpochOverride = -1;

// Map a semantic version to its determinism epoch. Only builds after the
// version-string switch render "major.minor.patch" (e.g. "1.2.8"); retail and
// pre-switch builds render "Version 1.04" and are handled by the caller as
// REPLAY_EPOCH_RETAIL.
static RecorderClass::ReplayEpoch epochFromSemanticVersion(Int major, Int minor, Int patch)
{
	if (major != 1)
		return RecorderClass::REPLAY_EPOCH_CURRENT;
	if (minor < 2)
		return RecorderClass::REPLAY_EPOCH_RETAIL;   // 1.0.x, 1.1.x
	if (minor == 2)
	{
		if (patch <= 0) return RecorderClass::REPLAY_EPOCH_RETAIL;  // 1.2.0
		if (patch <= 7) return RecorderClass::REPLAY_EPOCH_V121;    // 1.2.1 - 1.2.7
		return RecorderClass::REPLAY_EPOCH_V128;                    // 1.2.8, 1.2.9
	}
	// 1.5.3 is the odd one out. It was never a release: it is a dev build from
	// Jul 23 2026, handed out before 1.5.4, and it already carried community
	// patch sim fixes that mainline only shipped (and only then epoch-gated)
	// in 1.5.5 -- the empty-Stinger snipe fix landed upstream that same day.
	// So its replays re-simulate closer under the V155 gates than under V130,
	// measured over six of them against 1.5.2 data: every one survives longer
	// (0:10 -> 3:34 in the worst case, one clean to the end), none worse.
	if (minor == 5 && patch == 3)
		return RecorderClass::REPLAY_EPOCH_V155;
	// 1.5.4 carries none of the stable-upgrade-id / community-patch sim
	// changes -- those landed after it and first shipped in 1.5.5.
	if (minor < 5 || (minor == 5 && patch <= 4))
		return RecorderClass::REPLAY_EPOCH_V130;      // 1.3.0 - 1.5.4
	return RecorderClass::REPLAY_EPOCH_V155;          // 1.5.5+
}

// Pre-V155 replays carry upgrade purchases as raw NameKeyType values. The
// numbering belongs to the RECORDING environment (binary registrations + all
// names registered by data parsed before Upgrade.ini). Playback loads the
// recording's data package, so the data half cancels out; what remains is the
// binary difference: one extra early FunctionLexicon registration from 1.5.2
// on, shifting every later namekey by one -- the cause of the v1.5.2
// replay-fidelity regression. This returns OUR binary's early-registration
// count minus the recording binary's: add it to a recorded upgrade key to get
// the same upgrade under our numbering. If a future change ever adds/removes
// another pre-Upgrade.ini registration, gate it here the same way (older
// recordings get a bigger delta).
static Int legacyUpgradeKeyDeltaFromHeader(const RecorderClass::ReplayHeader& header)
{
	AsciiString vs;
	vs.translate(header.versionString);
	const char *s = vs.str();
	Int a = 0, b = 0, c = 0;
	if (s[0] >= '0' && s[0] <= '9' && sscanf(s, "%d.%d.%d", &a, &b, &c) == 3)
	{
		// 1.5.2 and later builds share our early-registration set.
		if (a > 1 || (a == 1 && (b > 5 || (b == 5 && c >= 2))))
			return 0;
	}
	return 1;   // retail and forks <= 1.5.1: one fewer early registration than us
}

static RecorderClass::ReplayEpoch deriveReplayEpochFromHeader(const RecorderClass::ReplayHeader& header)
{
	AsciiString vs;
	vs.translate(header.versionString);
	const char *s = vs.str();
	Int a = 0, b = 0, c = 0;
	// Treat as a semantic version only if it starts with a digit and has all
	// three components; otherwise (retail "Version 1.04", dev builds) fall
	// back to the retail epoch. The -replayEpoch override is authoritative for
	// anything the header can't disambiguate.
	if (s[0] >= '0' && s[0] <= '9' && sscanf(s, "%d.%d.%d", &a, &b, &c) == 3)
		return epochFromSemanticVersion(a, b, c);
	return RecorderClass::REPLAY_EPOCH_RETAIL;
}

RecorderClass::ReplayEpoch RecorderClass::getReplayEpoch() const
{
	if (m_mode == RECORDERMODETYPE_PLAYBACK
		|| m_mode == RECORDERMODETYPE_SIMULATION_PLAYBACK
		|| m_mode == RECORDERMODETYPE_LIVE_OBSERVER
		|| m_mode == RECORDERMODETYPE_RESUME_CATCHUP)
	{
		return m_replayEpoch;
	}
	return REPLAY_EPOCH_CURRENT;
}

Int RecorderClass::getReplayLegacyUpgradeKeyDelta() const
{
	if (m_mode == RECORDERMODETYPE_PLAYBACK
		|| m_mode == RECORDERMODETYPE_SIMULATION_PLAYBACK
		|| m_mode == RECORDERMODETYPE_LIVE_OBSERVER
		|| m_mode == RECORDERMODETYPE_RESUME_CATCHUP)
	{
		return m_replayLegacyUpgradeKeyDelta;
	}
	return 0;
}

/**
 * Initialization
 * The recorder will record by default since every game will be recorded.
 * Obviously a game that is being played back will not be recorded.
 * Since the playback is done through a special interface, that interface
 * will set the recorder mode to RECORDERMODETYPE_PLAYBACK.
 */
void RecorderClass::init() {
	if (m_mode == RECORDERMODETYPE_LIVE_OBSERVER)
		OBS_LOG("LiveObserver: recorder init() while live observing, state wiped");
	m_originalGameMode = GAME_NONE;
	m_mode = RECORDERMODETYPE_NONE;
	m_file = nullptr;
	m_fileName.clear();
	m_currentFilePosition = 0;
	m_gameInfo.clearSlotList();
	m_gameInfo.reset();
	if (TheGlobalData->m_pendingFile.isEmpty())
		m_gameInfo.setMap(TheGlobalData->m_mapName);
	else
		m_gameInfo.setMap(TheGlobalData->m_pendingFile);
	m_gameInfo.setSeed(GetGameLogicRandomSeed());
	m_wasDesync = FALSE;
	m_doingAnalysis = FALSE;
	m_playbackFrameCount = 0;
	m_replayAIFeatureVersion = ZULU_AI_FEATURE_CURRENT;
	m_replayEpoch = REPLAY_EPOCH_CURRENT;
	m_replayLegacyUpgradeKeyDelta = 0;
	m_playbackStaleObjectRefs = 0;

	m_replayShortRead             = FALSE;
	m_replayReadPos               = 0;
	m_liveObserverStreamOpen      = FALSE;
	m_liveObserverArming          = FALSE;
	m_liveObserverWaitingForBytes = FALSE;
	m_liveObserverRetryPos        = 0;
	m_liveObserverFpsBoosted      = FALSE;
	m_liveObserverSavedFpsLimit   = 0;
	m_liveObserverStarvedSinceMs  = 0;
	m_liveObserverSavedUseFpsLimit = TRUE;
	m_liveObserverKnownLength     = 0;
	m_liveObserverBytesArrived    = FALSE;
	m_liveObserverTruncated       = FALSE;
	m_liveObserverLastReopenMs    = 0;
	m_liveObserverEdgeMarginBytes = 2048;
	m_liveObserverPlayedAny       = FALSE;
	clearResumeState();

	OptionPreferences optionPref;
	m_archiveReplays = optionPref.getArchiveReplaysEnabled();
}

/**
 * Reset the recorder to the "initialized state."
 */
void RecorderClass::reset() {
	if (m_file != nullptr) {
		m_file->close();
		m_file = nullptr;
	}
	m_fileName.clear();

	init();
}

/**
 * update
 * Do the update for this frame.
 */
void RecorderClass::update() {
	// TheSuperHackers @feature bill-rich 15/09/2026 Live observing: keep the replay fast-forward off. Racing
	// past the live edge would starve playback and desync the viewer from the
	// stream; the snapshot is already drained at a boosted rate.
	if (m_mode == RECORDERMODETYPE_LIVE_OBSERVER && TheGlobalData && TheGlobalData->m_TiVOFastMode)
	{
		TheWritableGlobalData->m_TiVOFastMode = FALSE;
		if (TheInGameUI)
			TheInGameUI->messageNoFormat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:LiveObserverNoFF",
				L"Fast Forward is unavailable while observing a live game."));
	}

	if (m_mode == RECORDERMODETYPE_RESUME_CATCHUP) {
		// TheSuperHackers @feature bill-rich 15/09/2026 Inject the resume source's commands for this
		// frame, then record them like any other frame: the new recording is a normal replay of
		// the resumed match from frame 0. The handoff (or an abort) switches the mode inside.
		updateResumeCatchup();
		if (m_mode != RECORDERMODETYPE_NONE)
			updateRecord();
	}
	else if (m_mode == RECORDERMODETYPE_RECORD || m_mode == RECORDERMODETYPE_NONE) {
		updateRecord();
	}
	else if (isPlaybackMode()) {
		updatePlayback();
	}
}

/**
 * Do the update for the next frame of this playback.
 */
void RecorderClass::updatePlayback() {
	// Remove any bad commands that have been inserted by the local user that shouldn't be
	// executed during playback.
	CullBadCommandsResult result = cullBadCommands();

	if (result.hasClearGameDataMessage) {
		// TheSuperHackers @bugfix Stop appending more commands if the replay playback is about to end.
		// Previously this would be able to append more commands, which could have unintended consequences,
		// such as crashing the game when a MSG_PLACE_BEACON is appended after MSG_CLEAR_GAME_DATA.
		// MSG_CLEAR_GAME_DATA is supposed to be processed later this frame, which will then stop this playback.
		return;
	}

	// TheSuperHackers @feature bill-rich 15/09/2026 Live observing: if the last read hit the end of the
	// growing file, retry now. New bytes appended by the stream are not visible to
	// the engine File's buffered reader until it is closed and reopened.
	if (isLiveObserverMode() && m_liveObserverWaitingForBytes)
	{
		if (!m_liveObserverStreamOpen)
		{
			// the stream ended: the match is over, finish like a normal replay
			OBS_LOG("LiveObserver: stream closed at the live edge; ending playback");
			m_liveObserverWaitingForBytes = FALSE;
			m_nextFrame = -1;
			stopPlayback();
			return;
		}
		// A live match always produces bytes within seconds (logic CRC messages are
		// recorded on an interval even when nobody issues orders), so a long silence
		// with the stream still open means the host is gone. The window has to cover
		// the longest legitimate silence: watching a match from its first seconds means
		// waiting through the host's whole blocking map load before the first command.
		const UnsignedInt LIVE_OBSERVER_STARVATION_TIMEOUT_MS = 120000;
		const UnsignedInt nowMs = timeGetTime();
		if (m_liveObserverStarvedSinceMs == 0)
		{
			m_liveObserverStarvedSinceMs = nowMs;
		}
		else if (nowMs - m_liveObserverStarvedSinceMs > LIVE_OBSERVER_STARVATION_TIMEOUT_MS)
		{
			OBS_LOG("LiveObserver: starved for %u ms with the stream open; ending playback", nowMs - m_liveObserverStarvedSinceMs);
			m_liveObserverWaitingForBytes = FALSE;
			m_nextFrame = -1;
			stopPlayback();
			return;
		}
		if (m_file == nullptr)
		{
			m_liveObserverWaitingForBytes = FALSE;
			m_nextFrame = -1;
			return;
		}
		// Reopen only once the transport says it appended something (or once a second in
		// case it never says), not every logic tick.
		const UnsignedInt LIVE_OBSERVER_REOPEN_FALLBACK_MS = 1000;
		if (!m_liveObserverBytesArrived && nowMs - m_liveObserverLastReopenMs < LIVE_OBSERVER_REOPEN_FALLBACK_MS)
			return;
		m_liveObserverBytesArrived = FALSE;
		m_liveObserverLastReopenMs = nowMs;
		AsciiString fname = m_file->getName();
		m_file->close();
		m_file = TheFileSystem->openFile(fname.str(), File::READ | File::BINARY);
		if (m_file == nullptr)
		{
			OBS_LOG("LiveObserver: could not reopen %s", fname.str());
			m_liveObserverWaitingForBytes = FALSE;
			m_nextFrame = -1;
			return;
		}
		m_file->seek(m_liveObserverRetryPos, File::START);
		m_replayReadPos = m_liveObserverRetryPos;
		// Retry the read. If it still fails, readNextFrame re-arms the wait and we try
		// again next tick.
		m_liveObserverWaitingForBytes = FALSE;
		readNextFrame();
		if (m_liveObserverWaitingForBytes)
			return;
		m_liveObserverStarvedSinceMs = 0;
	}

	if (m_nextFrame == -1) {
		// This is reached if there are no more commands to be executed.
		return;
	}
	UnsignedInt curFrame = TheGameLogic->getFrame();
	if (m_doingAnalysis)
		curFrame = m_nextFrame;

	// While there are commands to be queued up for this frame, do it.
	while (m_nextFrame == curFrame) {
		appendNextCommand();	// append the next command to TheCommandQueue
		readNextFrame();	// Read the next command's frame number for playback.
		m_liveObserverPlayedAny = TRUE;
		// live observing: the file ran out mid-frame, let the retry path run next tick
		if (isLiveObserverMode() && m_liveObserverWaitingForBytes)
			break;
	}

	// Live observing: drain the backlog fast, but stop a little short of the live edge
	// and play the rest at normal speed, so the viewer keeps a small buffer instead of
	// stalling on every chunk boundary; boost again if the backlog builds back up.
	// Not before the first record has been consumed: until then the frame counter still
	// belongs to the shell map and the transport has no rate estimate yet.
	if (isLiveObserverMode() && TheFramePacer && m_liveObserverPlayedAny)
	{
		// The margin is a few seconds of match at the stream's observed byte rate (the
		// transport keeps it updated); a fixed byte count would be minutes on an idle map.
		const Int edgeMarginBytes = m_liveObserverEdgeMarginBytes;
		const Int reboostBytes = edgeMarginBytes * 4;
		const Int backlog = m_liveObserverKnownLength - m_replayReadPos;
		if (m_liveObserverFpsBoosted && (m_liveObserverWaitingForBytes || backlog < edgeMarginBytes))
		{
			OBS_LOG("LiveObserver: near the live edge at frame %u (backlog %d bytes); restoring FPS limit %d", TheGameLogic->getFrame(), backlog, m_liveObserverSavedFpsLimit);
			setLiveObserverBoost(FALSE);
		}
		else if (!m_liveObserverFpsBoosted && !m_liveObserverWaitingForBytes && backlog > reboostBytes)
		{
			OBS_LOG("LiveObserver: backlog %d bytes at frame %u; boosting again", backlog, TheGameLogic->getFrame());
			setLiveObserverBoost(TRUE);
		}
	}
}

/**
 * Run the logic as fast as the machine allows (TRUE) or at the user's frame rate (FALSE).
 * The saved user settings are restored on the way down.
 */
void RecorderClass::setLiveObserverBoost(Bool boost) {
	if (boost == m_liveObserverFpsBoosted || TheFramePacer == nullptr)
		return;
	if (boost)
	{
		// Never save a 0: FrameRateLimit::wait would spin forever restoring it.
		m_liveObserverSavedFpsLimit = TheFramePacer->getFramesPerSecondLimit();
		if (m_liveObserverSavedFpsLimit <= 0)
			m_liveObserverSavedFpsLimit = LOGICFRAMES_PER_SECOND;
		m_liveObserverSavedUseFpsLimit = TheGlobalData->m_useFpsLimit;
		TheFramePacer->setUncappedForCatchup(TRUE);
	}
	else
	{
		TheFramePacer->setUncappedForCatchup(FALSE);
		TheFramePacer->setFramesPerSecondLimit(m_liveObserverSavedFpsLimit);
		TheWritableGlobalData->m_useFpsLimit = m_liveObserverSavedUseFpsLimit;
	}
	m_liveObserverFpsBoosted = boost;
}

void RecorderClass::noteLiveObserverBytes(Int fileLength) {
	if (fileLength > m_liveObserverKnownLength)
		m_liveObserverKnownLength = fileLength;
	m_liveObserverBytesArrived = TRUE;
}

void RecorderClass::setLiveObserverEdgeMargin(Int bytes) {
	const Int LIVE_OBSERVER_EDGE_MARGIN_MIN = 256;
	const Int LIVE_OBSERVER_EDGE_MARGIN_MAX = 64 * 1024;
	if (bytes < LIVE_OBSERVER_EDGE_MARGIN_MIN)
		bytes = LIVE_OBSERVER_EDGE_MARGIN_MIN;
	if (bytes > LIVE_OBSERVER_EDGE_MARGIN_MAX)
		bytes = LIVE_OBSERVER_EDGE_MARGIN_MAX;
	m_liveObserverEdgeMarginBytes = bytes;
}

/**
 * Stop the currently running playback. This is probably due either to the user exiting out of the playback or
 * reaching the end of the playback file.
 */
void RecorderClass::stopPlayback() {
	if (isLiveObserverMode())
		setLiveObserverBoost(FALSE);
	if (m_file != nullptr) {
		m_file->close();
		m_file = nullptr;
	}
	m_fileName.clear();

	if (!m_doingAnalysis)
	{
		TheGameLogic->exitGame();
	}

	// A Replay Theater process is booted with some older release's data
	// mounted purely to watch one replay, so there is nowhere sensible for it
	// to go afterwards: dropping to the shell would let the player start a
	// live game on rules nobody else has. Quit instead and let ZuluLauncher
	// bring the picker back up.
	//
	// This covers the ways playback ends inside the recorder: the last frame,
	// a short read, the upload trailer. Quitting out from the in-game menu
	// never reaches here -- it returns to the shell instead, which is why
	// Shell::showShell carries the same guard.
	if (TheGlobalData->m_replayTheater && TheGameEngine != nullptr)
	{
		TheGameEngine->setQuitting(TRUE);
	}
}

/**
 * Update function for recording a game. Basically all the pertinent logic commands for this frame are written out
 * to a file.
 */
void RecorderClass::updateRecord()
{
	Bool needFlush = FALSE;
	static Int lastFrame = -1;
	GameMessage* msg = TheCommandList->getFirstMessage();
	while (msg != nullptr) {
		if (msg->getType() == GameMessage::MSG_NEW_GAME &&
			msg->getArgument(0)->integer != GAME_SHELL &&
			msg->getArgument(0)->integer != GAME_SINGLE_PLAYER && // Due to the massive amount of scripts that use <local player> in GC and single player, replays have been cut for them.
			msg->getArgument(0)->integer != GAME_NONE)
		{
			m_originalGameMode = msg->getArgument(0)->integer;
			DEBUG_LOG(("RecorderClass::updateRecord() - original game is mode %d", m_originalGameMode));
			lastFrame = 0;
			GameDifficulty diff = DIFFICULTY_NORMAL;
			if (msg->getArgumentCount() >= 2)
				diff = (GameDifficulty)msg->getArgument(1)->integer;
			Int rankPoints = 0;
			if (msg->getArgumentCount() >= 3)
				rankPoints = msg->getArgument(2)->integer;
			Int maxFPS = 0;
			if (msg->getArgumentCount() >= 4)
				maxFPS = msg->getArgument(3)->integer;

			// TheSuperHackers @feature bill-rich 15/09/2026 The new recording starts as for any game.
			// When the lobby's start path armed a resume-from-replay (ResumeFromReplay::prepareGameStart
			// validated this client's copy of the source), the source is then replayed into it.
			// startRecording resets the recorder, so the arming is carried across by hand. A source
			// that cannot be opened leaves the game: a client that silently recorded a fresh match
			// would desync everyone else.
			const UnsignedInt armedHandoffFrame = m_resumeArmedHandoffFrame;
			const UnsignedInt armedSourceLastFrame = m_resumeArmedSourceLastFrame;
			startRecording(diff, m_originalGameMode, rankPoints, maxFPS);
			if (armedHandoffFrame != 0)
			{
				armResumeForNextGame(armedHandoffFrame, armedSourceLastFrame);
				if (startResumeCatchup())
					DEBUG_LOG(("RecorderClass::updateRecord() - resume catchup to frame %u", armedHandoffFrame));
				else
					abortResume(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeSourceUnreadable", L"Resume aborted: the resume replay could not be opened"));
			}
		}
		else if (msg->getType() == GameMessage::MSG_CLEAR_GAME_DATA) {
			if (m_file != nullptr) {
				lastFrame = -1;
				writeToFile(msg);
				stopRecording();
				needFlush = FALSE;
			}
			m_fileName.clear();
			clearResumeState();
		}
		else {
			if (m_file != nullptr) {
				if ((msg->getType() > GameMessage::MSG_BEGIN_NETWORK_MESSAGES) &&
					(msg->getType() < GameMessage::MSG_END_NETWORK_MESSAGES)) {
					// Only write the important messages to the file.
					writeToFile(msg);
					needFlush = TRUE;
				}
			}
		}
		msg = msg->next();
	}

	if (needFlush) {
		DEBUG_ASSERTCRASH(m_file != nullptr, ("RecorderClass::updateRecord() - unexpected call to fflush(m_file)"));
		m_file->flush();
	}
}

/**
 * Start a new file for recording. This will always overwrite the "LastReplay.rep" file with the new one.
 * So don't call this unless you really mean it.
 */
void RecorderClass::startRecording(GameDifficulty diff, Int originalGameMode, Int rankPoints, Int maxFPS) {
	DEBUG_ASSERTCRASH(m_file == nullptr, ("Starting to record game while game is in progress."));

	reset();

	m_mode = RECORDERMODETYPE_RECORD;

	AsciiString filepath = getReplayDir();

	// We have to make sure the replay dir exists.
	TheFileSystem->createDirectory(filepath);

	m_fileName = getLastReplayFileName();
	m_fileName.concat(getReplayExtention());
	filepath.concat(m_fileName);
	m_file = TheFileSystem->openFile(filepath.str(), File::WRITE | File::BINARY);
	if (m_file == nullptr) {
		DEBUG_ASSERTCRASH(m_file != nullptr, ("Failed to create replay file"));
		return;
	}
	// TheSuperHackers @info the null terminator needs to be ignored to maintain retail replay file layout
	m_file->writeFormat("%s", s_genrep);

	//
	// save space for stats to be filled in.
	//
	// **** if this changes, change the LAN code above ****
	//
	replay_time_t time = 0;
	m_file->write(&time, sizeof(time));	// reserve space for start time
	m_file->write(&time, sizeof(time));	// reserve space for end time

	UnsignedInt frames = 0;
	m_file->write(&frames, sizeof(frames));	// reserve space for duration in frames

	Bool flag = FALSE;
	m_file->write(&flag, sizeof(flag));	// reserve space for flag (true if we desync)
	m_file->write(&flag, sizeof(flag));	// reserve space for flag (true if we quit early)
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_file->write(&flag, sizeof(flag));	// reserve space for flag (true if player i disconnects)
	}

	// Print out the name of the replay.
	UnicodeString replayName;
	replayName = TheGameText->fetch("GUI:LastReplay");
	m_file->writeFormat(L"%s", replayName.str());
	m_file->writeChar(L"\0");

	// Date and Time
	SYSTEMTIME systemTime;
	GetLocalTime(&systemTime);
	m_file->write(&systemTime, sizeof(systemTime));

	// write out version info
	UnicodeString versionString = TheVersion->getUnicodeVersion();
	UnicodeString versionTimeString = TheVersion->getUnicodeBuildTime();
	UnsignedInt versionNumber = TheVersion->getVersionNumber();
	m_file->writeFormat(L"%s", versionString.str());
	m_file->writeChar(L"\0");
	m_file->writeFormat(L"%s", versionTimeString.str());
	m_file->writeChar(L"\0");
	m_file->write(&versionNumber, sizeof(versionNumber));
	m_file->write(&(TheGlobalData->m_exeCRC), sizeof(TheGlobalData->m_exeCRC));
	m_file->write(&(TheGlobalData->m_iniCRC), sizeof(TheGlobalData->m_iniCRC));

	// Number of players
	/*
	Int numPlayers = ThePlayerList->getPlayerCount();
	fwrite(&numPlayers, sizeof(numPlayers), 1, m_file);
	*/

	// Write the slot list.
	AsciiString theSlotList;
	Int localIndex = -1;
	if (TheNetwork)
	{
		if (TheLAN)
		{
			GameInfo* game = TheLAN->GetMyGame();
			DEBUG_ASSERTCRASH(game, ("Starting a LAN game with no LANGameInfo object!"));
			theSlotList = GameInfoToAsciiString(game);

			for (Int i = 0; i < MAX_SLOTS; ++i)
			{
				if (game->getLocalIP() == game->getSlot(i)->getIP())
				{
					localIndex = i;
					break;
				}
			}
		}
		else
		{
#if defined(GENERALS_ONLINE)
			theSlotList = GameInfoToAsciiString(TheNGMPGame);
			localIndex = TheNGMPGame->getLocalSlotNum();
#else
			theSlotList = GameInfoToAsciiString(TheGameSpyGame);
			localIndex = TheGameSpyGame->getLocalSlotNum();
#endif
		}
	}
	else
	{
		if (TheSkirmishGameInfo)
		{
			TheSkirmishGameInfo->setCRCInterval(REPLAY_CRC_INTERVAL);
			theSlotList = GameInfoToAsciiString(TheSkirmishGameInfo);
			DEBUG_LOG(("GameInfo String: %s", theSlotList.str()));
			localIndex = 0;
		}
		else
		{
			// single player.  format the generic (empty) slotlist
			m_gameInfo.setCRCInterval(REPLAY_CRC_INTERVAL);
			theSlotList = GameInfoToAsciiString(&m_gameInfo);
		}
	}
	logGameStart(theSlotList);
	DEBUG_LOG(("RecorderClass::startRecording - theSlotList = %s", theSlotList.str()));

	// write slot list (starting spots, color, alliances, etc
	m_file->writeFormat("%s", theSlotList.str());
	m_file->writeChar("\0");

	m_file->writeFormat("%d", localIndex);
	m_file->writeChar("\0");

	/*
	/// @todo fix this to use starting spots and player alliances when those are put in the game.
	for (Int i = 0; i < numPlayers; ++i) {
		Player *player = ThePlayerList->getNthPlayer(i);
		if (player == nullptr) {
			continue;
		}
		UnicodeString name = player->getPlayerDisplayName();
		fwprintf(m_file, L"%s", name.str());
		fputwc(0, m_file);
		UnicodeString faction = player->getFaction()->getFactionDisplayName();
		fwprintf(m_file, L"%s", faction.str());
		fputwc(0, m_file);
		Int color = player->getColor()->getAsInt();
		fwrite(&color, sizeof(color), 1, m_file);
		Int team = 0;
		Int startingSpot = 0;
		fwrite(&startingSpot, sizeof(Int), 1, m_file);
		fwrite(&team, sizeof(Int), 1, m_file);
	}
	*/

	// Write the game difficulty.
	m_file->write(&diff, sizeof(diff));

	// Write original game mode
	m_file->write(&originalGameMode, sizeof(originalGameMode));

	// Write rank points to add at game start
	m_file->write(&rankPoints, sizeof(rankPoints));

	// Write maxFPS chosen
	m_file->write(&maxFPS, sizeof(maxFPS));

	// Zulu replay extension. Tags this replay with the AI feature version
	// the recording binary supports, so older binaries reject the file via
	// exeCRC mismatch and newer binaries can identify what features to run.
	writeZuluReplayExtension();

	DEBUG_LOG(("RecorderClass::startRecording() - diff=%d, mode=%d, FPS=%d", diff, originalGameMode, maxFPS));

	/*
	// Write the map name.
	fprintf(m_file, "%s", (TheGlobalData->m_mapName).str());
	fputc(0, m_file);
	*/

	/// @todo Need to write game options when there are some to be written.
}

/**
 * This will stop the current recording session and close the file. This should always be called at the end of
 * every game.
 */
void RecorderClass::stopRecording() {
	logGameEnd();

	// Release diagnostics (always compiled): mark the end of the match in the
	// uploadable release log, with one last NETSTAT so the final seconds are
	// covered even when the periodic line is up to 10 s stale.
	if (TheNetwork != nullptr && TheNetwork->GetConnectionManager() != nullptr)
		TheNetwork->GetConnectionManager()->flushNetStats("match end");
	ReleaseLog("=== Match end: frame=%d desync=%d ===",
		TheGameLogic ? TheGameLogic->getFrame() : -1, (int)m_wasDesync);
	if (TheNetwork)
	{
		//if (TheLAN)
		{
			if (m_wasDesync)
				cleanUpReplayFile();
			m_wasDesync = FALSE;
		}
	}
	if (m_file != nullptr) {
		m_file->close();
		m_file = nullptr;

		if (m_archiveReplays)
			archiveReplay(m_fileName);
		// GeneralsOnline's own replay upload. Compiled out by default in this
		// fork - see GENERALS_ONLINE_DISABLE_REPLAY_UPLOAD in
		// NextGenMP_defines.h - because the telemetry block below ships the
		// same file to radarvan. The call is left in place so the GO path is
		// one #define away.
#if defined(GENERALS_ONLINE)
		if (TheNGMPGame != nullptr)
		{
			NGMP_OnlineServicesManager* pOnlineServicesMgr = NGMP_OnlineServicesManager::GetInstance();
			if (pOnlineServicesMgr != nullptr)
			{
				AsciiString absoluteReplayPath = getReplayDir();
				absoluteReplayPath.concat(m_fileName);

				pOnlineServicesMgr->CommitReplay(absoluteReplayPath);
			}
		}
#endif
	}

	// Project telemetry (cncstats + radarvan): stats JSON, the replay, this
	// client's per-match logs, and any map assets the server is missing.
	//
	// Fires for every client in a LAN/online multiplayer game - the conditions
	// under which StatsExporterBeginRecording was called in
	// GameLogic::startNewGame. For replay viewers, live spectators and
	// single-player campaigns the exporter was never started, so
	// StatsExporterIsActive() is FALSE and every channel below is skipped.
	//
	// The replay file has already been closed above, so the bytes on disk are
	// complete before the worker snapshots them.
	if (!m_fileName.isEmpty())
	{
		// Capture this first: ExportGameStatsJSON clears the active flag as it
		// finishes, so asking afterwards would always say FALSE.
		const bool wasCollecting = StatsExporterIsActive();

		// Write the stats JSON on this (main) thread - it reads live game
		// state - but defer the actual upload to the background telemetry
		// worker below, so end-of-match never blocks on HTTP.
		const AsciiString statsFilePath = ExportGameStatsJSON(getReplayDir(), m_fileName, FALSE);

		// Telemetry uploads only fire if the game had at least two human
		// players. Mirrors the gate inside ExportGameStatsJSON for the stats
		// upload, so all channels behave consistently. Logged once here so the
		// skip is visible when debugging upload behaviour.
		const bool hasMinHumans = StatsExporterHasMinHumansForUpload();
		if (wasCollecting && !hasMinHumans)
		{
			printf("[telemetry] Skipping replay/map upload: fewer than 2 human players\n");
			fflush(stdout);
		}

		// Local player identity for the uploads below (replay + logs). The
		// lobby display name is UTF-8 encoded for the replay upload's optional
		// form field; the slot number is a stable fallback id for the
		// per-player log grouping. Computed once, under the shared gate.
		AsciiString playerNameUtf8;
		Int localPlayerSlot = -1;
		if (wasCollecting && hasMinHumans && TheGameInfo != nullptr)
		{
			localPlayerSlot = TheGameInfo->getLocalSlotNum();
			if (localPlayerSlot >= 0)
			{
				const GameSlot *s = TheGameInfo->getConstSlot(localPlayerSlot);
				if (s != nullptr)
					appendUtf8(playerNameUtf8, s->getName());
			}
		}

		// Gather every channel into one request and hand it to the background
		// worker. Everything below only reads main-thread-only state
		// (TheGameInfo / TheMapCache / TheGlobalData); StartMatchTelemetryUpload
		// snapshots the volatile files (replay, logs) before it returns, so the
		// next game is free to overwrite them while the upload is in flight.
		if (wasCollecting && hasMinHumans)
		{
			MatchTelemetryUpload up;
			up.seed = GetGameLogicRandomSeed();
			up.mapCRC = 0;
			up.mapContentsMask = 0;

			// Stats: the gzipped JSON we just wrote (upload deferred here).
			if (!TheGlobalData->m_statsUrl.isEmpty() && !statsFilePath.isEmpty())
			{
				up.statsUrl = TheGlobalData->m_statsUrl;
				up.statsFilePath = statsFilePath;
			}

			// Replay.
			if (!TheGlobalData->m_replayUrl.isEmpty())
			{
				AsciiString replayPath = getReplayDir();
				replayPath.concat(m_fileName);
				up.replayUrl = TheGlobalData->m_replayUrl;
				up.replayFilePath = replayPath;
				up.replayFileName = m_fileName;
				up.playerNameUtf8 = playerNameUtf8;
			}

			// Per-match client logs. The server groups them under
			// <seed>/<player>/, so every client's copy is retrievable together.
			// The debug log only exists in logging builds; the observer log
			// only when this client observed - absent files are skipped by the
			// worker.
			if (!TheGlobalData->m_logsUrl.isEmpty())
			{
				AsciiString playerId = playerNameUtf8;
				if (playerId.isEmpty())
					playerId.format("slot%d", localPlayerSlot);
				up.logsUrl = TheGlobalData->m_logsUrl;
				up.playerId = playerId;
#ifdef DEBUG_LOGGING
				up.logFilePaths.push_back(AsciiString(DebugGetLogFileName())); // absolute path
#endif
				up.logFilePaths.push_back(AsciiString(LANObsGetLogFileName()));   // observer log, in the user data dir
				up.logFilePaths.push_back(AsciiString(ReleaseGetLogFileName()));  // always-on release log, same dir
				// The observer log from before the last relaunch. Absent on a
				// first run, and skipped silently when so. Worth carrying
				// because a player whose spectating just broke restarts the
				// game, which is precisely what truncates the log that would
				// have explained it.
				//
				// Once per run, though: it is a snapshot of a file that is
				// already closed and can no longer change, so every match after
				// the first would re-upload the same bytes under a new match
				// seed.
				static Bool s_prevObserverLogSent = FALSE;
				if (!s_prevObserverLogSent)
				{
					s_prevObserverLogSent = TRUE;
					up.logFilePaths.push_back(AsciiString(LANObsGetPrevLogFileName()));
					// Same deal for the release log and the GeneralsOnline network
					// log of the previous session: a player who froze or crashed
					// and relaunched is the one whose previous logs matter.
					up.logFilePaths.push_back(AsciiString(ReleaseGetPrevLogFileName()));
					up.logFilePaths.push_back(AsciiString(NetworkLogGetPrevFileName()));
				}
				// The GeneralsOnline network log: the lobby, signalling and mesh
				// phase of an online game log only here (the tail is uploaded).
				up.logFilePaths.push_back(AsciiString(NetworkLogGetFileName()));
			}

			// Map check + conditional map upload. Look up the played map's CRC
			// from the cache; the worker asks the server whether it already has
			// it and, if not, uploads the .map plus every sidecar on disk.
			// 0xFE = all sidecar bits set (2|4|8|16|32|64).
			if (!TheGlobalData->m_mapCheckUrl.isEmpty() && TheMapCache != nullptr)
			{
				AsciiString mapName = TheGlobalData->m_mapName;
				const MapMetaData *md = TheMapCache->findMap(mapName);
				if (md == nullptr || md->m_CRC == 0)
				{
					printf("[map] No cached metadata for \"%s\", skipping map check\n", mapName.str());
					fflush(stdout);
				}
				else
				{
					up.mapCheckUrl = TheGlobalData->m_mapCheckUrl;
					up.mapAssetsUrl = TheGlobalData->m_mapAssetsUrl;
					up.mapUploadUrl = TheGlobalData->m_mapUploadUrl;
					up.mapCRC = md->m_CRC;
					up.mapFilePath = md->m_fileName;
					up.mapContentsMask = 0xFE;
				}
			}

			StartMatchTelemetryUpload(up);
		}
	}

	m_fileName.clear();
}

/**
 * TheSuperHackers @feature Stubbjax 17/10/2025 Copy the replay file to the archive directory and rename it using the current timestamp.
 */
void RecorderClass::archiveReplay(AsciiString fileName)
{
	SYSTEMTIME st;
	GetLocalTime(&st);

	AsciiString archiveFileName;
	// Use a standard YYYYMMDD_HHMMSS format for simplicity and to avoid conflicts.
	archiveFileName.format("%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

	AsciiString extension = getReplayExtention();
	AsciiString sourcePath = getReplayDir();
	sourcePath.concat(fileName);

	if (!sourcePath.endsWith(extension))
		sourcePath.concat(extension);

	AsciiString destPath = getReplayArchiveDir();
	TheFileSystem->createDirectory(destPath.str());

	destPath.concat(archiveFileName);
	destPath.concat(extension);

	if (!CopyFile(sourcePath.str(), destPath.str(), FALSE))
		DEBUG_LOG(("RecorderClass::archiveReplay: Failed to copy %s to %s", sourcePath.str(), destPath.str()));
}

/**
 * Write this game message to the record file. This also writes the game message's execution frame.
 */
void RecorderClass::writeToFile(GameMessage* msg) {
	// Write the frame number for this command.
	UnsignedInt frame = TheGameLogic->getFrame();
	m_file->write(&frame, sizeof(frame));

	// Write the command type
	GameMessage::Type type = msg->getType();
	m_file->write(&type, sizeof(type));

	// Write the player index
	Int playerIndex = msg->getPlayerIndex();
	m_file->write(&playerIndex, sizeof(playerIndex));

#ifdef DEBUG_LOGGING
	AsciiString commandName = msg->getCommandAsString();
	if (type < GameMessage::MSG_BEGIN_NETWORK_MESSAGES || type > GameMessage::MSG_END_NETWORK_MESSAGES)
	{
		commandName.concat(" (Non-Network message!)");
	}
	else if (type == GameMessage::MSG_BEGIN_NETWORK_MESSAGES)
	{
		AsciiString tmp;
		tmp.format(" (CRC 0x%8.8X)", msg->getArgument(0)->integer);
		commandName.concat(tmp);
	}

	//DEBUG_LOG(("RecorderClass::writeToFile - Adding %s command from player %d to TheCommandList on frame %d",
		//commandName.str(), msg->getPlayerIndex(), TheGameLogic->getFrame()));
#endif // DEBUG_LOGGING

	GameMessageParser* parser = newInstance(GameMessageParser)(msg);
	UnsignedByte numTypes = parser->getNumTypes();
	m_file->write(&numTypes, sizeof(numTypes));

	GameMessageParserArgumentType* argType = parser->getFirstArgumentType();
	while (argType != nullptr) {
		UnsignedByte type = (UnsignedByte)(argType->getType());
		m_file->write(&type, sizeof(type));

		UnsignedByte argTypeCount = (UnsignedByte)(argType->getArgCount());
		m_file->write(&argTypeCount, sizeof(argTypeCount));

		argType = argType->getNext();
	}

	const size_t argsCount = msg->getArgumentCount();

	for (size_t i = 0; i < argsCount; ++i) {
		GameMessageArgumentDataType argType = msg->getArgumentDataType(i);
		const GameMessageArgumentType* arg = msg->getArgument(i);
		writeArgument(argType, *arg);
	}

	deleteInstance(parser);
	parser = nullptr;

}

void RecorderClass::writeArgument(GameMessageArgumentDataType type, const GameMessageArgumentType arg) {

	switch (type) {

	case ARGUMENTDATATYPE_INTEGER:
		m_file->write(&(arg.integer), sizeof(arg.integer));
		break;
	case ARGUMENTDATATYPE_REAL:
		m_file->write(&(arg.real), sizeof(arg.real));
		break;
	case ARGUMENTDATATYPE_BOOLEAN:
		m_file->write(&(arg.boolean), sizeof(arg.boolean));
		break;
	case ARGUMENTDATATYPE_OBJECTID:
		m_file->write(&(arg.objectID), sizeof(arg.objectID));
		break;
	case ARGUMENTDATATYPE_DRAWABLEID:
		m_file->write(&(arg.drawableID), sizeof(arg.drawableID));
		break;
	case ARGUMENTDATATYPE_TEAMID:
		m_file->write(&(arg.teamID), sizeof(arg.teamID));
		break;
	case ARGUMENTDATATYPE_LOCATION:
		m_file->write(&(arg.location), sizeof(arg.location));
		break;
	case ARGUMENTDATATYPE_PIXEL:
		m_file->write(&(arg.pixel), sizeof(arg.pixel));
		break;
	case ARGUMENTDATATYPE_PIXELREGION:
		m_file->write(&(arg.pixelRegion), sizeof(arg.pixelRegion));
		break;
	case ARGUMENTDATATYPE_TIMESTAMP:
		m_file->write(&(arg.timestamp), sizeof(arg.timestamp));
		break;
	case ARGUMENTDATATYPE_WIDECHAR:
		m_file->write(&(arg.wChar), sizeof(arg.wChar));
		break;
	default:
		DEBUG_LOG(("Unknown GameMessageArgumentDataType in RecorderClass::writeArgument"));
		break;
	}
}

/**
 * Read in a replay header, for (1) populating a replay listbox or (2) starting playback.  In
 * case (2), set FILE *m_file.
 */
Bool RecorderClass::readReplayHeader(ReplayHeader& header)
{
	AsciiString filepath = getReplayDir();
	filepath.concat(header.filename.str());

	// TheSuperHackers @performance More buffered data reduces disk overhead and will improve fast forward playback
	const UnsignedInt buffersize = header.forPlayback ? replayBufferBytes : File::BUFFERSIZE;
	m_file = TheFileSystem->openFile(filepath.str(), File::READ | File::BINARY, buffersize);

	if (m_file == nullptr)
	{
		DEBUG_LOG(("Can't open %s (%s)", filepath.str(), header.filename.str()));
		return FALSE;
	}

	// Read the GENREP header.
	char genrep[sizeof(s_genrep) - 1] = {0};
	m_file->read( &genrep, sizeof(s_genrep) - 1 );
	if ( strncmp(genrep, s_genrep, sizeof(s_genrep) - 1 ) != 0 ) {
		DEBUG_LOG(("RecorderClass::readReplayHeader - replay file did not have GENREP at the start."));
		m_file->close();
		m_file = nullptr;
		return FALSE;
	}

	// read in some stats
	replay_time_t tmp;
	m_file->read(&tmp, sizeof(tmp));
	header.startTime = tmp;
	m_file->read(&tmp, sizeof(tmp));
	header.endTime = tmp;

	m_file->read(&header.frameCount, sizeof(header.frameCount));

	m_file->read(&header.desyncGame, sizeof(header.desyncGame));
	m_file->read(&header.quitEarly, sizeof(header.quitEarly));
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		m_file->read(&(header.playerDiscons[i]), sizeof(Bool));
	}

	// Read the Replay Name.  We don't actually do anything with it.  Oh well.
	header.replayName = readUnicodeString();

	// Read the date and time.  We don't really do anything with this either. Oh well.
	m_file->read(&header.timeVal, sizeof(header.timeVal));

	// Read in the Version info
	header.versionString = readUnicodeString();
	header.versionTimeString = readUnicodeString();
	m_file->read(&header.versionNumber, sizeof(header.versionNumber));
	m_file->read(&header.exeCRC, sizeof(header.exeCRC));
	m_file->read(&header.iniCRC, sizeof(header.iniCRC));

	// Read in the GameInfo
	header.gameOptions = readAsciiString();
	m_gameInfo.reset();
	m_gameInfo.enterGame();
	DEBUG_LOG(("RecorderClass::readReplayHeader - GameInfo = %s", header.gameOptions.str()));
	if (!ParseAsciiStringToGameInfo(&m_gameInfo, header.gameOptions))
	{
		DEBUG_LOG(("RecorderClass::readReplayHeader - replay file did not have a valid GameInfo string."));
		m_file->close();
		m_file = nullptr;
		return FALSE;
	}
	m_gameInfo.startGame(0);

	AsciiString playerIndex = readAsciiString();
	header.localPlayerIndex = atoi(playerIndex.str());
	if (header.localPlayerIndex < -1 || header.localPlayerIndex >= MAX_SLOTS)
	{
		DEBUG_LOG(("RecorderClass::readReplayHeader - invalid local slot number."));
		m_gameInfo.endGame();
		m_gameInfo.reset();
		m_file->close();
		m_file = nullptr;
		return FALSE;
	}
	if (header.localPlayerIndex >= 0)
	{
		Int localIP = m_gameInfo.getSlot(header.localPlayerIndex)->getIP();
		m_gameInfo.setLocalIP(localIP);
	}

	if (!header.forPlayback)
	{
		m_gameInfo.endGame();
		m_gameInfo.reset();
		m_file->close();
		m_file = nullptr;
	}

	return TRUE;
}

Bool RecorderClass::simulateReplay(AsciiString filename)
{
	Bool success = playbackFile(filename);
	if (success)
		m_mode = RECORDERMODETYPE_SIMULATION_PLAYBACK;
	return success;
}

#if defined(RTS_DEBUG)
Bool RecorderClass::analyzeReplay(AsciiString filename)
{
	m_doingAnalysis = TRUE;
	return playbackFile(filename);
}



#endif

/**
 * TheSuperHackers @feature bill-rich 15/09/2026 Start live observing: normal playbackFile(), then flip to
 * LIVE_OBSERVER and boost the frame rate while the snapshot is drained. The stream
 * transport must have opened the file and call setLiveObserverStreamOpen(FALSE) when
 * the stream ends.
 */
Bool RecorderClass::playbackFileLiveObserver(AsciiString filename)
{
	m_liveObserverArming = TRUE;
	Bool success = playbackFile(filename);
	m_liveObserverArming = FALSE;
	if (!success)
	{
		OBS_LOG("LiveObserver: playbackFile(%s) failed", filename.str());
		return FALSE;
	}

	m_mode = RECORDERMODETYPE_LIVE_OBSERVER;
	// Seed the tracked read offset from the file, which is reliable right after the
	// open; from here on every read updates it, since File::position() is not
	// trustworthy on every file class once reads have failed.
	if (m_file != nullptr && !m_liveObserverWaitingForBytes)
		m_replayReadPos = m_file->position();
	else if (m_liveObserverWaitingForBytes)
		m_replayReadPos = m_liveObserverRetryPos;
	// The open-time read may already have hit EOF (a snapshot of a match that just
	// started); keep that armed wait so updatePlayback's retry loop picks it up.
	if (!m_liveObserverWaitingForBytes)
		m_liveObserverRetryPos = 0;
	// playbackFile() tears the shell down (clearGameData -> reset -> init), which
	// wiped the flag the transport set before calling us. Playback is only ever
	// started from a connected stream, and the transport clears it again within a
	// tick if the stream has already died.
	m_liveObserverStreamOpen = TRUE;
	m_liveObserverStarvedSinceMs = 0;

	// Drain the snapshot fast; updatePlayback drops back to the user's rate near the
	// live edge (see setLiveObserverBoost).
	m_liveObserverTruncated = FALSE;
	setLiveObserverBoost(TRUE);
	OBS_LOG("LiveObserver: playing %s live, file pos %d, waiting=%d", filename.str(), m_file ? m_file->position() : -1, m_liveObserverWaitingForBytes ? 1 : 0);
	return TRUE;
}

Bool RecorderClass::isPlaybackInProgress() const
{
	return isPlaybackMode() && m_nextFrame != -1;
}

AsciiString RecorderClass::getCurrentReplayFilename()
{
	if (isPlaybackMode())
	{
		return m_currentReplayFilename;
	}
	return AsciiString::TheEmptyString;
}

Bool RecorderClass::sawCRCMismatch() const
{
	return m_crcInfo.sawCRCMismatch();
}

void RecorderClass::handleCRCMessage(UnsignedInt newCRC, Int playerIndex, Bool fromPlayback)
{
	if (fromPlayback)
	{
		//DEBUG_LOG(("RecorderClass::handleCRCMessage() - Adding CRC of %X from %d to m_crcInfo", newCRC, playerIndex));
		m_crcInfo.addCRC(newCRC);
		return;
	}

	Int localPlayerIndex = m_crcInfo.getLocalPlayer();
	Bool samePlayer = FALSE;
	AsciiString playerName;
	playerName.format("player%d", localPlayerIndex);
	const Player* p = ThePlayerList->getNthPlayer(playerIndex);
	if (!p || (p->getPlayerNameKey() == NAMEKEY(playerName)))
		samePlayer = TRUE;
	if (samePlayer || (localPlayerIndex < 0))
	{
		UnsignedInt playbackCRC = m_crcInfo.readCRC();
		//DEBUG_LOG(("RecorderClass::handleCRCMessage() - Comparing CRCs of InGame:%8.8X Replay:%8.8X Frame:%d from Player %d",
		//	playbackCRC, newCRC, TheGameLogic->getFrame()-m_crcInfo.GetQueueSize()-1, playerIndex));

		// Replays recorded by retail-lineage clients ("Version 1.04"/"1.06" headers) carry
		// Checksum values that never match a re-simulated CRC at any frame offset -- retail's
		// own playback had the same permanent mismatch, which is why the warning below was
		// disabled in 2003. A faithfully re-simulated retail game (verified by outcome) still
		// mismatches 100% of its checkpoints, so for the retail epoch these values carry no
		// information: drain the queue and leave the verdict to the completion and
		// stale-reference checks in ReplaySimulation. Fork-recorded replays keep the full
		// strict check below.
		if (getReplayEpoch() == REPLAY_EPOCH_RETAIL)
			return;

		if (TheGameLogic->getFrame() > 0 && newCRC != playbackCRC && !m_crcInfo.sawCRCMismatch())
		{
			//Kris: Patch 1.01 November 10, 2003 (integrated changes from Matt Campbell)
			// Since we don't seem to have any *visible* desyncs when replaying games, but get this warning
			// virtually every replay, the assumption is our CRC checking is faulty.  Since we're at the
			// tail end of patch season, let's just disable the message, and hope the users believe the
			// problem is fixed. -MDC 3/20/2003
			//
			// TheSuperHackers @tweak helmutbuhler 03/04/2025
			// More than 20 years later, but finally fixed and re-enabled!
			TheInGameUI->message("GUI:CRCMismatch");

			// TheSuperHackers @info helmutbuhler 03/04/2025
			// Note: We subtract the queue size from the frame number. This way we calculate the correct frame
			// the mismatch first happened in case the NetCRCInterval is set to 1 during the game.
			const UnsignedInt mismatchFrame = TheGameLogic->getFrame() - m_crcInfo.GetQueueSize() - 1;

			// Now also prints a UI message for it.
			const UnicodeString mismatchDetailsStr = TheGameText->FETCH_OR_SUBSTITUTE("GUI:CRCMismatchDetails", L"InGame:%8.8X Replay:%8.8X Frame:%d");
			TheInGameUI->message(mismatchDetailsStr, playbackCRC, newCRC, mismatchFrame);

			DEBUG_LOG(("Replay has gone out of sync!\nInGame:%8.8X Replay:%8.8X\nFrame:%d",
				playbackCRC, newCRC, mismatchFrame));

			// Print Mismatch in case we are simulating replays from console.
			printf("CRC Mismatch in Frame %d\n", mismatchFrame);

			// TheSuperHackers @tweak Pause the game on mismatch.
			// But not when a window with focus is opened, because that can make resuming difficult.
			if (TheWindowManager->winGetFocus() == nullptr)
			{
				Bool pause = TRUE;
				Bool pauseMusic = FALSE;
				Bool pauseInput = FALSE;
				TheGameLogic->setGamePaused(pause, pauseMusic, pauseInput);

				// Mark this mismatch as seen when we had the chance to pause once.
				m_crcInfo.setSawCRCMismatch();
			}
		}
		return;
	}

	//DEBUG_LOG(("RecorderClass::handleCRCMessage() - Skipping CRC of %8.8X from %d (our index is %d)", newCRC, playerIndex, localPlayerIndex));
}

/**
 * Returns true if this version of the file is the same as our version of the game
 */
Bool RecorderClass::replayMatchesGameVersion(AsciiString filename)
{
	ReplayHeader header;
	header.forPlayback = TRUE;
	header.filename = filename;
	if (readReplayHeader(header))
	{
		return replayMatchesGameVersion(header);
	}
	return FALSE;
}

Bool RecorderClass::replayMatchesGameVersion(const ReplayHeader& header)
{
	// TheSuperHackers @fix No longer checks the build time here to prevent incorrect Replay playback incompatibility messages when the Replay playback would actually be technically compatible.
	if (header.versionString != TheVersion->getUnicodeVersion())
		return false;
	if (header.versionNumber != TheVersion->getVersionNumber())
		return false;
	if (header.exeCRC != TheGlobalData->m_exeCRC)
		return false;
	if (header.iniCRC != TheGlobalData->m_iniCRC)
		return false;
	return true;
}

/**
 * Start playback of the file. Return true or false depending on if the file is
 * a valid replay file or not.
 */
Bool RecorderClass::playbackFile(AsciiString filename)
{
	if (!m_doingAnalysis)
	{
		if (TheGameLogic->isInGame())
		{
			TheGameLogic->clearGameData();
		}
	}

	ReplayHeader header;
	header.forPlayback = TRUE;
	header.filename = filename;
	Bool success = readReplayHeader(header);
	if (!success)
	{
		return FALSE;
	}

	// Select the determinism epoch to simulate: the -replayEpoch override wins,
	// otherwise auto-detect from the recorded version string. Gated sim paths
	// read this via getReplayEpoch() to reproduce older behavior bit-exactly.
	m_replayEpoch = (s_replayEpochOverride >= 0)
		? (ReplayEpoch)s_replayEpochOverride
		: deriveReplayEpochFromHeader(header);
	m_replayLegacyUpgradeKeyDelta = legacyUpgradeKeyDeltaFromHeader(header);
	DEBUG_LOG(("RecorderClass::playbackFile - replay determinism epoch = %d (override=%d)",
		(Int)m_replayEpoch, s_replayEpochOverride));

#ifdef DEBUG_CRASHING
	Bool versionStringDiff = header.versionString != TheVersion->getUnicodeVersion();
	Bool versionTimeStringDiff = header.versionTimeString != TheVersion->getUnicodeBuildTime();
	Bool versionNumberDiff = header.versionNumber != TheVersion->getVersionNumber();
	Bool exeCRCDiff = header.exeCRC != TheGlobalData->m_exeCRC;
	Bool exeDifferent = versionStringDiff || versionTimeStringDiff || versionNumberDiff || exeCRCDiff;
	Bool iniDifferent = header.iniCRC != TheGlobalData->m_iniCRC;

	AsciiString debugString;
	AsciiString tempStr;
	if (exeDifferent)
	{
		// TheSuperHackers @fix helmutbuhler 05/05/2025 No longer attempts to print unicode as ascii
		// via a call to AsciiString::format with %ls format. It does not work with non-ascii characters.
		UnicodeString tempStrWide;
		debugString = "EXE is different:\n";
		if (versionStringDiff)
		{
			tempStrWide.format(L"   Version [%s] vs [%s]\n", TheVersion->getUnicodeVersion().str(), header.versionString.str());
			tempStr.translate(tempStrWide);
			debugString.concat(tempStr);
		}
		if (versionTimeStringDiff)
		{
			tempStrWide.format(L"   Build Time [%s] vs [%s]\n", TheVersion->getUnicodeBuildTime().str(), header.versionTimeString.str());
			tempStr.translate(tempStrWide);
			debugString.concat(tempStr);
		}
		if (versionNumberDiff)
		{
			tempStr.format("   Version Number %8.8X vs %8.8X\n", TheVersion->getVersionNumber(), header.versionNumber);
			debugString.concat(tempStr);
		}
		if (exeCRCDiff)
		{
			tempStr.format("   CRC %8.8X vs %8.8X\n", TheGlobalData->m_exeCRC, header.exeCRC);
			debugString.concat(tempStr);
		}
	}
	if (iniDifferent)
	{
		debugString.concat("INIs are different:\n");
		tempStr.format("   CRC %8.8X vs %8.8X\n", TheGlobalData->m_iniCRC, header.iniCRC);
		debugString.concat(tempStr);
	}
	DEBUG_ASSERTCRASH(!exeDifferent && !iniDifferent, (debugString.str()));
#endif

#ifdef DEBUG_LOGGING
	if (header.localPlayerIndex >= 0)
	{
		DEBUG_LOG(("Local player is %ls (slot %d, IP %8.8X)",
			m_gameInfo.getSlot(header.localPlayerIndex)->getName().str(), header.localPlayerIndex, m_gameInfo.getSlot(header.localPlayerIndex)->getIP()));
	}
#endif

    REPLAY_CRC_INTERVAL = m_gameInfo.getCRCInterval();

    Int difficulty = 0;
    m_file->read(&difficulty, sizeof(difficulty));

    m_file->read(&m_originalGameMode, sizeof(m_originalGameMode));

    Int rankPoints = 0;
    m_file->read(&rankPoints, sizeof(rankPoints));

    Int maxFPS = 0;
    m_file->read(&maxFPS, sizeof(maxFPS));

    // Zulu replay extension. Vanilla / pre-Zulu replays don't have this block;
    // readZuluReplayExtension peeks for the magic and rewinds if absent so the
    // next read still sees the start of the command stream.
    readZuluReplayExtension();

    Bool isMultiplayer = (m_originalGameMode == GAME_INTERNET || m_originalGameMode == GAME_LAN);
    m_crcInfo = CRCInfo(header.localPlayerIndex, isMultiplayer);
    DEBUG_LOG(("Player index is %d, replay CRC interval is %d, isMultiplayer is %d", m_crcInfo.getLocalPlayer(), REPLAY_CRC_INTERVAL, isMultiplayer));

    DEBUG_LOG(("RecorderClass::playbackFile() - original game was mode %d", m_originalGameMode));

    // TheSuperHackers @fix helmutbuhler 03/04/2025
    // In case we restart a replay, we need to clear the command list.
    // Otherwise a crc message remains and messes up the crc calculation on the restarted replay.
    TheCommandList->reset();

	readNextFrame();
	// readNextFrame() closes m_file via stopPlayback() if the first frame cannot be read.
	if(m_file == nullptr)
	{
		return FALSE;
	}

	TheWritableGlobalData->m_pendingFile = m_gameInfo.getMap();

	// send a message to the logic for a new game
	if (!m_doingAnalysis)
	{
		// TheSuperHackers @info helmutbuhler 13/04/2025
		// We send the New Game message here directly to the command list and bypass the TheMessageStream.
		// That's ok because Multiplayer is disabled during replay playback and is actually required
		// during replay simulation because we don't update TheMessageStream during simulation.
		GameMessage* msg = newInstance(GameMessage)(GameMessage::MSG_NEW_GAME);
		msg->appendIntegerArgument(GAME_REPLAY);
		msg->appendIntegerArgument(difficulty);
		msg->appendIntegerArgument(rankPoints);
		if (maxFPS != 0)
			msg->appendIntegerArgument(maxFPS);
		TheCommandList->appendMessage(msg);
		InitRandom(m_gameInfo.getSeed());
	}

	// TheSuperHackers @bugfix bobtista 25/07/2026 Enter playback mode only once the playback is ready.
	// Previously a failed open left the recorder in playback mode with a NULL m_file, and the next
	// update dereferenced it, for example when the replay is deleted during the version mismatch prompt.
	m_mode = RECORDERMODETYPE_PLAYBACK;

	m_currentReplayFilename = filename;
	m_playbackFrameCount = header.frameCount;
	return TRUE;
}

/**
 * Read a unicode string from the current file position. The string is assumed to be 0-terminated.
 */
UnicodeString RecorderClass::readUnicodeString() {
	WideChar str[1024] = L"";
	Int index = 0;

	Int c = m_file->readWideChar();
	if (c == EOF) {
		str[index] = 0;
	}
	str[index] = c;

	while (index < 1024 && str[index] != 0) {
		++index;
		Int c = m_file->readWideChar();
		if (c == EOF) {
			str[index] = 0;
			break;
		}
		str[index] = c;
	}
	str[1023] = L'\0';

	UnicodeString retval(str);
	return retval;
}

/**
 * Read an ascii string from the current file position. The string is assumed to be 0-terminated.
 */
AsciiString RecorderClass::readAsciiString() {
	char str[1024] = "";
	Int index = 0;

	Int c = m_file->readChar();
	if (c == EOF) {
		str[index] = 0;
	}
	str[index] = c;

	while (index < 1024 && str[index] != 0) {
		++index;
		Int c = m_file->readChar();
		if (c == EOF) {
			str[index] = 0;
			break;
		}
		str[index] = c;
	}
	str[1023] = '\0';

	AsciiString retval(str);
	return retval;
}

/**
 * Read the frame number for the next command in the playback file. If the end of the file is reached, the playback
 * is stopped and the next frame is said to be -1.
 */
void RecorderClass::readNextFrame() {
	if (m_liveObserverTruncated) {
		m_nextFrame = -1;
		stopPlayback();
		return;
	}
	// while playbackFile is still opening the file the tracked offset is not seeded yet;
	// File::position() is reliable at that point
	const Int posBefore = m_liveObserverArming ? m_file->position() : m_replayReadPos;
	Int bytesRead = m_file->read(&m_nextFrame, sizeof(m_nextFrame));
	if (bytesRead > 0)
		m_replayReadPos += bytesRead;
	if (bytesRead != sizeof(m_nextFrame)) {
		// Live observing: the file is still being appended to, so end of file means
		// "wait for more bytes". m_liveObserverArming covers the open-time read inside
		// playbackFile, which runs before the mode has flipped: a snapshot of a match
		// that just started legitimately holds no commands yet.
		if (m_liveObserverArming || (isLiveObserverMode() && m_liveObserverStreamOpen))
		{
			m_liveObserverRetryPos        = posBefore;
			m_liveObserverWaitingForBytes = TRUE;
			return;
		}
		if (m_mode == RECORDERMODETYPE_LIVE_OBSERVER)
			OBS_LOG("LiveObserver: EOF at pos %d with stream closed; ending playback (frame %u)", posBefore, TheGameLogic->getFrame());
		DEBUG_LOG(("RecorderClass::readNextFrame - read failed on frame %d", TheGameLogic->getFrame()));
		m_nextFrame = -1;
		// TheSuperHackers @feature bill-rich 15/09/2026 Resume-from-replay catchup: running out of
		// records is handled by updateResumeCatchup (which never ends the live game here).
		if (m_mode == RECORDERMODETYPE_RESUME_CATCHUP)
			return;
		stopPlayback();
	}
}

/**
 * Read exactly size bytes, flagging a short read rather than silently continuing.
 */
Bool RecorderClass::readReplayBytes(void *dst, Int size) {
	const Int bytesRead = m_file->read(dst, size);
	if (bytesRead > 0)
		m_replayReadPos += bytesRead;
	if (bytesRead != size) {
		m_replayShortRead = TRUE;
		return FALSE;
	}
	return TRUE;
}

/**
 * A record could not be read in full. For a live observer the stream simply has not
 * delivered the rest of it yet: rewind to the start of the record so the retry in
 * updatePlayback re-reads it whole once the bytes have landed. A file that is not being
 * appended to is genuinely truncated; readNextFrame hits the same EOF next and ends
 * playback, so the partial command is just not appended.
 */
void RecorderClass::rollbackTornRecord(Int posBefore) {
	if (!isLiveObserverMode())
		return;
	if (m_liveObserverStreamOpen) {
		// posBefore is the start of the record body; its frame number sits just before it
		// and the retry in updatePlayback re-reads the frame number first, so rewind past it.
		m_liveObserverRetryPos        = posBefore - (Int)sizeof(m_nextFrame);
		m_liveObserverWaitingForBytes = TRUE;
		OBS_LOG("LiveObserver: torn record at pos %d (frame %u), will retry", posBefore, m_nextFrame);
	} else {
		// The stream is over and the last record is incomplete: nothing after it is
		// usable, so the next frame read must end playback rather than parse the
		// remaining argument bytes as a frame number.
		m_liveObserverTruncated = TRUE;
		OBS_LOG("LiveObserver: torn record at pos %d with the stream closed; ending at frame %u", posBefore, TheGameLogic->getFrame());
	}
}

/**
 * A record could not be read whole. For a live observer whose stream is still open this rewinds
 * to the start of the record so it is re-read once the rest of it lands; for every other mode,
 * and for a stream that has ended, it is the end of the usable data. Always returns FALSE so a
 * caller can simply return it.
 */
Bool RecorderClass::abortTornRecord(Int posBefore) {
	rollbackTornRecord(posBefore);
	if (!(isLiveObserverMode() && m_liveObserverWaitingForBytes)) {
		DEBUG_LOG(("RecorderClass::appendNextCommand - torn record on frame %d, treating it as the end of the replay", m_nextFrame));
		m_nextFrame = -1;
	}
	return FALSE;
}

/**
 * This reads the next command from the replay file and appends it to TheCommandList.
 * TheSuperHackers @feature bill-rich 24/09/2026 Returns FALSE when the record is incomplete (a
 * recording cut short by a crash ends in a torn record); the torn record is discarded and
 * m_nextFrame is set to -1 so the callers treat it as the end of the usable data.
 */
Bool RecorderClass::appendNextCommand() {
	// TheSuperHackers @feature bill-rich 15/09/2026 A replay record is [type][playerIndex][numTypes][argType,
	// argCount]*[args]* with no length prefix, so it can only be validated by reading it
	// whole. For a live observer the file is appended in arbitrary chunks and routinely
	// ends mid-record; every read is checked and a torn record is rolled back so it can
	// be re-read whole later. For every other mode a torn record is the end of the usable
	// data. Previously only the first read was checked and a torn record left the offset
	// stranded mid-record, feeding argument bytes back in as frame numbers and types.
	const Int posBefore = m_replayReadPos;
	m_replayShortRead = FALSE;
	GameMessage::Type type;
	if (!readReplayBytes(&type, sizeof(type))) {
		return abortTornRecord(posBefore);
	}

	GameMessage* msg = newInstance(GameMessage)(type);
	Bool complete = TRUE;

#ifdef DEBUG_LOGGING
	AsciiString commandName = msg->getCommandAsString();
	if (type < GameMessage::MSG_BEGIN_NETWORK_MESSAGES || type > GameMessage::MSG_END_NETWORK_MESSAGES)
	{
		commandName.concat(" (Non-Network message!)");
	}
	else if (type == GameMessage::MSG_BEGIN_NETWORK_MESSAGES)
	{
		commandName.concat(" (CRC message!)");
	}
#endif // DEBUG_LOGGING

	Int playerIndex = -1;
	if (!readReplayBytes(&playerIndex, sizeof(playerIndex))) {
		deleteInstance(msg);
		return abortTornRecord(posBefore);
	}
	msg->friend_setPlayerIndex(playerIndex);

	// don't debug log this if we're debugging sync errors, as it will cause diff problems between a game and it's replay...
#ifdef DEBUG_LOGGING
	Bool logCommand = true;
#ifdef DEBUG_CRC
	if (!m_doingAnalysis)
		logCommand = false;
#endif
	if (logCommand)
	{
		DEBUG_LOG(("RecorderClass::appendNextCommand - Adding %s command from player %d to TheCommandList on frame %d",
			commandName.str(), (type == GameMessage::MSG_BEGIN_NETWORK_MESSAGES) ? 0 : msg->getPlayerIndex(), m_nextFrame/*TheGameLogic->getFrame()*/));
	}
#endif

	UnsignedByte numTypes = 0;
	Int totalArgs = 0;
	if (!readReplayBytes(&numTypes, sizeof(numTypes))) {
		deleteInstance(msg);
		return abortTornRecord(posBefore);
	}

	GameMessageParser* parser = newInstance(GameMessageParser)();
	for (UnsignedByte i = 0; complete && i < numTypes; ++i) {
		UnsignedByte type = (UnsignedByte)ARGUMENTDATATYPE_UNKNOWN;
		UnsignedByte numArgs = 0;
		if (!readReplayBytes(&type, sizeof(type)) || !readReplayBytes(&numArgs, sizeof(numArgs))) {
			deleteInstance(parser);
			deleteInstance(msg);
			return abortTornRecord(posBefore);
		}
		parser->addArgType((GameMessageArgumentDataType)type, numArgs);
		totalArgs += numArgs;
	}

	GameMessageParserArgumentType* parserArgType = parser->getFirstArgumentType();
	GameMessageArgumentDataType lasttype = ARGUMENTDATATYPE_UNKNOWN;
	Int argsLeftForType = 0;
	if (parserArgType != nullptr) {
		lasttype = parserArgType->getType();
		argsLeftForType = parserArgType->getArgCount();
	}
	for (Int j = 0; j < totalArgs; ++j) {
		if (!readArgument(lasttype, msg)) {
			deleteInstance(parser);
			deleteInstance(msg);
			return abortTornRecord(posBefore);
		}

		--argsLeftForType;
		if (argsLeftForType == 0) {
			DEBUG_ASSERTCRASH(parserArgType != nullptr, ("parserArgType was null when it shouldn't have been."));
			if (parserArgType == nullptr) {
				deleteInstance(msg);
				deleteInstance(parser);
				m_nextFrame = -1;
				return FALSE;
			}

			parserArgType = parserArgType->getNext();
			// parserArgType is allowed to be null here, this is the case if there are no more arguments.
			if (parserArgType != nullptr) {
				argsLeftForType = parserArgType->getArgCount();
				lasttype = parserArgType->getType();
			}
		}
	}

	if (!complete)
	{
		DEBUG_LOG(("RecorderClass::appendNextCommand - torn record on frame %d, treating it as the end of the replay", m_nextFrame));
		deleteInstance(msg);
		deleteInstance(parser);
		m_nextFrame = -1;
		return FALSE;
	}

	if (type != GameMessage::MSG_BEGIN_NETWORK_MESSAGES && type != GameMessage::MSG_CLEAR_GAME_DATA && !m_doingAnalysis)
	{
		TheCommandList->appendMessage(msg);
	}
	else
	{
		deleteInstance(msg);
		msg = nullptr;
	}

	deleteInstance(parser);
	parser = nullptr;
	return TRUE;
}

/// Reads one argument of the given type; FALSE on a short read.
Bool RecorderClass::readArgument(GameMessageArgumentDataType type, GameMessage *msg) {
	switch (type) {
		case ARGUMENTDATATYPE_INTEGER: {
			Int theint;
			if (!readReplayBytes(&theint, sizeof(theint)))
				return FALSE;
			msg->appendIntegerArgument(theint);
#ifdef DEBUG_LOGGING
			if (m_doingAnalysis)
			{
				DEBUG_LOG(("Integer argument: %d (%8.8X)", theint, theint));
			}
#endif
			break;
		}
		case ARGUMENTDATATYPE_REAL: {
			Real thereal;
			if (!readReplayBytes(&thereal, sizeof(thereal)))
				return FALSE;
			msg->appendRealArgument(thereal);
#ifdef DEBUG_LOGGING
			if (m_doingAnalysis)
			{
				DEBUG_LOG(("Real argument: %g (%8.8X)", thereal, *(int *)&thereal));
			}
#endif
			break;
		}
		case ARGUMENTDATATYPE_BOOLEAN: {
			Bool thebool;
			if (!readReplayBytes(&thebool, sizeof(thebool)))
				return FALSE;
			msg->appendBooleanArgument(thebool);
#ifdef DEBUG_LOGGING
			if (m_doingAnalysis)
			{
				DEBUG_LOG(("Bool argument: %d", thebool));
			}
#endif
			break;
		}
		case ARGUMENTDATATYPE_OBJECTID: {
			ObjectID theid;
			if (!readReplayBytes(&theid, sizeof(theid)))
				return FALSE;
			msg->appendObjectIDArgument(theid);
#ifdef DEBUG_LOGGING
			if (m_doingAnalysis)
			{
				DEBUG_LOG(("Object ID argument: %d", theid));
			}
#endif
			break;
		}
		case ARGUMENTDATATYPE_DRAWABLEID: {
			DrawableID theid;
			if (!readReplayBytes(&theid, sizeof(theid)))
				return FALSE;
			msg->appendDrawableIDArgument(theid);
#ifdef DEBUG_LOGGING
			if (m_doingAnalysis)
			{
				DEBUG_LOG(("Drawable ID argument: %d", theid));
			}
#endif
			break;
		}
		case ARGUMENTDATATYPE_TEAMID: {
			UnsignedInt theid;
			if (!readReplayBytes(&theid, sizeof(theid)))
				return FALSE;
			msg->appendTeamIDArgument(theid);
#ifdef DEBUG_LOGGING
			if (m_doingAnalysis)
			{
				DEBUG_LOG(("Team ID argument: %d", theid));
			}
#endif
			break;
		}
		case ARGUMENTDATATYPE_LOCATION: {
			Coord3D loc;
			if (!readReplayBytes(&loc, sizeof(loc)))
				return FALSE;
			msg->appendLocationArgument(loc);
#ifdef DEBUG_LOGGING
			if (m_doingAnalysis)
			{
				DEBUG_LOG(("Coord3D argument: %g %g %g (%8.8X %8.8X %8.8X)", loc.x, loc.y, loc.z,
					*(int *)&loc.x, *(int *)&loc.y, *(int *)&loc.z));
			}
#endif
			break;
		}
		case ARGUMENTDATATYPE_PIXEL: {
			ICoord2D pixel;
			if (!readReplayBytes(&pixel, sizeof(pixel)))
				return FALSE;
			msg->appendPixelArgument(pixel);
#ifdef DEBUG_LOGGING
			if (m_doingAnalysis)
			{
				DEBUG_LOG(("Pixel argument: %d,%d", pixel.x, pixel.y));
			}
#endif
			break;
		}
		case ARGUMENTDATATYPE_PIXELREGION: {
			IRegion2D reg;
			if (!readReplayBytes(&reg, sizeof(reg)))
				return FALSE;
			msg->appendPixelRegionArgument(reg);
#ifdef DEBUG_LOGGING
			if (m_doingAnalysis)
			{
				DEBUG_LOG(("Pixel Region argument: %d,%d -> %d,%d", reg.lo.x, reg.lo.y, reg.hi.x, reg.hi.y));
			}
#endif
			break;
		}
		case ARGUMENTDATATYPE_TIMESTAMP: {  // Not to be confused with Terrance Stamp... Kneel before Zod!!!
			UnsignedInt stamp;
			if (!readReplayBytes(&stamp, sizeof(stamp)))
				return FALSE;
			msg->appendTimestampArgument(stamp);
#ifdef DEBUG_LOGGING
			if (m_doingAnalysis)
			{
				DEBUG_LOG(("Timestamp argument: %d", stamp));
			}
#endif
			break;
		}
		case ARGUMENTDATATYPE_WIDECHAR: {
			WideChar theid;
			if (!readReplayBytes(&theid, sizeof(theid)))
				return FALSE;
			msg->appendWideCharArgument(theid);
#ifdef DEBUG_LOGGING
			if (m_doingAnalysis)
			{
				DEBUG_LOG(("WideChar argument: %d (%lc)", theid, theid));
			}
#endif
			break;
		}
		default:
			break;
	}
	return TRUE;
}

/**
 * This needs to be called for every frame during playback. Basically it prevents the user from inserting.
 */
RecorderClass::CullBadCommandsResult RecorderClass::cullBadCommands() {
	CullBadCommandsResult result;

	if (m_doingAnalysis)
		return result;

	GameMessage *msg = TheCommandList->getFirstMessage();
	GameMessage *next = nullptr;

	while (msg != nullptr) {
		next = msg->next();
		if ((msg->getType() > GameMessage::MSG_BEGIN_NETWORK_MESSAGES) &&
				(msg->getType() < GameMessage::MSG_END_NETWORK_MESSAGES) &&
				(msg->getType() != GameMessage::MSG_LOGIC_CRC)) {

			deleteInstance(msg);
		}
		else if (msg->getType() == GameMessage::MSG_CLEAR_GAME_DATA)
		{
			result.hasClearGameDataMessage = true;
		}

		msg = next;
	}

	return result;
}

/**
 * returns the directory that holds the replay files.
 */
AsciiString RecorderClass::getReplayDir()
{
	AsciiString tmp = TheGlobalData->getPath_UserData();
	tmp.concat("Replays\\");
	return tmp;
}

/**
 * returns the directory that holds the archived replay files.
 */
AsciiString RecorderClass::getReplayArchiveDir()
{
	AsciiString tmp = TheGlobalData->getPath_UserData();
	tmp.concat("ArchivedReplays\\");
	return tmp;
}

/**
 * returns the file extension for the replay files.
 */
AsciiString RecorderClass::getReplayExtention() {
	return AsciiString(replayExtention);
}

/**
 * returns the file name used for the replay file that is recorded to.
 */
AsciiString RecorderClass::getLastReplayFileName()
{
#if defined(RTS_DEBUG)
	if (TheNetwork && TheGlobalData->m_saveStats)
	{
		GameInfo *game = nullptr;
		if (TheLAN)
			game = TheLAN->GetMyGame();
#if defined(GENERALS_ONLINE)
		else if (NGMP_OnlineServicesManager::GetInstance() != nullptr)
			game = TheNGMPGame;
#else
		else if (TheGameSpyInfo)
			game = TheGameSpyGame;
#endif
		if (game)
		{
			AsciiString players;
			AsciiString full;
			AsciiString fullPlusNum;
			AsciiString mapName = game->getMap();
			const char* fname = mapName.reverseFind('\\');
			if (fname)
				mapName = fname + 1;
			for (Int i = 0; i < MAX_SLOTS; ++i)
			{
				GameSlot* slot = game->getSlot(i);
				if (slot && slot->isHuman())
				{
					AsciiString player;
					player.format("%ls_", slot->getName().str());
					players.concat(player);
				}
			}
			full.format("%s%s_%d_%d", players.str(), mapName.str(), game->getSeed(), game->getLocalSlotNum());
			AsciiString testString;
			testString.format("%s%s%s", getReplayDir().str(), full.str(), replayExtention);

			FILE* fp;
			fp = fopen(testString.str(), "rb");
			if (fp)
			{
				fclose(fp);
			}
			else
			{
				return full;
			}
			Int test = 1;
			while (test < 20)
			{
				fullPlusNum.format("%s_%d", full.str(), test);
				testString.format("%s%s%s", getReplayDir().str(), fullPlusNum.str(), replayExtention);
				fp = fopen(testString.str(), "rb");
				if (fp)
				{
					fclose(fp);
					++test;
				}
				else
				{
					return fullPlusNum;
				}
			}
			return fullPlusNum;
		}
	}
#endif

	AsciiString filename;
	if (rts::ClientInstance::getInstanceId() > 1u)
	{
		filename.format("%s_Instance%.2u", lastReplayFileName, rts::ClientInstance::getInstanceId());
	}
	else
	{
		filename = lastReplayFileName;
	}
	return filename;
}

/**
 * return the current operating mode of TheRecorder.
 */
RecorderModeType RecorderClass::getMode() {
	return m_mode;
}

///< Show or Hide the Replay controls
void RecorderClass::initControls()
{
	NameKeyType parentReplayControlID = TheNameKeyGenerator->nameToKey( "ReplayControl.wnd:ParentReplayControl" );
	GameWindow *parentReplayControl = TheWindowManager->winGetWindowFromId( nullptr, parentReplayControlID );

	Bool show = (getMode() != RECORDERMODETYPE_PLAYBACK);
	if (parentReplayControl)
	{
		parentReplayControl->winHide(show);	// show the replay control window.
	}
}

///< is this a multiplayer game (record OR playback)?
Bool RecorderClass::isMultiplayer()
{

	if (isPlaybackMode())
	{
		GameSlot* slot;
		for (int i = 0; i < MAX_SLOTS; ++i)
		{
			slot = m_gameInfo.getSlot(i);
			if (slot && slot->isOccupied())	///< slots default to closed for non-networked games
				return true;
		}
	}
	if (TheGameLogic->getGameMode() == GAME_SINGLE_PLAYER) {
		return false; // single player isn't multiplayer.
	}
	if (TheGameLogic->getGameMode() == GAME_SHELL) {
		return false; // shell isn't multiplayer.
	}
	if (TheNetwork || TheSkirmishGameInfo)
		return true;

	return false;
}

//-------------------------------------------------------------------------------------------------
// TheSuperHackers @feature bill-rich 15/09/2026 Resume-from-replay. See Recorder.h for the phase overview.
//-------------------------------------------------------------------------------------------------

AsciiString RecorderClass::getResumeReplayFileName()
{
	return AsciiString(resumeReplayFileName);
}

void RecorderClass::clearResumeState()
{
	if (m_resumeSourceFile != nullptr)
	{
		m_resumeSourceFile->close();
		m_resumeSourceFile = nullptr;
	}
	m_resumeArmedHandoffFrame = 0;
	m_resumeArmedSourceLastFrame = 0;
	m_resumeHandoffFrame = 0;
	m_resumeSourceLastFrame = 0;
	m_resumeLeadInAnnounced = FALSE;
	m_resumeFreezeStartMs = 0;
	m_resumeFreezeLastAnnounced = -1;
	if (m_mode == RECORDERMODETYPE_RESUME_CATCHUP)
		m_mode = RECORDERMODETYPE_NONE;
}

void RecorderClass::armResumeForNextGame(UnsignedInt handoffFrame, UnsignedInt sourceLastFrame)
{
	m_resumeArmedHandoffFrame = handoffFrame;
	m_resumeArmedSourceLastFrame = sourceLastFrame;
}

void RecorderClass::disarmResume()
{
	m_resumeArmedHandoffFrame = 0;
	m_resumeArmedSourceLastFrame = 0;
}

Bool RecorderClass::isResumeCatchupLeadIn() const
{
	if (!isResumeCatchupMode())
		return FALSE;
	const UnsignedInt leadInFrames = RESUME_LEADIN_SECONDS * LOGICFRAMES_PER_SECOND;
	const UnsignedInt curFrame = TheGameLogic ? TheGameLogic->getFrame() : 0;
	return m_resumeHandoffFrame >= leadInFrames && curFrame >= m_resumeHandoffFrame - leadInFrames;
}

/**
 * CRC validation is off during catchup (the CRC messages of that window come out of the resume
 * source, not from the live peers) and stays off for one full run-ahead after the handoff: a CRC
 * generated on frame F is compared on F plus the run-ahead, and every set that could still be
 * compared before that was generated while the peers were not exchanging any.
 */
Bool RecorderClass::isResumeCRCValidationSuppressed() const
{
	if (isResumeCatchupMode())
		return TRUE;
	if (m_resumeHandoffFrame == 0 || TheGameLogic == nullptr)
		return FALSE;
	return TheGameLogic->getFrame() <= m_resumeHandoffFrame + MAX_FRAMES_AHEAD;
}

/**
 * Open the resume source (Resume.rep), skip its header, prime the first frame of commands and
 * switch the recorder into RECORDERMODETYPE_RESUME_CATCHUP. startRecording has already opened the
 * new recording in m_file and written its header for the live game; the source is read through a
 * second handle. While in catchup, update() injects the source's commands into TheCommandList each
 * frame and records them, while the live network keeps running in lockstep.
 */
Bool RecorderClass::startResumeCatchup()
{
	if (m_file == nullptr || m_resumeArmedHandoffFrame == 0)
		return FALSE;

	// The read helpers (readReplayHeader, readNextFrame, appendNextCommand) work on m_file:
	// point it at the source for the duration and put the recording back afterwards.
	File *recordFile = m_file;
	m_file = nullptr;
	ReplayHeader header;
	header.forPlayback = TRUE;
	header.filename = ResumeFromReplay::getSourceFileName();
	const Bool headerOk = readReplayHeader(header);
	m_resumeSourceFile = m_file;
	m_file = recordFile;
	if (!headerOk || m_resumeSourceFile == nullptr)
	{
		DEBUG_LOG(("RecorderClass::startResumeCatchup - readReplayHeader failed for %s", header.filename.str()));
		if (m_resumeSourceFile != nullptr)
		{
			m_resumeSourceFile->close();
			m_resumeSourceFile = nullptr;
		}
		return FALSE;
	}

	// playbackFile consumes these four values between the header and the command stream
	// (difficulty, original game mode, rank points, max FPS); mirror it or readNextFrame reads
	// the difficulty as a frame number.
	Int skipped[4];
	if (m_resumeSourceFile->read(skipped, sizeof(skipped)) != sizeof(skipped))
	{
		m_resumeSourceFile->close();
		m_resumeSourceFile = nullptr;
		return FALSE;
	}

	// Mirror playbackFile's handling of the Zulu extension block, and pick the epoch the
	// source was recorded under so the injected commands re-simulate the same way.
	m_file = m_resumeSourceFile;
	readZuluReplayExtension();
	m_resumeSourceFile = m_file;
	m_file = recordFile;
	m_replayEpoch = (s_replayEpochOverride >= 0)
		? (ReplayEpoch)s_replayEpochOverride
		: deriveReplayEpochFromHeader(header);
	m_replayLegacyUpgradeKeyDelta = legacyUpgradeKeyDeltaFromHeader(header);

	m_mode = RECORDERMODETYPE_RESUME_CATCHUP;
	m_resumeHandoffFrame = m_resumeArmedHandoffFrame;
	m_resumeSourceLastFrame = m_resumeArmedSourceLastFrame;
	m_resumeArmedHandoffFrame = 0;
	m_resumeArmedSourceLastFrame = 0;
	m_resumeLeadInAnnounced = FALSE;
	m_resumeFreezeStartMs = 0;
	m_resumeFreezeLastAnnounced = -1;

	// Prime the first record's frame number (in catchup mode, readNextFrame never ends the game).
	m_file = m_resumeSourceFile;
	readNextFrame();
	m_resumeSourceFile = m_file;
	m_file = recordFile;

	// Nothing may be selected when catchup starts: selection is blocked from here on and
	// a pre-existing selection would be a way to issue commands with the right mouse button.
	if (TheInGameUI)
	{
		TheInGameUI->deselectAllDrawables();
		TheInGameUI->message(TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:ResumeCatchupStart",
			L"Resuming from replay: fast-forwarding to %ls", ResumeFromReplay::formatGameTime(m_resumeHandoffFrame).str()));
	}

	// The realtime pacing is bypassed for the duration by the catchup checks in FramePacer,
	// GameEngine and Network (isResumeCatchupMode); lockstep is unchanged, every frame still
	// waits for every peer's commands.
	RESUME_LOG("Resume: catching up %s to frame %u (source ends at frame %u, lead-in %d s, freeze %d s)",
		header.filename.str(), m_resumeHandoffFrame, m_resumeSourceLastFrame, RESUME_LEADIN_SECONDS, RESUME_FREEZE_SECONDS);
	return TRUE;
}

/**
 * Last frame with a complete record in a replay, found by walking its records. The header's frame
 * count is only patched in when a recording ends normally, so a recording cut short by a crash
 * (the very file a resume is for) reads 0 there, and its last record is usually torn. Uses the
 * recorder's analysis mode, in which appendNextCommand parses and discards instead of feeding
 * TheCommandList. Lobby use only: refuses to run while the recorder has a file open.
 */
UnsignedInt RecorderClass::scanReplayLastFrame(AsciiString filename)
{
	if (m_file != nullptr || isPlaybackMode() || isResumeCatchupMode())
	{
		// Lobby-only helper; the read helpers it borrows (readReplayHeader, appendNextCommand) work on
		// m_file, so it cannot run while a recording or playback holds that. 0 is "no usable frames".
		DEBUG_CRASH(("RecorderClass::scanReplayLastFrame(%s) called while the recorder has a file open (mode %d)", filename.str(), m_mode));
		RESUME_LOG("Resume: cannot scan %s while the recorder has a file open (mode %d)", filename.str(), m_mode);
		return 0;
	}

	ReplayHeader header;
	header.forPlayback = TRUE;
	header.filename = filename;
	if (!readReplayHeader(header))
		return 0;

	// difficulty, original game mode, rank points, max FPS (see playbackFile)
	Int skipped[4];
	if (m_file->read(skipped, sizeof(skipped)) != sizeof(skipped))
	{
		m_file->close();
		m_file = nullptr;
		return 0;
	}

	// ...then the Zulu extension block, or its magic would be read as a frame number.
	readZuluReplayExtension();

	const Bool savedAnalysis = m_doingAnalysis;
	m_doingAnalysis = TRUE;

	UnsignedInt lastFrame = 0;
	UnsignedInt frame = 0;
	while (m_file->read(&frame, sizeof(frame)) == sizeof(frame))
	{
		if (frame < lastFrame)
			break; // torn tail: frame numbers only ever grow
		m_nextFrame = frame;
		if (!appendNextCommand())
			break; // torn record: everything before it is usable
		lastFrame = frame;
	}

	m_file->close();
	m_file = nullptr;
	m_doingAnalysis = savedAnalysis;
	m_nextFrame = (UnsignedInt)-1;
	return lastFrame;
}

/**
 * Read the source's records for curFrame into TheCommandList. Never reads past the last complete
 * frame found by scanReplayLastFrame, so a torn tail cannot be replayed, and never past the handoff.
 */
void RecorderClass::injectResumeCommands(UnsignedInt curFrame)
{
	File *recordFile = m_file;
	m_file = m_resumeSourceFile;
	while (m_nextFrame == curFrame && m_nextFrame <= m_resumeSourceLastFrame && curFrame <= m_resumeHandoffFrame)
	{
		if (!appendNextCommand())
			break; // torn record: appendNextCommand marked the end of the usable data
		readNextFrame();
		if (m_nextFrame != (UnsignedInt)-1 && m_nextFrame < curFrame)
			m_nextFrame = (UnsignedInt)-1; // regression: the file is not the one that was scanned
	}
	m_resumeSourceFile = m_file;
	m_file = recordFile;
}

/**
 * Close the source and leave catchup. The recording of the resumed match goes on in m_file.
 */
void RecorderClass::endResumeCatchup()
{
	if (m_resumeSourceFile != nullptr)
	{
		m_resumeSourceFile->close();
		m_resumeSourceFile = nullptr;
	}
	m_nextFrame = (UnsignedInt)-1;
	m_resumeSourceLastFrame = 0;
	if (m_mode == RECORDERMODETYPE_RESUME_CATCHUP)
		m_mode = RECORDERMODETYPE_RECORD;
}

/**
 * The source cannot be replayed to the handoff on this client. Every client validated its copy
 * against the handoff before starting, so this is not a shorter-replay case a local handoff could
 * paper over (that would desync the peers): tell the player and leave the game, never fall back
 * to playing on.
 */
void RecorderClass::abortResume(const UnicodeString &why)
{
	RESUME_LOG("Resume: aborted at frame %u: %ls", TheGameLogic ? TheGameLogic->getFrame() : 0, why.str());
	endResumeCatchup();
	m_resumeHandoffFrame = 0;
	m_resumeFreezeStartMs = 0;
	if (TheInGameUI)
		TheInGameUI->message(why);
	if (TheGameLogic)
		TheGameLogic->exitGame();
}

/**
 * Per-frame update for RECORDERMODETYPE_RESUME_CATCHUP: inject the source's commands for the
 * current logic frame, announce the lead-in, and start the freeze when the handoff frame is reached.
 * Unlike stopPlayback this does NOT call exitGame at the handoff; the network session continues.
 */
void RecorderClass::updateResumeCatchup()
{
	const UnsignedInt curFrame = TheGameLogic ? TheGameLogic->getFrame() : 0;

	// Lead-in: the pacing bypasses switch off (isResumeCatchupLeadIn) so players get a
	// normal-speed look at the last seconds before the handoff. If the handoff is closer than
	// that to the start, it stays fast.
	if (!m_resumeLeadInAnnounced && isResumeCatchupLeadIn())
	{
		m_resumeLeadInAnnounced = TRUE;
		RESUME_LOG("Resume: lead-in reached at frame %u, back to realtime", curFrame);
		if (TheInGameUI)
			TheInGameUI->message(TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:ResumeCatchupLeadIn",
				L"Catching up: the last %d seconds play at normal speed", RESUME_LEADIN_SECONDS));
	}

	// NOTE: cullBadCommands() is intentionally skipped here. It strips every command in
	// the network range from TheCommandList, which is exactly what appendNextCommand adds,
	// and the live network layer writes into the same list each frame. Local input is
	// blocked at its sources instead (isResumeInputBlocked).

	// Inject every command recorded for this frame, including the handoff frame itself.
	injectResumeCommands(curFrame);

	if (curFrame >= m_resumeHandoffFrame)
	{
		endResumeCatchup();

		// Hold the simulation with a countdown before control comes back. Network::update
		// polls updateResumeFreeze() and refuses to advance frames while it returns TRUE.
		m_resumeFreezeStartMs = timeGetTime();
		if (m_resumeFreezeStartMs == 0)
			m_resumeFreezeStartMs = 1;
		m_resumeFreezeLastAnnounced = -1;
		RESUME_LOG("Resume: handoff at frame %u, freezing for %d s", curFrame, RESUME_FREEZE_SECONDS);
		return;
	}

	// The source's records ended before the last complete frame the scan found: the file is
	// not the one that was validated. A local handoff here would desync the peers.
	if (m_nextFrame == (UnsignedInt)-1 && curFrame < m_resumeSourceLastFrame)
	{
		abortResume(TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:ResumeSourceEnded",
			L"Resume aborted: the replay ended at %ls, before the handoff at %ls",
			ResumeFromReplay::formatGameTime(curFrame).str(), ResumeFromReplay::formatGameTime(m_resumeHandoffFrame).str()));
	}
}

/**
 * Pumped from Network::update() every engine frame (GameLogic, and with it the recorder's own
 * update, does not run while frames are held). Shows the countdown and ends the freeze.
 */
Bool RecorderClass::updateResumeFreeze()
{
	if (m_resumeFreezeStartMs == 0)
		return FALSE;

	const UnsignedInt elapsedMs = timeGetTime() - m_resumeFreezeStartMs;
	const Int remaining = RESUME_FREEZE_SECONDS - (Int)(elapsedMs / 1000);
	if (remaining > 0)
	{
		if (remaining != m_resumeFreezeLastAnnounced && TheInGameUI)
		{
			m_resumeFreezeLastAnnounced = remaining;
			RESUME_LOG("Resume: countdown %d", remaining);
			TheInGameUI->message(TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:ResumeCountdown",
				L"Resuming in %d", remaining));
		}
		return TRUE;
	}

	m_resumeFreezeStartMs = 0;
	m_resumeFreezeLastAnnounced = -1;
	if (TheInGameUI)
		TheInGameUI->message(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeHandoffComplete", L"Control handed back. Go!"));
	RESUME_LOG("Resume: freeze over at frame %u, live play resumes", TheGameLogic ? TheGameLogic->getFrame() : 0);
	return FALSE;
}

/**
 * Create a new recorder object.
 */
RecorderClass* createRecorder() {
	return NEW RecorderClass;
}
