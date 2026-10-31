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

// FILE: ScoreScreen.cpp /////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//                                                                          
//                       Electronic Arts Pacific.                          
//                                                                          
//                       Confidential Information                           
//                Copyright (C) 2002 - All Rights Reserved                  
//                                                                          
//-----------------------------------------------------------------------------
//
//	created:	Jun 2002
//
//	Filename: 	ScoreScreen.cpp
//
//	author:		Chris Huybregts
//	
//	purpose:	Gui callbacks for the Score Screen
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////

//-----------------------------------------------------------------------------
// SYSTEM INCLUDES ////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// USER INCLUDES //////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/AudioAffect.h"
#include "Common/AudioEventRTS.h"
#include "Common/AudioHandleSpecialValues.h"
#include "Common/BattleHonors.h"
#include "Common/GameEngine.h"
#include "Common/GameLOD.h"
#include "Common/GameState.h"
#include "Common/GameSpyMiscPreferences.h"
#include "Common/GlobalData.h"
#include "Common/NameKeyGenerator.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/Recorder.h"
#include "Common/ReplaySimulation.h"
#include "Common/ScoreKeeper.h"
#include "Common/SkirmishBattleHonors.h"
#include "Common/StatsUploader.h"
#include "Common/ThingFactory.h"
#include "GameLogic/FPUControl.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine.h"
#include "GameLogic/VictoryConditions.h"
#include "GameClient/Color.h"
#include "GameClient/Display.h"
#include "GameClient/DisplayString.h"
#include "GameClient/DisplayStringManager.h"
#include "GameClient/GameFont.h"
#include "GameClient/GUICallbacks.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/Gadget.h"
#include "GameClient/GameText.h"
#include "GameClient/Keyboard.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/CampaignManager.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/VideoPlayer.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/GameResultsThread.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/GameSpy/BuddyThread.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameClient/InGameUI.h"
#include "GameClient/ChallengeGenerals.h"

#include "../NGMP_interfaces.h"
#include "../OnlineServices_Init.h"
#include "../OnlineServices_StatsInterface.h"


//-----------------------------------------------------------------------------
// DEFINES ////////////////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
static NameKeyType parentID = NAMEKEY_INVALID;
static NameKeyType buttonOkID = NAMEKEY_INVALID;
///static NameKeyType buttonRehostID = NAMEKEY_INVALID;
static NameKeyType textEntryChatID = NAMEKEY_INVALID;
static NameKeyType buttonEmoteID = NAMEKEY_INVALID;
static NameKeyType chatBoxBorderID = NAMEKEY_INVALID;
static NameKeyType buttonContinueID = NAMEKEY_INVALID;
static NameKeyType buttonBuddiesID = NAMEKEY_INVALID;
static NameKeyType buttonSaveReplayID = NAMEKEY_INVALID;
static NameKeyType backdropID = NAMEKEY_INVALID;

static GameWindow *parent = nullptr;
static GameWindow *buttonOk = nullptr;
//static GameWindow *buttonRehost = nullptr;
static GameWindow *buttonContinue = nullptr;
static GameWindow *textEntryChat = nullptr;
static GameWindow *buttonEmote = nullptr;
static GameWindow *chatBoxBorder = nullptr;
static GameWindow *buttonBuddies = nullptr;
static GameWindow *staticTextGameSaved = nullptr;
static GameWindow *backdrop = nullptr;
static GameWindow *challengePortrait = nullptr;
static GameWindow *challengeRemarks = nullptr;
static GameWindow *challengeWinLossText = nullptr;
static GameWindow *gadgetParent = nullptr;

static Bool overidePlayerDisplayName = FALSE;

//External declarations
NameKeyType listboxChatWindowScoreScreenID = NAMEKEY_INVALID;
GameWindow *listboxChatWindowScoreScreen = nullptr;

NameKeyType listboxAcademyWindowScoreScreenID = NAMEKEY_INVALID;
GameWindow *listboxAcademyWindowScoreScreen = nullptr;
NameKeyType staticTextAcademyTitleID = NAMEKEY_INVALID;
GameWindow *staticTextAcademyTitle = nullptr;


std::string LastReplayFileName;
static Bool canSaveReplay = FALSE;
extern void PopupReplayUpdate(WindowLayout *layout, void *userData);

void initSinglePlayer( void );
void finishSinglePlayerInit( void );
static Bool s_needToFinishSinglePlayerInit = FALSE;
static Bool buttonIsFinishCampaign = FALSE;
static WindowLayout *s_blankLayout = nullptr;

void initSkirmish( void );
void initLANMultiPlayer(void);
void initInternetMultiPlayer(void);
void initReplayMultiPlayer(void);
void initReplaySinglePlayer(void);
void grabMultiPlayerInfo( void );
void grabSinglePlayerInfo( void );
void hideWindows( Int pos );
void ScoreScreenEnableControls(Bool enable);
void setObserverWindows( Player *player, Int i );
void displayChallengewinLoss( Image *imageGeneral, AsciiString strGeneral );
enum {
	SCORESCREEN_SINGLEPLAYER = 0,
	SCORESCREEN_SKIRMISH,
	SCORESCREEN_LAN,
	SCORESCREEN_INTERNET,
	SCORESCREEN_REPLAY
	};
static Int screenType;

// ---------------------------------------------------------------------------
// Radarvan match report (see RadarvanReportStart in StatsUploader.h).
//
// The win-probability chart draws in the stats rows a small game leaves
// empty; when the table is too full for it, a MATCH REPORT button swaps the
// table for the chart. The story - how the result compares with radarvan's
// pre-game prediction, then radarvan's headline and beats - goes into the
// text box under the table. Best-effort throughout: a match radarvan never
// gets just shows no report.
// ---------------------------------------------------------------------------
enum
{
	RV_CHART_MIN_ROWS  = 3,   ///< free stats rows the inline chart needs
	RV_MAX_MARKERS     = 9,   ///< beats numbered on the chart
	RV_LIVE_WAIT_SEC   = 150, ///< a match just played: its upload is in flight
	RV_REPLAY_WAIT_SEC = 15,  ///< an old replay: radarvan has it or it doesn't
	RV_COMEBACK_PCT    = 30   ///< winner's low point that counts as a comeback
};

enum
{
	RV_LBL_TITLE = 0,
	RV_LBL_RESULT,
	RV_LBL_STATUS,
	RV_LBL_Y100,
	RV_LBL_Y50,
	RV_LBL_Y0,
	RV_LBL_X0,
	RV_LBL_XEND,
	RV_LBL_PREGAME,
	RV_LBL_MARK0,
	RV_LBL_COUNT = RV_LBL_MARK0 + RV_MAX_MARKERS
};

struct RvView
{
	Bool active;          ///< a report was requested for this screen
	Bool ready;           ///< the report arrived and is laid out
	Bool showing;         ///< toggle mode: the chart is shown instead of the table
	Bool localView;       ///< the local player played and the result is known
	Bool localWon;
	Bool localIsA;        ///< the local player is on radarvan's team A
	Bool havePreGame;     ///< preGame is valid
	Real preGame;         ///< pre-game P(win) for the charted side
	Int  rowCount;        ///< stats rows in use
	Int  slotCount;       ///< players snapshotted for tinting story lines
	WideChar slotName[MAX_SLOTS][64];
	Color slotColor[MAX_SLOTS];
	GameWindow *canvas;
	GameWindow *toggle;
	GameWindow *storyList;
	Int  markerCount;
	Real markerMinute[RV_MAX_MARKERS];
	Int  curveCount;      ///< curve points, including the t=0 start
	Real curveMinute[RADARVAN_REPORT_MAX_POINTS + 1];
	Real curveProb[RADARVAN_REPORT_MAX_POINTS + 1]; ///< P(charted side wins)
	Color aheadColor;
	Color behindColor;
	DisplayString *labels[RV_LBL_COUNT];
	RadarvanReport report;
};
static RvView s_rv;
static Int s_scoreRowCount = 0; ///< rows grabMultiPlayerInfo filled

static UnicodeString rvFromUtf8(const char *s)
{
	UnicodeString out;
	WideChar buf[RADARVAN_REPORT_TEXT_LEN + 1];
	if (MultiByteToWideChar(CP_UTF8, 0, s, -1, buf, RADARVAN_REPORT_TEXT_LEN + 1) > 0)
		out = buf;
	return out;
}

static UnicodeString rvMinuteText(Real minute)
{
	Int secs = (Int)(minute * 60.0f + 0.5f);
	UnicodeString out;
	out.format(L"%d:%02d", secs / 60, secs % 60);
	return out;
}

static UnicodeString rvJoinNames(const char names[][RADARVAN_REPORT_NAME_LEN], Int count)
{
	UnicodeString out;
	Int i;
	for (i = 0; i < count; ++i)
	{
		if (i > 0)
			out.concat(L", ");
		out.concat(rvFromUtf8(names[i]));
	}
	return out;
}

static void rvSetLabel(Int idx, const UnicodeString &text, Bool big)
{
	if (s_rv.labels[idx] == nullptr)
	{
		s_rv.labels[idx] = TheDisplayStringManager->newDisplayString();
		if (s_rv.labels[idx] == nullptr)
			return;
		s_rv.labels[idx]->setFont(TheFontLibrary->getFont(AsciiString("Arial"), big ? 12 : 10, big));
	}
	s_rv.labels[idx]->setText(text);
}

static void rvLabelSize(Int idx, Int *w, Int *h)
{
	*w = 0;
	*h = 0;
	if (s_rv.labels[idx] != nullptr)
		s_rv.labels[idx]->getSize(w, h);
}

static void rvDrawLabel(Int idx, Int x, Int y, Color color)
{
	if (s_rv.labels[idx] != nullptr)
		s_rv.labels[idx]->draw(x, y, color, GameMakeColor(0, 0, 0, 255));
}

// P(charted side wins) at `minute`, interpolated along the curve.
static Real rvProbAt(Real minute)
{
	Int i;
	for (i = 1; i < s_rv.curveCount; ++i)
	{
		if (minute <= s_rv.curveMinute[i])
		{
			Real t0 = s_rv.curveMinute[i - 1];
			Real span = s_rv.curveMinute[i] - t0;
			Real f = (span > 0.0f) ? (minute - t0) / span : 1.0f;
			return s_rv.curveProb[i - 1] + (s_rv.curveProb[i] - s_rv.curveProb[i - 1]) * f;
		}
	}
	return s_rv.curveProb[s_rv.curveCount - 1];
}

static void rvDrawChart(GameWindow *win, WinInstanceData *)
{
	if (win == nullptr || TheDisplay == nullptr)
		return;

	Int x, y, w, h;
	win->winGetScreenPosition(&x, &y);
	win->winGetSize(&w, &h);
	TheDisplay->drawFillRect(x, y, w, h, GameMakeColor(6, 8, 12, 200));
	TheDisplay->drawOpenRect(x, y, w, h, 1.0f, GameMakeColor(120, 140, 160, 200));

	Int tw, th;
	if (!s_rv.ready || s_rv.curveCount < 2)
	{
		rvLabelSize(RV_LBL_STATUS, &tw, &th);
		rvDrawLabel(RV_LBL_STATUS, x + (w - tw) / 2, y + (h - th) / 2, GameMakeColor(200, 210, 220, 255));
		return;
	}

	const Int pad = 6;
	Int titleH, resultW, resultH, gutterW, xLabelH;
	rvLabelSize(RV_LBL_TITLE, &tw, &titleH);
	rvLabelSize(RV_LBL_RESULT, &resultW, &resultH);
	rvLabelSize(RV_LBL_Y100, &gutterW, &th);
	rvLabelSize(RV_LBL_X0, &tw, &xLabelH);

	rvDrawLabel(RV_LBL_TITLE, x + pad, y + pad, GameMakeColor(230, 230, 230, 255));
	rvDrawLabel(RV_LBL_RESULT, x + w - pad - resultW, y + pad,
		s_rv.localView ? (s_rv.localWon ? s_rv.aheadColor : s_rv.behindColor) : GameMakeColor(255, 200, 60, 255));

	const Int px = x + pad + gutterW + 4;
	const Int py = y + pad + titleH + 4;
	const Int pw = x + w - pad - px;
	const Int ph = y + h - pad - xLabelH - 2 - py;
	if (pw < 40 || ph < 20)
		return;

	const Real tMax = s_rv.curveMinute[s_rv.curveCount - 1];
	const Int midY = py + ph / 2;
	const Color grid = GameMakeColor(150, 160, 170, 90);

	// 0% / 100% rails and the dashed 50% line.
	TheDisplay->drawLine(px, py, px + pw, py, 1.0f, grid);
	TheDisplay->drawLine(px, py + ph, px + pw, py + ph, 1.0f, grid);
	Int dx;
	for (dx = 0; dx < pw; dx += 8)
		TheDisplay->drawLine(px + dx, midY, px + ((dx + 4 < pw) ? dx + 4 : pw), midY, 1.0f, GameMakeColor(200, 200, 200, 140));

	// Shade between the curve and 50%: ahead colour above, behind colour below.
	Color aheadFill = (s_rv.aheadColor & 0x00FFFFFF) | (90 << 24);
	Color behindFill = (s_rv.behindColor & 0x00FFFFFF) | (90 << 24);
	for (dx = 0; dx <= pw; ++dx)
	{
		Real q = rvProbAt(tMax * (Real)dx / (Real)pw);
		Int cy = py + (Int)((1.0f - q) * ph);
		if (cy < midY)
			TheDisplay->drawFillRect(px + dx, cy, 1, midY - cy, aheadFill);
		else if (cy > midY)
			TheDisplay->drawFillRect(px + dx, midY, 1, cy - midY, behindFill);
	}

	// Beat markers: a faint vertical rule with its number along the top.
	Int k;
	for (k = 0; k < s_rv.markerCount; ++k)
	{
		Int mx = px + (Int)(pw * s_rv.markerMinute[k] / tMax);
		Int my = py + (Int)((1.0f - rvProbAt(s_rv.markerMinute[k])) * ph);
		TheDisplay->drawLine(mx, py, mx, py + ph, 1.0f, GameMakeColor(255, 200, 60, 70));
		TheDisplay->drawFillRect(mx - 2, my - 2, 5, 5, GameMakeColor(255, 200, 60, 255));
		rvLabelSize(RV_LBL_MARK0 + k, &tw, &th);
		rvDrawLabel(RV_LBL_MARK0 + k, mx - tw / 2, py + 1, GameMakeColor(255, 200, 60, 255));
	}

	// The curve itself.
	Int i;
	for (i = 1; i < s_rv.curveCount; ++i)
	{
		Int x0 = px + (Int)(pw * s_rv.curveMinute[i - 1] / tMax);
		Int x1 = px + (Int)(pw * s_rv.curveMinute[i] / tMax);
		Int y0 = py + (Int)((1.0f - s_rv.curveProb[i - 1]) * ph);
		Int y1 = py + (Int)((1.0f - s_rv.curveProb[i]) * ph);
		TheDisplay->drawLine(x0, y0, x1, y1, 2.0f, GameMakeColor(235, 235, 235, 255));
	}

	// Radarvan's pre-game prediction is where the curve starts.
	if (s_rv.havePreGame)
	{
		Int dy = py + (Int)((1.0f - s_rv.preGame) * ph);
		TheDisplay->drawFillRect(px - 3, dy - 3, 7, 7, GameMakeColor(255, 200, 60, 255));
		TheDisplay->drawOpenRect(px - 3, dy - 3, 7, 7, 1.0f, GameMakeColor(0, 0, 0, 255));
		rvLabelSize(RV_LBL_PREGAME, &tw, &th);
		Int ly = (s_rv.preGame >= 0.5f) ? dy + 5 : dy - 5 - th;
		rvDrawLabel(RV_LBL_PREGAME, px + 6, ly, GameMakeColor(255, 200, 60, 255));
	}

	// Axes.
	const Color axis = GameMakeColor(170, 180, 190, 255);
	rvLabelSize(RV_LBL_Y100, &tw, &th);
	rvDrawLabel(RV_LBL_Y100, px - 4 - tw, py - th / 2, axis);
	rvLabelSize(RV_LBL_Y50, &tw, &th);
	rvDrawLabel(RV_LBL_Y50, px - 4 - tw, midY - th / 2, axis);
	rvLabelSize(RV_LBL_Y0, &tw, &th);
	rvDrawLabel(RV_LBL_Y0, px - 4 - tw, py + ph - th / 2, axis);
	rvDrawLabel(RV_LBL_X0, px, py + ph + 2, axis);
	rvLabelSize(RV_LBL_XEND, &tw, &th);
	rvDrawLabel(RV_LBL_XEND, px + pw - tw, py + ph + 2, axis);
}

static void rvShowReport(Bool show)
{
	s_rv.showing = show;
	if (s_rv.canvas)
		s_rv.canvas->winHide(!show);
	if (gadgetParent)
		gadgetParent->winHide(show);
	if (s_rv.toggle)
		GadgetButtonSetText(s_rv.toggle, show ? UnicodeString(L"SCORES") : UnicodeString(L"MATCH REPORT"));
}

static Int rvAddStoryLine(const UnicodeString &text, Color color)
{
	return GadgetListBoxAddEntryText(s_rv.storyList, text, color, -1);
}

// Colour of the player whose display name matches radarvan's canonical name;
// the default text colour when nobody does (radarvan may know a player by
// another name).
static Color rvPlayerColor(const char *canonical)
{
	if (canonical[0] == '\0')
		return GameSpyColor[GSCOLOR_DEFAULT];
	UnicodeString name = rvFromUtf8(canonical);
	Int i;
	for (i = 0; i < s_rv.slotCount; ++i)
	{
		if (name.compareNoCase(s_rv.slotName[i]) == 0)
			return s_rv.slotColor[i];
	}
	return GameSpyColor[GSCOLOR_DEFAULT];
}

static void rvWriteStory(void)
{
	const RadarvanReport &r = s_rv.report;
	if (s_rv.storyList == nullptr || !r.hasStory)
		return;
	if (s_rv.storyList->winIsHidden())
	{
		// Replays show neither text box; give the story the academy one.
		s_rv.storyList->winHide(FALSE);
		if (chatBoxBorder)
			chatBoxBorder->winHide(FALSE);
	}

	const Color gold = GameMakeColor(255, 200, 60, 255);
	const Color plain = GameSpyColor[GSCOLOR_DEFAULT];
	UnicodeString line;
	Int first = rvAddStoryLine(UnicodeString(L"RADARVAN MATCH REPORT"), gold);

	// How the result compares with the pre-game prediction.
	if (s_rv.havePreGame)
	{
		Int pct = (Int)(s_rv.preGame * 100.0f + 0.5f);
		if (s_rv.localView)
		{
			Bool favored = s_rv.preGame > 0.5f;
			if (pct >= 45 && pct <= 55)
				line.format(L"Radarvan gave you %d%% before the game: a coin flip.", pct);
			else if (favored == s_rv.localWon)
				line.format(L"Radarvan gave you %d%% before the game. As predicted.", pct);
			else if (s_rv.localWon)
				line.format(L"Radarvan gave you %d%% before the game. UPSET WIN!", pct);
			else
				line.format(L"Radarvan gave you %d%% before the game. Upset loss.", pct);
			rvAddStoryLine(line, (s_rv.localWon && !favored && pct < 45) ? gold : plain);
		}
		else if (r.hasCurve)
		{
			Bool aFavored = s_rv.preGame >= 0.5f;
			Int favPct = aFavored ? pct : 100 - pct;
			UnicodeString fav = aFavored ? rvJoinNames(r.teamA, r.teamACount) : rvJoinNames(r.teamB, r.teamBCount);
			line.format(L"Radarvan's pre-game pick: %s at %d%%. %s", fav.str(), favPct,
				(aFavored == r.teamAWon) ? L"Called it." : L"UPSET!");
			rvAddStoryLine(line, (aFavored == r.teamAWon) ? plain : gold);
		}
	}

	// The winners' lowest point, when it was low enough to call a comeback.
	if (r.hasCurve)
	{
		Real low = 1.0f, lowMinute = 0.0f;
		Int i;
		for (i = 0; i < r.pointCount; ++i)
		{
			Real q = r.teamAWon ? r.pointProbA[i] : 1.0f - r.pointProbA[i];
			if (q < low)
			{
				low = q;
				lowMinute = r.pointMinute[i];
			}
		}
		Int lowPct = (Int)(low * 100.0f + 0.5f);
		if (lowPct < RV_COMEBACK_PCT)
		{
			UnicodeString at = rvMinuteText(lowMinute);
			if (s_rv.localView && s_rv.localWon)
				line.format(L"Comeback! You were down to %d%% at %s.", lowPct, at.str());
			else if (s_rv.localView)
				line.format(L"So close: they were down to %d%% at %s.", lowPct, at.str());
			else
				line.format(L"Comeback: the winners were down to %d%% at %s.", lowPct, at.str());
			rvAddStoryLine(line, gold);
		}
	}

	rvAddStoryLine(rvFromUtf8(r.headline), GameMakeColor(235, 235, 235, 255));

	Int b, marker = 0;
	for (b = 0; b < r.beatCount; ++b)
	{
		const RadarvanReportBeat &beat = r.beats[b];
		line.clear();
		if (beat.atMinute >= 0.0f)
		{
			line = rvMinuteText(beat.atMinute);
			line.concat(L"  ");
			if (marker < s_rv.markerCount)
			{
				UnicodeString num;
				num.format(L"[%d] ", ++marker);
				line.concat(num);
			}
		}
		line.concat(rvFromUtf8(beat.text));
		rvAddStoryLine(line, rvPlayerColor(beat.playerName));
	}

	if (first >= 0)
		GadgetListBoxSetTopVisibleEntry(s_rv.storyList, first);
}

// Turn the report into the curve, markers and labels the chart draws.
static void rvLayout(void)
{
	const RadarvanReport &r = s_rv.report;

	// Whose side the chart is drawn from. The local player won exactly when
	// their team is the team that won, which pins them to A or B without
	// matching names (radarvan's canonical names can differ from in-game ones).
	Bool sideKnown = FALSE;
	if (s_rv.localView && r.hasCurve)
	{
		s_rv.localIsA = (r.teamAWon == s_rv.localWon);
		sideKnown = TRUE;
	}

	// LAN games keep the exact number the loading screen showed; otherwise
	// ask radarvan's prediction, which only makes sense once we know the side.
	if (!s_rv.havePreGame && r.hasPrediction && (sideKnown || !s_rv.localView))
	{
		s_rv.preGame = (s_rv.localView && !s_rv.localIsA) ? 1.0f - r.predProbA : r.predProbA;
		s_rv.havePreGame = TRUE;
	}

	if (r.hasCurve)
	{
		Bool flip = s_rv.localView && !s_rv.localIsA;
		Int i;
		s_rv.curveMinute[0] = 0.0f;
		s_rv.curveProb[0] = s_rv.havePreGame ? s_rv.preGame : (flip ? 1.0f - r.pointProbA[0] : r.pointProbA[0]);
		s_rv.curveCount = 1;
		for (i = 0; i < r.pointCount; ++i)
		{
			s_rv.curveMinute[s_rv.curveCount] = r.pointMinute[i];
			s_rv.curveProb[s_rv.curveCount] = flip ? 1.0f - r.pointProbA[i] : r.pointProbA[i];
			++s_rv.curveCount;
		}

		Real tMax = s_rv.curveMinute[s_rv.curveCount - 1];
		Int b;
		s_rv.markerCount = 0;
		for (b = 0; b < r.beatCount && s_rv.markerCount < RV_MAX_MARKERS; ++b)
		{
			if (r.beats[b].atMinute < 0.0f)
				continue;
			Real m = r.beats[b].atMinute;
			s_rv.markerMinute[s_rv.markerCount] = (m < tMax) ? m : tMax;
			UnicodeString num;
			num.format(L"%d", s_rv.markerCount + 1);
			rvSetLabel(RV_LBL_MARK0 + s_rv.markerCount, num, TRUE);
			++s_rv.markerCount;
		}

		UnicodeString text;
		if (s_rv.localView)
		{
			s_rv.aheadColor = GameMakeColor(60, 180, 70, 255);
			s_rv.behindColor = GameMakeColor(200, 60, 60, 255);
			rvSetLabel(RV_LBL_TITLE, UnicodeString(L"YOUR WIN CHANCE"), TRUE);
			rvSetLabel(RV_LBL_RESULT, s_rv.localWon ? UnicodeString(L"VICTORY") : UnicodeString(L"DEFEAT"), TRUE);
		}
		else
		{
			s_rv.aheadColor = GameMakeColor(60, 120, 220, 255);
			s_rv.behindColor = GameMakeColor(200, 60, 60, 255);
			text.format(L"WIN CHANCE: %s", rvJoinNames(r.teamA, r.teamACount).str());
			rvSetLabel(RV_LBL_TITLE, text, TRUE);
			text.format(L"%s WON", r.teamAWon ? rvJoinNames(r.teamA, r.teamACount).str() : rvJoinNames(r.teamB, r.teamBCount).str());
			rvSetLabel(RV_LBL_RESULT, text, TRUE);
		}
		rvSetLabel(RV_LBL_Y100, UnicodeString(L"100%"), FALSE);
		rvSetLabel(RV_LBL_Y50, UnicodeString(L"50%"), FALSE);
		rvSetLabel(RV_LBL_Y0, UnicodeString(L"0%"), FALSE);
		rvSetLabel(RV_LBL_X0, UnicodeString(L"0:00"), FALSE);
		rvSetLabel(RV_LBL_XEND, rvMinuteText(tMax), FALSE);
		if (s_rv.havePreGame)
		{
			text.format(L"pre-game %d%%", (Int)(s_rv.preGame * 100.0f + 0.5f));
			rvSetLabel(RV_LBL_PREGAME, text, FALSE);
		}
	}
	s_rv.ready = TRUE;
}

// Screen rect of stats row `row` (only its vertical extent is used).
static Bool rvRowRect(Int row, Int *top, Int *bottom)
{
	AsciiString name;
	name.format("ScoreScreen.wnd:StaticTextPlayer%d", row);
	GameWindow *win = TheWindowManager->winGetWindowFromId(parent, TheNameKeyGenerator->nameToKey(name));
	if (win == nullptr)
		return FALSE;
	Int x, y, w, h;
	win->winGetScreenPosition(&x, &y);
	win->winGetSize(&w, &h);
	*top = y;
	*bottom = y + h;
	return TRUE;
}

static void rvShutdown(void)
{
	RadarvanReportReset();
	if (s_rv.canvas)
		TheWindowManager->winDestroy(s_rv.canvas);
	if (s_rv.toggle)
		TheWindowManager->winDestroy(s_rv.toggle);
	Int i;
	for (i = 0; i < RV_LBL_COUNT; ++i)
	{
		if (s_rv.labels[i])
			TheDisplayStringManager->freeDisplayString(s_rv.labels[i]);
	}
	memset(&s_rv, 0, sizeof(s_rv));
}

// Ask radarvan for the report and set up where it will show. Called once the
// screen is laid out, while the game's logic and players still exist.
static void rvBegin(void)
{
	rvShutdown();
	if (parent == nullptr || backdrop == nullptr || TheGlobalData->m_radarvanApiUrl.isEmpty())
		return;
	if (screenType == SCORESCREEN_REPLAY && !TheRecorder->isMultiplayer())
		return;
	if (ReplaySimulation::getReplayCount() > 0)
		return; // headless replay runs have nobody to show it to

	// Radarvan only gets games with two or more humans. The players are gone
	// by the first update (clearGameData), so keep what the story needs now.
	Int humans = 0, i;
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		AsciiString key;
		key.format("player%d", i);
		Player *p = ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey(key));
		if (p == nullptr || p->isPlayerObserver())
			continue;
		if (p->getPlayerType() == PLAYER_HUMAN)
			++humans;
		wcsncpy(s_rv.slotName[s_rv.slotCount], p->getPlayerDisplayName().str(), 63);
		s_rv.slotName[s_rv.slotCount][63] = 0;
		s_rv.slotColor[s_rv.slotCount] = GameMakeColorReadable(p->getPlayerColor());
		++s_rv.slotCount;
	}
	if (humans < 2)
		return;

	Player *local = ThePlayerList->getLocalPlayer();
	s_rv.localWon = TheVictoryConditions->isLocalAlliedVictory();
	s_rv.localView = screenType != SCORESCREEN_REPLAY && local != nullptr && !local->isPlayerObserver()
		&& (s_rv.localWon || TheVictoryConditions->isLocalAlliedDefeat());

	// The loading screen's own number, so the end screen agrees with it.
	// Only LAN games start the intel worker, and each one resets it.
	RadarvanIntelData intel;
	if (s_rv.localView && screenType == SCORESCREEN_LAN && RadarvanIntelReadyData(intel) && intel.localTeam != 0)
	{
		s_rv.preGame = (intel.favoredTeam == intel.localTeam) ? intel.favoredWinProb : 1.0f - intel.favoredWinProb;
		s_rv.havePreGame = TRUE;
	}

	s_rv.storyList = (screenType == SCORESCREEN_LAN) ? listboxChatWindowScoreScreen : listboxAcademyWindowScoreScreen;

	// Inline in the empty rows when there's room, else behind a toggle that
	// gives the chart the whole table.
	s_rv.rowCount = s_scoreRowCount;
	Bool inlineChart = (MAX_SLOTS - s_rv.rowCount) >= RV_CHART_MIN_ROWS;
	Int top, bottom, unused;
	if (!rvRowRect(inlineChart ? s_rv.rowCount : 0, &top, &unused) || !rvRowRect(MAX_SLOTS - 1, &unused, &bottom))
		return;
	Int bx, by, bw, bh, ox, oy;
	backdrop->winGetScreenPosition(&bx, &by);
	backdrop->winGetSize(&bw, &bh);
	parent->winGetScreenPosition(&ox, &oy);
	Int inset = bw / 40;
	WinInstanceData inst;
	inst.init();
	s_rv.canvas = TheWindowManager->winCreate(parent, WIN_STATUS_ENABLED | WIN_STATUS_NO_INPUT,
		bx + inset - ox, top + 4 - oy, bw - 2 * inset, bottom - top - 6, nullptr, &inst);
	if (s_rv.canvas == nullptr)
		return;
	s_rv.canvas->winSetDrawFunc(rvDrawChart);
	rvSetLabel(RV_LBL_STATUS, UnicodeString(L"Radarvan is analysing the match..."), FALSE);

	if (!inlineChart)
	{
		// Above the top-right corner of the backdrop, clear of the table header.
		Int btnW = bw / 5, btnH = by * 4 / 5;
		WinInstanceData btn;
		btn.init();
		BitSet(btn.m_style, GWS_PUSH_BUTTON | GWS_MOUSE_TRACK);
		btn.m_textLabelString = "MATCH REPORT";
		s_rv.toggle = TheWindowManager->gogoGadgetPushButton(parent, WIN_STATUS_ENABLED | WIN_STATUS_IMAGE,
			bx + bw - btnW - ox, (by - btnH) / 2 - oy, btnW, btnH, &btn, nullptr, TRUE);
		rvShowReport(FALSE);
	}

	s_rv.active = TRUE;
	RadarvanReportStart(TheGlobalData->m_radarvanApiUrl, (Int)GetGameLogicRandomSeed(),
		screenType == SCORESCREEN_REPLAY ? RV_REPLAY_WAIT_SEC : RV_LIVE_WAIT_SEC);
}

static void rvUpdate(void)
{
	if (!s_rv.active || s_rv.ready)
		return;
	RadarvanReportState state = RadarvanReportPoll(s_rv.report);
	if (state == RADARVAN_REPORT_WAITING)
		return;
	if (state == RADARVAN_REPORT_READY)
	{
		rvLayout();
		rvWriteStory();
	}
	if (state != RADARVAN_REPORT_READY || s_rv.curveCount < 2)
	{
		// Nothing to chart: give the rows back.
		if (s_rv.showing)
			rvShowReport(FALSE);
		if (s_rv.canvas)
			s_rv.canvas->winHide(TRUE);
		if (s_rv.toggle)
			s_rv.toggle->winHide(TRUE);
	}
	s_rv.active = FALSE;
}

struct ScoreGather
{
	Int m_totalMoneyEarned;						///< The total money that was harvested, refined, received in crates
	Int m_totalMoneySpent;						///< The total money spent on units, buildings, repairs
	Int m_totalUnitsDestroyed;				///< The total number of enimies that we've killed
	Int m_totalUnitsBuilt;						///< The total number of units we've created (created meaning that we built from a building)
	Int m_totalUnitsLost;							///< The total number of our units lost
	Int m_totalBuildingsDestroyed;		///< The total number of Buildings we've destroyed
	Int m_totalBuildingsBuilt;				///< The total number of buildings we've constructed
	Int m_totalBuildingsLost;					///< The total number of our buildings lost

	// Aggregate value (sum of buildCost) totals. The four built/lost
	// totals replace their count siblings on the score screen; the two
	// destroyed totals feed the efficiency column (value killed / income).
	Int m_totalUnitsBuiltValue;
	Int m_totalUnitsLostValue;
	Int m_totalUnitsDestroyedValue;
	Int m_totalBuildingsBuiltValue;
	Int m_totalBuildingsLostValue;
	Int m_totalBuildingsDestroyedValue;

	const Image *m_sideImage;
};

// Column averages computed across all displayed rows (defeated included,
// observers excluded). Used to render the per-cell delta-from-average that
// trails each value, e.g. "$1.20k (-15%)".
struct ScoreAverages
{
	Int unitsBuiltValue;
	Int unitsLostValue;
	Int unitsKilledValue;
	Int bldgBuiltValue;
	Int bldgLostValue;
	Int bldgKilledValue;
	Int suppliesCollected;
	Int efficiencyPct;
};

static void zeroAverages(ScoreAverages &a)
{
	a.unitsBuiltValue = 0;
	a.unitsLostValue = 0;
	a.unitsKilledValue = 0;
	a.bldgBuiltValue = 0;
	a.bldgLostValue = 0;
	a.bldgKilledValue = 0;
	a.suppliesCollected = 0;
	a.efficiencyPct = 0;
}

// Per-column min/max captured across the displayed rows. Used to outline
// the "best" cell in green and the "worst" in red for each column. Loss
// columns invert: the lowest loss is best, the highest is worst.
struct ScoreExtremes
{
	Int unitsBuiltMin, unitsBuiltMax;
	Int unitsLostMin, unitsLostMax;
	Int unitsKilledMin, unitsKilledMax;
	Int bldgBuiltMin, bldgBuiltMax;
	Int bldgLostMin, bldgLostMax;
	Int bldgKilledMin, bldgKilledMax;
	Int suppliesMin, suppliesMax;
	Int efficiencyMin, efficiencyMax;
};

static void seedExtreme(Int v, Int &lo, Int &hi, Bool seen)
{
	if (!seen)
	{
		lo = v;
		hi = v;
		return;
	}
	if (v < lo) lo = v;
	if (v > hi) hi = v;
}

static const Color kHighlightBest    = GameMakeColor( 60, 200,  60, 255);  // muted green border
static const Color kHighlightWorst   = GameMakeColor(220,  60,  60, 255);  // muted red border
static const Color kHighlightDefault = GameMakeColor(  2,   1,   1,   0);  // matches .wnd transparent default

// Outline the cell green if its value is the column's "best" and red if it
// is the column's "worst". lowIsBest=TRUE inverts the comparison for loss
// columns. When every row is tied (lo == hi) we clear back to default so
// the screen does not light up like a Christmas tree.
static void applyCellHighlight(GameWindow *win, Int value, Int lo, Int hi, Bool lowIsBest)
{
	if (!win)
		return;
	Color border = kHighlightDefault;
	if (lo != hi)
	{
		const Int best  = lowIsBest ? lo : hi;
		const Int worst = lowIsBest ? hi : lo;
		if (value == best)
			border = kHighlightBest;
		else if (value == worst)
			border = kHighlightWorst;
	}
	win->winSetEnabledBorderColor(0, border);
}

// Per-player efficiency in whole percent. Zero income means undefined; we
// fold it to 0 so the row still participates in the average.
static Int computeEfficiencyPct(Int killedValue, Int earned)
{
	if (earned <= 0)
		return 0;
	return (killedValue * 100) / earned;
}

// Format a non-negative dollar amount as "$0" or "$X.XXk" with three
// significant figures, growing the integer part as the value grows
// (e.g. 1000 -> $1.00k, 12345 -> $12.3k, 123456789 -> $123000k). Negatives
// and zero render as "$0".
static void formatDollarsK(Int value, UnicodeString &out)
{
	if (value <= 0)
	{
		out.set(L"$0");
		return;
	}
	if (value < 9995)
	{
		out.format(L"$%.2fk", value / 1000.0);
	}
	else if (value < 99950)
	{
		out.format(L"$%.1fk", value / 1000.0);
	}
	else
	{
		// Three sig figs as an integer in thousands. Round directly from
		// the dollar value so the result is not skewed by a prior rounding
		// pass (e.g. $1,234,567 -> $1230k, not $1240k).
		Int divisor = 1;
		Int probe = value / 100000;
		while (probe >= 10) { divisor *= 10; probe /= 10; }
		Int unit = divisor * 1000;
		Int rounded = ((value + unit / 2) / unit) * divisor;
		out.format(L"$%dk", rounded);
	}
}

// Append " (+X%)", " (-X%)", or " (0%)" to a cell value based on the row's
// delta from the column average. Whole percent only, signed.
static void appendPercentDelta(Int value, Int avg, UnicodeString &out)
{
	Int pct = 0;
	if (avg > 0)
	{
		Int diff = value - avg;
		if (diff >= 0)
			pct = (diff * 100 + avg / 2) / avg;
		else
			pct = -(((-diff) * 100 + avg / 2) / avg);
	}
	UnicodeString delta;
	if (pct > 0)
		delta.format(L" (+%d%%)", pct);
	else if (pct < 0)
		delta.format(L" (%d%%)", pct);
	else
		delta.set(L" (0%)");
	out.concat(delta);
}

// Compose "$X.XXk (+Y%)" for a dollar-value column cell.
static void formatDollarCell(Int value, Int avg, UnicodeString &out)
{
	formatDollarsK(value, out);
	appendPercentDelta(value, avg, out);
}

// Compose a two-line cell with the raw count stacked above the dollar value:
//   12
//   $1.20k (-15%)
// The static-text renderer treats '\n' as a hard line break, and the data
// cells carry WRAP_CENTERED so each line centers individually.
static void formatCountAndValueCell(Int count, Int value, Int avg, UnicodeString &out)
{
	UnicodeString dollarLine;
	formatDollarCell(value, avg, dollarLine);
	out.format(L"%d\n%s", count, dollarLine.str());
}

// Compose "Z% (+Y%)" for the efficiency column cell. Empty-income players
// render as "0% (...)" since the average treats them the same way.
static void formatEfficiencyCell(Int killedValue, Int earned, Int avgPct, UnicodeString &out)
{
	Int pct = computeEfficiencyPct(killedValue, earned);
	out.format(L"%d%%", pct);
	appendPercentDelta(pct, avgPct, out);
}

void populateSideInfo( UnicodeString side, ScoreGather *sg, Int pos, Color color, const ScoreAverages &avgs);
//-----------------------------------------------------------------------------
// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

void startNextCampaignGame(void)
{
	TheShell->popImmediate();
	TheShell->hideShell();
	TheWritableGlobalData->m_pendingFile = TheCampaignManager->getCurrentMap();
	if (TheCampaignManager->getCurrentCampaign() && TheCampaignManager->getCurrentCampaign()->isChallengeCampaign())
	{
		DEBUG_ASSERTCRASH( TheChallengeGameInfo, ("TheChallengeGameInfo doesn't exist.") );
		TheChallengeGameInfo->init();  
		TheChallengeGameInfo->clearSlotList();
		TheChallengeGameInfo->reset();
		TheChallengeGameInfo->enterGame();
		TheChallengeGameInfo->setMap(TheCampaignManager->getCurrentMap());

		Int templateNum = TheChallengeGenerals->getCurrentPlayerTemplateNum();
		const PlayerTemplate *playerTemplate = ThePlayerTemplateStore->getNthPlayerTemplate(templateNum);
		GameSlot slot;
		slot.setState(SLOT_PLAYER, playerTemplate->getDisplayName());
		slot.setPlayerTemplate(templateNum);
		TheChallengeGameInfo->setSlot(0, slot);
		
		if (TheGameLogic->isInGame())
			TheGameLogic->clearGameData();
	}

	// send a message to the logic for a new game
	GameMessage *msg = TheMessageStream->appendMessage( GameMessage::MSG_NEW_GAME );
	msg->appendIntegerArgument(GAME_SINGLE_PLAYER);
	msg->appendIntegerArgument(TheCampaignManager->getGameDifficulty());
	msg->appendIntegerArgument(TheCampaignManager->getRankPoints());
	
	InitRandom(0);
}


void ScoreScreenEnableControls(Bool enable)
{
	// if we are using the button, do the enable thing.
	if ((buttonOk != nullptr) && (buttonOk->winIsHidden() == FALSE)) {
		buttonOk->winEnable(enable);
	}

	if ((buttonContinue != nullptr) && (buttonContinue->winIsHidden() == FALSE)) {
		buttonContinue->winEnable(enable);
	}

	if ((buttonBuddies != nullptr) && (buttonBuddies->winIsHidden() == FALSE)) {
		buttonBuddies->winEnable(enable);
	}

	GameWindow *buttonSaveReplay = TheWindowManager->winGetWindowFromId( parent, buttonSaveReplayID );
	if ((buttonSaveReplay != nullptr) && (buttonSaveReplay->winIsHidden() == FALSE)) {
		if (!canSaveReplay)
			enable = FALSE;
		buttonSaveReplay->winEnable(enable);
	}
}

// Addon bigs (ControlBarPro) ship their own ScoreScreen.wnd with larger fonts, which overflow the
// grid. This table mirrors the FONT entries of the retail ScoreScreen.wnd and wins over whatever
// .wnd the file system loaded. Keep it in sync with assets/Window/Menus/ScoreScreen.wnd.
static GameFont *getClassicScoreScreenFont( const AsciiString& decoratedName )
{
	if( TheFontLibrary == nullptr || decoratedName.isEmpty() )
		return nullptr;

	const char *shortName = strchr( decoratedName.str(), ':' );
	if( shortName == nullptr || *(++shortName) == '\0' )
		return nullptr;

	if( strcmp( shortName, "StaticTextWarSchool" ) == 0 )
		return TheFontLibrary->getFont( AsciiString("Arial"), 14, TRUE );

	if( strcmp( shortName, "GeneralRemarks" ) == 0 )
		return TheFontLibrary->getFont( AsciiString("Arial"), 14, FALSE );

	if( strcmp( shortName, "ChallengeWinLossText" ) == 0 )
		return TheFontLibrary->getFont( AsciiString("Generals"), 20, FALSE );

	if( strcmp( shortName, "ButtonOk" ) == 0
			|| strcmp( shortName, "ButtonBuddy" ) == 0
			|| strcmp( shortName, "ButtonContinue" ) == 0
			|| strcmp( shortName, "ButtonSaveReplay" ) == 0 )
		return TheFontLibrary->getFont( AsciiString("Generals"), 15, FALSE );

	// the chat area keeps the retail size; a bigger font just fits fewer lines in the box
	if( strcmp( shortName, "TextEntryChat" ) == 0
			|| strcmp( shortName, "ButtonEmote" ) == 0
			|| strcmp( shortName, "ListboxChatWindowScoreScreen" ) == 0
			|| strcmp( shortName, "ListboxWarschoolAdvice" ) == 0 )
		return TheFontLibrary->getFont( AsciiString("Arial"), 10, FALSE );

	// every score grid cell, column header and observer row
	if( strncmp( shortName, "StaticText", 10 ) == 0 )
		return TheFontLibrary->getFont( AsciiString("Arial"), 12, FALSE );

	return TheFontLibrary->getFont( AsciiString("Times New Roman"), 14, FALSE );
}

static void enforceClassicScoreScreenFonts( GameWindow *window )
{
	GameFont *font = getClassicScoreScreenFont( window->winGetInstanceData()->m_decoratedNameString );
	if( font )
		window->winSetFont( font );

	GameWindow *child;
	for( child = window->winGetChild(); child; child = child->winGetNext() )
		enforceClassicScoreScreenFonts( child );
}

extern Bool DontShowMainMenu; //KRIS
Bool g_playMusic = FALSE;
Bool ReplayWasPressed = FALSE;

Bool g_bNeedToTakeDoneEOGScreenshot = FALSE;
int64_t g_TimeEnterState = -1;
/** Initialize the ScoreScreen */
//-------------------------------------------------------------------------------------------------
void ScoreScreenInit( WindowLayout *layout, void *userData )
{
	//Play music after subsystems get reset including the audio...
	g_playMusic = TRUE;
	g_bNeedToTakeDoneEOGScreenshot = FALSE;
	
	if (TheGameSpyInfo)
	{
		DEBUG_LOG(("ScoreScreenInit(): TheGameSpyInfo->stuff(%s/%s/%s)", TheGameSpyInfo->getLocalBaseName().str(), TheGameSpyInfo->getLocalEmail().str(), TheGameSpyInfo->getLocalPassword().str()));
	}

	DontShowMainMenu = TRUE; //KRIS
	buttonIsFinishCampaign = FALSE;

	//Store the keys so we have them later
	parentID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ParentScoreScreen" );
	buttonOkID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ButtonOk" );
	textEntryChatID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:TextEntryChat" );
	buttonEmoteID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ButtonEmote" );
	listboxChatWindowScoreScreenID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ListboxChatWindowScoreScreen" );
	listboxAcademyWindowScoreScreenID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ListboxWarschoolAdvice" );
	staticTextAcademyTitleID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:StaticTextWarSchool" );
//	buttonRehostID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ButtonRehost" );
	chatBoxBorderID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ChatBoxBorder" );
	buttonBuddiesID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ButtonBuddy" );
	buttonContinueID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ButtonContinue" );
	buttonSaveReplayID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:ButtonSaveReplay" );
	backdropID = TheNameKeyGenerator->nameToKey( "ScoreScreen.wnd:MainBackdrop" );

	parent = TheWindowManager->winGetWindowFromId( nullptr, parentID );
	buttonOk = TheWindowManager->winGetWindowFromId( parent, buttonOkID );
	textEntryChat = TheWindowManager->winGetWindowFromId( parent, textEntryChatID );
	buttonEmote = TheWindowManager->winGetWindowFromId( parent,buttonEmoteID  );
	listboxChatWindowScoreScreen = TheWindowManager->winGetWindowFromId( parent, listboxChatWindowScoreScreenID );
	listboxAcademyWindowScoreScreen = TheWindowManager->winGetWindowFromId( parent, listboxAcademyWindowScoreScreenID );
	staticTextAcademyTitle = TheWindowManager->winGetWindowFromId( parent, staticTextAcademyTitleID );

//	buttonRehost = TheWindowManager->winGetWindowFromId( parent, buttonRehostID );
	chatBoxBorder = TheWindowManager->winGetWindowFromId( parent, chatBoxBorderID );
	buttonContinue = TheWindowManager->winGetWindowFromId( parent, buttonContinueID );
	buttonBuddies = TheWindowManager->winGetWindowFromId( parent, buttonBuddiesID );
	staticTextGameSaved= TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("ScoreScreen.wnd:StaticTextGameSaveComplete") );
	backdrop = TheWindowManager->winGetWindowFromId( parent, backdropID );
	challengePortrait = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("ScoreScreen.wnd:BigPortrait") );
	challengeWinLossText = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("ScoreScreen.wnd:ChallengeWinLossText") );
	challengeRemarks = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("ScoreScreen.wnd:GeneralRemarks") );
	gadgetParent = TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey("ScoreScreen.wnd:GadgetParent") );

	// force the classic fonts back on, no matter which .wnd the bigs supplied
	GameWindow *layoutWindow;
	for( layoutWindow = layout->getFirstWindow(); layoutWindow; layoutWindow = layoutWindow->winGetNextInLayout() )
		enforceClassicScoreScreenFonts( layoutWindow );

	// get the replay filename for later (not full path)
	LastReplayFileName = TheRecorder->getLastReplayFileName().str();
	staticTextGameSaved->winHide(TRUE);
	overidePlayerDisplayName = FALSE;
	WindowLayout *replayLayout = TheShell->getPopupReplayLayout();
	if (replayLayout != nullptr) {
		replayLayout->hide(TRUE);
	}
	canSaveReplay = FALSE;
	if(TheRecorder->getMode() == RECORDERMODETYPE_RECORD)
		canSaveReplay = TRUE;
	GameWindow *buttonSaveReplay = TheWindowManager->winGetWindowFromId( parent, buttonSaveReplayID );
	if (TheRecorder->getMode() == RECORDERMODETYPE_NONE && buttonSaveReplay)
		buttonSaveReplay->winEnable(FALSE);

	s_needToFinishSinglePlayerInit = FALSE;
	if (TheGameLogic->isInReplayGame())
	{
		if (buttonSaveReplay)
			buttonSaveReplay->winHide(TRUE);

		if (TheRecorder->isMultiplayer())
		{
			initReplayMultiPlayer();
			TheTransitionHandler->setGroup("ScoreScreenShow");
		}
		else
		{
			overidePlayerDisplayName = TRUE;
			initReplaySinglePlayer();
			TheTransitionHandler->setGroup("ScoreScreenShow");
		}
	}
	else
	{
		if(TheGameLogic->isInInternetGame())
		{
			initInternetMultiPlayer();
			TheTransitionHandler->setGroup("ScoreScreenShow");
		}
		else if( TheGameLogic->isInLanGame())
		{
			initLANMultiPlayer();
			TheTransitionHandler->setGroup("ScoreScreenShow");
		}
		else if( TheGameLogic->isInSkirmishGame())
		{
			initSkirmish();
			TheTransitionHandler->setGroup("ScoreScreenShow");
		}
		else
		{
			overidePlayerDisplayName = TRUE;
			initSinglePlayer();
			buttonSaveReplay->winHide(TRUE);
		}
	}

	if (screenType == SCORESCREEN_LAN || screenType == SCORESCREEN_INTERNET || screenType == SCORESCREEN_REPLAY)
		rvBegin();

	// challenge maps have no replay (design and technical issues)
	Bool isChallengeCampaign = TheCampaignManager->getCurrentCampaign() ? TheCampaignManager->getCurrentCampaign()->isChallengeCampaign() : FALSE;
	if (isChallengeCampaign)
	{
		buttonSaveReplay->winEnable(FALSE);
		buttonSaveReplay->winHide(TRUE);
	}


	// Make Sure the layout is visible
	layout->hide( FALSE );

	// set keyboard focus to main parent
	TheWindowManager->winSetFocus( parent );
	ReplayWasPressed = FALSE;
	if (s_blankLayout)
	{
		s_blankLayout->hide(FALSE);
		s_blankLayout->bringForward();
	}

#if defined(GENERALS_ONLINE)
    // Update the communicator button anytime we get notifications
    NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
    if (pSocialInterface != nullptr)
    {
        // notifiactions callback
        pSocialInterface->RegisterForCallback_OnNumberGlobalNotificationsChanged([](int numNotifications)
            {
                // update communicator button
                GameWindow *currentButtonBuddies = TheWindowManager->winGetWindowFromId(nullptr, buttonBuddiesID);
                if (currentButtonBuddies != nullptr)
                {
                    UnicodeString buttonText;

					if (numNotifications > 0)
					{
						buttonText.format(L"%s [%d]", TheGameText->fetch("GUI:Buddies").str(), numNotifications);
					}
					else
					{
						buttonText.format(L"%s", TheGameText->fetch("GUI:Buddies").str());
					}
					currentButtonBuddies->winSetText(buttonText);
                }
            });

        // And also initialize it
        if (buttonBuddies != nullptr && pSocialInterface->GetNumTotalNotifications() > 0)
        {
            UnicodeString buttonText;
            buttonText.format(L"%s [%d]", TheGameText->fetch("GUI:Buddies").str(), pSocialInterface->GetNumTotalNotifications());
            buttonBuddies->winSetText(buttonText);
        }
    }
#endif
}

void FixupScoreScreenMovieWindow( void )
{
	if (s_blankLayout)
	{
		s_blankLayout->hide(FALSE);
		s_blankLayout->bringForward();
	}
}

/** Shutdown the ScoreScreen */
//-------------------------------------------------------------------------------------------------
void ScoreScreenShutdown( WindowLayout *layout, void *userData )
{
	DontShowMainMenu = FALSE; //KRIS

#if defined(GENERALS_ONLINE)
	NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
	if (pSocialInterface != nullptr)
		pSocialInterface->RegisterForCallback_OnNumberGlobalNotificationsChanged(nullptr);
#endif
	buttonBuddies = nullptr;
	rvShutdown();

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout );

	TheAudio->removeAudioEvent( AHSV_StopTheMusicFade );

}

/** Update the ScoreScreen */
//-------------------------------------------------------------------------------------------------
void ScoreScreenUpdate( WindowLayout * layout, void *userData)
{
	WindowLayout *popupReplayLayout = TheShell->getPopupReplayLayout();
	if (popupReplayLayout != nullptr) {
		if (popupReplayLayout->isHidden() == FALSE) {
			PopupReplayUpdate(popupReplayLayout, nullptr);
		}
	}

	// TODO_NGMP: Find a better way of doing this... before the user exists
	if (NGMP_OnlineServicesManager::GetInstance() != nullptr && g_bNeedToTakeDoneEOGScreenshot)
	{
		int64_t currTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();
		if (currTime - g_TimeEnterState >= 1000)
		{
			g_bNeedToTakeDoneEOGScreenshot = false;

			NGMP_OnlineServicesManager::GetInstance()->CaptureScreenshotForProbe(EScreenshotType::SCREENSHOT_TYPE_SCORESCREEN, std::string()); // pass no URI here, wait until we have one received from server
		}
	}

	rvUpdate();

	if (s_needToFinishSinglePlayerInit)
	{
		finishSinglePlayerInit();
		s_needToFinishSinglePlayerInit = FALSE;
	}
	
	//TheGameLogic->clearGameData() gets called after ScoreScreenInit and before the
	//first ScoreScreenUpdate(), so it was creatively moved here so we can actually
	//hear the music play.
	if( TheGameInfo && g_playMusic )
	{
		g_playMusic = FALSE;

		Int localSlotNum = TheGameInfo->getLocalSlotNum();

		if (localSlotNum != -1)
		{
			GameSlot* lSlot = TheGameInfo->getSlot(localSlotNum);
			const PlayerTemplate* pt;

			if (lSlot && lSlot->getPlayerTemplate() >= 0)
				pt = ThePlayerTemplateStore->getNthPlayerTemplate(lSlot->getPlayerTemplate());
			else
				pt = ThePlayerTemplateStore->findPlayerTemplate(TheNameKeyGenerator->nameToKey("FactionObserver"));
			AsciiString musicName = pt->getScoreScreenMusic();
			if (!musicName.isEmpty())
			{
				TheAudio->removeAudioEvent(AHSV_StopTheMusicFade);
				AudioEventRTS event(musicName);
				event.setShouldFade(TRUE);
				TheAudio->addAudioEvent(&event);
				TheAudio->update();//Since GameEngine::update() is suspended until after I am gone... 
			}
		}

		NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		if (pLobbyInterface != nullptr)
		{
			if (pLobbyInterface->IsInLobby())
			{
				// commit current lobby player list to 'recently played with'
				NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
				if (pSocialInterface != nullptr)
				{
					pSocialInterface->CommitLobbyPlayerListToRecentlyPlayedWithList();
				}

				pLobbyInterface->LeaveCurrentLobby();

			}
		}
	}
}

/** Input function for the ScoreScreen */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ScoreScreenInput( GameWindow *window, UnsignedInt msg,
																		WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg ) 
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;

			switch( key )
			{

				// ----------------------------------------------------------------------------------------
				case KEY_ESC:
				{
					
					//
					// send a simulated selected event to the parent window of the
					// back/exit button
					//
					if( BitIsSet( state, KEY_STATE_UP ) )
					{

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, 
																								(WindowMsgData)buttonOk, buttonOkID );

					}

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}

			}

		}

	}

	return MSG_IGNORED;

}

static Bool showReplayButtonContinue()
{
	bool hasSimulationReplay = ReplaySimulation::getReplayCount() > 0;
	bool isLastSimulationReplay = ReplaySimulation::getCurrentReplayIndex() == ReplaySimulation::getReplayCount()-1;

	return hasSimulationReplay && !isLastSimulationReplay;
}

/** System Function for the ScoreScreen */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ScoreScreenSystem( GameWindow *window, UnsignedInt msg, 
																				  WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;

	switch( msg ) 
	{
		// --------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{
			break;
		}

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			break;
		}

		// --------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			TheTransitionHandler->remove("ScoreScreenShow", TRUE);
			ReplayWasPressed = FALSE;

			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
			if( s_rv.toggle != nullptr && control == s_rv.toggle )
			{
				rvShowReport(!s_rv.showing);
				break;
			}
			if( controlID == buttonOkID )
			{
				GameSpyCloseOverlay(GSOVERLAY_BUDDY);
				TheShell->pop();
				TheCampaignManager->setCampaign(AsciiString::TheEmptyString);

				if ( ReplaySimulation::getReplayCount() > 0 )
				{
					ReplaySimulation::stop();
					TheGameEngine->setQuitting(TRUE);
				}
			}
			else if ( controlID == buttonContinueID )	
			{
				if( ReplaySimulation::getReplayCount() > 0 )
				{
					TheGameEngine->setQuitting(TRUE);
				}
				else
				{
					if(!buttonIsFinishCampaign)
						ReplayWasPressed = TRUE;
					if( screenType == SCORESCREEN_SINGLEPLAYER)	
					{
						AsciiString mapName = TheCampaignManager->getCurrentMap();

						if( mapName.isEmpty() )
						{
							ReplayWasPressed = FALSE;
							TheShell->pop();
						}
						else
						{

						}
					}
					else if (screenType == SCORESCREEN_INTERNET)
					{
						NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
						if (pLobbyInterface != nullptr)
						{
							uint64_t currentMatchID = pLobbyInterface->GetCurrentMatchID();

							if (currentMatchID != 0)
							{
								AsciiString strMatchURL;
#if defined(USE_TEST_ENV)
								strMatchURL.format("https://www.playgenerals.online/viewmatch?match=%" PRIu64 "&env=test", currentMatchID);
#else
								strMatchURL.format("https://strata.gamereplays.org/zh/match/%" PRIu64, currentMatchID);
#endif
								ShellExecuteA(NULL, "open", strMatchURL.str(), NULL, NULL, SW_SHOWNORMAL);
							}
						}
					}
				}
			}
			else if ( controlID == buttonBuddiesID )	
			{
				GameSpyToggleOverlay( GSOVERLAY_BUDDY );
			}
			else if ( controlID == buttonSaveReplayID )
			{
				ScoreScreenEnableControls(FALSE);
        WindowLayout *saveReplayLayout = TheShell->getPopupReplayLayout();
				DEBUG_ASSERTCRASH( saveReplayLayout, ("Unable to get save replay menu layout.") );
				saveReplayLayout->runInit();
				saveReplayLayout->hide( FALSE );
				saveReplayLayout->bringForward();
			}

			else if ( controlID == buttonEmoteID )
			{
				// read the user's input
				txtInput.set(GadgetTextEntryGetText( textEntryChat ));
				// Clear the text entry line
				GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
				// Clean up the text (remove leading/trailing chars, etc)
				txtInput.trim();
				// Echo the user's input to the chat window
				if (!txtInput.isEmpty())
					if(TheLAN)
						TheLAN->RequestChat(txtInput, LANAPIInterface::LANCHAT_EMOTE);
					//add the gamespy chat request here
			}
			for(Int i = 0; i < MAX_SLOTS; ++i)
			{
				AsciiString name;
				name.format("ScoreScreen.wnd:ButtonAdd%d", i);
				if( controlID == TheNameKeyGenerator->nameToKey(name))
				{
					Bool notBuddy = TRUE;
					Int playerID = (Int)GadgetButtonGetData(TheWindowManager->winGetWindowFromId(nullptr,controlID));
											// request to add a buddy
					BuddyInfoMap *buddies = TheGameSpyInfo->getBuddyMap();
					BuddyInfoMap::iterator bIt;
					if( playerID > 0)
					{
						bIt = buddies->find(playerID);
						if (bIt != buddies->end())
						{
							notBuddy = FALSE;
						}
					}
					if(notBuddy)
					{
						BuddyRequest req;
						req.buddyRequestType = BuddyRequest::BUDDYREQUEST_ADDBUDDY;
						req.arg.addbuddy.id = playerID;
						UnicodeString buddyAddstr;
						buddyAddstr = TheGameText->fetch("GUI:BuddyAddReq");
						wcslcpy(req.arg.addbuddy.text, buddyAddstr.str(), MAX_BUDDY_CHAT_LEN);
						TheGameSpyBuddyMessageQueue->addRequest(req);
					}
				}
			}

			break;
		}

		case GEM_EDIT_DONE:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			// Take the user's input and echo it into the chat window as well as
			// send it to the other clients on the lan
			if ( controlID == textEntryChatID )
			{
				
				// read the user's input
				txtInput.set(GadgetTextEntryGetText( textEntryChat ));
				// Clear the text entry line
				GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
				// Clean up the text (remove leading/trailing chars, etc)
				txtInput.trim();
				// Echo the user's input to the chat window
				if (!txtInput.isEmpty())
					if(TheLAN)
						TheLAN->RequestChat(txtInput, LANAPIInterface::LANCHAT_NORMAL);
					//add the gamespy chat request here

			}

			break;
		}
	}		
	return MSG_HANDLED;
}


//-----------------------------------------------------------------------------
// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------

/** Special Init path for making this a single player Score Screen */
//-------------------------------------------------------------------------------------------------
void initSkirmish( void )
{
	screenType = SCORESCREEN_SKIRMISH;
	grabMultiPlayerInfo();
	if (textEntryChat)
		textEntryChat->winHide(TRUE);
	if (buttonEmote)
		buttonEmote->winHide(TRUE);
	if (chatBoxBorder)
		chatBoxBorder->winHide(TRUE);
	if (buttonBuddies)
		buttonBuddies->winHide(TRUE);
	if (buttonContinue)
		buttonContinue->winHide(TRUE);
	if (listboxChatWindowScoreScreen)
		listboxChatWindowScoreScreen->winHide(TRUE);
	if(staticTextGameSaved)
		staticTextGameSaved->winHide(TRUE);
//	if (buttonRehost)
//		buttonRehost->winHide(TRUE);
}

void PlayMovieAndBlock(AsciiString movieTitle)
{
	VideoStreamInterface *videoStream = TheVideoPlayer->open( movieTitle );
	if ( videoStream == nullptr )
	{
		return;
	}

	// Create the new buffer
	VideoBuffer *videoBuffer = TheDisplay->createVideoBuffer();
	if (	videoBuffer == nullptr ||
				!videoBuffer->allocate(	videoStream->width(),
													videoStream->height())
		)
	{
		delete videoBuffer;
		videoBuffer = nullptr;

		if ( videoStream )
		{
			videoStream->close();
			videoStream = nullptr;
		}

		return;
	}

	// TheSuperHackers @bugfix Originally this movie render loop stopped rendering when the game window was inactive.
	// This either skipped the movie or caused decompression artifacts. Now the video just keeps playing until it done.

	GameWindow *movieWindow = s_blankLayout->getFirstWindow();
	TheWritableGlobalData->m_loadScreenRender = TRUE;
	while (videoStream->frameIndex() < videoStream->frameCount() - 1)
	{
		// TheSuperHackers @feature User can now skip video by pressing ESC
		if (TheKeyboard)
		{
			TheKeyboard->UPDATE();
			KeyboardIO *io = TheKeyboard->findKey(KEY_ESC, KeyboardIO::STATUS_UNUSED);
			if (io && BitIsSet(io->state, KEY_STATE_DOWN))
			{
				io->setUsed();
				break;
			}
		}

		TheGameEngine->serviceWindowsOS();

		if(!videoStream->isFrameReady())
		{
			Sleep(1);
			continue;
		}

		videoStream->frameDecompress();
		videoStream->frameRender(videoBuffer);
		videoStream->frameNext();

		if(videoBuffer)
			movieWindow->winGetInstanceData()->setVideoBuffer(videoBuffer);

		//TheWindowManager->update();

		TheDisplay->draw();
	}
	TheWritableGlobalData->m_loadScreenRender = FALSE;
	movieWindow->winGetInstanceData()->setVideoBuffer(nullptr);

	delete videoBuffer;
	videoBuffer = nullptr;

	if (videoStream)
	{
		videoStream->close();
		videoStream = nullptr;
	}

	setFPMode();
}

void initSinglePlayer( void )
{
	screenType = SCORESCREEN_SINGLEPLAYER;
	TheCampaignManager->setRankPoints(ThePlayerList->getLocalPlayer()->getSkillPoints());
	TheCampaignManager->setGameDifficulty(TheScriptEngine->getGlobalDifficulty());
	grabSinglePlayerInfo();
	s_needToFinishSinglePlayerInit = TRUE;
	s_blankLayout = TheWindowManager->winCreateLayout("Menus/BlankWindow.wnd");
	DEBUG_ASSERTCRASH(s_blankLayout,("We Couldn't Load Menus/BlankWindow.wnd"));
	s_blankLayout->hide(FALSE);
	s_blankLayout->bringForward();
	s_blankLayout->getFirstWindow()->winClearStatus(WIN_STATUS_IMAGE);
}

void displayChallengeWinLoss( const Image *imageGeneral, const UnicodeString strHeader, const UnicodeString strRemarks )
{
	// format the display for challenge mode win/loss
	backdrop->winHide(TRUE);
	gadgetParent->winHide(TRUE);	
	challengeWinLossText->winHide(FALSE);
	challengeRemarks->winHide(FALSE);
	challengePortrait->winHide(FALSE);
	parent->winSetEnabledImage(0, TheMappedImageCollection->findImageByName("GeneralsChallengeWinLoss"));

	// display the defeated enemy general
	challengePortrait->winSetEnabledImage(0, imageGeneral);
	GadgetStaticTextSetText(challengeWinLossText, strHeader);
	GadgetStaticTextSetText(challengeRemarks, strRemarks);
}

void finishSinglePlayerInit( void )
{
	if(TheCampaignManager->isVictorious())
	{
		if (TheCampaignManager->getCurrentCampaign()
		 && TheCampaignManager->getCurrentCampaign()->isChallengeCampaign())
		{
			// display challenge style win/loss
			AsciiString name = TheCampaignManager->getCurrentMission()->m_generalName;
			const GeneralPersona *general = TheChallengeGenerals->getGeneralByGeneralName(name);
			const Image *imageGeneralDefeated = general->getImageDefeated();
			const UnicodeString strGeneralDefeated = TheGameText->fetch(general->getStringDefeated());
			UnicodeString strHeader;
			strHeader.format( TheGameText->fetch("GUI:ChallengeWinText"), TheGameText->fetch(name).str() ) ;
			displayChallengeWinLoss(imageGeneralDefeated, strHeader, strGeneralDefeated);

			AudioEventRTS event( general->getWinSound() );
			TheAudio->addAudioEvent( &event );
			TheAudio->update();
		}

		TheCampaignManager->gotoNextMission();

		if(TheCampaignManager->getCurrentMap().isEmpty())
		{
			GadgetButtonSetText(buttonContinue, TheGameText->fetch("GUI:EndCampaign"));
			buttonIsFinishCampaign = TRUE;
			// mark us as having completed the campaign
			Campaign* campaign = TheCampaignManager->getCurrentCampaign();
			if (campaign)
			{
				GameDifficulty difficulty = TheCampaignManager->getGameDifficulty();
				SkirmishBattleHonors stats;
				if (campaign->m_name.compareNoCase("USA") == 0)
				{
					stats.setUSACampaignComplete(difficulty);
					stats.setHonors(BATTLE_HONOR_CAMPAIGN_USA);
				}

				if (campaign->m_name.compareNoCase("China") == 0)
				{
					stats.setCHINACampaignComplete(difficulty);
					stats.setHonors(BATTLE_HONOR_CAMPAIGN_CHINA);
				}

				if (campaign->m_name.compareNoCase("GLA") == 0)
				{
					stats.setGLACampaignComplete(difficulty);
					stats.setHonors(BATTLE_HONOR_CAMPAIGN_GLA);
				}

				if (campaign->m_name.compareNoCase("GLA") == 0)
				{
					stats.setGLACampaignComplete(difficulty);
					stats.setHonors(BATTLE_HONOR_CAMPAIGN_GLA);
				}

				for (int i = 0; i < MAX_GLOBAL_GENERAL_TYPES; ++i)
				{
					char campaignName[128];
					sprintf(campaignName, "CHALLENGE_%d", i);
					if (campaign->m_name.compareNoCase(campaignName) == 0)
					{
						stats.setChallengeCampaignComplete(i, difficulty);
						stats.setHonors(BATTLE_HONOR_CHALLENGE_MODE);
					}
				}

				stats.write();

				if (buttonOk)
					buttonOk->winHide(TRUE);
				if (buttonContinue)
					buttonContinue->winHide(TRUE);
				if (textEntryChat)
					textEntryChat->winHide(TRUE);
				if (buttonEmote)
					buttonEmote->winHide(TRUE);
				if (listboxChatWindowScoreScreen)
					listboxChatWindowScoreScreen->winHide(TRUE);
				if( listboxAcademyWindowScoreScreen )
					listboxAcademyWindowScoreScreen->winHide( TRUE );
				if( staticTextAcademyTitle )
					staticTextAcademyTitle->winHide( TRUE );
				if (chatBoxBorder)
					chatBoxBorder->winHide(TRUE);
				if (buttonBuddies)
					buttonBuddies->winHide(TRUE);
				if (campaign->getFinalVictoryMovie().isNotEmpty())
				{
					AsciiString vidName;
					vidName = campaign->getFinalVictoryMovie();
					Bool useLowRes = FALSE;
					if (TheGameLODManager) {
						if (!TheGameLODManager->didMemPass()) {
							useLowRes = TRUE;
						}
						if (TheGameLODManager->getRecommendedStaticLODLevel()==STATIC_GAME_LOD_LOW) {
							useLowRes = TRUE;
						}
						if (TheGameLODManager->getStaticLODLevel()==STATIC_GAME_LOD_LOW) {
							useLowRes = TRUE;
						}
					}
					if(!useLowRes)
						PlayMovieAndBlock(vidName);
				}
			}
		}
		else 
		{
			GadgetButtonSetText(buttonContinue, TheGameText->fetch("GUI:SaveAndContinue"));
			
			// auto save game
			TheGameState->missionSave();
			if(staticTextGameSaved)
				staticTextGameSaved->winHide(FALSE);
		}
	}
	else
	{
		if (TheCampaignManager->getCurrentCampaign()
		 && TheCampaignManager->getCurrentCampaign()->isChallengeCampaign())
		{
			// display challenge style win/loss
			AsciiString name = TheCampaignManager->getCurrentMission()->m_generalName;
			const GeneralPersona *general = TheChallengeGenerals->getGeneralByGeneralName(name);
			const Image *imageGeneralVictorious = general->getImageVictorious();
			const UnicodeString strGeneralVictorious = TheGameText->fetch(general->getStringVictorious());
			UnicodeString strHeader;
			strHeader.format( TheGameText->fetch("GUI:ChallengeLossText"), TheGameText->fetch(name).str() ) ;
			displayChallengeWinLoss(imageGeneralVictorious, strHeader, strGeneralVictorious);

			AudioEventRTS event( general->getLossSound() );
			TheAudio->addAudioEvent( &event );
			TheAudio->update();
		}

		GadgetButtonSetText(buttonContinue, TheGameText->fetch("GUI:Retry"));

	}

	TheInGameUI->freeMessageResources();

	if (s_blankLayout)
	{
		s_blankLayout->destroyWindows();
		deleteInstance(s_blankLayout);
		s_blankLayout = nullptr;
	}

	// set keyboard focus to main parent
	TheWindowManager->winSetFocus( parent );

	if (buttonOk)
		buttonOk->winHide(FALSE);
	if (buttonContinue)
		buttonContinue->winHide(FALSE);
	if (textEntryChat)
		textEntryChat->winHide(TRUE);
	if (buttonEmote)
		buttonEmote->winHide(TRUE);

	if (listboxChatWindowScoreScreen)
		listboxChatWindowScoreScreen->winHide(TRUE);
	if( listboxAcademyWindowScoreScreen )
		listboxAcademyWindowScoreScreen->winHide( TRUE );
	if( staticTextAcademyTitle )
		staticTextAcademyTitle->winHide( TRUE );

	if (chatBoxBorder)
		chatBoxBorder->winHide(TRUE);
	if (buttonBuddies)
		buttonBuddies->winHide(TRUE);
//	if (buttonRehost)
//		buttonRehost->winHide(TRUE);

	// need to do this here
	if ( TheCampaignManager->getCurrentCampaign()
	 && !TheCampaignManager->getCurrentCampaign()->isChallengeCampaign())
		TheTransitionHandler->setGroup("ScoreScreenShow");
}

/** Special Init path for making this a single player replay Score Screen */
//-------------------------------------------------------------------------------------------------
void initReplaySinglePlayer( void )
{
	screenType = SCORESCREEN_REPLAY;
	grabSinglePlayerInfo();
	if(staticTextGameSaved)
		staticTextGameSaved->winHide(TRUE);
	if (textEntryChat)
		textEntryChat->winHide(TRUE);
	if (buttonEmote)
		buttonEmote->winHide(TRUE);
	if (chatBoxBorder)
		chatBoxBorder->winHide(TRUE);
	if (buttonContinue)
		buttonContinue->winHide(!showReplayButtonContinue());
	if (buttonBuddies)
		buttonBuddies->winHide(TRUE);
	if (listboxChatWindowScoreScreen)
		listboxChatWindowScoreScreen->winHide(TRUE);
	if( listboxAcademyWindowScoreScreen )
		listboxAcademyWindowScoreScreen->winHide( TRUE );
	if( staticTextAcademyTitle )
		staticTextAcademyTitle->winHide( TRUE );

//	if (buttonRehost)
//		buttonRehost->winHide(TRUE);
}

/** Special Init path for making this a Multiplayer Score Screen(LAN) */
//-------------------------------------------------------------------------------------------------
void initLANMultiPlayer(void)
{
	screenType = SCORESCREEN_LAN;
	grabMultiPlayerInfo();
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
	TheWindowManager->winSetFocus( textEntryChat );
	if(staticTextGameSaved)
		staticTextGameSaved->winHide(TRUE);
	if (textEntryChat)
		textEntryChat->winHide(FALSE);
	if (buttonEmote)
		buttonEmote->winHide(FALSE);
	if (buttonContinue)
		buttonContinue->winHide(TRUE);
	if (listboxChatWindowScoreScreen)
		listboxChatWindowScoreScreen->winHide(FALSE);
	//No academy in LAN
	if( listboxAcademyWindowScoreScreen )
		listboxAcademyWindowScoreScreen->winHide( TRUE );
	if( staticTextAcademyTitle )
		staticTextAcademyTitle->winHide( TRUE );
	if (chatBoxBorder)
		chatBoxBorder->winHide(FALSE);
	if (buttonBuddies)
		buttonBuddies->winHide(TRUE);
}

/** Special Init path for making this a Multiplayer Score Screen(Internet) */
//-------------------------------------------------------------------------------------------------
void initInternetMultiPlayer(void)
{
	screenType = SCORESCREEN_INTERNET;
	grabMultiPlayerInfo();
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);
	TheWindowManager->winSetFocus( textEntryChat );
	if(staticTextGameSaved)
		staticTextGameSaved->winHide(TRUE);
	if (buttonContinue)
#if defined(GENERALS_ONLINE)
		buttonContinue->winHide(FALSE);
#else
		buttonContinue->winHide(TRUE);
#endif
	if (textEntryChat)
		textEntryChat->winHide(TRUE);
	if (buttonEmote)
		buttonEmote->winHide(TRUE);
	if (listboxChatWindowScoreScreen)
		listboxChatWindowScoreScreen->winHide(FALSE);

	// attempt to register our outcome
    NGMP_OnlineServices_StatsInterface* pStatsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_StatsInterface>();
    if (pStatsInterface != nullptr && TheNGMPGame != nullptr)
    {
        Player* localPlayer = ThePlayerList->getLocalPlayer();

        if (localPlayer != nullptr)
        {
            if (!TheNGMPGame->HasCommittedOutcome())
            {
                TheNGMPGame->SetHasCommittedOutcome();
                pStatsInterface->CommitMyOutcome(localPlayer->getScoreKeeper(), TheVictoryConditions->isLocalAlliedVictory());
            }
        }
    }

	// Leave the lobby
#if defined(GENERALS_ONLINE)
	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if (pLobbyInterface != nullptr)
	{
		// populate match info
		if (pLobbyInterface->IsInLobby())
		{
			LobbyEntry& lobby = pLobbyInterface->GetCurrentLobby();

 			UnicodeString strMatchID;
 			strMatchID.format(L"\nMatch ID: %" PRIu64, lobby.match_id);
 
 			UnicodeString strMatchURL;

			if (lobby.match_id == 0) // probably AI or < 2 humans
			{
				buttonContinue->winHide(TRUE);

				GadgetListBoxAddEntryText(listboxAcademyWindowScoreScreen, UnicodeString(L"\nMatch data is not available online because the match had AI present OR less than 2 human players."), GameSpyColor[GSCOLOR_DEFAULT], -1);
			}
			else
			{
				buttonContinue->winHide(FALSE);

#if defined(USE_TEST_ENV)
				strMatchURL.format(L"\nView match data, participants, replays, anti-cheat data: https://www.playgenerals.online/viewmatch?match=%" PRIu64 "&env=test", lobby.match_id);
#else
				strMatchURL.format(L"\nView match data, participants, replays, anti-cheat data: https://strata.gamereplays.org/zh/match/%" PRIu64, lobby.match_id);
#endif

				buttonContinue->winSetText(UnicodeString(L"VIEW MATCH ONLINE"));

				GadgetListBoxAddEntryText(listboxAcademyWindowScoreScreen, strMatchID, GameSpyColor[GSCOLOR_DEFAULT], -1);
				GadgetListBoxAddEntryText(listboxAcademyWindowScoreScreen, strMatchURL, GameSpyColor[GSCOLOR_DEFAULT], -1);
			}


 			

		}
	}
#endif

	//Provide academy advice in internet games.
	if( listboxAcademyWindowScoreScreen )
		listboxAcademyWindowScoreScreen->winHide( FALSE );
	if( staticTextAcademyTitle )
		staticTextAcademyTitle->winHide( FALSE );

	if (chatBoxBorder)
		chatBoxBorder->winHide(FALSE);

	// TODO_NGMP: Enable this once friends works
#if !defined(GENERALS_ONLINE)
	if(TheGameSpyInfo && TheGameSpyInfo->getLocalProfileID() ==0)
		buttonBuddies->winHide(TRUE);
	else
		buttonBuddies->winHide(FALSE);
#else
	buttonBuddies->winHide(FALSE);
#endif

	g_bNeedToTakeDoneEOGScreenshot = true;
	g_TimeEnterState = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();

	if (!TheGameSpyBuddyMessageQueue)
		return;
	BuddyRequest req;
	req.buddyRequestType = BuddyRequest::BUDDYREQUEST_SETSTATUS;
	req.arg.status.status = GP_ONLINE;
	strcpy(req.arg.status.statusString, "Online");
	strcpy(req.arg.status.locationString, "");
	TheGameSpyBuddyMessageQueue->addRequest(req);
}

/** Special Init path for making this a Multiplayer Score Screen(Replay) */
//-------------------------------------------------------------------------------------------------
void initReplayMultiPlayer(void)
{
	screenType = SCORESCREEN_REPLAY;
	grabMultiPlayerInfo();
	if(staticTextGameSaved)
		staticTextGameSaved->winHide(TRUE);
	if (textEntryChat)
		textEntryChat->winHide(TRUE);
	if (buttonEmote)
		buttonEmote->winHide(TRUE);
	if (listboxChatWindowScoreScreen)
		listboxChatWindowScoreScreen->winHide(TRUE);
	if( listboxAcademyWindowScoreScreen )
		listboxAcademyWindowScoreScreen->winHide( TRUE );
	if( staticTextAcademyTitle )
		staticTextAcademyTitle->winHide( TRUE );
	if (chatBoxBorder)
		chatBoxBorder->winHide(TRUE);
	if (buttonContinue)
		buttonContinue->winHide(!showReplayButtonContinue());
	if (buttonBuddies)
		buttonBuddies->winHide(TRUE);
//	if (buttonRehost)
//		buttonRehost->winHide(TRUE);
}


static Bool isSlotLocalAlly(GameInfo *game, const GameSlot *slot)
{
	const GameSlot *localSlot = game->getConstSlot(game->getLocalSlotNum());
	if (!localSlot)
		return TRUE;

	if (slot == localSlot)
		return TRUE;

	if (slot->getTeamNumber() < 0)
		return FALSE;

	return slot->getTeamNumber() == localSlot->getTeamNumber();
}

static void updateSkirmishBattleHonors(SkirmishBattleHonors& stats)
{
	DEBUG_LOG(("Updating Skirmish battle honors"));
	Player *localPlayer = ThePlayerList->getLocalPlayer();
	ScoreKeeper *s = localPlayer->getScoreKeeper();

	if (stats.getWinStreak() >= 5)
		stats.setHonors(BATTLE_HONOR_STREAK);

	// For Apocalypse Honor, see if the player has used each category of super weapon.
	const ThingTemplate *pTemplate = TheThingFactory->findTemplate("GLAScudStorm");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltSCUD();
	pTemplate = TheThingFactory->findTemplate("Boss_GLAScudStorm");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltSCUD();
	pTemplate = TheThingFactory->findTemplate("Chem_GLAScudStorm");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltSCUD();
	pTemplate = TheThingFactory->findTemplate("Slth_GLAScudStorm");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltSCUD();
	pTemplate = TheThingFactory->findTemplate("Demo_GLAScudStorm");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltSCUD();

	pTemplate = TheThingFactory->findTemplate("AmericaParticleCannonUplink");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltParticleCannon();
	pTemplate = TheThingFactory->findTemplate("AirF_AmericaParticleCannonUplink");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltParticleCannon();
	pTemplate = TheThingFactory->findTemplate("Lazr_AmericaParticleCannonUplink");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltParticleCannon();
	pTemplate = TheThingFactory->findTemplate("SupW_AmericaParticleCannonUplink");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltParticleCannon();
	pTemplate = TheThingFactory->findTemplate("Boss_ParticleCannonUplink");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltParticleCannon();

	pTemplate = TheThingFactory->findTemplate("ChinaNuclearMissileLauncher");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltNuke();
	pTemplate = TheThingFactory->findTemplate("Boss_NuclearMissileLauncher");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltNuke();
	pTemplate = TheThingFactory->findTemplate("Infa_ChinaNuclearMissileLauncher");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltNuke();
	pTemplate = TheThingFactory->findTemplate("Nuke_ChinaNuclearMissileLauncher");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltNuke();
	pTemplate = TheThingFactory->findTemplate("Tank_ChinaNuclearMissileLauncher");
	if (s->getTotalObjectsBuilt(pTemplate) > 0)
		stats.setBuiltNuke();
	if (stats.builtNuke() && stats.builtParticleCannon() && stats.builtSCUD())
		stats.setHonors(BATTLE_HONOR_APOCALYPSE);

	KindOfMaskType validMask, invalidMask;
	validMask.set(KINDOF_VEHICLE);
	invalidMask.set(KINDOF_AIRCRAFT);
	if (/*TheGameInfo->isSkirmish() &&*/ s->getTotalUnitsBuilt(validMask, invalidMask) >= 50)
		stats.setHonors(BATTLE_HONOR_BATTLE_TANK);

	validMask.clear();
	validMask.set(KINDOF_AIRCRAFT);
	invalidMask.clear();
	if (/*TheGameInfo->isSkirmish() &&*/ s->getTotalUnitsBuilt(validMask, invalidMask) >= 20)
		stats.setHonors(BATTLE_HONOR_AIR_WING);

	if (/*TheGameInfo->isSkirmish() &&*/ (TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND / 60) < 5)
	{
		stats.setHonors(BATTLE_HONOR_BLITZ5);
	}

	if (/*TheGameInfo->isSkirmish() &&*/ (TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND / 60) < 10)
	{
		stats.setHonors(BATTLE_HONOR_BLITZ10);
	}

	if (stats.getNumGamesLoyal() >= 20 && localPlayer->getSide() == "America")
		stats.setHonors(BATTLE_HONOR_LOYALTY_USA);

	if (stats.getNumGamesLoyal() >= 20 && 	localPlayer->getSide() == "China")
		stats.setHonors(BATTLE_HONOR_LOYALTY_CHINA);

	if (stats.getNumGamesLoyal() >= 20 && 	localPlayer->getSide() == "GLA")
		stats.setHonors(BATTLE_HONOR_LOYALTY_GLA);

	// endurance medal(s)
	Int numEasy = 0;
	Int numMedium = 0;
	Int numBrutal = 0;
	Bool anyAlliedAI = FALSE;
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = TheGameInfo->getConstSlot(i);
		if (slot->isAI() && !isSlotLocalAlly(TheGameInfo, slot))
		{
			if (TheGameInfo->getConstSlot(i)->getState() == SLOT_EASY_AI)
				++numEasy;
			if (TheGameInfo->getConstSlot(i)->getState() == SLOT_MED_AI)
				++numMedium;
			if (TheGameInfo->getConstSlot(i)->getState() == SLOT_BRUTAL_AI
				|| TheGameInfo->getConstSlot(i)->getState() == SLOT_TACTICAL_AI)
				++numBrutal;
		}
		else if (slot->isAI())
		{
			// can't get challenge medals with AI helpers
			anyAlliedAI = TRUE;
		}
	}
	if (/*!anyAlliedAI &&*/ (numEasy || numMedium || numBrutal))
	{
		Int oldEasy = stats.getEnduranceMedal(TheGameInfo->getMap(), SLOT_EASY_AI);
		Int oldMedium = stats.getEnduranceMedal(TheGameInfo->getMap(), SLOT_MED_AI);
		Int oldBrutal = stats.getEnduranceMedal(TheGameInfo->getMap(), SLOT_BRUTAL_AI);
		if (numEasy)
		{
			stats.setEnduranceMedal(TheGameInfo->getMap(), SLOT_EASY_AI, max(oldEasy, numEasy + numMedium + numBrutal));
		}
		if (numMedium)
		{
			stats.setEnduranceMedal(TheGameInfo->getMap(), SLOT_MED_AI, max(oldMedium, numMedium + numBrutal));
		}
		if (numBrutal)
		{
			stats.setEnduranceMedal(TheGameInfo->getMap(), SLOT_BRUTAL_AI, max(oldBrutal, numBrutal));
		}
	}
}

static void updateMPBattleHonors(Int& honors, PSPlayerStats& stats)
{
	DEBUG_LOG(("Updating MP battle honors"));
	Player *localPlayer = ThePlayerList->getLocalPlayer();
	ScoreKeeper *s = localPlayer->getScoreKeeper();

	//	BATTLE_HONOR_STREAK 				= 0x0000002,
	if (stats.winsInARow >= 5)
		honors |= BATTLE_HONOR_STREAK;

	//	BATTLE_HONOR_LOYALTY_USA		= 0x0000020,
	if (stats.gamesInRowWithLastGeneral >= 20 && 	localPlayer->getSide() == "America")
		honors |= BATTLE_HONOR_LOYALTY_USA;

	//	BATTLE_HONOR_LOYALTY_CHINA	= 0x0000040,
	if (stats.gamesInRowWithLastGeneral >= 20 && 	localPlayer->getSide() == "China")
		honors |= BATTLE_HONOR_LOYALTY_CHINA;

	//	BATTLE_HONOR_LOYALTY_GLA		= 0x0000060,
	if (stats.gamesInRowWithLastGeneral >= 20 && 	localPlayer->getSide() == "GLA")
		honors |= BATTLE_HONOR_LOYALTY_GLA;

	//	BATTLE_HONOR_BATTLE_TANK		= 0x0000080,
	KindOfMaskType validMask, invalidMask;
	validMask.set(KINDOF_VEHICLE);
	invalidMask.set(KINDOF_AIRCRAFT);
	if (/*TheGameInfo->isSkirmish() &&*/ s->getTotalUnitsBuilt(validMask, invalidMask) >= 50)
		honors |= BATTLE_HONOR_BATTLE_TANK;

	//	BATTLE_HONOR_AIR_WING				= 0x0000100,
	validMask.clear();
	validMask.set(KINDOF_AIRCRAFT);
	invalidMask.clear();
	if (/*TheGameInfo->isSkirmish() &&*/ s->getTotalUnitsBuilt(validMask, invalidMask) >= 20)
		honors |= BATTLE_HONOR_AIR_WING;

	if (stats.builtNuke && stats.builtParticleCannon && stats.builtSCUD)
		honors |= BATTLE_HONOR_APOCALYPSE;

	//	BATTLE_HONOR_BLITZ5					= 0x0008000,
	if (/*TheGameInfo->isSkirmish() &&*/ (TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND / 60) < 5)
	{
		honors |= BATTLE_HONOR_BLITZ5;
	}

	//	BATTLE_HONOR_BLITZ10				= 0x0008000,
	if (/*TheGameInfo->isSkirmish() &&*/ (TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND / 60) < 10)
	{
		honors |= BATTLE_HONOR_BLITZ10;
	}

	//	BATTLE_HONOR_GLOBAL_GENERAL
	Int numGlobalChallengeWins = 0;
	for (int i = GLOBAL_GENERAL_BEGIN; i <= GLOBAL_GENERAL_END; ++i)
	{
		PerGeneralMap::const_iterator pit = stats.wins.find(i);
		if (pit != stats.wins.end())
		{
			if (pit->second > 0)
			{
				++numGlobalChallengeWins;
			}
		}
	}

	if (numGlobalChallengeWins >= MAX_GLOBAL_GENERAL_TYPES)
	{
		honors |= BATTLE_HONOR_GLOBAL_GENERAL;
	}

	/*  NOT IMPLEMENTED YET */
	//	BATTLE_HONOR_LADDER_CHAMP		= 0x0000001, (IGNORED HERE)
	//	BATTLE_HONOR_SPECIAL_FORCES	= 0x0000200, (Not Valid)
	//	BATTLE_HONOR_ENDURANCE			= 0x0000400,
	//	BATTLE_HONOR_CAMPAIGN_USA		= 0x0000800,
	//	BATTLE_HONOR_CAMPAIGN_CHINA	= 0x0001000,
	//	BATTLE_HONOR_CAMPAIGN_GLA  	= 0x0002000,
}

// challenge medals are beating 1-7 Brutal AIs
static void updateChallengeMedals(Int& medals)
{
	if (!TheGameInfo->isSkirmish())
		return;

	Int numAIs = 0;
	Int numBrutals = 0;
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		const GameSlot *slot = TheGameInfo->getConstSlot(i);
		if (slot->isAI() && !isSlotLocalAlly(TheGameInfo, slot))
		{
			++numAIs;
			if (TheGameInfo->getConstSlot(i)->getState() == SLOT_BRUTAL_AI
				|| TheGameInfo->getConstSlot(i)->getState() == SLOT_TACTICAL_AI)
				++numBrutals;
		}
		else if (slot->isAI())
		{
			// can't get challenge medals with AI helpers
			return;
		}
	}

	if (numAIs)
	{
		switch(numBrutals)
		{
		case 1:
			medals |= BH_CHALLENGE_MASK_1;
			break;
		case 2:
			medals |= BH_CHALLENGE_MASK_2;
			break;
		case 3:
			medals |= BH_CHALLENGE_MASK_3;
			break;
		case 4:
			medals |= BH_CHALLENGE_MASK_4;
			break;
		case 5:
			medals |= BH_CHALLENGE_MASK_5;
			break;
		case 6:
			medals |= BH_CHALLENGE_MASK_6;
			break;
		case 7:
			medals |= BH_CHALLENGE_MASK_7;
			break;
		}
	}
}

static inline int CheckForApocalypse( ScoreKeeper *s, const char* szWeapon )
{
	const ThingTemplate* pTemplate = TheThingFactory->findTemplate(szWeapon);
	if( s->getTotalObjectsBuilt(pTemplate) > 0 )
		return 1;
	return 0;
}

/** Populate the various windows with the information about the game based on each player's score
		keeper. */
//-------------------------------------------------------------------------------------------------
void populatePlayerInfo( Player *player, Int pos, const ScoreAverages &avgs, const ScoreExtremes *ex)
{
	if(!player || pos < 0 || pos >= MAX_SLOTS)
		return;
	Color color = GameMakeColorReadable(player->getPlayerColor());
	ScoreKeeper *scoreKpr = player->getScoreKeeper();
	if(!scoreKpr)
	{
		DEBUG_ASSERTCRASH(FALSE,("Player %s does not have a scoreKeeper", player->getPlayerDisplayName().str()));
		return;
	}
	AsciiString winName;
	UnicodeString winValue;
	GameWindow *win;
	// set the player name
	winName.format("ScoreScreen.wnd:StaticTextPlayer%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	if(overidePlayerDisplayName)
	{
		GadgetStaticTextSetText(win, TheGameText->fetch("GUI:Player"));
	}
	else
		GadgetStaticTextSetText(win, player->getPlayerDisplayName());
	win->winHide(FALSE);
	win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());

	// set the player name
	winName.format("ScoreScreen.wnd:StaticTextObserver%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	win->winHide(TRUE);

	// set the total units built (value, not count)
	winName.format("ScoreScreen.wnd:StaticTextUnitsBuilt%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	{
		const Int v = scoreKpr->getTotalUnitsBuiltValue();
		formatCountAndValueCell(scoreKpr->getTotalUnitsBuilt(), v, avgs.unitsBuiltValue, winValue);
		GadgetStaticTextSetText(win, winValue);
		win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());
		if (ex) applyCellHighlight(win, v, ex->unitsBuiltMin, ex->unitsBuiltMax, FALSE);
	}
	win->winHide(FALSE);

	// set the total units Lost (count + value). Lost columns invert: the
	// lowest loss is best (green), the highest is worst (red).
	winName.format("ScoreScreen.wnd:StaticTextUnitsLost%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	{
		const Int v = scoreKpr->getTotalUnitsLostValue();
		formatCountAndValueCell(scoreKpr->getTotalUnitsLost(), v, avgs.unitsLostValue, winValue);
		GadgetStaticTextSetText(win, winValue);
		win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());
		if (ex) applyCellHighlight(win, v, ex->unitsLostMin, ex->unitsLostMax, TRUE);
	}
	win->winHide(FALSE);

	// set the total units Destroyed (count + value)
	winName.format("ScoreScreen.wnd:StaticTextUnitsDestroyed%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	{
		const Int v = scoreKpr->getTotalUnitsDestroyedValue();
		formatCountAndValueCell(scoreKpr->getTotalUnitsDestroyed(), v, avgs.unitsKilledValue, winValue);
		GadgetStaticTextSetText(win, winValue);
		win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());
		if (ex) applyCellHighlight(win, v, ex->unitsKilledMin, ex->unitsKilledMax, FALSE);
	}
	win->winHide(FALSE);

	// set the total BuildingsBuilt (count + value)
	winName.format("ScoreScreen.wnd:StaticTextBuildingsBuilt%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	{
		const Int v = scoreKpr->getTotalBuildingsBuiltValue();
		formatCountAndValueCell(scoreKpr->getTotalBuildingsBuilt(), v, avgs.bldgBuiltValue, winValue);
		GadgetStaticTextSetText(win, winValue);
		win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());
		if (ex) applyCellHighlight(win, v, ex->bldgBuiltMin, ex->bldgBuiltMax, FALSE);
	}
	win->winHide(FALSE);

	// set the total BuildingsLost (count + value). Lost column inverts.
	winName.format("ScoreScreen.wnd:StaticTextBuildingsLost%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	{
		const Int v = scoreKpr->getTotalBuildingsLostValue();
		formatCountAndValueCell(scoreKpr->getTotalBuildingsLost(), v, avgs.bldgLostValue, winValue);
		GadgetStaticTextSetText(win, winValue);
		win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());
		if (ex) applyCellHighlight(win, v, ex->bldgLostMin, ex->bldgLostMax, TRUE);
	}
	win->winHide(FALSE);

	// set the total BuildingsDestroyed (count + value)
	winName.format("ScoreScreen.wnd:StaticTextBuildingsDestroyed%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	{
		const Int v = scoreKpr->getTotalBuildingsDestroyedValue();
		formatCountAndValueCell(scoreKpr->getTotalBuildingsDestroyed(), v, avgs.bldgKilledValue, winValue);
		GadgetStaticTextSetText(win, winValue);
		win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());
		if (ex) applyCellHighlight(win, v, ex->bldgKilledMin, ex->bldgKilledMax, FALSE);
	}
	win->winHide(FALSE);

	// set the total Resources
	winName.format("ScoreScreen.wnd:StaticTextResources%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	{
		const Int v = scoreKpr->getTotalMoneyEarned();
		formatDollarCell(v, avgs.suppliesCollected, winValue);
		GadgetStaticTextSetText(win, winValue);
		win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());
		if (ex) applyCellHighlight(win, v, ex->suppliesMin, ex->suppliesMax, FALSE);
	}
	win->winHide(FALSE);

	if( player == ThePlayerList->getLocalPlayer() )
	{
		//Handle academy stats
		if( listboxAcademyWindowScoreScreen )
		{
			listboxAcademyWindowScoreScreen->winHide( FALSE );
			if( staticTextAcademyTitle )
				staticTextAcademyTitle->winHide( FALSE );
			if( TheGameLogic->isInSkirmishGame() || TheGameLogic->isInInternetGame() )
			{
				AcademyAdviceInfo info;
				if( player->getAcademyStats()->calculateAcademyAdvice( &info ) )
				{
					for( UnsignedInt i = 0; i < info.numTips; i++ )
					{
						GadgetListBoxAddEntryText( listboxAcademyWindowScoreScreen, info.advice[ i ],	GameSpyColor[GSCOLOR_DEFAULT], -1 );
					}
				}
			}
		}
	}

	// set the Score field — repurposed as the efficiency column. Efficiency
	// = (value of units + buildings killed) / income * 100. The layout's
	// header still reads "Score" since the .wnd ships in a packed .big; the
	// number underneath is what matters.
	winName.format("ScoreScreen.wnd:StaticTextScore%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	{
		const Int killedValue = scoreKpr->getTotalUnitsDestroyedValue()
			+ scoreKpr->getTotalBuildingsDestroyedValue();
		const Int pct = computeEfficiencyPct(killedValue, scoreKpr->getTotalMoneyEarned());
		formatEfficiencyCell(killedValue, scoreKpr->getTotalMoneyEarned(), avgs.efficiencyPct, winValue);
		GadgetStaticTextSetText(win, winValue);
		win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());
		if (ex) applyCellHighlight(win, pct, ex->efficiencyMin, ex->efficiencyMax, FALSE);
	}
	win->winHide(FALSE);

	// set the Buttons
//	winName.format("ScoreScreen.wnd:ButtonAdd%d", pos);
	//	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	//	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	//	if(screenType ==	SCORESCREEN_INTERNET && TheGameSpyInfo && TheGameSpyInfo->getLocalProfileID() != 0)
	//	{
	//		// Get the stats for the player
	//		
	//		AsciiString aName;
	//		aName.translate(player->getPlayerDisplayName());
	//
	//		PlayerInfoMap::iterator blah = TheGameSpyInfo->getPlayerInfoMap()->find(aName);
	//		if(blah->second.m_profileID != 0)
	//		{
	//			
	//			BuddyInfoMap::iterator bIt = TheGameSpyInfo->getBuddyMap()->find(blah->second.m_profileID);
	//			if(bIt !=  TheGameSpyInfo->getBuddyMap()->end()  || (TheGameSpyInfo->getLocalProfileID() == blah->second.m_profileID))
	//			{
	//				// already our buddy
	//				win->winHide(TRUE);
	//			}
	//			else
	//			{
	//		
	//				GadgetButtonSetData(win, (void *) blah->second.m_profileID);
	//				win->winEnable(TRUE);
	//				win->winHide(FALSE);
	//			}
	//		}
	//		else
	//			win->winHide(TRUE);
	//	}
	//	else
	//	{
	//		win->winHide(TRUE);
	//	}
	

	// set a marker for who won and lost
	winName.format("ScoreScreen.wnd:GameWindowWinner%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	win->winHide(FALSE);
//	if(TheVictoryConditions->hasAchievedVictory(player))
//		win->winEnable(TRUE);
//	else if(TheVictoryConditions->hasBeenDefeated(player))
//		win->winEnable(FALSE);
//	else
//		win->winHide(TRUE);
//
	const PlayerTemplate *fact = player->getPlayerTemplate();
	if(fact != nullptr)
	{
		win->winSetEnabledImage(0, fact->getSideIconImage());
	}
	

	if ( screenType == SCORESCREEN_SKIRMISH && player->isLocalPlayer() )
	{
		if (TheGameInfo->isSandbox() || !(TheVictoryConditions->isLocalAlliedDefeat() || TheVictoryConditions->isLocalAlliedVictory()))
		{
			// If we died, and are watching sparring AIs, we still get the loss.
			if (player->isPlayerActive())
			{
				DEBUG_LOG(("Skipping skirmish stats update: sandbox:%d defeat:%d victory:%d",
					TheGameInfo->isSandbox(), TheVictoryConditions->isLocalAlliedDefeat(), TheVictoryConditions->isLocalAlliedVictory()));
				return;
			}
		}

		SkirmishBattleHonors stats;

		if (TheVictoryConditions->isLocalAlliedVictory())
		{
			stats.setWins(stats.getWins()+1);
			stats.setWinStreak(stats.getWinStreak()+1);
			stats.setBestWinStreak(max(stats.getBestWinStreak(), stats.getWinStreak()));
			updateSkirmishBattleHonors(stats);
			Int challengeMedals = stats.getChallengeMedals();
			updateChallengeMedals(challengeMedals);
			stats.setChallengeMedals(challengeMedals);
		}
		else
		{
			stats.setLosses(stats.getLosses()+1);
			stats.setWinStreak(0);
		}

		AsciiString lastGeneral = stats.getLastGeneral();
		stats.setLastGeneral(player->getPlayerTemplate()->getSide());
		if (lastGeneral != stats.getLastGeneral())
		{
			stats.setNumGamesLoyal(0);
		}
		else
		{
			stats.setNumGamesLoyal(stats.getNumGamesLoyal()+1);
		}

		stats.write();
	}

	if ( screenType == SCORESCREEN_INTERNET )
	{
		DEBUG_LOG(("populatePlayerInfo() - SCORESCREEN_INTERNET\n"));

#if defined(GENERALS_ONLINE)
		if (TheNGMPGame == nullptr)
			return;

		if (!TheNGMPGame->getUseStats()
			&& !TheNGMPGame->isQMGame())  //QuickMatch games always record stats
			return;	//the host has requested not to record stats for this game.
#else
		if (TheGameSpyGame && !TheGameSpyGame->getUseStats()
			&& !TheGameSpyGame->isQMGame() )  //QuickMatch games always record stats
			return;	//the host has requested not to record stats for this game.

		Int localID = TheGameSpyInfo->getLocalProfileID();
		if (localID)
#endif

		{
#if defined(GENERALS_ONLINE)
			Int localSlotNum = TheNGMPGame->getLocalSlotNum();
#else
			Int localSlotNum = TheGameSpyGame->getLocalSlotNum();
#endif
			if (player->isLocalPlayer())
			{
#if defined(GENERALS_ONLINE)
				NGMPGameSlot* localSlot = TheNGMPGame->getGameSpySlot(localSlotNum);
#else
				GameSpyGameSlot* localSlot = TheGameSpyGame->getGameSpySlot(localSlotNum);
#endif
				if (localSlot)
				{
					if (TheVictoryConditions->amIObserver())
					{
						// nothing to track
						DEBUG_LOG(("populatePlayerInfo() - not tracking stats for observer\n"));
						return;
					}

#if defined(GENERALS_ONLINE)
					NGMP_OnlineServices_AuthInterface* pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
					int64_t localID = pAuthInterface == nullptr ? -1 : pAuthInterface->GetUserID();

					// do this part sync from cache, should have our own stats + up to date
					NGMP_OnlineServices_StatsInterface* pStatsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_StatsInterface>();
					if (pStatsInterface == nullptr)
					{
						return;
					}

					pStatsInterface->findPlayerStatsByID(localID, [=](bool bSuccess, PSPlayerStats stats)
#else
					PSPlayerStats stats = TheGameSpyPSMessageQueue->findPlayerStatsByID(localID);
#endif
					{
						UnsignedInt latestHumanInGame = 0;
						UnsignedInt lastFrameOfGame = 0;
						Bool gameEndedInDisconnect = TRUE;
						Bool sawAnyDisconnects = FALSE;
						Bool anyNonAI = FALSE;
						Bool anyAI = FALSE;
						Int i = 0;
						for (; i < MAX_SLOTS; ++i)
						{
							const GameSlot* slot = TheGameInfo->getConstSlot(i);
							if (slot->isOccupied() && i != localSlotNum)
							{
								if (slot->isAI())
									anyAI = TRUE;
								else
									anyNonAI = TRUE;
							}
							if (slot->isOccupied())
							{
								lastFrameOfGame = max(lastFrameOfGame, slot->lastFrameInGame());
							}
							if (slot->isHuman())
							{
								if (i != localSlotNum)
								{
									latestHumanInGame = max(latestHumanInGame, slot->lastFrameInGame());
								}
							}
						}
						DEBUG_LOG(("Game ended on frame %d - TheGameLogic->getFrame()=%d\n", lastFrameOfGame - 1, TheGameLogic->getFrame() - 1));
						for (i = 0; i < MAX_SLOTS; ++i)
						{
							const GameSlot* slot = TheGameInfo->getConstSlot(i);
							DEBUG_LOG(("latestHumanInGame=%d, slot->isOccupied()=%d, slot->disconnected()=%d, slot->isAI()=%d, slot->lastFrameInGame()=%d\n",
								latestHumanInGame, slot->isOccupied(), slot->disconnected(), slot->isAI(), slot->lastFrameInGame()));
							if (slot->isOccupied() && slot->disconnected())
							{
								DEBUG_LOG(("Marking game as a possible disconnect game\n"));
								sawAnyDisconnects = TRUE;
							}
							if (slot->isOccupied() && !slot->disconnected() && i != localSlotNum &&
								(slot->isAI() || (slot->lastFrameInGame() >= lastFrameOfGame/*TheGameLogic->getFrame()*/ - 1)))
							{
								DEBUG_LOG(("Marking game as not ending in disconnect\n"));
								gameEndedInDisconnect = FALSE;
							}
						}

						if (!sawAnyDisconnects)
						{
							DEBUG_LOG(("Didn't see any disconnects - making gameEndedInDisconnect == FALSE\n"));
							gameEndedInDisconnect = FALSE;
						}

						if (gameEndedInDisconnect)
						{
#if defined(GENERALS_ONLINE)
							// TODO_NGMP we need to detect this on service.
							// Use victory conditions to avoid penolizing the innocent player for now.
							if (!TheVictoryConditions->isLocalAlliedVictory())
							{
								DEBUG_LOG(("[DISC] we disconnected, gameEndedInDisconnect = true\n"));
							}
							else
							{
								DEBUG_LOG(("[DISC] opponent disconnected, gameEndedInDisconnect = false\n"));
								gameEndedInDisconnect = FALSE;
							}
#else
							if (TheNetwork->getPingsRecieved() < max(1, TheNetwork->getPingsSent() / 2))
							{
								DEBUG_LOG(("We were to blame. Leaving gameEndedInDisconnect = true\n"));
							}
							else
							{
								DEBUG_LOG(("We were not to blame. Changing gameEndedInDisconnect = false\n"));
								gameEndedInDisconnect = FALSE;
							}
#endif
						}

						ScoreKeeper* s = player->getScoreKeeper();
						if (!anyNonAI)
						{
							// play against all ai players -- no stats to gather.

							// even though we don't want to register stats, we still want to register the match outcome, since we started it on the backend, it needs finishing too

							return;
						}

						if (anyAI)
						{
							// Holy mis-implemented, Batman, You get no stats for _any_ AI, not _all_ AI.  
							// No wonder we fired the whole department.

							// even though we don't want to register stats, we still want to register the match outcome, since we started it on the backend, it needs finishing too

							return;
						}

						//Remove the extra disconnection we add to all games when they start.
						DEBUG_LOG(("populatePlayerInfo() - removing extra disconnect\n"));

						// TODO_NGMP_STATS
						//if (TheGameSpyInfo)
							//TheGameSpyInfo->updateAdditionalGameSpyDisconnections(-1);

						Bool sawEndOfGame = FALSE;
						if (TheVictoryConditions->isLocalAlliedDefeat() || TheVictoryConditions->isLocalAlliedVictory())
						{
							sawEndOfGame = TRUE;
						}
						if (TheVictoryConditions->isLocalDefeat())
						{
							sawEndOfGame = TRUE;
						}
						if (TheNetwork->sawCRCMismatch() || gameEndedInDisconnect)
						{
							sawEndOfGame = TRUE;
						}
						if (!sawEndOfGame)
						{
							DEBUG_LOG(("Not sending results - we didn't finish a game. %d\n", TheVictoryConditions->getEndFrame()));

							// even though we don't want to register stats, we still want to register the match outcome, since we started it on the backend, it needs finishing too

							return;
						}

#if !defined(GENERALS_ONLINE)
						// TODO_NGMP_STATS: Impl ladders again, how did these work in the base game?
						// send ladder results (even if we end the game in the first N seconds)

						if (TheGameSpyGame->getLadderPort() && TheGameSpyGame->getLadderIP().isNotEmpty())
						{
							GameResultsRequest gameResReq;
							gameResReq.hostname = TheGameSpyGame->getLadderIP().str();
							gameResReq.port = TheGameSpyGame->getLadderPort();
							gameResReq.results = TheGameSpyGame->generateLadderGameResultsPacket().str();
							DEBUG_ASSERTCRASH(TheGameResultsQueue, ("No Game Results queue!\n"));
							if (TheGameResultsQueue)
							{
								TheGameResultsQueue->addRequest(gameResReq);
							}
						}

						// TODO_NGMP: Why did they do this? dont register short games?
						if (TheVictoryConditions->getEndFrame() < LOGICFRAMES_PER_SECOND * 25 && TheVictoryConditions->getEndFrame())
						{
							return;
						}

						// generate and send a gameres packet
						AsciiString resultsPacket = TheGameSpyGame->generateGameSpyGameResultsPacket();
						DEBUG_LOG(("About to send results packet: %s\n", resultsPacket.str()));
						PSRequest grReq;
						grReq.requestType = PSRequest::PSREQUEST_SENDGAMERESTOGAMESPY;
						grReq.results = resultsPacket.str();
						TheGameSpyPSMessageQueue->addRequest(grReq);
#endif

						Int ptIdx;
						const PlayerTemplate* myTemplate = player->getPlayerTemplate();
						DEBUG_LOG(("myTemplate = %X(%s)\n", myTemplate, myTemplate->getName().str()));
						for (ptIdx = 0; ptIdx < ThePlayerTemplateStore->getPlayerTemplateCount(); ++ptIdx)
						{
							const PlayerTemplate* nthTemplate = ThePlayerTemplateStore->getNthPlayerTemplate(ptIdx);
							DEBUG_LOG(("nthTemplate = %X(%s)\n", nthTemplate, nthTemplate->getName().str()));
							if (nthTemplate == myTemplate)
							{
								break;
							}
						}

						if (stats.id == 0)
						{
							// we haven't gotten stats for ourselves yet.  Bummer.
							// what we'll do is just update our disconnects in the registry if we disconnected.
							// other than that, there's not much we can do.  :P
							if (gameEndedInDisconnect || TheNetwork->sawCRCMismatch())
							{
								/* @todo: this the right way
								UnsignedInt discons = 0;
								UnsignedInt syncs = 0;
								GetUnsignedIntFromRegistry("", "dc", discons);
								GetUnsignedIntFromRegistry("", "se", sync);
								++discons;
								++syncs;
								SetUnsignedIntInRegistry("", "dc", discons);
								SetUnsignedIntInRegistry("", "se", syncs);
								*/
								DEBUG_LOG(("populatePlayerInfo() - need to save off info for disconnect games!\n"));

#if !defined(GENERALS_ONLINE)
								PSRequest req;
								req.requestType = PSRequest::PSREQUEST_UPDATEPLAYERSTATS;
								req.email = TheGameSpyInfo->getLocalEmail().str();
								req.nick = TheGameSpyInfo->getLocalBaseName().str();
								req.password = "";
								req.player = stats;
								req.addDesync = TheNetwork->sawCRCMismatch();
								req.addDiscon = gameEndedInDisconnect;
								req.lastHouse = ptIdx;
								TheGameSpyPSMessageQueue->addRequest(req);
#endif
							}
							DEBUG_CRASH(("populatePlayerInfo() - not tracking stats - we haven't gotten the original stuff yet\n"));
							return;
						}

						if (TheNetwork->sawCRCMismatch())
						{
							++stats.desyncs[ptIdx];
						}
						else if (gameEndedInDisconnect)
						{
							++stats.discons[ptIdx];
						}
						else if (TheVictoryConditions->isLocalAlliedDefeat() || !TheVictoryConditions->getEndFrame())
						{
							++stats.losses[ptIdx];
						}
						else
						{
							++stats.wins[ptIdx];
						}

						stats.buildingsBuilt[ptIdx] += s->getTotalBuildingsBuilt();
						stats.buildingsKilled[ptIdx] += s->getTotalBuildingsDestroyed();
						stats.buildingsLost[ptIdx] += s->getTotalBuildingsLost();

#if defined(GENERALS_ONLINE)
						if (TheNGMPGame->isQMGame())
#else
						if (TheGameSpyGame->isQMGame())
#endif
						{
							stats.QMGames[ptIdx]++;
						}
						else
						{
							stats.customGames[ptIdx]++;
						}

						if (TheNetwork->sawCRCMismatch())
						{
							stats.lossesInARow = 0;
							stats.desyncsInARow++;
							stats.disconsInARow = 0;
							stats.winsInARow = 0;
							stats.maxDesyncsInARow = max(stats.desyncsInARow, stats.maxDesyncsInARow);
						}
						else if (gameEndedInDisconnect)
						{
							stats.lossesInARow = 0;
							stats.desyncsInARow = 0;
							stats.disconsInARow++;
							stats.winsInARow = 0;
							stats.maxDisconsInARow = max(stats.disconsInARow, stats.maxDisconsInARow);
						}
						else if (TheVictoryConditions->isLocalAlliedVictory())
						{
							stats.lossesInARow = 0;
							stats.desyncsInARow = 0;
							stats.disconsInARow = 0;
							stats.winsInARow++;
							stats.maxWinsInARow = max(stats.winsInARow, stats.maxWinsInARow);
						}
						else
						{
							stats.lossesInARow++;
							stats.desyncsInARow = 0;
							stats.disconsInARow = 0;
							stats.winsInARow = 0;
							stats.maxLossesInARow = max(stats.lossesInARow, stats.maxLossesInARow);
						}

						stats.earnings[ptIdx] += s->getTotalMoneyEarned();
						stats.duration[ptIdx] += TheGameLogic->getFrame() / LOGICFRAMES_PER_SECOND / 60; // in minutes
						stats.games[ptIdx]++;
						//since we raise this number when game starts, we need to lower it back on completion
						Int disCons;
						disCons = stats.discons[ptIdx];
						disCons -= 1;
						if (disCons >= 0)
							stats.discons[ptIdx] = disCons;

						stats.gamesAsRandom += (localSlot->getOriginalPlayerTemplate() == PLAYERTEMPLATE_RANDOM);

						if (stats.lastGeneral != ptIdx)
							stats.gamesInRowWithLastGeneral = 0;
						stats.gamesInRowWithLastGeneral++;
						stats.lastGeneral = ptIdx;

						Int gameSize = 0;
						for (int i = 0; i < MAX_SLOTS; ++i)
						{
#if defined(GENERALS_ONLINE)
							if (TheNGMPGame->getConstSlot(i)->isOccupied() && TheNGMPGame->getConstSlot(i)->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
#else
							if (TheGameSpyGame->getConstSlot(i)->isOccupied() && TheGameSpyGame->getConstSlot(i)->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
#endif
								++gameSize;
						}
						switch (gameSize)
						{
						case 2:
							stats.gamesOf2p[ptIdx]++;
							break;
						case 3:
							stats.gamesOf3p[ptIdx]++;
							break;
						case 4:
							stats.gamesOf4p[ptIdx]++;
							break;
						case 5:
							stats.gamesOf5p[ptIdx]++;
							break;
						case 6:
							stats.gamesOf6p[ptIdx]++;
							break;
						case 7:
							stats.gamesOf7p[ptIdx]++;
							break;
						case 8:
							stats.gamesOf8p[ptIdx]++;
							break;
						default:
							return; // nothing to track.
						}

						stats.surrenders[ptIdx] += TheGameInfo->haveWeSurrendered() || !TheVictoryConditions->getEndFrame();

						AsciiString systemSpec;
						systemSpec.format("LOD%d", TheGameLODManager->getRecommendedStaticLODLevel());
						stats.systemSpec = systemSpec.str();

						stats.techCaptured[ptIdx] += s->getTotalTechBuildingsCaptured();

						stats.unitsBuilt[ptIdx] += s->getTotalUnitsBuilt();
						stats.unitsKilled[ptIdx] += s->getTotalUnitsDestroyed();
						stats.unitsLost[ptIdx] += s->getTotalUnitsLost();

						DEBUG_LOG(("Before game built scud:%d, cannon:%d, nuke:%d\n", stats.builtSCUD, stats.builtParticleCannon, stats.builtNuke));
						stats.builtSCUD += CheckForApocalypse(s, "GLAScudStorm");
						stats.builtSCUD += CheckForApocalypse(s, "Chem_GLAScudStorm");
						stats.builtSCUD += CheckForApocalypse(s, "Demo_GLAScudStorm");
						stats.builtSCUD += CheckForApocalypse(s, "Slth_GLAScudStorm");
						stats.builtParticleCannon += CheckForApocalypse(s, "AmericaParticleCannonUplink");
						stats.builtParticleCannon += CheckForApocalypse(s, "AirF_AmericaParticleCannonUplink");
						stats.builtParticleCannon += CheckForApocalypse(s, "Lazr_AmericaParticleCannonUplink");
						stats.builtParticleCannon += CheckForApocalypse(s, "SupW_AmericaParticleCannonUplink");
						stats.builtNuke += CheckForApocalypse(s, "ChinaNuclearMissileLauncher");
						stats.builtNuke += CheckForApocalypse(s, "Nuke_ChinaNuclearMissileLauncher");
						stats.builtNuke += CheckForApocalypse(s, "Infa_ChinaNuclearMissileLauncher");
						stats.builtNuke += CheckForApocalypse(s, "Tank_ChinaNuclearMissileLauncher");
						DEBUG_LOG(("After game built scud:%d, cannon:%d, nuke:%d\n", stats.builtSCUD, stats.builtParticleCannon, stats.builtNuke));

						// TODO_NGMP_STATS: ladders
#if !defined(GENERALS_ONLINE)
						if (TheGameSpyGame->getLadderPort() && TheGameSpyGame->getLadderIP().isNotEmpty())
						{
							stats.lastLadderPort = TheGameSpyGame->getLadderPort();
							stats.lastLadderHost = TheGameSpyGame->getLadderIP().str();
						}
#endif

						if (!TheNetwork->sawCRCMismatch() && !gameEndedInDisconnect && !TheVictoryConditions->isLocalAlliedDefeat() && TheVictoryConditions->getEndFrame())
						{
							updateMPBattleHonors(stats.battleHonors, stats);
							updateChallengeMedals(stats.challengeMedals);
						}

#if !defined(GENERALS_ONLINE)
						DEBUG_LOG(("populatePlayerInfo() - tracking stats for %s/%s/%s\n", TheGameSpyGame->getLocalBaseName().str(), TheGameSpyGame->getLocalEmail().str(), TheNGMPGameTheGameSpyGamegetLocalPassword().str()));

						PSRequest req;
						req.requestType = PSRequest::PSREQUEST_UPDATEPLAYERSTATS;
						req.email = TheGameSpyInfo->getLocalEmail().str();
						req.nick = TheGameSpyInfo->getLocalBaseName().str();
						req.password = TheGameSpyInfo->getLocalPassword().str();
						req.player = stats;
						req.addDesync = TheNetwork->sawCRCMismatch();
						req.addDiscon = gameEndedInDisconnect;
						req.lastHouse = ptIdx;
						TheGameSpyPSMessageQueue->addRequest(req);
						TheGameSpyPSMessageQueue->trackPlayerStats(stats);

						// force an update of our shtuff
						PSResponse newResp;
						newResp.responseType = PSResponse::PSRESPONSE_PLAYERSTATS;
						newResp.player = stats;
						TheGameSpyPSMessageQueue->addResponse(newResp);

						// cache our stuff for easy reading next time
						GameSpyMiscPreferences mPref;
						mPref.setCachedStats(GameSpyPSMessageQueueInterface::formatPlayerKVPairs(stats).c_str());
						mPref.write();
					}
#else

						pStatsInterface->UpdateMyStats(stats);

						

						}, EStatsRequestPolicy::CACHED_ONLY); // NOTE: we really need the latest stats here, but this could cause a UI delay...
#endif
				}
			}
		}
	}
}

/** We Grab information about the players differently in Multiplayer.  We only want the players
		listed in the slots */
//-------------------------------------------------------------------------------------------------
void grabMultiPlayerInfo( void )
{
	typedef std::multimap<Int, Player *> ScoreMap;
	typedef ScoreMap::reverse_iterator RevScoreMapIt;

	Int playerCount = 0;
	AsciiString playerName;
	Player *player;
	ScoreMap scores;
	scores.clear();

	player = ThePlayerList->getLocalPlayer();
	if (player && parent)
	{
		const Image *image = TheMappedImageCollection->findImageByName("MutiPlayer_ScoreScreen");
		if(image)
		{
			parent->winSetEnabledImage(0, image);
			parent->winSetStatus(parent->winGetStatus() | WIN_STATUS_IMAGE );
		}
	}

	// Add each player and score to the map. THis allows us to sort the players based on score.
	for( Int i = 0; i < MAX_SLOTS; ++i)
	{
		playerName.format("player%d",i);
		player = ThePlayerList->findPlayerWithNameKey(TheNameKeyGenerator->nameToKey(playerName));
		if(player)
		{
			Int score = player->getScoreKeeper()->calculateScore();
			scores.emplace(score, player);
			++playerCount;
		}
	}
	hideWindows(playerCount);
	s_scoreRowCount = playerCount;

	// Pass 1: accumulate column totals + per-column min/max across non-observer
	// rows so we can render per-cell deltas vs the average and outline the
	// "best" / "worst" cell in each column.
	ScoreAverages avgs;
	zeroAverages(avgs);
	ScoreExtremes ex;
	Int rowCount = 0;
	RevScoreMapIt revIt;
	for ( revIt = scores.rbegin(); revIt != scores.rend(); ++revIt)
	{
		Player *p = revIt->second;
		if (p->isPlayerObserver())
			continue;
		ScoreKeeper *sk = p->getScoreKeeper();
		if (!sk)
			continue;
		const Int killedValue = sk->getTotalUnitsDestroyedValue()
			+ sk->getTotalBuildingsDestroyedValue();
		const Int eff = computeEfficiencyPct(killedValue, sk->getTotalMoneyEarned());
		avgs.unitsBuiltValue   += sk->getTotalUnitsBuiltValue();
		avgs.unitsLostValue    += sk->getTotalUnitsLostValue();
		avgs.unitsKilledValue  += sk->getTotalUnitsDestroyedValue();
		avgs.bldgBuiltValue    += sk->getTotalBuildingsBuiltValue();
		avgs.bldgLostValue     += sk->getTotalBuildingsLostValue();
		avgs.bldgKilledValue   += sk->getTotalBuildingsDestroyedValue();
		avgs.suppliesCollected += sk->getTotalMoneyEarned();
		avgs.efficiencyPct     += eff;
		const Bool seen = rowCount > 0;
		seedExtreme(sk->getTotalUnitsBuiltValue(),       ex.unitsBuiltMin,   ex.unitsBuiltMax,   seen);
		seedExtreme(sk->getTotalUnitsLostValue(),        ex.unitsLostMin,    ex.unitsLostMax,    seen);
		seedExtreme(sk->getTotalUnitsDestroyedValue(),   ex.unitsKilledMin,  ex.unitsKilledMax,  seen);
		seedExtreme(sk->getTotalBuildingsBuiltValue(),   ex.bldgBuiltMin,    ex.bldgBuiltMax,    seen);
		seedExtreme(sk->getTotalBuildingsLostValue(),    ex.bldgLostMin,     ex.bldgLostMax,     seen);
		seedExtreme(sk->getTotalBuildingsDestroyedValue(),ex.bldgKilledMin,  ex.bldgKilledMax,   seen);
		seedExtreme(sk->getTotalMoneyEarned(),           ex.suppliesMin,     ex.suppliesMax,     seen);
		seedExtreme(eff,                                  ex.efficiencyMin,   ex.efficiencyMax,   seen);
		++rowCount;
	}
	if (rowCount > 0)
	{
		avgs.unitsBuiltValue   /= rowCount;
		avgs.unitsLostValue    /= rowCount;
		avgs.unitsKilledValue  /= rowCount;
		avgs.bldgBuiltValue    /= rowCount;
		avgs.bldgLostValue     /= rowCount;
		avgs.bldgKilledValue   /= rowCount;
		avgs.suppliesCollected /= rowCount;
		avgs.efficiencyPct     /= rowCount;
	}
	// grabMultiPlayerInfo serves every per-player row path (skirmish, LAN,
	// internet, replay-of-MP). Each row is one player so column min/max
	// outlining is meaningful here. The solo path (grabSinglePlayerInfo)
	// passes nullptr to skip highlighting since its rows are heterogeneous.
	const ScoreExtremes *exPtr = (rowCount > 0) ? &ex : nullptr;

	Int count = 0;
	// display the players based on Score
	for ( revIt = scores.rbegin(); revIt != scores.rend(); ++revIt) 
	{
		Player *p = revIt->second;
		if(p->isPlayerObserver())
			setObserverWindows( p, count );
		else
			populatePlayerInfo( p, count, avgs, exPtr);
		count ++;
	}
	
	DEBUG_ASSERTCRASH(count == playerCount, ("For some reason we added %d players to the scores map, but only read %d", playerCount, count));
	
}

enum
{
	USA_FRIEND = 0,
	CHINA_FRIEND,
	GLA_FRIEND,
	USA_ENEMY,		// Keep friends with friends, enemys with enemys
	CHINA_ENEMY,
	GLA_ENEMY,

	MAX_RELATIONS
};
/**	Grab the single player info */
//-------------------------------------------------------------------------------------------------
void grabSinglePlayerInfo( void )
{
	Player *player, *localPlayer;
	localPlayer = ThePlayerList->getLocalPlayer();

	// Pick the player that will own the localPlayer row. Observers borrow
	// the first human's stats so the screen has at least one populated row.
	Player *displayLocal = nullptr;
	if (localPlayer)
	{
		if (!localPlayer->isPlayerObserver())
		{
			displayLocal = localPlayer;
		}
		else
		{
			for (Int k = 0; k < MAX_PLAYER_COUNT; ++k)
			{
				localPlayer = ThePlayerList->getNthPlayer(k);
				if (localPlayer && localPlayer->getPlayerType() == PLAYER_HUMAN)
				{
					displayLocal = localPlayer;
					break;
				}
				localPlayer = nullptr;
			}
		}
		PlayerTemplate const *fact = ThePlayerList->getLocalPlayer()->getPlayerTemplate();
		if (fact != nullptr)
		{
			const Image *image = TheMappedImageCollection->findImageByName(ThePlayerList->getLocalPlayer()->getPlayerTemplate()->getScoreScreen());
			if (image)
			{
				parent->winSetEnabledImage(0, image);
				parent->winSetStatus(parent->winGetStatus() | WIN_STATUS_IMAGE );
			}
		}
	}
	if (!localPlayer)
		return;

	// Buffer the side aggregates so we can compute column averages across
	// (local player + each populated side) before rendering. The order in
	// this buffer matches the order rows are displayed on the screen.
	struct SideRow
	{
		UnicodeString label;
		ScoreGather sg;
		Color color;
	};
	SideRow sideRows[MAX_RELATIONS];
	Int sideCount = 0;

	AsciiString side;
	Int j;
	// okay, there's all kinds of hard coding going on here.  THe reason why! well,
	// We have no way of telling what sides we have in the game.  Hence, the hardcoding.
	for (j = 0; j < MAX_RELATIONS; ++j)
	{
		Bool isFriend = TRUE;
		
		// set the string we'll be comparing to
		switch (j) {
		case USA_ENEMY:
			isFriend = FALSE;
			FALLTHROUGH;
		case USA_FRIEND:
			side.set("USA");
			break;
		case CHINA_ENEMY:
			isFriend = FALSE;
			FALLTHROUGH;
		case CHINA_FRIEND:
			side.set("China");
			break;
		case GLA_ENEMY:
			isFriend = FALSE;
			FALLTHROUGH;
		case GLA_FRIEND:
			side.set("GLA");
			break;
		}
		ScoreGather sg;
		sg.m_totalBuildingsBuilt = 0;
		sg.m_totalBuildingsDestroyed = 0;
		sg.m_totalBuildingsLost = 0;
		sg.m_totalMoneyEarned = 0;
		sg.m_totalMoneySpent = 0;
		sg.m_totalUnitsBuilt = 0;
		sg.m_totalUnitsDestroyed = 0;
		sg.m_totalUnitsLost = 0;
		sg.m_totalUnitsBuiltValue = 0;
		sg.m_totalUnitsLostValue = 0;
		sg.m_totalUnitsDestroyedValue = 0;
		sg.m_totalBuildingsBuiltValue = 0;
		sg.m_totalBuildingsLostValue = 0;
		sg.m_totalBuildingsDestroyedValue = 0;
		sg.m_sideImage = nullptr;
		Bool populate = FALSE;
		Color color;
		for (Int i = 0; i < MAX_PLAYER_COUNT; ++i)
		{
			player = ThePlayerList->getNthPlayer(i);
			if (player && player != localPlayer &&
				 side.compare(player->getBaseSide()) == 0)
			{
				if ((TheGameLogic->isInSinglePlayerGame() == FALSE) || (player->getListInScoreScreen() == TRUE))
				{
					if ((isFriend == TRUE && localPlayer->getRelationship(player->getDefaultTeam()) == ALLIES) ||
							(isFriend == FALSE && localPlayer->getRelationship(player->getDefaultTeam()) == ENEMIES))
					{
						ScoreKeeper *sk = player->getScoreKeeper();
						sg.m_totalBuildingsBuilt += sk->getTotalBuildingsBuilt();
						sg.m_totalBuildingsDestroyed += sk->getTotalBuildingsDestroyed();
						sg.m_totalBuildingsLost += sk->getTotalBuildingsLost();
						sg.m_totalMoneyEarned += sk->getTotalMoneyEarned();
						sg.m_totalMoneySpent += sk->getTotalMoneySpent();
						sg.m_totalUnitsBuilt += sk->getTotalUnitsBuilt();
						sg.m_totalUnitsDestroyed += sk->getTotalUnitsDestroyed();
						sg.m_totalUnitsLost += sk->getTotalUnitsLost();
						sg.m_totalUnitsBuiltValue       += sk->getTotalUnitsBuiltValue();
						sg.m_totalUnitsLostValue        += sk->getTotalUnitsLostValue();
						sg.m_totalUnitsDestroyedValue   += sk->getTotalUnitsDestroyedValue();
						sg.m_totalBuildingsBuiltValue   += sk->getTotalBuildingsBuiltValue();
						sg.m_totalBuildingsLostValue    += sk->getTotalBuildingsLostValue();
						sg.m_totalBuildingsDestroyedValue += sk->getTotalBuildingsDestroyedValue();
						sg.m_sideImage = player->getPlayerTemplate()->getSideIconImage();
						color = player->getPlayerColor();
						populate = TRUE;	
					}
				}
			}
		}
		if (populate)
		{
			AsciiString label;
			label.set("GUI:");
			label.concat(side);
			if (isFriend)
				label.concat("Allies");
			else
				label.concat("Enemies");
			sideRows[sideCount].label = TheGameText->fetch(label);
			sideRows[sideCount].sg = sg;
			sideRows[sideCount].color = color;
			++sideCount;
		}
	}

	// Compute column averages across the localPlayer row and each side row.
	ScoreAverages avgs;
	zeroAverages(avgs);
	Int rowCount = 0;
	if (displayLocal)
	{
		ScoreKeeper *sk = displayLocal->getScoreKeeper();
		if (sk)
		{
			const Int killedValue = sk->getTotalUnitsDestroyedValue()
				+ sk->getTotalBuildingsDestroyedValue();
			avgs.unitsBuiltValue   += sk->getTotalUnitsBuiltValue();
			avgs.unitsLostValue    += sk->getTotalUnitsLostValue();
			avgs.unitsKilledValue  += sk->getTotalUnitsDestroyedValue();
			avgs.bldgBuiltValue    += sk->getTotalBuildingsBuiltValue();
			avgs.bldgLostValue     += sk->getTotalBuildingsLostValue();
			avgs.bldgKilledValue   += sk->getTotalBuildingsDestroyedValue();
			avgs.suppliesCollected += sk->getTotalMoneyEarned();
			avgs.efficiencyPct     += computeEfficiencyPct(killedValue, sk->getTotalMoneyEarned());
			++rowCount;
		}
	}
	for (j = 0; j < sideCount; ++j)
	{
		const ScoreGather &sg = sideRows[j].sg;
		const Int killedValue = sg.m_totalUnitsDestroyedValue
			+ sg.m_totalBuildingsDestroyedValue;
		avgs.unitsBuiltValue   += sg.m_totalUnitsBuiltValue;
		avgs.unitsLostValue    += sg.m_totalUnitsLostValue;
		avgs.unitsKilledValue  += sg.m_totalUnitsDestroyedValue;
		avgs.bldgBuiltValue    += sg.m_totalBuildingsBuiltValue;
		avgs.bldgLostValue     += sg.m_totalBuildingsLostValue;
		avgs.bldgKilledValue   += sg.m_totalBuildingsDestroyedValue;
		avgs.suppliesCollected += sg.m_totalMoneyEarned;
		avgs.efficiencyPct     += computeEfficiencyPct(killedValue, sg.m_totalMoneyEarned);
		++rowCount;
	}
	if (rowCount > 0)
	{
		avgs.unitsBuiltValue   /= rowCount;
		avgs.unitsLostValue    /= rowCount;
		avgs.unitsKilledValue  /= rowCount;
		avgs.bldgBuiltValue    /= rowCount;
		avgs.bldgLostValue     /= rowCount;
		avgs.bldgKilledValue   /= rowCount;
		avgs.suppliesCollected /= rowCount;
		avgs.efficiencyPct     /= rowCount;
	}

	// Render rows.
	Int playerCount = 0;
	if (displayLocal)
	{
		populatePlayerInfo(displayLocal, playerCount, avgs, nullptr);
		++playerCount;
	}
	for (j = 0; j < sideCount; ++j)
	{
		populateSideInfo(sideRows[j].label, &sideRows[j].sg, playerCount, sideRows[j].color, avgs);
		++playerCount;
	}
	hideWindows(playerCount);
}

/** Hide the windows we're not using */
//-------------------------------------------------------------------------------------------------
void hideWindows( Int pos )
{
	if(pos < 0 || pos >= MAX_SLOTS)
		return;
	AsciiString winName;
	GameWindow *win;
	for( Int i = pos; i < MAX_SLOTS; ++i)
	{

		// set the player name
		winName.format("ScoreScreen.wnd:StaticTextPlayer%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// set the player name
		winName.format("ScoreScreen.wnd:StaticTextObserver%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// set the total units built
		winName.format("ScoreScreen.wnd:StaticTextUnitsBuilt%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// set the total units Lost
		winName.format("ScoreScreen.wnd:StaticTextUnitsLost%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// set the total units Destroyed
		winName.format("ScoreScreen.wnd:StaticTextUnitsDestroyed%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// set the total BuildingsBuilt
		winName.format("ScoreScreen.wnd:StaticTextBuildingsBuilt%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// set the total BuildingsLost
		winName.format("ScoreScreen.wnd:StaticTextBuildingsLost%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// set the total BuildingsDestroyed
		winName.format("ScoreScreen.wnd:StaticTextBuildingsDestroyed%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// set the total Resources
		winName.format("ScoreScreen.wnd:StaticTextResources%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// hide the score (efficiency) column for slots we're not using
		winName.format("ScoreScreen.wnd:StaticTextScore%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);

		// Set the Game Winner marker
		winName.format("ScoreScreen.wnd:GameWindowWinner%d", i);
		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
		win->winHide(TRUE);
		

//		// Set the Game Add Buttons
//		winName.format("ScoreScreen.wnd:ButtonAdd%d", i);
//		win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
//		DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
//		win->winHide(TRUE);
	}
}

/** Hide the windows we're not using */
//-------------------------------------------------------------------------------------------------
void setObserverWindows( Player *player, Int i )
{
	if(!player || i < 0 || i >= MAX_SLOTS)
		return;
	AsciiString winName;
	GameWindow *win;
	
	Color color = 0xffffffff;
	
	// set the player name
	winName.format("ScoreScreen.wnd:StaticTextPlayer%d", i);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));

	GadgetStaticTextSetText(win, player->getPlayerDisplayName());
	win->winHide(FALSE);
	win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());

	// set the player name
	winName.format("ScoreScreen.wnd:StaticTextObserver%d", i);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	win->winHide(FALSE);


	// set the total units built
	winName.format("ScoreScreen.wnd:StaticTextUnitsBuilt%d", i);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	win->winHide(TRUE);

	// set the total units Lost
	winName.format("ScoreScreen.wnd:StaticTextUnitsLost%d", i);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	win->winHide(TRUE);

	// set the total units Destroyed
	winName.format("ScoreScreen.wnd:StaticTextUnitsDestroyed%d", i);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	win->winHide(TRUE);

	// set the total BuildingsBuilt
	winName.format("ScoreScreen.wnd:StaticTextBuildingsBuilt%d", i);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	win->winHide(TRUE);

	// set the total BuildingsLost
	winName.format("ScoreScreen.wnd:StaticTextBuildingsLost%d", i);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	win->winHide(TRUE);

	// set the total BuildingsDestroyed
	winName.format("ScoreScreen.wnd:StaticTextBuildingsDestroyed%d", i);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	win->winHide(TRUE);

	// set the total Resources
	winName.format("ScoreScreen.wnd:StaticTextResources%d", i);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	win->winHide(TRUE);

	// hide the score (efficiency) column for observers
	winName.format("ScoreScreen.wnd:StaticTextScore%d", i);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	win->winHide(TRUE);

	// Set the Game Winner marker
	winName.format("ScoreScreen.wnd:GameWindowWinner%d", i);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	win->winHide(FALSE);
	const PlayerTemplate *fact = player->getPlayerTemplate();
	if(fact != nullptr)
	{
		win->winSetEnabledImage(0, fact->getSideIconImage());
	}
	
//	// set the Buttons
//	winName.format("ScoreScreen.wnd:ButtonAdd%d", i);
//	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
//	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
////	if(screenType ==	SCORESCREEN_INTERNET)
////		win->winHide(FALSE);
////	else
////		win->winHide(TRUE);
//	if(screenType ==	SCORESCREEN_INTERNET && TheGameSpyInfo && TheGameSpyInfo->getLocalProfileID() != 0)
//	{
//		// Get the stats for the player
//		
//		AsciiString aName;
//		aName.translate(player->getPlayerDisplayName());
//
//		PlayerInfoMap::iterator blah = TheGameSpyInfo->getPlayerInfoMap()->find(aName);
//		if(blah->second.m_profileID != 0)
//		{
//			
//			BuddyInfoMap::iterator bIt = TheGameSpyInfo->getBuddyMap()->find(blah->second.m_profileID);
//			if(bIt !=  TheGameSpyInfo->getBuddyMap()->end()  || (TheGameSpyInfo->getLocalProfileID() == blah->second.m_profileID))
//			{
//				// already our buddy
//				win->winHide(TRUE);
//			}
//			else
//			{
//		
//				GadgetButtonSetData(win, (void *) blah->second.m_profileID);
//				win->winEnable(TRUE);
//				win->winHide(FALSE);
//			}
//		}
//		else
//			win->winHide(TRUE);
//	}
//	else
//	{
//		win->winHide(TRUE);
//	}


}

/** Populate the various windows with the information about the game based on each player's score
		keeper. */
//-------------------------------------------------------------------------------------------------
void populateSideInfo( UnicodeString side, ScoreGather *sg, Int pos, Color color, const ScoreAverages &avgs)
{
	if(pos < 0 || pos > MAX_SLOTS)
		return;
	color = GameMakeColorReadable(color);

	AsciiString winName;
	UnicodeString winValue;
	GameWindow *win;
	// set the player name
	winName.format("ScoreScreen.wnd:StaticTextPlayer%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	GadgetStaticTextSetText(win, side);
	win->winHide(FALSE);
	win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());

	// set the player name
	winName.format("ScoreScreen.wnd:StaticTextObserver%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	win->winHide(TRUE);

	// set the total units built (count + value)
	winName.format("ScoreScreen.wnd:StaticTextUnitsBuilt%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	formatCountAndValueCell(sg->m_totalUnitsBuilt, sg->m_totalUnitsBuiltValue, avgs.unitsBuiltValue, winValue);
	GadgetStaticTextSetText(win, winValue);
	win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());
	win->winHide(FALSE);

	// set the total units Lost (count + value)
	winName.format("ScoreScreen.wnd:StaticTextUnitsLost%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	formatCountAndValueCell(sg->m_totalUnitsLost, sg->m_totalUnitsLostValue, avgs.unitsLostValue, winValue);
	GadgetStaticTextSetText(win, winValue);
	win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());
	win->winHide(FALSE);

	// set the total units Destroyed (count + value)
	winName.format("ScoreScreen.wnd:StaticTextUnitsDestroyed%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	formatCountAndValueCell(sg->m_totalUnitsDestroyed, sg->m_totalUnitsDestroyedValue, avgs.unitsKilledValue, winValue);
	GadgetStaticTextSetText(win, winValue);
	win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());
	win->winHide(FALSE);

	// set the total BuildingsBuilt (count + value)
	winName.format("ScoreScreen.wnd:StaticTextBuildingsBuilt%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	formatCountAndValueCell(sg->m_totalBuildingsBuilt, sg->m_totalBuildingsBuiltValue, avgs.bldgBuiltValue, winValue);
	GadgetStaticTextSetText(win, winValue);
	win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());
	win->winHide(FALSE);

	// set the total BuildingsLost (count + value)
	winName.format("ScoreScreen.wnd:StaticTextBuildingsLost%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	formatCountAndValueCell(sg->m_totalBuildingsLost, sg->m_totalBuildingsLostValue, avgs.bldgLostValue, winValue);
	GadgetStaticTextSetText(win, winValue);
	win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());
	win->winHide(FALSE);

	// set the total BuildingsDestroyed (count + value)
	winName.format("ScoreScreen.wnd:StaticTextBuildingsDestroyed%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	formatCountAndValueCell(sg->m_totalBuildingsDestroyed, sg->m_totalBuildingsDestroyedValue, avgs.bldgKilledValue, winValue);
	GadgetStaticTextSetText(win, winValue);
	win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());
	win->winHide(FALSE);

	// set the total Resources
	winName.format("ScoreScreen.wnd:StaticTextResources%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	formatDollarCell(sg->m_totalMoneyEarned, avgs.suppliesCollected, winValue);
	GadgetStaticTextSetText(win, winValue);
	win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());
	win->winHide(FALSE);

	// set the Score field — repurposed as the efficiency column.
	// Efficiency = (value killed) / income * 100 across the aggregated side.
	winName.format("ScoreScreen.wnd:StaticTextScore%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	{
		const Int killedValue = sg->m_totalUnitsDestroyedValue
			+ sg->m_totalBuildingsDestroyedValue;
		formatEfficiencyCell(killedValue, sg->m_totalMoneyEarned, avgs.efficiencyPct, winValue);
	}
	GadgetStaticTextSetText(win, winValue);
	win->winSetEnabledTextColors(color, win->winGetEnabledTextBorderColor());
	win->winHide(FALSE);

	// set a marker for who won and lost
	winName.format("ScoreScreen.wnd:GameWindowWinner%d", pos);
	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
	if(sg->m_sideImage)
	{
		win->winHide(FALSE);	
		win->winSetEnabledImage(0, sg->m_sideImage);
	}

//	// set the Buttons
//	winName.format("ScoreScreen.wnd:ButtonAdd%d", pos);
//	win =  TheWindowManager->winGetWindowFromId( parent, TheNameKeyGenerator->nameToKey( winName ) );
//	DEBUG_ASSERTCRASH(win,("Could not find window %s on the score screen", winName.str()));
//	if(screenType ==	SCORESCREEN_INTERNET)
//		win->winHide(FALSE);
//	else
//		win->winHide(TRUE);
}
