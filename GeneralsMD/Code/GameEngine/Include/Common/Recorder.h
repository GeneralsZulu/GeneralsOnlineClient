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

#pragma once

#include "Common/MessageStream.h"
#include "GameNetwork/GameInfo.h"

class File;

/**
  * The ReplayGameInfo class holds information about the replay game and
	* the contents of its slot list for reconstructing multiplayer games.
	*/
class ReplayGameInfo : public GameInfo
{
private:
	GameSlot m_ReplaySlot[MAX_SLOTS];

public:
	ReplayGameInfo()
	{
		for (Int i = 0; i< MAX_SLOTS; ++i)
			setSlotPointer(i, &m_ReplaySlot[i]);
	}
};

enum RecorderModeType CPP_11(: Int) {
	RECORDERMODETYPE_RECORD,
	RECORDERMODETYPE_PLAYBACK,
	RECORDERMODETYPE_SIMULATION_PLAYBACK, // Play back replay without any graphics
	RECORDERMODETYPE_NONE, // this is a valid state to be in on the shell map, or in saved games
	RECORDERMODETYPE_LIVE_OBSERVER, // playback of a replay that is still being written: readNextFrame waits at EOF for more bytes instead of ending
	RECORDERMODETYPE_RESUME_CATCHUP // Resume-from-replay: feed replay commands into a live network game until the handoff frame.
};

class RecorderClass : public SubsystemInterface
{
protected:
	// TheSuperHackers @info helmutbuhler 03/04/2025 CRC overview:
	// Each peer periodically computes a CRC from its local game state and broadcasts it to all peers, including itself,
	// to verify synchronization. CRC messages are received a few frames later in network games to avoid stalling every
	// frame while waiting for all peers. This works because all peers compare the same received CRCs on the same frame.
	//
	// Replays are different: recorded CRC messages appear on the frame they were originally received, so directly
	// comparing them against the current local state would mismatch. To handle this, local CRCs must be queued until the
	// corresponding replay CRC messages arrive. This class implements that queue.
	class CRCInfo
	{
	public:
		CRCInfo();
		CRCInfo(UnsignedInt localPlayer, Bool isMultiplayer);
		void addCRC(UnsignedInt val);
		UnsignedInt readCRC();
		int GetQueueSize() const { return m_data.size(); }
		UnsignedInt getLocalPlayer() const { return m_localPlayer; }
		void setSawCRCMismatch() { m_sawCRCMismatch = TRUE; }
		Bool sawCRCMismatch() const { return m_sawCRCMismatch; }

	protected:
		Bool m_sawCRCMismatch;
		Bool m_skippedOne;
		UnsignedInt m_localPlayer;
		std::list<UnsignedInt> m_data;
	};

public:
	struct ReplayHeader;

	RecorderClass();																	///< Constructor.
	virtual ~RecorderClass() override;													///< Destructor.

	virtual void init() override;																			///< Initialize TheRecorder.
	virtual void reset() override;																			///< Reset the state of TheRecorder.
	virtual void update() override;																		///< General purpose update function.

	// Methods dealing with recording.
	void updateRecord();															///< The update function for recording.

	// Methods dealing with playback.
	void updatePlayback();														///< The update function for playing back a file.
	Bool playbackFile(AsciiString filename);					///< Starts playback of the specified file.
	Bool replayMatchesGameVersion(AsciiString filename); ///< Returns true if the playback is a valid playback file for this version.
	static Bool replayMatchesGameVersion(const ReplayHeader& header); ///< Returns true if the playback is a valid playback file for this version.
	AsciiString getCurrentReplayFilename();			///< valid during playback only
	UnsignedInt getPlaybackFrameCount() const { return m_playbackFrameCount; }			///< valid during playback only
	void stopPlayback();															///< Stops playback.  Its fine to call this even if not playing back a file.
	Bool simulateReplay(AsciiString filename);
#if defined(RTS_DEBUG)
	Bool analyzeReplay( AsciiString filename );
#endif
	Bool isPlaybackInProgress() const;

	// TheSuperHackers @feature Playback-fidelity signal: replayed commands whose object IDs
	// did not resolve in our re-simulated world. The recording client only issued commands
	// against objects that existed for it, so anything beyond the occasional
	// clicked-a-dying-unit race points at a diverged simulation. Used by the replay verdict
	// for retail-epoch replays, whose recorded checksums are unusable (see handleCRCMessage).
	UnsignedInt getPlaybackStaleObjectRefs() const { return m_playbackStaleObjectRefs; }
	void notePlaybackStaleObjectRef() { ++m_playbackStaleObjectRefs; }

public:
	void handleCRCMessage(UnsignedInt newCRC, Int playerIndex, Bool fromPlayback);

	// read in info relating to a replay, conditionally setting up m_file for playback
	struct ReplayHeader
	{
		AsciiString filename;
		Bool forPlayback;
		UnicodeString replayName;
		SYSTEMTIME timeVal;
		UnicodeString versionString;
		UnicodeString versionTimeString;
		UnsignedInt versionNumber;
		UnsignedInt exeCRC;
		UnsignedInt iniCRC;
		time_t startTime;
		time_t endTime;
		UnsignedInt frameCount;
		Bool quitEarly;
		Bool desyncGame;
		Bool playerDiscons[MAX_SLOTS];
		AsciiString gameOptions;
		Int localPlayerIndex;
	};
	Bool readReplayHeader( ReplayHeader& header );

	RecorderModeType getMode();												///< Returns the current operating mode.
	Bool isPlaybackMode() const { return m_mode == RECORDERMODETYPE_PLAYBACK || m_mode == RECORDERMODETYPE_SIMULATION_PLAYBACK || m_mode == RECORDERMODETYPE_LIVE_OBSERVER; }

	// TheSuperHackers @feature bill-rich 15/09/2026 Live observing: play back a replay file that a stream
	// (any transport) keeps appending to while the match is still running. Identical
	// to PLAYBACK except that end of file means "wait for more bytes", the initial
	// snapshot is drained at a boosted frame rate, torn records are re-read once the
	// rest of them arrives, and manual fast-forward is refused so the viewer cannot
	// race past the live edge. The stream owner calls setLiveObserverStreamOpen().
	Bool isLiveObserverMode() const { return m_mode == RECORDERMODETYPE_LIVE_OBSERVER; }
	Bool isLiveObserverCatchup() const { return isLiveObserverMode() && m_liveObserverFpsBoosted; } ///< still draining the snapshot faster than realtime
	Bool isLiveObserverWaitingForBytes() const { return m_liveObserverWaitingForBytes; }
	void setLiveObserverStreamOpen(Bool open) { m_liveObserverStreamOpen = open; }
	void noteLiveObserverBytes(Int fileLength); ///< the transport appended bytes; fileLength is the file's new size
	void setLiveObserverEdgeMargin(Int bytes); ///< how far short of the live edge to stop draining, in bytes (a few seconds of stream)
	Bool playbackFileLiveObserver(AsciiString filename); ///< playbackFile() that then flips the mode to LIVE_OBSERVER
	void setLiveObserverBoost(Bool boost); ///< run the logic uncapped (draining a backlog) or at the user's rate

	// TheSuperHackers @feature bill-rich 15/09/2026 Resume-from-replay.
	//
	// The lobby's start path validates this client's copy of the resume source (Resume.rep,
	// see ResumeFromReplay) and arms the recorder. On the next MSG_NEW_GAME a brand new
	// recording of the resumed match is started as for any other game, and the source's
	// commands are injected into TheCommandList frame by frame while the live network game
	// runs in lockstep at an uncapped rate (catchup); every injected command is written to the
	// new recording as it goes, so the new replay holds the whole match. The last
	// RESUME_LEADIN_SECONDS before the handoff frame run at normal speed (lead-in), then the
	// simulation is held for RESUME_FREEZE_SECONDS of wall-clock time with a countdown (freeze),
	// and finally control is handed back to the players. Camera scrolling works throughout;
	// every other interaction is blocked until the freeze ends (see isResumeInputBlocked).
	static const Int RESUME_LEADIN_SECONDS = 10;
	static const Int RESUME_FREEZE_SECONDS = 10;
	static AsciiString getResumeReplayFileName();    ///< name (without extension) of the resume source every client replays
	void armResumeForNextGame(UnsignedInt handoffFrame, UnsignedInt sourceLastFrame); ///< validated by ResumeFromReplay::prepareGameStart
	void disarmResume();
	Bool isResumeArmed() const { return m_resumeArmedHandoffFrame != 0; }
	Bool isResumeCatchupMode() const { return m_mode == RECORDERMODETYPE_RESUME_CATCHUP; }
	Bool isResumeCatchupLeadIn() const;              ///< catchup, within the last RESUME_LEADIN_SECONDS before handoff
	Bool isResumeFreezeActive() const { return m_resumeFreezeStartMs != 0; } ///< handoff reached, holding the sim with a countdown
	Bool isResumeInputBlocked() const { return isResumeCatchupMode() || isResumeFreezeActive(); }
	Bool isResumeCRCValidationSuppressed() const;    ///< catchup, and the run-ahead window after the handoff
	Bool updateResumeFreeze();                       ///< pumped every engine frame by Network::update; TRUE while frames must not advance
	UnsignedInt scanReplayLastFrame(AsciiString filename); ///< last frame with a complete record; walks the records, for recordings whose header was never finalized (crash) or whose tail is torn
	void initControls();															///< Show or Hide the Replay controls

	static AsciiString getReplayDir();								///< Returns the directory that holds the replay files.
	static AsciiString getReplayArchiveDir();					///< Returns the directory that holds the archived replay files.
	static AsciiString getReplayExtention();					///< Returns the file extention for replay files.
	static AsciiString getLastReplayFileName();				///< Returns the filename used for the default replay.

	GameInfo *getGameInfo() { return &m_gameInfo; }	///< Returns the slot list for playback game start

	Bool isMultiplayer();												///< is this a multiplayer game (record OR playback)?

	// Zulu replay extension. Replay-affecting AI features added by the Zulu
	// mod are tagged with a monotonic feature-version. New binaries playing
	// older replays disable any feature whose version exceeds what the replay
	// declares, so older replays remain deterministically replayable. Old
	// binaries cannot play new replays - that is by design.
	enum
	{
		ZULU_AI_FEATURE_NONE        = 0,
		ZULU_AI_FEATURE_IDLE_COMMIT = 1, ///< AISkirmishPlayer::commitIdleArmy()
		ZULU_AI_FEATURE_SPOT_SOLVER = 2, ///< global start-position solver in populateRandomStartPosition(), incl. teammate spot shuffle
		ZULU_AI_FEATURE_USA_DRONES  = 3, ///< TacticalAI USA vehicle drones (AISkirmishPlayer::buyObjectUpgrades)
		ZULU_AI_FEATURE_CHINA_OVERLORD = 4, ///< TacticalAI China Overlord gattling/propaganda (AISkirmishPlayer::buyObjectUpgrades)
		ZULU_AI_FEATURE_MD_LASER_LOCK = 5, ///< TacticalAI USA Missile Defender laser lock (AISkirmishPlayer::laserLockMissileDefenders)
		ZULU_AI_FEATURE_CURRENT     = 5
	};
	// True if the AI feature at the given version should run this frame.
	// Always true during live games / recording. During playback / resume-catchup
	// / simulation-playback, true only if the loaded replay declared support
	// for that feature version.
	Bool isAIFeatureEnabled(UnsignedInt featureVersion) const;

	// TheSuperHackers @feature Replay determinism epochs. Some sim behaviors
	// changed across versions WITHOUT being gated by the AI-feature block
	// above (they shipped ungated), so old replays don't carry a flag to
	// disable them. To re-simulate an old replay bit-exactly on a newer binary,
	// we derive a monotonic "epoch" from the replay's recorded version (header
	// version string, or the -replayEpoch override) and gate each divergent
	// path on it. Higher = newer behavior; CURRENT during live play/recording.
	enum ReplayEpoch
	{
		REPLAY_EPOCH_RETAIL = 0, ///< retail 1.04 / Zulu <= 1.2.0
		REPLAY_EPOCH_V121   = 1, ///< 1.2.1+: create-team processed at network frame for all players
		REPLAY_EPOCH_V128   = 2, ///< 1.2.8+: poison/flame XP source, crate multi-pickup guard, scaffold-resume-on-dead-builder
		REPLAY_EPOCH_V130   = 3, ///< 1.3.0+: AISkirmishPlayer surrender directive
		REPLAY_EPOCH_V155   = 4, ///< 1.5.5+: upgrade commands carry the stable upgrade id (mask bit) instead of a raw namekey; community patch sim fixes (battle bus lethal damage, empty stinger snipe, stealth during repair, reliable firestorms)
		REPLAY_EPOCH_CURRENT = REPLAY_EPOCH_V155
	};
	// The epoch to simulate. CURRENT during live play/recording; during playback
	// it is the recorded version's epoch (override or auto-detected).
	ReplayEpoch getReplayEpoch() const;
	// True if the behavior introduced at 'epoch' should run this frame.
	Bool isReplayEpochAtLeast(ReplayEpoch epoch) const { return getReplayEpoch() >= epoch; }
	// Command-line override (-replayEpoch); <0 means "auto-detect from header".
	static void setReplayEpochOverride(Int epoch) { s_replayEpochOverride = epoch; }
	static Int getReplayEpochOverride() { return s_replayEpochOverride; }

	// Pre-V155 replays recorded upgrade purchases as raw NameKeyType values.
	// Those are dealt at runtime in first-use order, so the numbering belongs to
	// the RECORDING environment: the binary's early registrations plus every name
	// registered by data parsed before Upgrade.ini (mods shift it too). Playback
	// loads the recording's data package, so the data-driven registrations cancel
	// out and the only asymmetry is the binary's own: one extra FunctionLexicon
	// name registered at init from 1.5.2 on, which is how 1.5.2 came to misresolve
	// every upgrade in every older replay (the v1.5.2 staleRefs regression). During
	// playback of a pre-V155 replay this returns (our binary's early registrations -
	// the recording binary's): add it to a recorded upgrade key to get the same
	// upgrade under our numbering.
	Int getReplayLegacyUpgradeKeyDelta() const;

	Int getGameMode() { return m_originalGameMode; }

	void logPlayerDisconnect(UnicodeString player, Int slot);
	void logCRCMismatch();
	Bool sawCRCMismatch() const;
	void cleanUpReplayFile();										///< after a crash, send replay/debug info to a central repository

	void setArchiveEnabled(Bool enable) { m_archiveReplays = enable; } ///< Enable or disable replay archiving.
	void stopRecording();															///< Stop recording and close m_file.
protected:
	void startRecording(GameDifficulty diff, Int originalGameMode, Int rankPoints, Int maxFPS);					///< Start recording to m_file.
	void writeToFile(GameMessage *msg);								///< Write this GameMessage to m_file.

	// Zulu replay extension block. Written immediately after maxFPS in the
	// header and consumed at the same point on read.
	void writeZuluReplayExtension();
	void readZuluReplayExtension();
	void archiveReplay(AsciiString fileName);					///< Move the specified replay file to the archive directory.

	void logGameStart(AsciiString options);
	void logGameEnd();

	AsciiString readAsciiString();										///< Read the next string from m_file using ascii characters.
	UnicodeString readUnicodeString();								///< Read the next string from m_file using unicode characters.
	void readNextFrame();															///< Read the next frame number to execute a command on.
	Bool appendNextCommand();													///< Read the next GameMessage and append it to TheCommandList. FALSE on a torn record.
	void writeArgument(GameMessageArgumentDataType type, const GameMessageArgumentType arg);
	Bool readReplayBytes(void *dst, Int size);				///< read exactly size bytes; on a short read set m_replayShortRead and return FALSE
	void rollbackTornRecord(Int posBefore);					///< a record could not be read whole: rewind to its start for the live-observer retry
	Bool abortTornRecord(Int posBefore);					///< give up on a record: retry it for a live stream, end the data otherwise. Always FALSE.
	Bool readArgument(GameMessageArgumentDataType type, GameMessage *msg);	///< FALSE on a short read.

	struct CullBadCommandsResult
	{
		CullBadCommandsResult() : hasClearGameDataMessage(false) {}
		Bool hasClearGameDataMessage;
	};

	CullBadCommandsResult cullBadCommands(); ///< prevent the user from giving mouse commands that he shouldn't be able to do during playback.

	CRCInfo m_crcInfo;
	File* m_file;
	AsciiString m_fileName;
	Int m_currentFilePosition;
	RecorderModeType m_mode;
	AsciiString m_currentReplayFilename;							///< valid during playback only
	UnsignedInt m_playbackFrameCount;

	ReplayGameInfo m_gameInfo;
	Bool m_wasDesync;

	Bool m_doingAnalysis;
	Bool m_archiveReplays;														///< if true, each replay is archived to the replay archive folder after recording

	Int m_originalGameMode; // valid in replays

	UnsignedInt m_replayAIFeatureVersion;						///< CURRENT for live games; loaded from the replay's extension block during playback.
	ReplayEpoch m_replayEpoch;											///< CURRENT for live games; derived from the replay's recorded version during playback.
	Int m_replayLegacyUpgradeKeyDelta;							///< Our binary's early-namekey count minus the recording binary's; see getReplayLegacyUpgradeKeyDelta().
	static Int s_replayEpochOverride;								///< -replayEpoch command-line override; <0 = auto-detect from header.
	UnsignedInt m_playbackStaleObjectRefs;					///< Replayed commands referencing objects missing from our world; see getPlaybackStaleObjectRefs().

	UnsignedInt m_nextFrame;												///< The Frame that the next message is to be executed on.  This can be -1.

	Bool m_replayShortRead;													///< set when a record could not be read in full
	Int  m_replayReadPos;														///< bytes consumed from the replay so far; File::position() is not reliable on every file class

	// live-observer state (see isLiveObserverMode). m_liveObserverStreamOpen is
	// owned by the stream transport; m_liveObserverArming covers the open-time
	// read inside playbackFile before the mode has flipped; the rest is the
	// EOF-wait / retry machinery in readNextFrame and updatePlayback.
	Bool        m_liveObserverStreamOpen;
	Bool        m_liveObserverArming;
	Bool        m_liveObserverWaitingForBytes;
	Int         m_liveObserverRetryPos;
	Bool        m_liveObserverFpsBoosted;
	Int         m_liveObserverSavedFpsLimit;
	Bool        m_liveObserverSavedUseFpsLimit;
	Int         m_liveObserverKnownLength;		///< bytes the transport has written so far (backlog = this - m_replayReadPos)
	Bool        m_liveObserverBytesArrived;		///< the transport appended since the last reopen attempt
	Bool        m_liveObserverTruncated;		///< a torn record with the stream closed: the file ends here
	UnsignedInt m_liveObserverLastReopenMs;
	Int         m_liveObserverEdgeMarginBytes;
	Bool        m_liveObserverPlayedAny;		///< at least one record of the live file has been consumed
	UnsignedInt m_liveObserverStarvedSinceMs;	///< wall clock when the byte starvation began, 0 while fed
	// resume-from-replay state (see the accessors above)
	File* m_resumeSourceFile;												///< the resume source (Resume.rep) being read during catchup; m_file is the new recording
	UnsignedInt m_resumeArmedHandoffFrame;					///< handoff frame for the next MSG_NEW_GAME, 0 when not armed
	UnsignedInt m_resumeArmedSourceLastFrame;				///< last complete frame of the resume source, from scanReplayLastFrame
	UnsignedInt m_resumeHandoffFrame;								///< frame at which injection stops and the freeze begins; stays set after the handoff for the CRC window
	UnsignedInt m_resumeSourceLastFrame;						///< injection never reads past this frame, so a torn tail cannot be replayed
	Bool m_resumeLeadInAnnounced;										///< the lead-in message has been shown
	UnsignedInt m_resumeFreezeStartMs;							///< wall-clock start of the freeze, 0 when not frozen
	Int m_resumeFreezeLastAnnounced;								///< last countdown second shown

	Bool startResumeCatchup();											///< open the resume source and switch to catchup; the recording is already started
	void updateResumeCatchup();											///< inject the source's commands for this frame, hand off at the handoff frame
	void injectResumeCommands(UnsignedInt curFrame);	///< read the source's records for curFrame into TheCommandList
	void endResumeCatchup();												///< close the source and leave catchup without handing off
	void abortResume(const UnicodeString &why);			///< leave the game: the source cannot be replayed to the handoff
	void clearResumeState();
};

extern RecorderClass *TheRecorder;
RecorderClass *createRecorder();
