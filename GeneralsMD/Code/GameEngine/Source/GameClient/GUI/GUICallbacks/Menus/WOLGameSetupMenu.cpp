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

///////////////////////////////////////////////////////////////////////////////////////
// FILE: WOLGameSetupMenu.cpp
// Author: Matt Campbell, December 2001
// Description: WOL Game Options Menu
///////////////////////////////////////////////////////////////////////////////////////

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GameEngine.h"
#include "Common/GameState.h"
#include "Common/MultiplayerSettings.h"
#include "Common/OptionPreferences.h"
#include "GameClient/Display.h"
#include "GameClient/GameText.h"
#include "Common/PlayerTemplate.h"
#include "Common/CustomMatchPreferences.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/InGameUI.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Mouse.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/ResumeFromReplay.h"
#include "GameClient/EstablishConnectionsMenu.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"

#include "GameNetwork/GameSpy/BuddyDefs.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpy/PeerThread.h"
#include "GameNetwork/GameSpy/PersistentStorageDefs.h"
#include "GameNetwork/GameSpy/PersistentStorageThread.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameNetwork/NAT.h"
#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/GameSpy/GSConfig.h"

#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"
#include "GameNetwork/RandomAssign.h"
#include "Common/LobbyDiscord.h"
#include "Common/StatsUploader.h" // ChooseMapFromServer / DownloadAndInstallMap (Radarvan Pick)
#include "Common/ReleaseLog.h"
#include "GameNetwork/GeneralsOnline/NetworkSelfTest.h"
#include "GameNetwork/MapDownloadHook.h" // peer-side map fetch for a map we do not have
#include "Common/GlobalData.h"
#include <ws2ipdef.h>
#include <format>
#include <cmath>
#include "../OnlineServices_Init.h"
#include "GameLogic/GameLogic.h"
NGMPGame* TheNGMPGame = NULL;

void WOLDisplaySlotList( void );
static void WOLRefreshConnectionIndicators( void );


extern std::list<PeerResponse> TheLobbyQueuedUTMs;
extern void MapSelectorTooltip(GameWindow *window, WinInstanceData *instData,	UnsignedInt mouse);


#if defined(RTS_DEBUG)
extern Bool g_debugSlots;
void slotListDebugLog(const char *fmt, ...)
{
	static char buf[1024];
	va_list va;
	va_start( va, fmt );
	vsnprintf(buf, 1024, fmt, va );
	va_end( va );

	DEBUG_LOG(("%s", buf));
	if (g_debugSlots)
	{
		UnicodeString msg;
		msg.translate(buf);
		// TODO_NGMP: Impl again
		TheGameSpyInfo->addText(msg, GameSpyColor[GSCOLOR_DEFAULT], NULL);
	}
}
#define SLOTLIST_DEBUG_LOG(x) slotListDebugLog x
#else
#define SLOTLIST_DEBUG_LOG(x) DEBUG_LOG(x)
#endif

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static Bool isShuttingDown = false;
static Bool buttonPushed = false;
static const char *nextScreen = NULL;
static Bool raiseMessageBoxes = false;
static Bool launchGameNext = FALSE;

// window ids ------------------------------------------------------------------------------
static NameKeyType parentWOLGameSetupID = NAMEKEY_INVALID;

static NameKeyType comboBoxPlayerID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType staticTextPlayerID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID,
																											NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType buttonAcceptID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType comboBoxColorID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType comboBoxPlayerTemplateID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType comboBoxTeamID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };
//static NameKeyType buttonStartPositionID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
//																										NAMEKEY_INVALID,NAMEKEY_INVALID,
//																										NAMEKEY_INVALID,NAMEKEY_INVALID,
//																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType buttonMapStartPositionID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };
static NameKeyType genericPingWindowID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static NameKeyType textEntryChatID = NAMEKEY_INVALID;
static NameKeyType textEntryMapDisplayID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType buttonStartID = NAMEKEY_INVALID;
static NameKeyType buttonEmoteID = NAMEKEY_INVALID;
static NameKeyType buttonSelectMapID = NAMEKEY_INVALID;
static NameKeyType buttonRandomizeID = NAMEKEY_INVALID;
// TheSuperHackers @feature bill-rich 02/10/2026 Radarvan Pick, the LAN lobby's map
// vote button, now on this screen too: host-only, asks radarvan for a map that
// suits the roster, and applies it the way Select Map does.
static NameKeyType buttonMapVoteID = NAMEKEY_INVALID;
// TheSuperHackers @feature bill-rich 11/10/2026 Resume-from-replay button (host arms or cancels; guests see the state).
static NameKeyType buttonResumeID = NAMEKEY_INVALID;
static NameKeyType windowMapID = NAMEKEY_INVALID;

#if defined(GENERALS_ONLINE_ENABLE_MATCH_START_COUNTDOWN)
static bool s_matchStartCountdownWasRunning = false;
#endif

static NameKeyType windowMapSelectMapID = NAMEKEY_INVALID;
static NameKeyType checkBoxUseStatsID = NAMEKEY_INVALID;
static NameKeyType checkBoxLimitSuperweaponsID = NAMEKEY_INVALID;
// TheSuperHackers @feature bill-rich 27/09/2026 enforce-random, host controlled:
// while it is set the service refuses any concrete faction or team, and every
// player's two dropdowns are locked (see EnableAcceptControls). The Randomize
// button still resolves the randoms, because it sends a bulk slot update.
static NameKeyType checkBoxEnforceRandomID = NAMEKEY_INVALID;
static NameKeyType comboBoxStartingCashID = NAMEKEY_INVALID;
static NameKeyType checkBoxLimitArmiesID = NAMEKEY_INVALID;

// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentWOLGameSetup = NULL;
static GameWindow *buttonBack = NULL;
static GameWindow *buttonStart = NULL;
static GameWindow *buttonSelectMap = NULL;
static GameWindow *buttonRandomize = NULL;
static GameWindow *buttonMapVote = NULL;
static GameWindow *buttonResume = NULL;
// TheSuperHackers @feature bill-rich 15/09/2026 How many times the host pressed Randomize in this
// lobby; announced with every roll so re-rolling is visible to everyone present.
static Int s_randomizeCount = 0;
// TheSuperHackers @feature bill-rich 24/09/2026 The lobby remembers a faction and colour as the player's
// preference only when the player picked them here. A value that arrived from the service, because
// the host pressed Randomize or moved this slot, is not a choice and must not be stored.
// s_applyingLobbyUpdate marks the repaint that a service push drives: it fires the same combo
// callbacks as a real click, so without it the push that delivers a roll looks like a choice.
static Bool s_localSlotChosen = FALSE;
static Bool s_applyingLobbyUpdate = FALSE;
static GameWindow *buttonEmote = NULL;
static GameWindow *textEntryChat = NULL;
static GameWindow *textEntryMapDisplay = NULL;
static GameWindow *windowMap = NULL;
static GameWindow *checkBoxUseStats = NULL;
static GameWindow *checkBoxLimitSuperweapons = NULL;
static GameWindow *checkBoxEnforceRandom = NULL;
static GameWindow *comboBoxStartingCash = NULL;
static GameWindow *checkBoxLimitArmies = NULL;

static GameWindow *comboBoxPlayer[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																									 NULL,NULL,NULL,NULL };
static GameWindow *staticTextPlayer[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																									 NULL,NULL,NULL,NULL };
static GameWindow *buttonAccept[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };

static GameWindow *comboBoxColor[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };

static GameWindow *comboBoxPlayerTemplate[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };

static GameWindow *comboBoxTeam[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };

//static GameWindow *buttonStartPosition[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
//																								NULL,NULL,NULL,NULL };
//
static GameWindow *buttonMapStartPosition[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };

static GameWindow *genericPingWindow[MAX_SLOTS] = {NULL,NULL,NULL,NULL,
																								NULL,NULL,NULL,NULL };

// Connection indicator per slot: 0..4 is the signal level (1..5 bars), negative values are special states.
enum
{
	CONNECTION_INDICATOR_CONNECTING = -1,
	CONNECTION_INDICATOR_WARNING = -2,
};
static Int connectionIndicatorState[MAX_SLOTS];

static const Int CONNECTION_LEVEL_COUNT = 5;
static const Int connectionLevelThresholds[CONNECTION_LEVEL_COUNT - 1] = { 40, 60, 75, 90 }; ///< score needed for 2..5 bars
static const Int CONNECTION_LEVEL_HYSTERESIS = 3;

WindowLayout *WOLMapSelectLayout = NULL;

void PopBackToLobby()
{
	// delete TheNAT, its no good for us anymore.
	if (TheNAT != nullptr)
	{
		delete TheNAT;
		TheNAT = NULL;
	}

	if (TheNGMPGame) // this can be blown away by a disconnect on the map transfer screen
	{
		TheNGMPGame->reset();


	}

	DEBUG_LOG(("PopBackToLobby() - parentWOLGameSetup is %X", parentWOLGameSetup));
	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if (pLobbyInterface != nullptr)
	{
		pLobbyInterface->LeaveCurrentLobby();
	}

	if (parentWOLGameSetup)
	{
		nextScreen = "Menus/WOLCustomLobby.wnd";
		TheShell->pop();


	}
}

void updateMapStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], Bool onLoadScreen = FALSE );
void positionStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);
void positionStartSpots(AsciiString mapName, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);
void WOLPositionStartSpots()
{
	GameWindow *win = windowMap;
	if (WOLMapSelectLayout != NULL) {
		win = TheWindowManager->winGetWindowFromId(NULL, windowMapSelectMapID);

		// get the controls.
		NameKeyType listboxMapID = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:ListboxMap" );
		GameWindow *listboxMap = TheWindowManager->winGetWindowFromId( NULL, listboxMapID );

		if (listboxMap != NULL) {
			Int selected;
			UnicodeString map;

			// get the selected index
			GadgetListBoxGetSelected( listboxMap, &selected );

			if( selected != -1 )
			{

				// get text of the map to load
				map = GadgetListBoxGetText( listboxMap, selected, 0 );


				// set the map name in the global data map name
				AsciiString asciiMap;
				const char *mapFname = (const char *)GadgetListBoxGetItemData( listboxMap, selected );
				DEBUG_ASSERTCRASH(mapFname, ("No map item data"));
				if (mapFname) {
					asciiMap = mapFname;
				} else {
					asciiMap.translate( map );
				}

				positionStartSpots(asciiMap, buttonMapStartPosition, win);
			}
		}

	} else {
		DEBUG_ASSERTCRASH(win != NULL, ("no map preview window"));

		AsciiString map = TheNGMPGame->getMap();
		positionStartSpots( map, buttonMapStartPosition, win);
	}
}
static void savePlayerInfo()
{
	if (TheNGMPGame)
	{
		Int slotNum = TheNGMPGame->getLocalSlotNum();
		if (slotNum >= 0)
		{
			NGMPGameSlot *slot = TheNGMPGame->getGameSpySlot(slotNum);
			if (slot)
			{
				// save off some prefs
				CustomMatchPreferences pref;
				if (s_localSlotChosen)
				{
					pref.setPreferredColor(slot->getColor());
					pref.setPreferredFaction(slot->getPlayerTemplate());
				}
				if (TheNGMPGame->amIHost())
				{
					pref.setPreferredMap(TheNGMPGame->getMap());
				}
				pref.write();
			}
		}
	}
}

// Tooltips -------------------------------------------------------------------------------

static void playerTooltip(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	Int slotIdx = -1;
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		if (window == comboBoxPlayer[i] || window == staticTextPlayer[i])
		{
			slotIdx = i;
			break;
		}
	}
	if (slotIdx < 0)
	{
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString, -1, NULL, 1.5f );
		return;
	}

	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		TheMouse->setCursorTooltip(UnicodeString::TheEmptyString, -1, NULL, 1.5f);
		return;
	}

	NGMPGame* game = pLobbyInterface->GetCurrentGame();
	if (!game)
	{
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString, -1, NULL, 1.5f );
		return;
	}

	NGMPGameSlot *slot = game->getGameSpySlot(slotIdx);
	if (!slot || !slot->isHuman())
	{
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString, -1, NULL, 1.5f );
		return;
	}

	// for tooltip, we want:
	// * player name
	// * ping
	// * locale
	// * win/loss history
	// * discons/desyncs as one var
	// * favorite army
	// in that order.  got it?  good.

	UnicodeString uName = slot->getName();
// 
// 	AsciiString aName;
// 	aName.translate(uName);
// 	PlayerInfoMap::iterator pmIt = TheGameSpyInfo->getPlayerInfoMap()->find(aName);
// 	if (pmIt == TheGameSpyInfo->getPlayerInfoMap()->end())
// 	{
// 		TheMouse->setCursorTooltip( uName, -1, NULL, 1.5f );
// 		return;
// 	}
	//Int profileID = pmIt->second.m_profileID;

	int64_t profileID = slot->m_userID;

	//TheMouse->setCursorTooltip(UnicodeString(L"Retrieving User Stats..."), -1, NULL, 1.5f);

	NGMP_OnlineServices_StatsInterface* pStatsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_StatsInterface>();
	if (pStatsInterface == nullptr)
	{
		return;
	}

	pStatsInterface->findPlayerStatsByID(profileID, [=](bool bSuccess, PSPlayerStats stats)
	{
			if (stats.id == 0)
			{
				TheMouse->setCursorTooltip(uName, -1, NULL, 1.5f);
				return;
			}

			Bool isLocalPlayer = slot == game->getGameSpySlot(game->getLocalSlotNum());

			//AsciiString localeIdentifier;
			//localeIdentifier.format("WOL:Locale%2.2d", stats.locale);
			UnicodeString	playerInfo;
			Int totalWins = 0, totalLosses = 0, totalDiscons = 0;
			PerGeneralMap::iterator it;

	for (it = stats.wins.begin(); it != stats.wins.end(); ++it)
	{
		totalWins += it->second;
	}
	for (it = stats.losses.begin(); it != stats.losses.end(); ++it)
	{
		totalLosses += it->second;
	}
	for (it = stats.discons.begin(); it != stats.discons.end(); ++it)
	{
		totalDiscons += it->second;
	}
	for (it = stats.desyncs.begin(); it != stats.desyncs.end(); ++it)
	{
		totalDiscons += it->second;
	}
	UnicodeString favoriteSide;
	Int numGames = 0;
	Int favorite = 0;
	for(it = stats.games.begin(); it != stats.games.end(); ++it)
	{
		if (it->first == PLAYERTEMPLATE_OBSERVER) // not a real faction
			continue;
		if(it->second >= numGames)
		{
			numGames = it->second;
			favorite = it->first;
		}
	}
	if(numGames == 0)
		favoriteSide = TheGameText->fetch("GUI:None");
	else if( stats.gamesAsRandom >= numGames )
		favoriteSide = TheGameText->fetch("GUI:Random");
	else
	{
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(favorite);
		if (fac)
		{
			AsciiString side;
			side.format("SIDE:%s", fac->getSide().str());

					favoriteSide = TheGameText->fetch(side);
				}
			}

#if defined(GENERALS_ONLINE)
	int64_t localPlayerID = -1;
	NGMP_OnlineServices_AuthInterface* pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	if (pAuthInterface != nullptr)
	{
		localPlayerID = pAuthInterface->GetUserID();
	}

	bool bIsConnected = false;
	int connectionScore = -1;
	int connectionLatency = -1;
	int connectionJitter = -1;
	int connectionQualityPct = -1;
	std::string strConnectionType = "";

	LobbyMemberEntry member = pLobbyInterface->GetRoomMemberFromID(slot->m_userID);

	if (localPlayerID != slot->m_userID)
	{
		if (NGMP_OnlineServicesManager::GetNetworkMesh() != nullptr)
		{
			PlayerConnection* pConnection = NGMP_OnlineServicesManager::GetNetworkMesh()->GetConnectionForUser(slot->m_userID);

			if (pConnection != nullptr)
			{
				strConnectionType = pConnection->GetConnectionType();
				if (pConnection->GetState() == EConnectionState::CONNECTED_DIRECT)
				{
					bIsConnected = true;
				}
				else
				{
					bIsConnected = false;
				}
				connectionScore = pConnection->ComputeConnectionScore();
				connectionLatency = pConnection->GetLatency();
				connectionJitter = pConnection->GetJitter();
				float rawQuality = pConnection->GetConnectionQuality();
				connectionQualityPct = (rawQuality >= 0.0f) ? static_cast<int>(rawQuality * 100.0f) : -1;
			}
		}
	}

	if (localPlayerID == slot->m_userID)
	{
		// local user wont have a connection
		playerInfo.format(L"\nWins: %d\nLosses: %d\nDisconnects: %d\nFavorite Army: %s",
			totalWins, totalLosses, totalDiscons, favoriteSide.str());
	}
	else if (bIsConnected)
	{
		UnicodeString scoreStr, latencyStr, jitterStr, qualityStr;
		if (connectionScore >= 0) scoreStr.format(L"%d%%", connectionScore); else scoreStr = L"Unknown";
		if (connectionLatency >= 0) latencyStr.format(L"%d ms", connectionLatency); else latencyStr = L"Unknown";
		if (connectionJitter >= 0) jitterStr.format(L"%d ms", connectionJitter); else jitterStr = L"Unknown";
		if (connectionQualityPct >= 0) qualityStr.format(L"%d%%", connectionQualityPct); else qualityStr = L"Unknown";
		playerInfo.format(L"\nConnection State: Connected (%hs)\nConnection Score: %s\nLatency: %s\nJitter: %s\nReliability: %s\nRegion: %hs\nWins: %d\nLosses: %d\nDisconnects: %d\nFavorite Army: %s",
			strConnectionType.c_str(), scoreStr.str(), latencyStr.str(), jitterStr.str(), qualityStr.str(),
			member.region.c_str(), totalWins, totalLosses, totalDiscons, favoriteSide.str());
	}
	else
	{
		playerInfo.format(L"\nConnection State: Connecting...\nRegion: %hs\nWins: %d\nLosses: %d\nDisconnects: %d\nFavorite Army: %s",
			member.region.c_str(), totalWins, totalLosses, totalDiscons, favoriteSide.str());
	}
#else
			playerInfo.format(L"\nLatency: %d ms\nWins: %d\nLosses: %d\nDisconnects: %d\nFavorite Army: %s",
				slot->getPingAsInt(), totalWins, totalLosses, totalDiscons, favoriteSide.str());
#endif

			UnicodeString tooltip = UnicodeString::TheEmptyString;
			if (isLocalPlayer)
			{
				tooltip.format(TheGameText->fetch("TOOLTIP:LocalPlayer"), uName.str());
			}
			else
			{
				// not us
				// TODO_NGMP: Impl friends again
				bool bIsFriend = false;
				if (bIsFriend)
				//if (TheGameSpyInfo->getBuddyMap()->find(profileID) != TheGameSpyInfo->getBuddyMap()->end())
				{
					// buddy
					tooltip.format(TheGameText->fetch("TOOLTIP:BuddyPlayer"), uName.str());
				}
				else
				{
					if (profileID)
					{
						// non-buddy profiled player
						tooltip.format(TheGameText->fetch("TOOLTIP:ProfiledPlayer"), uName.str());
					}
					else
					{
						// non-profiled player
						tooltip.format(TheGameText->fetch("TOOLTIP:GenericPlayer"), uName.str());
					}
				}
			}

			tooltip.concat(playerInfo);

			NGMP_OnlineServices_RoomsInterface* pRoomsInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_RoomsInterface>();
			if (pAuthInterface != nullptr && pRoomsInterface != nullptr)
			{
				NetworkRoomMember* localMember = pRoomsInterface->GetRoomMemberFromID(pAuthInterface->GetUserID());
				if (localMember != nullptr && localMember->m_bIsAdmin)
				{
					UnicodeString idLine;
					idLine.format(L"\n\nUser ID: %lld", slot->m_userID);
					tooltip.concat(idLine);
				}
			}

			TheMouse->setCursorTooltip(tooltip, -1, NULL, 1.5f); // the text and width are the only params used.  the others are the default values.

	}, EStatsRequestPolicy::CACHED_ONLY);
}

void gameAcceptTooltip(GameWindow *window, WinInstanceData *instData, UnsignedInt mouse)
{
	Int x, y;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);

	Int winPosX, winPosY, winWidth, winHeight;

	window->winGetScreenPosition(&winPosX, &winPosY);

	window->winGetSize(&winWidth, &winHeight);

	if ((x > winPosX && x < (winPosX + winWidth)) && (y > winPosY && y < (winPosY + winHeight)))
	{
		TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:GameAcceptance"), -1, NULL);
	}
}

void pingTooltip(GameWindow *window, WinInstanceData *instData, UnsignedInt mouse)
{
	Int x, y;
	x = LOLONGTOSHORT(mouse);
	y = HILONGTOSHORT(mouse);


	Int winPosX, winPosY, winWidth, winHeight;

	window->winGetScreenPosition(&winPosX, &winPosY);

	window->winGetSize(&winWidth, &winHeight);

	if ((x > winPosX && x < (winPosX + winWidth)) && (y > winPosY && y < (winPosY + winHeight)))
	{
		TheMouse->setCursorTooltip(TheGameText->fetch("TOOLTIP:ConnectionSpeed"), -1, NULL);
	}
}

//external declarations of the Gadgets the callbacks can use
GameWindow *listboxGameSetupChat = NULL;
NameKeyType listboxGameSetupChatID = NAMEKEY_INVALID;

static void handleColorSelection(int index)
{
	if (!s_applyingLobbyUpdate && TheNGMPGame && index == TheNGMPGame->getLocalSlotNum())
		s_localSlotChosen = TRUE; // picked here, so it is worth remembering
	GameWindow *combo = comboBoxColor[index];
	Int color, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	color = (Int)GadgetComboBoxGetItemData(combo, selIndex);

	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

	NGMPGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	if (myGame)
	{
		GameSlot * slot = myGame->getSlot(index);
		if (color == slot->getColor())
			return;

		if (color >= -1 && color < TheMultiplayerSettings->getNumColors())
		{
			Bool colorAvailable = TRUE;
			if(color != -1 )
			{
				for(Int i=0; i <MAX_SLOTS; i++)
				{
					GameSlot *checkSlot = myGame->getSlot(i);
					if(color == checkSlot->getColor() && slot != checkSlot)
					{
						colorAvailable = FALSE;
						break;
					}
				}
			}

			// TODO_NGMP: Enforce this on the service too
			if(!colorAvailable)
				return;
		}

		// NGMP: Dont set it locally / directly anymore, rely on the lobby service instead
		//slot->setColor(color);

		// NGMP: Update lobby (if local, for remote players we'll get it from the service)
		NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

		if (index == myGame->getLocalSlotNum())
		{
			if (pLobbyInterface != nullptr)
			{
				pLobbyInterface->UpdateCurrentLobby_MyColor(color);
			}
		}
		else if (slot->getState() == SLOT_EASY_AI || slot->getState() == SLOT_MED_AI || slot->getState() == SLOT_BRUTAL_AI)
		{
			if (pLobbyInterface != nullptr)
			{
				pLobbyInterface->UpdateCurrentLobby_AIColor(index, color);
			}
		}
	}
}

static void handlePlayerTemplateSelection(int index, bool bInitialSetup = false)
{
	if (!bInitialSetup && !s_applyingLobbyUpdate && TheNGMPGame && index == TheNGMPGame->getLocalSlotNum())
		s_localSlotChosen = TRUE; // picked here, so it is worth remembering
	GameWindow *combo = comboBoxPlayerTemplate[index];
	Int playerTemplate, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	playerTemplate = (Int)GadgetComboBoxGetItemData(combo, selIndex);

	if (!bInitialSetup)
	{
		NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		NGMPGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

		if (myGame)
		{
			GameSlot* slot = myGame->getSlot(index);
			if (playerTemplate == slot->getPlayerTemplate())
				return;

			Int oldTemplate = slot->getPlayerTemplate();
			slot->setPlayerTemplate(playerTemplate);

			int updatedStartPos = slot->getStartPos();

			if (oldTemplate == PLAYERTEMPLATE_OBSERVER)
			{
				// was observer, so populate color & team with all, and enable
				GadgetComboBoxSetSelectedPos(comboBoxColor[index], 0);
				GadgetComboBoxSetSelectedPos(comboBoxTeam[index], 0);
				slot->setStartPos(-1);
				updatedStartPos = -1;
			}
			else if (playerTemplate == PLAYERTEMPLATE_OBSERVER)
			{
				// is becoming observer, so populate color & team with random only, and disable
				GadgetComboBoxSetSelectedPos(comboBoxColor[index], 0);
				GadgetComboBoxSetSelectedPos(comboBoxTeam[index], 0);
				slot->setStartPos(-1);
				updatedStartPos = -1;
			}

			// NGMP: Update lobby (if local, for remote players we'll get it from the service)
			if (index == myGame->getLocalSlotNum())
			{
				pLobbyInterface->UpdateCurrentLobby_MySide(playerTemplate, updatedStartPos);
			}
			else if (slot->getState() == SLOT_EASY_AI || slot->getState() == SLOT_MED_AI || slot->getState() == SLOT_BRUTAL_AI)
			{
				pLobbyInterface->UpdateCurrentLobby_AISide(index, playerTemplate, updatedStartPos);
			}
		}
	}
}


static void handleStartPositionSelection(Int player, int startPos)
{
	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	NGMPGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	if (myGame)
	{
		NGMPGameSlot * slot = myGame->getGameSpySlot(player);
		if (!slot)
			return;

		if (startPos == slot->getStartPos())
			return;
		Bool skip = FALSE;
		if (startPos < 0)
		{
			skip = TRUE;
		}

		if(!skip)
		{
			Bool isAvailable = TRUE;
			for(Int i = 0; i < MAX_SLOTS; ++i)
			{
				if(i != player && myGame->getSlot(i)->getStartPos() == startPos)
				{
					isAvailable = FALSE;
					break;
				}
			}
			if( !isAvailable )
				return;
		}

		// NGMP: Dont set it locally / directly anymore, rely on the lobby service instead
		//slot->setStartPos(startPos);

		// NGMP: Update lobby (if local, for remote players we'll get it from the service)
		//		 We also allow host to set AI here
		if (player == myGame->getLocalSlotNum())
		{
			pLobbyInterface->UpdateCurrentLobby_MyStartPos(startPos);
		}
		else if (myGame->amIHost() && slot->isAI()) // AI + Host
		{
			pLobbyInterface->UpdateCurrentLobby_AIStartPos(player, startPos);
		}
	}
}



static void handleTeamSelection(int index)
{
	GameWindow *combo = comboBoxTeam[index];
	Int team, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	team = (Int)GadgetComboBoxGetItemData(combo, selIndex);

	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	NGMPGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	if (myGame)
	{
		GameSlot * slot = myGame->getSlot(index);
		if (team == slot->getTeamNumber())
			return;

		// NGMP: Dont set it locally / directly anymore, rely on the lobby service instead
		//slot->setTeamNumber(team);

		// NGMP: Update lobby (if local, for remote players we'll get it from the service)
		if (index == myGame->getLocalSlotNum())
		{
			pLobbyInterface->UpdateCurrentLobby_MyTeam(team);
		}
		else if (slot->getState() == SLOT_EASY_AI || slot->getState() == SLOT_MED_AI || slot->getState() == SLOT_BRUTAL_AI)
		{
			pLobbyInterface->UpdateCurrentLobby_AITeam(index, team);
		}
	}
}

static void handleStartingCashSelection()
{
#if defined(GENERALS_ONLINE)
// update it on the service
	Int selIndex;
	GadgetComboBoxGetSelectedPos(comboBoxStartingCash, &selIndex);

	UnsignedInt startingCashValue = (UnsignedInt)GadgetComboBoxGetItemData(comboBoxStartingCash, selIndex);

	Money startingCash;
	startingCash.deposit(startingCashValue, FALSE);

	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if (pLobbyInterface != nullptr)
	{
		pLobbyInterface->UpdateCurrentLobby_StartingCash(startingCashValue);
	}
#else
  GameInfo *myGame = TheGameSpyInfo->getCurrentStagingRoom();

  if (myGame)
  {
    Int selIndex;
    GadgetComboBoxGetSelectedPos(comboBoxStartingCash, &selIndex);

    Money startingCash;
    startingCash.deposit( (UnsignedInt)GadgetComboBoxGetItemData( comboBoxStartingCash, selIndex ), FALSE, FALSE );
    myGame->setStartingCash( startingCash );
    myGame->resetAccepted();

    if (myGame->amIHost())
    {
      // send around the new data
      TheGameSpyInfo->setGameOptions();
      WOLDisplaySlotList();// Update the accepted button UI
    }
  }
#endif
}

static void handleEnforceRandomClick()
{
	if (checkBoxEnforceRandom == NULL)
		return;

	// The service owns the flag: it is what refuses a concrete pick, and what
	// every client reads the state back from on the next lobby push. Nothing is
	// written into the local game here, so a rejected update simply leaves the
	// box where the lobby says it should be.
	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if (pLobbyInterface != nullptr && pLobbyInterface->IsHost())
	{
		pLobbyInterface->UpdateCurrentLobby_EnforceRandom(GadgetCheckBoxIsChecked(checkBoxEnforceRandom) ? true : false);
	}
}

static void handleLimitSuperweaponsClick()
{

#if defined(GENERALS_ONLINE)
	// update it on the service
	bool bLimitSuperweapons = GadgetCheckBoxIsChecked(checkBoxLimitSuperweapons);

	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if (pLobbyInterface != nullptr)
	{
		pLobbyInterface->UpdateCurrentLobby_LimitSuperweapons(bLimitSuperweapons);
	}
#else
  GameInfo *myGame = TheGameSpyInfo->getCurrentStagingRoom();

  if (myGame)
  {
    // At the moment, 1 and 0 are the only choices supported in the GUI, though the system could
    // support more.
    if ( GadgetCheckBoxIsChecked( checkBoxLimitSuperweapons ) )
    {
      myGame->setSuperweaponRestriction( 1 );
    }
    else
    {
      myGame->setSuperweaponRestriction( 0 );
    }
    myGame->resetAccepted();

    if (myGame->amIHost())
    {
      // send around a new slotlist
      TheGameSpyInfo->setGameOptions();
      WOLDisplaySlotList();// Update the accepted button UI
    }
  }
#endif
}

static void WOLLockSettings()
{
	buttonBack->winEnable(false);
	checkBoxLimitSuperweapons->winEnable(false);
	if (checkBoxEnforceRandom) checkBoxEnforceRandom->winEnable(false);
	comboBoxStartingCash->winEnable(false);

	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		comboBoxPlayer[i]->winEnable(false);
		comboBoxColor[i]->winEnable(false);
		comboBoxPlayerTemplate[i]->winEnable(false);
		comboBoxTeam[i]->winEnable(false);
		buttonMapStartPosition[i]->winEnable(false);
	}
}

static Bool wolArmedResumeStillMatches(NGMPGame *game);
static void wolCheckResumeArmChange();
static void StartPressed()
{
	// TheSuperHackers @feature bill-rich 15/09/2026 An armed resume-from-replay only works if the lobby still
	// matches the recording and every human holds the resume source.
	{
		NGMP_OnlineServices_LobbyInterface* pResumeLobby = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		NGMPGame *resumeGame = pResumeLobby ? pResumeLobby->GetCurrentGame() : nullptr;
		if (resumeGame != nullptr && resumeGame->isResumeArmed() && !wolArmedResumeStillMatches(resumeGame))
			return;
	}
	Bool isReady = TRUE;
	Bool allHaveMap = TRUE;
	Int playerCount = 0;
	Int humanCount = 0;

	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	NGMP_OnlineServices_AuthInterface* pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
	NGMPGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	if (pLobbyInterface == nullptr || !myGame || pAuthInterface == nullptr)
		return;

	NetworkMesh* pMesh = NGMP_OnlineServicesManager::GetNetworkMesh();
	if (pMesh == nullptr)
	{
		return;
	}

	int numHumanPlayers = 0;
	for(LobbyMemberEntry & member : pLobbyInterface->GetCurrentLobby().members)
	{
		if (member.IsHuman())
		{
			++numHumanPlayers;
		}
	}

	if (pMesh->GetAllConnections().size() < numHumanPlayers - 1)
	{
		UnicodeString text(L"Connections: Some players are still connecting. Try again shortly:");
		GadgetListBoxAddEntryText(listboxGameSetupChat, text, GameMakeColor(255, 194, 15, 255), -1, -1);

		
		int64_t myUserID = pAuthInterface->GetUserID();
		auto vecLobbyMembers = pLobbyInterface->GetCurrentLobby().members;
		auto allConnections = pMesh->GetAllConnections();

		for (LobbyMemberEntry& lobbyMember : vecLobbyMembers)
		{
			if (lobbyMember.IsHuman() && lobbyMember.user_id != myUserID) // dont show AI or self
			{
				bool bFoundLobbyMemberForConnection = false;

				if (allConnections.find(lobbyMember.user_id) != allConnections.end())
				{
					// we have a connection for this lobby member
					bFoundLobbyMemberForConnection = true;
				}
				
				if (!bFoundLobbyMemberForConnection)
				{
					UnicodeString strDisplayName(from_utf8(lobbyMember.display_name).c_str());
					GadgetListBoxAddEntryText(listboxGameSetupChat, strDisplayName, GameMakeColor(255, 194, 15, 255), -1, -1);
				}
			}
		}
		
		return;
	}

	// see if everyone's accepted and count the number of players in the game
	UnicodeString mapDisplayName;
	const MapMetaData *mapData = TheMapCache->findMap( myGame->getMap() );
	Bool willTransfer = TRUE;
	if (mapData)
	{
		mapDisplayName.format(L"%ls", mapData->m_displayName.str());
		willTransfer = !mapData->m_isOfficial;
	}
	else
	{
		mapDisplayName.translate(myGame->getMap().str());
		willTransfer = WouldMapTransfer(myGame->getMap());
	}
	for( int i = 0; i < MAX_SLOTS; i++ )
	{
		bool bIsAccepted = myGame->getSlot(i)->isAccepted();
		bool bIsHuman = myGame->getSlot(i)->isHuman();
		if ((bIsAccepted == FALSE) && (bIsHuman == TRUE))
		{
			isReady = FALSE;
			if (!myGame->getSlot(i)->hasMap() && !willTransfer)
			{
				UnicodeString msg;
				msg.format(TheGameText->fetch("GUI:PlayerNoMap"), myGame->getSlot(i)->getName().str(), mapDisplayName.str());
				GadgetListBoxAddEntryText(listboxGameSetupChat, msg, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
				allHaveMap = FALSE;
			}
		}
		if(myGame->getSlot(i)->isOccupied() && myGame->getSlot(i)->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
		{
			if (myGame->getSlot(i)->isHuman())
				humanCount++;
			playerCount++;
		}
	}

	// Check for too many players
	const MapMetaData *md = TheMapCache->findMap( myGame->getMap() );
	if (!md || md->m_numPlayers < playerCount)
	{
		if (myGame->amIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:TooManyPlayers"), (md)?md->m_numPlayers:0);
			GadgetListBoxAddEntryText(listboxGameSetupChat, text, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
		}
		return;
	}

	// Check for observer + AI players
	if (TheGlobalData->m_netMinPlayers && !humanCount)
	{
		if (myGame->amIHost())
		{
			UnicodeString text = TheGameText->fetch("GUI:NeedHumanPlayers");
			GadgetListBoxAddEntryText(listboxGameSetupChat, text, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
		}
		return;
	}

	// Check for too few players
	if (playerCount < TheGlobalData->m_netMinPlayers)
	{
		if (myGame->amIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:NeedMorePlayers"),playerCount);
			GadgetListBoxAddEntryText(listboxGameSetupChat, text, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
		}
		return;
	}

	// Check for too few teams
	int numRandom = 0;
	std::set<Int> teams;
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = myGame->getSlot(i);
		if (slot && slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
		{
			if (slot->getTeamNumber() >= 0)
			{
				teams.insert(slot->getTeamNumber());
			}
			else
			{
				++numRandom;
			}
		}
	}
	if (numRandom + teams.size() < TheGlobalData->m_netMinPlayers)
	{
		if (myGame->amIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:NeedMoreTeams"));
			GadgetListBoxAddEntryText(listboxGameSetupChat, text, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
		}
		return;
	}

	if (numRandom + teams.size() < 2)
	{
		UnicodeString text;
		text.format(TheGameText->fetch("GUI:SandboxMode"));
		GadgetListBoxAddEntryText(listboxGameSetupChat, text, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
	}

	if(isReady)
	{
		// start full mesh connection check
		ReleaseLog("GO lobby: host pressed start, full mesh check requested");
		UnicodeString strInform = UnicodeString(L"Connections: Checking all players...");
		GadgetListBoxAddEntryText(listboxGameSetupChat, strInform, GameMakeColor(255, 194, 15, 255), -1, -1);

		std::shared_ptr<WebSocket>  pWS = NGMP_OnlineServicesManager::GetWebSocket();
		if (pWS != nullptr)
		{
			if (buttonStart != nullptr)
			{
				buttonStart->winEnable(FALSE);
			}

			pWS->SendData_StartFullMeshConnectivityCheck([=](bool bMeshFullyConnected, std::list<std::pair<int64_t, int64_t>> missingConnections, std::string strFailureReason)
				{
					if (bMeshFullyConnected)
					{
						UnicodeString strInform = UnicodeString(L"Connections: All players are connected.");
						GadgetListBoxAddEntryText(listboxGameSetupChat, strInform, GameMakeColor(0, 255, 0, 255), -1, -1);

						// reset autostart just incase
#if !defined(GENERALS_ONLINE_DISABLE_AUTO_ACCEPT)
						pLobbyInterface->ClearAutoReadyCountdown();
#endif

						//PeerRequest req;
						//req.peerRequestType = PeerRequest::PEERREQUEST_STARTGAME;
						//TheGameSpyPeerMessageQueue->addRequest(req);

						if (TheNGMPGame != nullptr)
						{
							if (!TheNGMPGame->IsCountdownStarted())
							{
								// remote msg
								UnicodeString strInform;
								strInform.format(TheGameText->fetch("LAN:GameStartTimerPlural"), TheNGMPGame->GetTotalCountdownDuration());
								pLobbyInterface->SendAnnouncementMessageToCurrentLobby(strInform, true);

								TheNGMPGame->StartCountdown();
								buttonSelectMap->winEnable(FALSE);		
							}
						}

						GameSpyCloseOverlay(GSOVERLAY_BUDDY);
					}
					else
					{
						UnicodeString strInform = UnicodeString(L"Connections: The player network is not ready. Try again shortly.");
						GadgetListBoxAddEntryText(listboxGameSetupChat, strInform, GameMakeColor(255, 194, 15, 255), -1, -1);

						if (!strFailureReason.empty())
						{
							UnicodeString strReasonLine;
							strReasonLine.format(L"Connections: Reason: %s", from_utf8(strFailureReason).c_str());
							GadgetListBoxAddEntryText(listboxGameSetupChat, strReasonLine, GameMakeColor(255, 194, 15, 255), -1, -1);
						}

						NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

						// who is missing who?
						//std::list<std::pair<int64_t, int64_t>> missingConnections
						GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"Connections: Missing links:"), GameMakeColor(255, 194, 15, 255), -1, -1);
						for (auto& missingPair : missingConnections)
						{
							bool bFoundPlayer = false;
							if (pLobbyInterface != nullptr)
							{
								LobbyMemberEntry lobbyMemberSource = pLobbyInterface->GetRoomMemberFromID(missingPair.first);
								LobbyMemberEntry lobbyMemberTarget = pLobbyInterface->GetRoomMemberFromID(missingPair.second);
								if (lobbyMemberSource.user_id != -1 && lobbyMemberTarget.user_id != -1)
								{
									bFoundPlayer = true;

									UnicodeString strMissingConnection;
									strMissingConnection.format(L"%s is not connected to %s.", from_utf8(lobbyMemberSource.display_name).c_str(), from_utf8(lobbyMemberTarget.display_name).c_str());
									GadgetListBoxAddEntryText(listboxGameSetupChat, strMissingConnection, GameMakeColor(255, 194, 15, 255), -1, -1);
								}
							}

							if (!bFoundPlayer) // if we couldnt find a display name... show a user ID instead, better than nothing
							{
								UnicodeString strMissingConnection;
								strMissingConnection.format(L"Player %lld is not connected to player %lld.", missingPair.first, missingPair.second);
								GadgetListBoxAddEntryText(listboxGameSetupChat, strMissingConnection, GameMakeColor(255, 194, 15, 255), -1, -1);
							}
						}

						// restore state
						if (buttonBack != nullptr)
						{
							buttonBack->winEnable(TRUE);
						}

						if (buttonStart != nullptr)
						{
							buttonStart->winEnable(TRUE);
						}
					}
					
				});
		}
	}
	else if (allHaveMap)
	{
		// send HWS chat message

#if defined(GENERALS_ONLINE_DISABLE_AUTO_ACCEPT)
		// local msg
		GadgetListBoxAddEntryText(listboxGameSetupChat, TheGameText->fetch("GUI:NotifiedStartIntent"), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);

		// remote msg
		UnicodeString strInform = TheGameText->fetch("GUI:HostWantsToStart");
		pLobbyInterface->SendAnnouncementMessageToCurrentLobby(strInform, false);
#else
		if (!pLobbyInterface->HasAutoReadyCountdown())
		{
			// local msg
			GadgetListBoxAddEntryText(listboxGameSetupChat, TheGameText->fetch("GUI:NotifiedStartIntent"), GameSpyColor[GSCOLOR_DEFAULT], -1, -1);

			// remote msg
			UnicodeString strInform = TheGameText->fetch("GUI:HostWantsToStart");
			UnicodeString strInform2 = UnicodeString(L"Ready check: All players will be marked ready in 30 seconds.");
			pLobbyInterface->SendAnnouncementMessageToCurrentLobby(strInform, false);
			pLobbyInterface->SendAnnouncementMessageToCurrentLobby(strInform2, true);

			// TODO_NGMP: Add the reverse too, if everyone is ready but the host wont start... just start it in X seconds

			// start a countdown to auto start
			// TODO_NGMP: Don't have this client driven...
			pLobbyInterface->StartAutoReadyCountdown();
		}
		else
		{
			UnicodeString strInform = UnicodeString(L"Ready check: A countdown is already running. Players will be marked ready when it ends.");
			GadgetListBoxAddEntryText(listboxGameSetupChat, strInform, GameMakeColor(192, 192, 192, 255), -1, -1);
		}
#endif
	}
}


//-------------------------------------------------------------------------------------------------
/** Update options on screen */
//-------------------------------------------------------------------------------------------------
void WOLDisplayGameOptions()
{
	if (!parentWOLGameSetup)
		return;

	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	NGMPGame* theGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	if (pLobbyInterface == nullptr || theGame == nullptr)
	{
		return;
	}

	const GameSlot *localSlot = NULL;
	if (theGame->getLocalSlotNum() >= 0)
		localSlot = theGame->getConstSlot(theGame->getLocalSlotNum());

	AsciiString map = theGame->getMap();
	const MapMetaData *md = TheMapCache->findMap(map);
	if (md && localSlot && localSlot->hasMap())
	{
		GadgetStaticTextSetText(textEntryMapDisplay, md->m_displayName);
	}
	else
	{
#if defined(GENERALS_ONLINE)
		AsciiString s = theGame->getMap();
#else
		AsciiString s = TheGameSpyInfo->getCurrentStagingRoom()->getMap();
#endif
		if (s.reverseFind('\\'))
		{
			s = s.reverseFind('\\') + 1;
		}
		UnicodeString mapDisplay;
		mapDisplay.translate(s);
		GadgetStaticTextSetText(textEntryMapDisplay, mapDisplay);
	}
	WOLPositionStartSpots();
	updateMapStartSpots(theGame, buttonMapStartPosition);

#if defined(GENERALS_ONLINE)
	Bool isUsingStats = TheNGMPGame->getUseStats();
#else
  //If our display does not match the current state of game settings, update the checkbox.
  Bool isUsingStats = TheGameSpyInfo->getCurrentStagingRoom()->getUseStats() ? TRUE : FALSE;
#endif
  // TheSuperHackers @feature bill-rich 02/10/2026 Record Stats is gone from the
  // shipped layout (every match reports to radarvan, so it switched nothing a
  // player can see), which frees its slot in the options row for Radarvan Pick.
  // Every use stays guarded so an older .wnd that still has the box keeps working.
  if (checkBoxUseStats && GadgetCheckBoxIsChecked(checkBoxUseStats) != isUsingStats)
  {
  	GadgetCheckBoxSetChecked(checkBoxUseStats, isUsingStats);
    checkBoxUseStats->winSetTooltip( TheGameText->fetch( isUsingStats ? "TOOLTIP:UseStatsOn" : "TOOLTIP:UseStatsOff" ) );
  }

  Bool oldFactionsOnly = theGame->oldFactionsOnly();
  if (GadgetCheckBoxIsChecked(checkBoxLimitArmies) != oldFactionsOnly)
  {
    GadgetCheckBoxSetChecked(checkBoxLimitArmies, oldFactionsOnly);
    // Repopulate the lists of available armies, since the old list is now wrong
    for (Int i = 0; i < MAX_SLOTS; i++)
    {
      PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, theGame, theGame->getAllowObservers() );

      // Make sure selections are up to date on all machines
      handlePlayerTemplateSelection(i) ;
    }
  }

  // Note: must check if checkbox is already correct to avoid infinite recursion
  Bool limitSuperweapons = (theGame->getSuperweaponRestriction() != 0);
  if ( limitSuperweapons != GadgetCheckBoxIsChecked(checkBoxLimitSuperweapons))
    GadgetCheckBoxSetChecked( checkBoxLimitSuperweapons, limitSuperweapons );

  // Same recursion guard: setting a checkbox posts back into this screen.
  if (checkBoxEnforceRandom)
  {
    Bool enforceRandom = theGame->getEnforceRandom();
    if ( enforceRandom != GadgetCheckBoxIsChecked(checkBoxEnforceRandom))
      GadgetCheckBoxSetChecked( checkBoxEnforceRandom, enforceRandom );
  }

  Int itemCount = GadgetComboBoxGetLength(comboBoxStartingCash);
  Int index = 0;
  for ( ; index < itemCount; index++ )
  {
    Int value  = (Int)GadgetComboBoxGetItemData(comboBoxStartingCash, index);
    if ( value == theGame->getStartingCash().countMoney() )
    {
      // Note: must check if combobox is already correct to avoid infinite recursion
      Int selectedIndex;
      GadgetComboBoxGetSelectedPos( comboBoxStartingCash, &selectedIndex );
      if ( index != selectedIndex )
        GadgetComboBoxSetSelectedPos(comboBoxStartingCash, index, TRUE);

      break;
    }
  }

  DEBUG_ASSERTCRASH( index < itemCount, ("Could not find new starting cash amount %d in list", theGame->getStartingCash().countMoney() ) );
}


//  -----------------------------------------------------------------------------------------
// The Bad munkee slot list displaying function
//-------------------------------------------------------------------------------------------------
static Int WOLUpdateConnectionLevel(Int level, Int score)
{
    if (level < 0)
    {
        level = 0;
        while (level < CONNECTION_LEVEL_COUNT - 1 && score >= connectionLevelThresholds[level])
            ++level;
        return level;
    }

    while (level < CONNECTION_LEVEL_COUNT - 1 && score >= connectionLevelThresholds[level] + CONNECTION_LEVEL_HYSTERESIS)
        ++level;
    while (level > 0 && score < connectionLevelThresholds[level - 1] - CONNECTION_LEVEL_HYSTERESIS)
        --level;
    return level;
}

static void WOLRefreshConnectionIndicators(void)
{
    NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
    NGMPGame* game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
    if (pLobbyInterface == nullptr || game == nullptr || !game->isInGame())
        return;

    NetworkMesh* pMesh = NGMP_OnlineServicesManager::GetNetworkMesh();

    for (Int i = 0; i < MAX_SLOTS; ++i)
    {
        NGMPGameSlot* slot = game->getGameSpySlot(i);
        if (slot == nullptr || !slot->isHuman() || i == game->getLocalSlotNum())
        {
            if (genericPingWindow[i])
                genericPingWindow[i]->winHide(TRUE);
            connectionIndicatorState[i] = CONNECTION_INDICATOR_CONNECTING;
            continue;
        }

        if (genericPingWindow[i] == nullptr)
            continue;

        genericPingWindow[i]->winHide(FALSE);

        EConnectionState connectionState = EConnectionState::NOT_CONNECTED;
        int connectionScore = -1;

        if (pMesh != nullptr)
        {
            PlayerConnection* pConnection = pMesh->GetConnectionForUser(slot->m_userID);
            if (pConnection != nullptr)
            {
                connectionState = pConnection->GetState();
                connectionScore = pConnection->ComputeConnectionScore();
            }
        }

        Int& state = connectionIndicatorState[i];
        if (connectionState == EConnectionState::CONNECTION_FAILED || connectionState == EConnectionState::CONNECTION_DISCONNECTED)
            state = CONNECTION_INDICATOR_WARNING;
        else if (connectionState != EConnectionState::CONNECTED_DIRECT || connectionScore < 0)
            state = CONNECTION_INDICATOR_CONNECTING;
        else
            state = WOLUpdateConnectionLevel(state, connectionScore);
    }
}

//-------------------------------------------------------------------------------------------------
/** Draws the connection indicator: a spinner while connecting, a warning triangle on failure, else signal bars. */
//-------------------------------------------------------------------------------------------------
static void WOLConnectionIndicatorDraw(GameWindow *window, WinInstanceData *instData)
{
    Int slot = -1;
    for (Int i = 0; i < MAX_SLOTS; ++i)
    {
        if (genericPingWindow[i] == window)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return;

    Int x, y, width, height;
    window->winGetScreenPosition(&x, &y);
    window->winGetSize(&width, &height);

    const Int size = (Int)(min(width, height) * 0.7f);
    const Int left = x + (width - size) / 2;
    const Int top = y + (height - size) / 2;
    const Real centerX = left + size * 0.5f;
    const Real centerY = top + size * 0.5f;
    const Int state = connectionIndicatorState[slot];

    if (state == CONNECTION_INDICATOR_CONNECTING)
    {
        // Faint full ring with a solid arc rotating around it, drawn as short segments.
        const Int segmentCount = 24;
        const Int arcSegments = 7;
        const Real radius = size * 0.4f;
        const Real rotation = (timeGetTime() % 900) * 2.0f * PI / 900.0f;
        const Color ringColor = GameMakeColor(47, 55, 168, 70); // Zero Hour UI blue
        const Color arcColor = GameMakeColor(47, 55, 168, 255);
        for (Int segment = 0; segment < segmentCount; ++segment)
        {
            const Real startAngle = rotation + segment * 2.0f * PI / segmentCount;
            const Real endAngle = startAngle + 2.0f * PI / segmentCount;
            TheDisplay->drawLine((Int)(centerX + std::cos(startAngle) * radius), (Int)(centerY + std::sin(startAngle) * radius),
                (Int)(centerX + std::cos(endAngle) * radius), (Int)(centerY + std::sin(endAngle) * radius),
                3.0f, segment < arcSegments ? arcColor : ringColor);
        }
    }
    else if (state == CONNECTION_INDICATOR_WARNING)
    {
        const Color amber = GameMakeColor(255, 190, 0, 255);
        const Int apexY = top + (Int)(size * 0.1f);
        const Int baseY = top + (Int)(size * 0.9f);
        const Int baseLeft = left + (Int)(size * 0.05f);
        const Int baseRight = left + (Int)(size * 0.95f);
        TheDisplay->drawLine((Int)centerX, apexY, baseLeft, baseY, 1.5f, amber);
        TheDisplay->drawLine((Int)centerX, apexY, baseRight, baseY, 1.5f, amber);
        TheDisplay->drawLine(baseLeft, baseY, baseRight, baseY, 1.5f, amber);
        TheDisplay->drawLine((Int)centerX, top + (Int)(size * 0.38f), (Int)centerX, top + (Int)(size * 0.66f), 2.0f, amber);
        TheDisplay->drawFillRect((Int)centerX - 1, top + (Int)(size * 0.73f), 2, 2, amber);
    }
    else
    {
        Color levelColor = GameMakeColor(220, 40, 40, 255);
        if (state >= 3)
            levelColor = GameMakeColor(0, 200, 0, 255);
        else if (state == 2)
            levelColor = GameMakeColor(255, 190, 0, 255);
        const Color emptyColor = GameMakeColor(90, 90, 90, 200);

        const Int gap = 1;
        const Int barWidth = max(1, (size - gap * (CONNECTION_LEVEL_COUNT - 1)) / CONNECTION_LEVEL_COUNT);
        for (Int bar = 0; bar < CONNECTION_LEVEL_COUNT; ++bar)
        {
            const Int barHeight = max(1, size * (bar + 1) / CONNECTION_LEVEL_COUNT);
            TheDisplay->drawFillRect(left + bar * (barWidth + gap), top + size - barHeight, barWidth, barHeight,
                bar <= state ? levelColor : emptyColor);
        }
    }
}

void WOLDisplaySlotList(void)
{
    // TODO_NGMP
    //if (!parentWOLGameSetup || !TheGameSpyInfo->getCurrentStagingRoom())
    //	return;

    NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
    NGMPGame* game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
    if (pLobbyInterface == nullptr || game == nullptr || !game->isInGame())
        return;

    DEBUG_ASSERTCRASH(!game->getConstSlot(0)->isOpen(), ("Open host!"));

    s_applyingLobbyUpdate = TRUE;
    UpdateSlotList(game, comboBoxPlayer, comboBoxColor,
        comboBoxPlayerTemplate, comboBoxTeam, buttonAccept, buttonStart, buttonMapStartPosition);
    s_applyingLobbyUpdate = FALSE;

    WOLDisplayGameOptions();

    for (Int i = 0; i < MAX_SLOTS; ++i)
    {
        NGMPGameSlot* slot = game->getGameSpySlot(i);
        if (slot && slot->isHuman())
        {
            // Determine friends and blocked players in lobby setup and highlight them
            Color nameColor = GameSpyColor[GSCOLOR_PLAYER_NORMAL];
            NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();

            if (pSocialInterface != nullptr && pSocialInterface->IsUserFriend(slot->m_userID))
            {
                nameColor = GameSpyColor[GSCOLOR_PLAYER_BUDDY];
            }

            else if (pSocialInterface != nullptr && pSocialInterface->IsUserIgnored(slot->m_userID))
            {
                nameColor = GameSpyColor[GSCOLOR_PLAYER_IGNORED];
            }

            if (comboBoxPlayer[i])
            {
                GadgetTextEntrySetTextColor(GadgetComboBoxGetEditBox(comboBoxPlayer[i]), nameColor);
            }
        }
    }

    WOLRefreshConnectionIndicators();
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Gadgets Options Menu */
//-------------------------------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// TheSuperHackers @feature bill-rich 15/09/2026 Resume-from-replay arming for online lobbies (host only).
//
// Same contract as the LAN lobby (LanGameOptionsMenu.cpp): /resume copies the host's last
// recording to Resume.rep and aligns the lobby with it. Online the file is uploaded to the
// service first and the arm (handoff frame plus the replay's seed) is sent only once the
// upload finished; guests download the file when the arm reaches them, report in chat
// whether it matches and flag themselves as holding it, and the host cannot start until
// every human holds it. Slot order is owned by the service, so the lobby must already
// match the replay's slot order; the host is told what differs instead of the lobby
// being reordered.
// -----------------------------------------------------------------------------

static UnsignedInt s_wolLastSeenResumeHandoff = 0;      ///< the arm the last lobby update carried, to notice changes
static Bool s_wolResumeArmInProgress = FALSE;           ///< an upload plus arm is under way (host)
static uint32_t s_wolResumeDownloadedGeneration = 0;    ///< the service's upload generation this client last downloaded (guest)
static Bool s_wolResumeDownloadInProgress = FALSE;      ///< a download is under way (guest)

static void wolLocalSystemChat(const UnicodeString &text)
{
	ReleaseLog("GO lobby (local): %ls", text.str());
	if (listboxGameSetupChat)
		GadgetListBoxAddEntryText(listboxGameSetupChat, text, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
}

// Roster problems go to the whole lobby: the player who has to fix something is
// usually not the host.
static void wolLobbySystemChat(const UnicodeString &text)
{
	ReleaseLog("GO lobby: %ls", text.str());
	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	UnicodeString msg = text;
	if (pLobbyInterface != nullptr)
		pLobbyInterface->SendAnnouncementMessageToCurrentLobby(msg, true);
	else
		wolLocalSystemChat(text);
}

// -----------------------------------------------------------------------------
// Peer-side map fetch. The LAN lobby has done this since August (LANAPI, driven
// by the host's game-options packets); online it never existed, so a member who
// did not have the lobby's map just sat there as "does not have map" and the host
// could not start unless the map was a user map the launch-time P2P transfer
// could carry. The cncstats map store is keyed by CRC, which is why this had to
// wait for the lobby to carry one (LobbyEntry::map_crc).
//
// One download at a time (the hook enforces that too), and a CRC that failed is
// not retried: the lobby pushes its state repeatedly and we would otherwise
// re-request the same map forever.
// -----------------------------------------------------------------------------
static Bool s_mapDownloadPending = FALSE;
static UnsignedInt s_mapDownloadCRC = 0;
static UnsignedInt s_mapDownloadFailedCRC = 0;

// Ask the store for every sidecar kind. Unlike LAN there is no contents mask in
// the lobby to say which ones the host actually has, and both the download and
// the install treat sidecars as best-effort, so asking for all of them costs
// nothing beyond a 404 for the kinds that were never stored.
static const UnsignedInt MAP_SIDECARS_ALL = 0x7E;

// Do we have the lobby's map, as opposed to a map with the same name? Same test
// as UpdateCurrentLobby_HasMap.
static Bool wolLocalHasLobbyMap(NGMPGame *game)
{
	if (game == NULL || TheMapCache == nullptr)
		return FALSE;
	AsciiString key = game->getMap();
	key.toLower();
	const MapMetaData *md = TheMapCache->findMap(key);
	if (md == nullptr)
		return FALSE;
	const UnsignedInt want = game->getMapCRC();
	return (want == 0) || (want == md->m_CRC);
}

static void wolUpdateMapDownload()
{
	NGMPGame *game = TheNGMPGame;
	if (game == NULL)
		return;

	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if (pLobbyInterface == nullptr)
		return;

	const UnsignedInt crc = game->getMapCRC();

	if (!s_mapDownloadPending
		&& TheMapDownloadStartHook != nullptr
		&& crc != 0
		&& crc != s_mapDownloadFailedCRC
		&& !wolLocalHasLobbyMap(game))
	{
		if (TheMapDownloadStartHook(game->getMap(), crc, MAP_SIDECARS_ALL))
		{
			s_mapDownloadPending = TRUE;
			s_mapDownloadCRC = crc;
			wolLocalSystemChat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:MapDownloadStarted",
				L"Downloading map from server..."));
		}
		else
		{
			// Nothing started (no download URL configured, or one already in
			// flight). Do not ask again for this CRC.
			s_mapDownloadFailedCRC = crc;
		}
	}

	if (s_mapDownloadPending && TheMapDownloadPollHook != nullptr)
	{
		const MapDownloadStatus status = TheMapDownloadPollHook();
		if (status == MAPDOWNLOAD_PENDING)
			return;

		s_mapDownloadPending = FALSE;

		// The lobby's map can change while a download runs (the host picks
		// something else). A result nobody is waiting for any more must not touch
		// the game's CRC.
		if (s_mapDownloadCRC != crc)
			return;

		// Re-applying the CRC re-runs the availability check against the file that
		// just landed, which is what flips this slot to "has map".
		game->setMapCRC(s_mapDownloadCRC);

		if (status == MAPDOWNLOAD_INSTALLED && wolLocalHasLobbyMap(game))
		{
			pLobbyInterface->UpdateCurrentLobby_HasMap();
			WOLDisplaySlotList();
			WOLDisplayGameOptions();
			wolLocalSystemChat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:MapDownloadDone",
				L"Map downloaded."));
		}
		else
		{
			// Either the fetch failed, or the bytes are on disk and the cache still
			// cannot use them. Retiring the CRC stops the retry loop; for a user map
			// the host's transfer at launch is still a fallback.
			s_mapDownloadFailedCRC = s_mapDownloadCRC;
			wolLocalSystemChat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:MapDownloadFailed",
				L"Map download failed; ask the host for the map."));
		}
	}
}

// -----------------------------------------------------------------------------
// Radarvan "Map Vote" button, host only. Same server call and the same matching
// rules as the LAN lobby's (see LanGameOptionsMenu + GUIUtil
// FindMapByDisplayName); only the apply differs, because here the lobby service
// owns the map and has to be told about it. The body sends humans-only names;
// the URL path segment counts playing humans and AI, excluding observers,
// so the server picks from a pool sized to the actual match.
// -----------------------------------------------------------------------------
static void handleOnlineMapVoteClick()
{
	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if (pLobbyInterface == nullptr || !pLobbyInterface->IsHost())
		return;

	NGMPGame *game = pLobbyInterface->GetCurrentGame();
	if (game == nullptr)
		return;

	std::vector<AsciiString> humanNames;
	unsigned int playerCount = 0;
	Int i;
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		const GameSlot *slot = game->getConstSlot(i);
		if (slot == NULL || !slot->isOccupied())
			continue;
		if (slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			continue;
		++playerCount;
		if (slot->isHuman())
		{
			AsciiString name;
			name.translate(slot->getName());
			if (!name.isEmpty())
				humanNames.push_back(name);
		}
	}

	if (humanNames.empty())
	{
		UnicodeString errU;
		errU.format(TheGameText->fetch("GUI:MapVoteFailed"), "no human players");
		wolLocalSystemChat(errU);
		return;
	}

	ChooseMapResult res = ChooseMapFromServer(
		TheGlobalData->m_mapVoteUrl, humanNames, playerCount);
	if (!res.success)
	{
		UnicodeString errU;
		errU.format(TheGameText->fetch("GUI:MapVoteFailed"), res.errorMessage.str());
		wolLocalSystemChat(errU);
		return;
	}

	AsciiString mapKey;
	const MapMetaData *md = FindMapByDisplayName(res.chosenMap, &mapKey);

	// Don't have it? Fetch it by the CRC radarvan returned, same as the LAN
	// handler (see InstallMapByCRC).
	if (md == nullptr && res.mapCRC != 0)
	{
		UnicodeString dlU;
		dlU.format(TheGameText->fetch("GUI:MapVoteDownloading"), res.chosenMap.str());
		wolLocalSystemChat(dlU);

		md = InstallMapByCRC(res.chosenMap, res.mapFileName, res.mapCRC,
			res.contentsMask, &mapKey);
	}

	if (md == nullptr || mapKey.isEmpty())
	{
		UnicodeString errU;
		errU.format(TheGameText->fetch("GUI:MapVoteNotFound"), res.chosenMap.str());
		wolLocalSystemChat(errU);
		return;
	}

	// Apply exactly as the Select Map confirm does (WOLMapSelectMenu), including
	// the lobby push: without UpdateCurrentLobby_Map the service keeps the old
	// map and the next push undoes the pick.
	UnicodeString mapDisplay = md->m_displayName;

	game->setMap(mapKey);
	if (game->getGameSpySlot(0))
		game->getGameSpySlot(0)->setMapAvailability(TRUE);
	game->setMapCRC(md->m_CRC);
	game->setMapSize(md->m_filesize);
	game->adjustSlotsForMap();
	game->resetAccepted();
	game->resetStartSpots();

	// TheSuperHackers @feature bill-rich 02/10/2026 Push the .map + sidecars to the
	// cncstats store so a lobby member who does not have this map can pull it over
	// HTTP. No-ops when the URLs are unset or the store already holds this CRC; the
	// sidecar helpers skip whatever is not on disk here. Runs after setMap, which is
	// what computes the contents mask.
	if (TheGlobalData != nullptr)
	{
		UploadAllMapAssetsIfMissing(TheGlobalData->m_mapCheckUrl,
			TheGlobalData->m_mapAssetsUrl,
			TheGlobalData->m_mapUploadUrl,
			md->m_CRC,
			md->m_fileName,
			game->getMapContentsMask(),
			0 /* no per-game seed in the lobby */);
	}

	WOLDisplaySlotList();
	WOLDisplayGameOptions();

	pLobbyInterface->UpdateCurrentLobby_Map(mapDisplay, game->getMap(), md->m_isOfficial, md->m_numPlayers, md->m_CRC);

	UnicodeString okU;
	okU.format(TheGameText->fetch("GUI:MapVoteApplied"), res.chosenMap.str());
	wolLobbySystemChat(okU);
}

// Called on the host's start while a resume is armed: refuse when the lobby has drifted
// from the recording since arming, or when a human does not hold the resume source yet.
static Bool wolArmedResumeStillMatches(NGMPGame *game)
{
	ResumeFromReplay::SourceReplay replay;
	UnicodeString why;
	if (!ResumeFromReplay::readSource(replay, why)
		|| !ResumeFromReplay::sourceMatchesGame(replay, game, game->getResumeHandoffFrame(), why))
	{
		wolLobbySystemChat(TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:ResumeStartRefused",
			L"Resume is armed but the lobby no longer matches the recording (%ls). Host: press Resume again to realign it, or Cancel Resume for a fresh game.", why.str()));
		return FALSE;
	}

	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if (pLobbyInterface != nullptr)
	{
		for (const LobbyMemberEntry &member : pLobbyInterface->GetCurrentLobby().members)
		{
			if (member.IsHuman() && !member.has_resume_replay)
			{
				UnicodeString name(from_utf8(member.display_name).c_str());
				wolLobbySystemChat(TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:ResumeWaitingForDownload",
					L"Resume: waiting for %ls to download the replay. If it never arrives, they should leave and rejoin, then the host presses Resume again", name.str()));
				return FALSE;
			}
		}
	}
	return TRUE;
}

// -----------------------------------------------------------------------------
// TheSuperHackers @feature bill-rich 11/10/2026 The arm is a chain of service
// requests, each started from the previous one's completion: the map, the
// starting cash and the superweapon limit (only those that differ from the
// recording), then the whole slot table in one piece, then the upload and the
// arm itself. The service disarms on every one of those changes, which is why
// the arm comes last and nothing is sent in parallel. The local lobby is not
// touched here: every step shows up through the lobby push, exactly as it does
// when the host changes a setting by hand.
// -----------------------------------------------------------------------------
struct WolResumeArmJob
{
	UnsignedInt handoff = 0;
	Int seed = 0;
	AsciiString mapKey;
	AsciiString mapDisplay;
	UnicodeString mapDisplayU;
	Bool mapOfficial = FALSE;
	Int mapNumPlayers = 0;
	UnsignedInt mapCRC = 0;
	UnsignedInt startingCash = 0;
	Bool limitSuperweapons = FALSE;
	std::vector<LobbySlotSpec> slots;
};
static WolResumeArmJob s_wolResumeJob;

// Everyone sees why it stopped and what to do about it; the host is usually not
// the one who has to act.
static void wolResumeArmFailed(const UnicodeString &what, const UnicodeString &fix)
{
	s_wolResumeArmInProgress = FALSE;
	// Some reasons already end in a period; do not double it.
	UnicodeString reason = what;
	if (!reason.isEmpty() && reason.getCharAt(reason.getLength() - 1) == L'.')
	{
		reason.removeLastChar();
	}
	UnicodeString msg;
	msg.format(L"Resume: %ls. %ls", reason.str(), fix.str());
	wolLobbySystemChat(msg);
}

static Bool wolResumeSlotsAlreadyAligned(NGMP_OnlineServices_LobbyInterface *pLobbyInterface)
{
	const LobbyEntry &lobby = pLobbyInterface->GetCurrentLobby();
	for (const LobbySlotSpec &spec : s_wolResumeJob.slots)
	{
		const LobbyMemberEntry *m = nullptr;
		for (const LobbyMemberEntry &member : lobby.members)
		{
			if (member.m_SlotIndex == spec.slotIndex)
			{
				m = &member;
				break;
			}
		}
		if (m == nullptr || m->m_SlotState != spec.slotState)
			return FALSE;
		if (spec.slotState == SLOT_PLAYER && m->user_id != spec.userID)
			return FALSE;
		if (spec.slotState == SLOT_OPEN || spec.slotState == SLOT_CLOSED)
			continue;
		if (m->side != spec.side || m->color != spec.color || m->startpos != spec.startPos || m->team != spec.team)
			return FALSE;
	}
	return TRUE;
}

static void wolUpdateResumeButton();

static void wolResumeArmStep(Int step)
{
	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	NGMPGame *game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
	if (pLobbyInterface == nullptr || game == nullptr || !pLobbyInterface->IsHost())
	{
		s_wolResumeArmInProgress = FALSE;
		return;
	}
	const WolResumeArmJob &job = s_wolResumeJob;
	const LobbyEntry &lobby = pLobbyInterface->GetCurrentLobby();
	ReleaseLog("Resume: arm step %d", step);

	if (step <= 0 && (game->getMap().compareNoCase(job.mapKey) != 0 || lobby.map_crc != job.mapCRC))
	{
		wolLocalSystemChat(TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:ResumeSettingMap",
			L"Resume: switching the lobby to the recording's map, %ls", job.mapDisplayU.str()));
		pLobbyInterface->UpdateCurrentLobby_Map(job.mapDisplayU, job.mapKey, job.mapOfficial, job.mapNumPlayers, job.mapCRC, [](bool bOK)
		{
			if (bOK)
				wolResumeArmStep(1);
			else
				wolResumeArmFailed(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeMapRefused", L"the service refused the map change"),
					TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeFixMap", L"Host: select the recording's map with Select Map, then press Resume again."));
		});
		return;
	}
	if (step <= 1 && lobby.starting_cash != job.startingCash)
	{
		wolLocalSystemChat(TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:ResumeSettingCash",
			L"Resume: setting the starting cash to %u, as recorded", job.startingCash));
		pLobbyInterface->UpdateCurrentLobby_StartingCash(job.startingCash, [](bool bOK)
		{
			if (bOK)
				wolResumeArmStep(2);
			else
				wolResumeArmFailed(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeCashRefused", L"the service refused the starting cash change"),
					TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeFixCash", L"Host: set the starting cash the recording used, then press Resume again."));
		});
		return;
	}
	if (step <= 2 && lobby.limit_superweapons != (job.limitSuperweapons != FALSE))
	{
		wolLocalSystemChat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeSettingSuperweapons",
			L"Resume: setting the superweapon limit as recorded"));
		pLobbyInterface->UpdateCurrentLobby_LimitSuperweapons(job.limitSuperweapons != FALSE, [](bool bOK)
		{
			if (bOK)
				wolResumeArmStep(3);
			else
				wolResumeArmFailed(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeSuperweaponsRefused", L"the service refused the superweapon limit change"),
					TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeFixSuperweapons", L"Host: set the superweapon limit the recording used, then press Resume again."));
		});
		return;
	}
	if (step <= 3 && !wolResumeSlotsAlreadyAligned(pLobbyInterface))
	{
		wolLobbySystemChat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeAligning",
			L"Resume: moving everyone to the slot, faction, color and start position they had in the recording"));
		pLobbyInterface->UpdateCurrentLobby_AlignSlots(job.slots, [](bool bOK, const std::string &error)
		{
			if (bOK)
			{
				wolResumeArmStep(4);
				return;
			}
			UnicodeString what;
			what.format(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeAlignRefused", L"the lobby could not be arranged like the recording (%hs)"), error.c_str());
			wolResumeArmFailed(what, TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeFixAlign",
				L"Everyone: take the slot you had in that match, then the host presses Resume again."));
		});
		return;
	}

	// Upload the source, then arm; the arm is announced only once the service accepted it.
	const UnsignedInt handoffFrame = job.handoff;
	const Int seed = job.seed;
	wolLocalSystemChat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeUploading", L"Resume: uploading the replay to the service..."));
	pLobbyInterface->UploadResumeReplay(ResumeFromReplay::getSourceFilePath(), [handoffFrame, seed](bool bUploaded)
	{
		NGMP_OnlineServices_LobbyInterface* pLobby = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		if (!bUploaded || pLobby == nullptr)
		{
			wolResumeArmFailed(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeUploadFailed", L"the replay upload failed, nothing is armed"),
				TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeFixUpload", L"Host: press Resume again; if it keeps failing the replay may be too large for the service."));
			return;
		}
		pLobby->UpdateCurrentLobby_ArmResume(handoffFrame, seed, [handoffFrame](bool bArmed)
		{
			s_wolResumeArmInProgress = FALSE;
			NGMP_OnlineServices_LobbyInterface* pLobby2 = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
			if (!bArmed || pLobby2 == nullptr)
			{
				wolResumeArmFailed(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeArmRefused", L"the service refused to arm"),
					TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeFixArm", L"Host: press Resume again. A setting changed while it was arming, or the upload was not accepted."));
				return;
			}
			s_wolLastSeenResumeHandoff = handoffFrame;
			pLobby2->UpdateCurrentLobby_HasResumeReplay(true); // the host holds the source by definition
			wolLobbySystemChat(TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:ResumeArmed",
				L"Resume armed: the next start replays the last match to %ls, then hands control back", ResumeFromReplay::formatGameTime(handoffFrame).str()));
			wolUpdateResumeButton();
		});
	});
}

// Press Resume (or type /resume): checks the recording against the lobby, then
// drives the service through whatever has to change (see wolResumeArmStep).
// /resume off, or the button while armed, disarms.
static void wolTryArmResumeFromReplay(Bool disarm)
{
	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if (pLobbyInterface == nullptr || pLobbyInterface->GetCurrentGame() == nullptr)
		return;
	if (!pLobbyInterface->IsHost())
	{
		wolLocalSystemChat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeHostOnly", L"Only the host can resume a game"));
		return;
	}
	NGMPGame *game = pLobbyInterface->GetCurrentGame();

	if (disarm)
	{
		pLobbyInterface->UpdateCurrentLobby_ArmResume(0, game->getSeed(), [](bool bSuccess)
		{
			if (bSuccess)
				wolLobbySystemChat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeDisarmed", L"Resume disarmed: the next start is a fresh game"));
			else
				wolLocalSystemChat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeDisarmRefused", L"Resume: the service refused to disarm; try again"));
			wolUpdateResumeButton();
		});
		return;
	}
	if (s_wolResumeArmInProgress)
	{
		wolLocalSystemChat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeUploadInProgress", L"Resume: still arming, wait for it to finish"));
		return;
	}

	ResumeFromReplay::SourceReplay replay;
	UnsignedInt handoff = 0;
	UnicodeString why;
	if (!ResumeFromReplay::prepareHostSource(replay, handoff, why))
	{
		// The recording itself is unusable (or there is none): nothing in the lobby can fix that.
		UnicodeString reason = why;
		if (!reason.isEmpty() && reason.getCharAt(reason.getLength() - 1) == L'.')
		{
			reason.removeLastChar(); // the sentence continues
		}
		UnicodeString msg;
		msg.format(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeSourceRefused", L"Resume: %ls. The host's last recording is what gets resumed, and it must come from this build."), reason.str());
		wolLobbySystemChat(msg);
		return;
	}
	const ReplayGameInfo &rg = replay.game;

	if (replay.mapData->m_CRC != rg.getMapCRC())
	{
		UnicodeString msg;
		msg.format(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeHostMapDiffers",
			L"Resume: the host's copy of %ls is not the file that match was played on. Host: reinstall that map, then press Resume again."), replay.mapData->m_displayName.str());
		wolLobbySystemChat(msg);
		return;
	}

	// Every human must be present under the recorded name, and only the recorded host can do this.
	if (!ResumeFromReplay::validateRoster(rg, game, why))
	{
		UnicodeString msg;
		msg.format(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeRosterRefused", L"Resume: %ls. Get exactly the players of that match into this lobby, then press Resume again."), why.str());
		wolLobbySystemChat(msg);
		return;
	}

	// Everything the service has to be told, taken from the recording up front: the job
	// outlives this call, and the ReplayGameInfo does not.
	WolResumeArmJob &job = s_wolResumeJob;
	job = WolResumeArmJob();
	job.handoff = handoff;
	job.seed = rg.getSeed();
	job.mapKey = rg.getMap();
	job.mapDisplayU = replay.mapData->m_displayName;
	job.mapDisplay.translate(job.mapDisplayU);
	job.mapOfficial = replay.mapData->m_isOfficial;
	job.mapNumPlayers = replay.mapData->m_numPlayers;
	job.mapCRC = rg.getMapCRC();
	job.startingCash = rg.getStartingCash().countMoney();
	job.limitSuperweapons = rg.getSuperweaponRestriction() != 0;
	const LobbyEntry &lobby = pLobbyInterface->GetCurrentLobby();
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		LobbySlotSpec spec;
		spec.slotIndex = i;
		const GameSlot *rs = rg.getConstSlot(i);
		if (rs != nullptr && rs->isHuman())
		{
			spec.slotState = SLOT_PLAYER;
			for (const LobbyMemberEntry &member : lobby.members)
			{
				if (member.IsHuman() && rs->getName().compare(UnicodeString(from_utf8(member.display_name).c_str())) == 0)
				{
					spec.userID = member.user_id;
					break;
				}
			}
			if (spec.userID == -1)
			{
				// validateRoster matched the names against the local slots, which mirror the members list;
				// a miss here means the two disagree, which a lobby push will cure.
				UnicodeString msg;
				msg.format(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeMemberUnknown",
					L"Resume: %ls is not in the lobby yet. Wait a moment, then press Resume again."), rs->getName().str());
				wolLobbySystemChat(msg);
				return;
			}
		}
		else if (rs != nullptr && rs->isOccupied())
		{
			spec.slotState = rs->getState();
		}
		else
		{
			spec.slotState = (i < job.mapNumPlayers) ? SLOT_OPEN : SLOT_CLOSED;
		}
		if (rs != nullptr && rs->isOccupied())
		{
			spec.side = rs->getPlayerTemplate();
			spec.color = rs->getColor();
			spec.startPos = rs->getStartPos();
			spec.team = rs->getTeamNumber();
		}
		job.slots.push_back(spec);
	}

	ReleaseLog("Resume: arming, handoff=%u seed=%d map=%s crc=%08X cash=%u sw=%d", handoff, job.seed, job.mapKey.str(), job.mapCRC, job.startingCash, (int)job.limitSuperweapons);
	s_wolResumeArmInProgress = TRUE;
	wolResumeArmStep(0);
}

// The Resume button says what the arm is doing: for the host it arms or cancels;
// for everyone else it is read-only and shows RESUME ARMED while one is pending,
// so a guest who missed the chat line still knows the next start is a resume.
static void wolUpdateResumeButton()
{
	if (buttonResume == NULL)
		return;
	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	NGMPGame *game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
	const Bool armed = game != nullptr && game->isResumeArmed();
	const Bool host = pLobbyInterface != nullptr && pLobbyInterface->IsHost();
	if (host)
	{
		buttonResume->winEnable(TRUE);
		buttonResume->winSetText(armed
			? TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeCancel", L"CANCEL RESUME")
			: TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeFromReplay", L"RESUME"));
	}
	else
	{
		buttonResume->winEnable(FALSE);
		buttonResume->winSetText(armed
			? TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeArmedButton", L"RESUME ARMED")
			: TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeFromReplay", L"RESUME"));
	}
}


// Every lobby update: a guest downloads the source whenever the lobby is armed with an upload
// it has not fetched yet (the service bumps ResumeReplayGeneration on every arm, so a re-arm
// with the same handoff frame is fetched again), tells the lobby whether its copy matches and
// flags itself; everyone notes an arm or a disarm.
static void wolCheckResumeArmChange()
{
	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	NGMPGame *game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
	if (game == nullptr)
		return;
	const UnsignedInt handoff = game->getResumeHandoffFrame();
	if (handoff != s_wolLastSeenResumeHandoff)
	{
		s_wolLastSeenResumeHandoff = handoff;
		if (handoff == 0)
			wolLocalSystemChat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeDisarmed", L"Resume disarmed: the next start is a fresh game"));
		else if (!pLobbyInterface->IsHost())
			// Said locally, from the lobby state itself, so it does not depend on the host's
			// chat line getting through.
			wolLocalSystemChat(TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:ResumeArmedGuest",
				L"Resume armed by the host: the next start replays the last match to %ls, then hands control back", ResumeFromReplay::formatGameTime(handoff).str()));
	}
	wolUpdateResumeButton();
	if (handoff == 0 || pLobbyInterface->IsHost())
		return; // nothing armed, or the host: it announced the arm and holds the source

	const uint32_t generation = pLobbyInterface->GetCurrentLobby().resume_replay_generation;
	if (generation == s_wolResumeDownloadedGeneration || s_wolResumeDownloadInProgress)
		return;
	s_wolResumeDownloadedGeneration = generation;
	s_wolResumeDownloadInProgress = TRUE;

	wolLocalSystemChat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeDownloading", L"Resume: downloading the replay from the service..."));
	pLobbyInterface->DownloadResumeReplay(ResumeFromReplay::getSourceFilePath(), [](bool bDownloaded)
	{
		s_wolResumeDownloadInProgress = FALSE;
		NGMP_OnlineServices_LobbyInterface* pLobby = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		NGMPGame *lobbyGame = pLobby == nullptr ? nullptr : pLobby->GetCurrentGame();
		if (pLobby == nullptr || lobbyGame == nullptr)
			return;
		UnicodeString report;
		Bool usable = FALSE;
		if (!bDownloaded)
			report = TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeDownloadFailed", L"Resume: my download of the replay failed. Host: press Cancel Resume, then Resume again");
		else if (!(usable = ResumeFromReplay::guestSourceReport(lobbyGame, report)))
			report.concat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeGuestMismatchFix", L". Host: press Resume again to realign the lobby"));
		pLobby->SendChatMessageToCurrentLobby(report, false);
		pLobby->UpdateCurrentLobby_HasResumeReplay(usable);
	});
}

void InitWOLGameGadgets()
{
	ClearGSMessageBoxes();
	s_wolLastSeenResumeHandoff = 0;
	s_wolResumeArmInProgress = FALSE;
	s_wolResumeDownloadedGeneration = 0;
	s_wolResumeDownloadInProgress = FALSE;

	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	NGMPGame* theGameInfo = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

	if (theGameInfo == nullptr || pLobbyInterface == nullptr)
	{
		return;
	}

	for (Int i = 0; i < MAX_SLOTS; ++i)
		connectionIndicatorState[i] = CONNECTION_INDICATOR_CONNECTING;

	//Initialize the gadget IDs
	parentWOLGameSetupID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:GameSpyGameOptionsMenuParent" );
	buttonBackID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ButtonBack" );
	buttonStartID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ButtonStart" );
	textEntryChatID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:TextEntryChat" );
	textEntryMapDisplayID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:TextEntryMapDisplay" );
	listboxGameSetupChatID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ListboxChatWindowGameSpyGameSetup" );
	buttonEmoteID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ButtonEmote" );
	buttonSelectMapID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ButtonSelectMap" );
	buttonRandomizeID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ButtonRandomize" );
	buttonMapVoteID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ButtonMapVote" );
	buttonResumeID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:ButtonResume" );
	checkBoxUseStatsID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:CheckBoxUseStats" );
	windowMapID = TheNameKeyGenerator->nameToKey( "GameSpyGameOptionsMenu.wnd:MapWindow" );
  checkBoxLimitSuperweaponsID = TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:CheckboxLimitSuperweapons");
  checkBoxEnforceRandomID = TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:CheckboxEnforceRandom");
  comboBoxStartingCashID = TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:ComboBoxStartingCash");
  checkBoxLimitArmiesID = TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:CheckBoxLimitArmies");
	windowMapSelectMapID = TheNameKeyGenerator->nameToKey("WOLMapSelectMenu.wnd:WinMapPreview");

	NameKeyType staticTextTitleID = NAMEKEY("GameSpyGameOptionsMenu.wnd:StaticTextGameName");

	// Initialize the pointers to our gadgets
	parentWOLGameSetup = TheWindowManager->winGetWindowFromId( NULL, parentWOLGameSetupID );
	buttonEmote = TheWindowManager->winGetWindowFromId( parentWOLGameSetup,buttonEmoteID  );
	buttonSelectMap = TheWindowManager->winGetWindowFromId( parentWOLGameSetup,buttonSelectMapID  );
	buttonRandomize = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, buttonRandomizeID ); // absent from older .wnd layouts
	if (buttonRandomize)
		buttonRandomize->winSetText(TheGameText->FETCH_OR_SUBSTITUTE("GUI:Randomize", L"Randomize")); // no .csf change needed
	buttonMapVote = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, buttonMapVoteID ); // absent from older .wnd layouts
	if (buttonMapVote)
		buttonMapVote->winSetText(TheGameText->FETCH_OR_SUBSTITUTE("GUI:MapVote", L"Radarvan Pick"));
	buttonResume = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, buttonResumeID ); // absent from older .wnd layouts
	wolUpdateResumeButton();
	checkBoxUseStats = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, checkBoxUseStatsID );
	buttonStart = TheWindowManager->winGetWindowFromId( parentWOLGameSetup,buttonStartID  );
	buttonBack = TheWindowManager->winGetWindowFromId( parentWOLGameSetup,  buttonBackID);
	listboxGameSetupChat = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, listboxGameSetupChatID );
	textEntryChat = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, textEntryChatID );
	textEntryMapDisplay = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, textEntryMapDisplayID );
	windowMap = TheWindowManager->winGetWindowFromId( parentWOLGameSetup,windowMapID  );
	SetListBoxRowAnimMode(listboxGameSetupChat, LIST_ROW_ANIM_SLOT);
  DEBUG_ASSERTCRASH(windowMap, ("Could not find the parentWOLGameSetup.wnd:MapWindow" ));

  checkBoxLimitSuperweapons = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, checkBoxLimitSuperweaponsID );
  // Absent from an older .wnd layout, so every use of it is guarded.
  checkBoxEnforceRandom = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, checkBoxEnforceRandomID );
  if (checkBoxEnforceRandom)
  {
    checkBoxEnforceRandom->winSetText(TheGameText->FETCH_OR_SUBSTITUTE("GUI:EnforceRandom", L"Enforce Random"));
    checkBoxEnforceRandom->winSetTooltip(TheGameText->FETCH_OR_SUBSTITUTE("TOOLTIP:EnforceRandom", L"Lock all players to random faction and random team"));
  }
  DEBUG_ASSERTCRASH(windowMap, ("Could not find the GameSpyGameOptionsMenu.wnd:CheckboxLimitSuperweapons" ));
  comboBoxStartingCash = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, comboBoxStartingCashID );
  DEBUG_ASSERTCRASH(windowMap, ("Could not find the GameSpyGameOptionsMenu.wnd:ComboBoxStartingCash" ));

#if defined(GENERALS_ONLINE)
  PopulateStartingCashComboBox(comboBoxStartingCash, theGameInfo);
#else
  PopulateStartingCashComboBox( comboBoxStartingCash, TheGameSpyGame );
#endif
  checkBoxLimitArmies = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, checkBoxLimitArmiesID );
  DEBUG_ASSERTCRASH(windowMap, ("Could not find the GameSpyGameOptionsMenu.wnd:CheckBoxLimitArmies" ));

  // Limit Armies can ONLY be set in the Host Game window (PopupHostGame.wnd)
  checkBoxLimitArmies->winEnable( false );
  // Ditto use stats, when a layout still carries the box at all.
  if (checkBoxUseStats)
    checkBoxUseStats->winEnable( false );

#if defined(GENERALS_ONLINE)
  Int isUsingStats = theGameInfo->getUseStats();
#else
  Int isUsingStats = TheGameSpyGame->getUseStats();
#endif

	
  if (checkBoxUseStats)
  {
    GadgetCheckBoxSetChecked(checkBoxUseStats, isUsingStats );
    checkBoxUseStats->winSetTooltip( TheGameText->fetch( isUsingStats ? "TOOLTIP:UseStatsOn" : "TOOLTIP:UseStatsOff" ) );
  }

#if defined(GENERALS_ONLINE)
  if (!pLobbyInterface->IsHost())
#else
  if ( !TheGameSpyGame->amIHost() )
#endif
  {
    checkBoxLimitSuperweapons->winEnable( false );
    if (checkBoxEnforceRandom) checkBoxEnforceRandom->winEnable( false );
    comboBoxStartingCash->winEnable( false );
    if (buttonRandomize) buttonRandomize->winEnable( false );
    if (buttonMapVote) buttonMapVote->winEnable( false );
    wolUpdateResumeButton();
		NameKeyType labelID = TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:StartingCashLabel");
		TheWindowManager->winGetWindowFromId(parentWOLGameSetup, labelID)->winEnable( FALSE );
  }
#if defined(GENERALS_ONLINE)
  else
  {
	  checkBoxLimitSuperweapons->winEnable(true);
	  // Deliberately not switched off for a stats match, unlike the settings
	  // below: locking everyone to random changes nobody's economy or rules,
	  // and a ranked game is where it gets asked for.
	  if (checkBoxEnforceRandom) checkBoxEnforceRandom->winEnable(true);
	  comboBoxStartingCash->winEnable(true);
	  if (buttonRandomize) buttonRandomize->winEnable(true);
	  if (buttonMapVote) buttonMapVote->winEnable(true);
	  wolUpdateResumeButton();
  }
#endif


#if !defined(GENERALS_ONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES)
	if (isUsingStats)
	{
		// Recorded stats games can never limit superweapons, limit armies, or have inflated starting cash.
		// This should probably be enforced at the gamespy level as well, to prevent expoits.
		checkBoxLimitSuperweapons->winEnable( FALSE );
		comboBoxStartingCash->winEnable( FALSE );
		checkBoxLimitArmies->winEnable( FALSE );
		if (buttonRandomize) buttonRandomize->winEnable( FALSE );
		NameKeyType labelID = TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:StartingCashLabel");
		TheWindowManager->winGetWindowFromId(parentWOLGameSetup, labelID)->winEnable( FALSE );
	}
#endif

	windowMap->winSetTooltipFunc(MapSelectorTooltip);

	GameWindow *staticTextTitle = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, staticTextTitleID );
	if (staticTextTitle)
	{
#if defined(GENERALS_ONLINE)
		GadgetStaticTextSetText(staticTextTitle, theGameInfo->getGameName());
#else
		GadgetStaticTextSetText(staticTextTitle, TheGameSpyGame->getGameName());
#endif
	}

	if (!theGameInfo)
	{
		DEBUG_CRASH(("No staging room!"));
		return;
	}

	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		AsciiString tmpString;
		tmpString.format("GameSpyGameOptionsMenu.wnd:ComboBoxPlayer%d", i);
		comboBoxPlayerID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxPlayer[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, comboBoxPlayerID[i] );
		GadgetComboBoxReset(comboBoxPlayer[i]);
		comboBoxPlayer[i]->winSetTooltipFunc(playerTooltip);

		tmpString.format("GameSpyGameOptionsMenu.wnd:StaticTextPlayer%d", i);
		staticTextPlayerID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		staticTextPlayer[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, staticTextPlayerID[i] );
		staticTextPlayer[i]->winSetTooltipFunc(playerTooltip);
		
		bool bIsHost = pLobbyInterface->IsHost();
		if (bIsHost)
			staticTextPlayer[i]->winHide(TRUE);

		if (theGameInfo->getLocalSlotNum() != i)
		{
			// AI tiers run strongest-first, matching the LAN and Skirmish lobbies.
			// slotStateFromLobbyComboPos / lobbyComboPosFromSlotState (GameInfo.h)
			// are the only translation between a position here and a SlotState.
			GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:Open"), GameSpyColor[GSCOLOR_PLAYER_NORMAL]);
			GadgetComboBoxSetItemData(comboBoxPlayer[i], 0, (void *)SLOT_OPEN);
			GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:Closed"), GameSpyColor[GSCOLOR_PLAYER_NORMAL]);
			GadgetComboBoxSetItemData(comboBoxPlayer[i], 1, (void *)SLOT_CLOSED);
			GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:TacticalAI"), GameSpyColor[GSCOLOR_PLAYER_NORMAL]);
			GadgetComboBoxSetItemData(comboBoxPlayer[i], 2, (void *)SLOT_TACTICAL_AI);
			GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:HardAI"), GameSpyColor[GSCOLOR_PLAYER_NORMAL]);
			GadgetComboBoxSetItemData(comboBoxPlayer[i], 3, (void *)SLOT_BRUTAL_AI);
			GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:MediumAI"), GameSpyColor[GSCOLOR_PLAYER_NORMAL]);
			GadgetComboBoxSetItemData(comboBoxPlayer[i], 4, (void *)SLOT_MED_AI);
			GadgetComboBoxAddEntry(comboBoxPlayer[i], TheGameText->fetch("GUI:EasyAI"), GameSpyColor[GSCOLOR_PLAYER_NORMAL]);
			GadgetComboBoxSetItemData(comboBoxPlayer[i], 5, (void *)SLOT_EASY_AI);
			GadgetComboBoxSetSelectedPos(comboBoxPlayer[i], 0);
		}
		else
		{
			// Local player, so add the local player name
			NGMPGameSlot* slot = theGameInfo->getGameSpySlot(i);
			if (slot)
			{
				GadgetComboBoxAddEntry(comboBoxPlayer[i], slot->getName(), GameSpyColor[GSCOLOR_PLAYER_NORMAL]);
				GadgetComboBoxSetSelectedPos(comboBoxPlayer[i], 0);
			}
		}

		tmpString.format("GameSpyGameOptionsMenu.wnd:ComboBoxColor%d", i);
		comboBoxColorID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxColor[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, comboBoxColorID[i] );
		DEBUG_ASSERTCRASH(comboBoxColor[i], ("Could not find the comboBoxColor[%d]",i ));

		PopulateColorComboBox(i, comboBoxColor, theGameInfo);
		GadgetComboBoxSetSelectedPos(comboBoxColor[i], 0);

		tmpString.format("GameSpyGameOptionsMenu.wnd:ComboBoxPlayerTemplate%d", i);
		comboBoxPlayerTemplateID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxPlayerTemplate[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, comboBoxPlayerTemplateID[i] );
		DEBUG_ASSERTCRASH(comboBoxPlayerTemplate[i], ("Could not find the comboBoxPlayerTemplate[%d]",i ));

		// add tooltips to the player template combobox and listbox
		comboBoxPlayerTemplate[i]->winSetTooltipFunc(playerTemplateComboBoxTooltip);
		GadgetComboBoxGetListBox(comboBoxPlayerTemplate[i])->winSetTooltipFunc(playerTemplateListBoxTooltip);

		tmpString.format("GameSpyGameOptionsMenu.wnd:ComboBoxTeam%d", i);
		comboBoxTeamID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxTeam[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, comboBoxTeamID[i] );
		DEBUG_ASSERTCRASH(comboBoxTeam[i], ("Could not find the comboBoxTeam[%d]",i ));

		PopulateTeamComboBox(i, comboBoxTeam, theGameInfo);

		tmpString.format("GameSpyGameOptionsMenu.wnd:ButtonAccept%d", i);
		buttonAcceptID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonAccept[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, buttonAcceptID[i] );
		DEBUG_ASSERTCRASH(buttonAccept[i], ("Could not find the buttonAccept[%d]",i ));
		buttonAccept[i]->winSetTooltipFunc(gameAcceptTooltip);

		tmpString.format("GameSpyGameOptionsMenu.wnd:GenericPing%d", i);
		genericPingWindowID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		genericPingWindow[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, genericPingWindowID[i] );
		DEBUG_ASSERTCRASH(genericPingWindow[i], ("Could not find the genericPingWindow[%d]",i ));
		genericPingWindow[i]->winSetTooltipFunc(pingTooltip);
		genericPingWindow[i]->winSetDrawFunc(WOLConnectionIndicatorDraw);

//		tmpString.format("GameSpyGameOptionsMenu.wnd:ButtonStartPosition%d", i);
//		buttonStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
//		buttonStartPosition[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, buttonStartPositionID[i] );
//		DEBUG_ASSERTCRASH(buttonStartPosition[i], ("Could not find the ButtonStartPosition[%d]",i ));

		tmpString.format("GameSpyGameOptionsMenu.wnd:ButtonMapStartPosition%d", i);
		buttonMapStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId( parentWOLGameSetup, buttonMapStartPositionID[i] );
		DEBUG_ASSERTCRASH(buttonMapStartPosition[i], ("Could not find the ButtonMapStartPosition[%d]",i ));

//		if (buttonStartPosition[i])
//			buttonStartPosition[i]->winHide(TRUE);

		if(i !=0 && buttonAccept[i])
			buttonAccept[i]->winHide(TRUE);
	}

	if( buttonAccept[0] )
		buttonAccept[0]->winEnable(TRUE);

	if (buttonBack != NULL)
	{
		buttonBack->winEnable(TRUE);
	}
		//GadgetButtonSetEnabledColor(buttonAccept[0], GameSpyColor[GSCOLOR_ACCEPT_TRUE]);

		// TODO_NGMP: Where does this happen in the normal game?
#if defined(GENERALS_ONLINE)
	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, theGameInfo, TRUE);

		// Make sure selections are up to date on all machines
		handlePlayerTemplateSelection(i, true);
	}
#endif
}

void DeinitWOLGameGadgets()
{
	parentWOLGameSetup = NULL;
	buttonEmote = NULL;
	buttonSelectMap = NULL;
	buttonRandomize = NULL;
	buttonMapVote = NULL;
	buttonResume = NULL;
	buttonStart = NULL;
	buttonBack = NULL;
	listboxGameSetupChat = NULL;
	textEntryChat = NULL;
	textEntryMapDisplay = NULL;
	if (windowMap)
	{
		windowMap->winSetUserData(NULL);
		windowMap = NULL;
	}
	checkBoxUseStats = NULL;
  checkBoxLimitSuperweapons = NULL;
  checkBoxEnforceRandom = NULL;
  comboBoxStartingCash = NULL;

//	GameWindow *staticTextTitle = NULL;
	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		comboBoxPlayer[i] = NULL;
		staticTextPlayer[i] = NULL;
		comboBoxColor[i] = NULL;
		comboBoxPlayerTemplate[i] = NULL;
		comboBoxTeam[i] = NULL;
		buttonAccept[i] = NULL;
//		buttonStartPosition[i] = NULL;
		buttonMapStartPosition[i] = NULL;
		genericPingWindow[i] = NULL;
	}
}

static Bool initDone = false;
UnsignedInt lastSlotlistTime = 0;
UnsignedInt enterTime = 0;
Bool initialAcceptEnable = FALSE;
//-------------------------------------------------------------------------------------------------
/** Initialize the Lan Game Options Menu */
//-------------------------------------------------------------------------------------------------
void WOLGameSetupMenuInit( WindowLayout *layout, void *userData )
{
	s_randomizeCount = 0;
	s_localSlotChosen = FALSE;
	s_applyingLobbyUpdate = FALSE;
	// A fresh lobby: forget the last one's map-download bookkeeping.
	s_mapDownloadPending = FALSE;
	s_mapDownloadCRC = 0;
	s_mapDownloadFailedCRC = 0;
	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		return;
	}

	// register for chat events
	pLobbyInterface->RegisterForChatCallback([](UnicodeString strMessage, Color color)
		{
			GadgetListBoxAddEntryText(listboxGameSetupChat, strMessage, color, -1, -1);
		});

	// cannot connect to the lobby we joined
	pLobbyInterface->RegisterForCannotConnectToLobbyCallback([](void)
		{
			if (TheNetwork != NULL) {
				delete TheNetwork;
				TheNetwork = NULL;
			}
			GSMessageBoxOk(TheGameText->fetch("GUI:Error"), UnicodeString(L"Could not connect to all players in the lobby"));

			PopBackToLobby();
		});

	// connection events (for debug really)
	NetworkMesh* pMesh = NGMP_OnlineServicesManager::GetNetworkMesh();
	if (pMesh != nullptr)
	{
		pMesh->RegisterForConnectionEvents([](int64_t userID, std::wstring strDisplayName, PlayerConnection* connection)
			{
				std::string strState = "Unknown";

				EConnectionState connState = connection->GetState();

				switch (connState)
				{
				case EConnectionState::NOT_CONNECTED:
					strState = "Not Connected";
					break;

				case EConnectionState::CONNECTING_DIRECT:
					strState = "Connecting";
					break;
				case EConnectionState::FINDING_ROUTE:
					strState = "Connecting (Finding Route)";
					break;

				case EConnectionState::CONNECTED_DIRECT:
					strState = "Connected";
					break;

				case EConnectionState::CONNECTION_FAILED:
					strState = "Connection Failed";
					break;

				case EConnectionState::CONNECTION_DISCONNECTED:
					strState = "Disconnected (Was Connected Previously)";
					break;

				default:
					strState = "Unknown";
					break;
				}

				UnicodeString strConnectionMessage;
				if (connState == EConnectionState::CONNECTED_DIRECT || connState == EConnectionState::CONNECTION_DISCONNECTED)
				{
					//strConnectionMessage.format(L"Connection state to %s changed to: %hs (mechanism: %hs | protocol: %hs)", strDisplayName.c_str(), strState.c_str(), strConnectionType.c_str(), connection->IsIPV4() ? "IPv4" : "IPv6");
					//GadgetListBoxAddEntryText(listboxGameSetupChat, strConnectionMessage, GameMakeColor(255, 194, 15, 255), -1, -1);
				}
				else
				{
#if !defined(_DEBUG)
					if (connState == EConnectionState::CONNECTION_FAILED)
					{
#endif
						strConnectionMessage.format(L"Connection: %s is now %hs.", strDisplayName.c_str(), strState.c_str());
						const Color connectionColor = connState == EConnectionState::CONNECTION_FAILED
							? GameMakeColor(255, 0, 0, 255)
							: GameMakeColor(192, 192, 192, 255);
						GadgetListBoxAddEntryText(listboxGameSetupChat, strConnectionMessage, connectionColor, -1, -1);

#if !defined(_DEBUG)
					}
#endif
				}

				// update UI
				WOLDisplaySlotList();
			});
	}

	// player doesnt have map events
	pLobbyInterface->RegisterForPlayerDoesntHaveMapCallback([](LobbyMemberEntry lobbyMember)
		{
			// tell the host the user doesn't have the map
			UnicodeString mapDisplayName;
			const MapMetaData* mapData = TheMapCache->findMap(TheNGMPGame->getMap());
			Bool willTransfer = TRUE;
			if (mapData)
			{
				mapDisplayName.format(L"%ls", mapData->m_displayName.str());
				willTransfer = !mapData->m_isOfficial;
			}
			else
			{
				mapDisplayName.translate(TheNGMPGame->getMap().str());
				willTransfer = WouldMapTransfer(TheNGMPGame->getMap());
			}

			UnicodeString strDisplayName(from_utf8(lobbyMember.display_name).c_str());

			UnicodeString text;
			if (willTransfer)
				text.format(TheGameText->fetch("GUI:PlayerNoMapWillTransfer"), strDisplayName.str(), mapDisplayName.str());
			else
				text.format(TheGameText->fetch("GUI:PlayerNoMap"), strDisplayName.str(), mapDisplayName.str());
			GadgetListBoxAddEntryText(listboxGameSetupChat, text, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
		});

	

	// register for roster events
	pLobbyInterface->RegisterForRosterNeedsRefreshCallback([]()
		{
			WOLDisplaySlotList();
			WOLDisplayGameOptions();
			wolCheckResumeArmChange(); // TheSuperHackers @feature bill-rich 24/09/2026 resume-from-replay arm arrived or left
		});

	pLobbyInterface->RegisterForGameStartPacket([]()
		{
			NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
			NGMPGame* myGame = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();

			if (pLobbyInterface == nullptr || !myGame || !myGame->isInGame())
				return;

			if (!TheNGMPGame)
				return;

			// TODO_NGMP

			GameWindow* buttonBuddy = TheWindowManager->winGetWindowFromId(NULL, NAMEKEY("GameSpyGameOptionsMenu.wnd:ButtonCommunicator"));
			if (buttonBuddy)
				buttonBuddy->winEnable(FALSE);
			GameSpyCloseOverlay(GSOVERLAY_BUDDY);
			GameSpyCloseOverlay(GSOVERLAY_PLAYERINFO);

			TheNGMPGame->startGame(0);
		});


	if (TheNGMPGame == nullptr || (TheNGMPGame && TheNGMPGame->isGameInProgress()))
	{
		if (TheNGMPGame != nullptr)
		{
			TheNGMPGame->setGameInProgress(FALSE);
		}

		pLobbyInterface->LeaveCurrentLobby();

		// check if we were disconnected

		// TODO_NGMP: Handle disconnected
		/*
		Int disconReason;
		if (TheGameSpyInfo->isDisconnectedAfterGameStart(&disconReason))
		{
			AsciiString disconMunkee;
			disconMunkee.format("GUI:GSDisconReason%d", disconReason);
			UnicodeString title, body;
			title = TheGameText->fetch( "GUI:GSErrorTitle" );
			body = TheGameText->fetch( disconMunkee );
			GameSpyCloseAllOverlays();
			GSMessageBoxOk( title, body );
			TheGameSpyInfo->reset();
			DEBUG_LOG(("WOLGameSetupMenuInit() - game was in progress, and we were disconnected, so pop immediate back to main menu"));
			TheShell->popImmediate();
			return;
		}
		*/

		// If we init while the game is in progress, we are really returning to the menu
		// after the game.  So, we pop the menu and go back to the lobby.  Whee!
		DEBUG_LOG(("WOLGameSetupMenuInit() - game was in progress, so pop immediate back to lobby"));
		TheShell->popImmediate();

		// TODO_NGMP: Only do this if still connected to service
		//if (TheGameSpyPeerMessageQueue && TheGameSpyPeerMessageQueue->isConnected())
		{
			DEBUG_LOG(("We're still connected, so pushing back on the lobby"));
			TheShell->push("Menus/WOLCustomLobby.wnd", TRUE);
		}

		return;
	}

#if !defined(GENERALS_ONLINE)
	TheGameSpyInfo->setCurrentGroupRoom(0);
#endif

	delete TheNAT;
	TheNAT = NULL;

	nextScreen = NULL;
	buttonPushed = false;
	isShuttingDown = false;
	launchGameNext = FALSE;

	//initialize the gadgets
	EnableSlotListUpdates(FALSE);
	InitWOLGameGadgets();
	EnableSlotListUpdates(TRUE);
	// TODO_NGMP
	//TheGameSpyInfo->registerTextWindow(listboxGameSetupChat);

	//The dialog needs to react differently depending on whether it's the host or not.
	TheMapCache->updateCache();

	// TODO_NGMP
	NGMPGame* game = pLobbyInterface == nullptr ? nullptr : pLobbyInterface->GetCurrentGame();
	if (game == nullptr)
	{
		return;
	}

	NGMPGameSlot* hostSlot = game->getGameSpySlot(0);
	hostSlot->setAccept();

	bool bIsHost = pLobbyInterface->IsHost();

	if (bIsHost)
	{
		// TODO_NGMP
		/*
		OptionPreferences natPref;
		CustomMatchPreferences customPref;
		game->setMap(customPref.getPreferredMap());
		*/

		// TODO_NGMP: Preferred color & factionsupport
		hostSlot->setColor(0);
		hostSlot->setPlayerTemplate(PLAYERTEMPLATE_RANDOM);
		//hostSlot->setNATBehavior((FirewallHelperClass::FirewallBehaviorType)natPref.getFirewallBehavior());
		hostSlot->setPingString(UnicodeString(L"TODO_NGMP"));

		CustomMatchPreferences customPref;


	// Recorded stats games can never limit superweapons, limit armies, or have inflated starting cash.
		// This should probably be enforced at the gamespy level as well, to prevent expoits.
#if !defined(GENERALS_ONLINE)
		Int isUsingStats = TheGameSpyGame->getUseStats();
#else
#if !defined(GENERALS_ONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES)
		Int isUsingStats = game->getUseStats();
#endif
#endif

#if !defined(GENERALS_ONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES)
		game->setStartingCash( isUsingStats? TheMultiplayerSettings->getDefaultStartingMoney() : customPref.getStartingCash() );
		game->setSuperweaponRestriction( isUsingStats? 0 : customPref.getSuperweaponRestricted() ? 1 : 0 );
		if (isUsingStats)
			game->setOldFactionsOnly( 0 );
#else
		game->setStartingCash(customPref.getStartingCash());
		game->setSuperweaponRestriction(customPref.getSuperweaponRestricted() ? 1 : 0);
#endif

		//game->setOldFactionsOnly( customPref.getFactionsLimited() );
    if ( game->oldFactionsOnly() )
    {
      // Make sure host follows the old factions only restrictions!
      const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(hostSlot->getPlayerTemplate());

      if ( fac != NULL && !fac->isOldFaction() )
      {
        hostSlot->setPlayerTemplate( PLAYERTEMPLATE_RANDOM );
      }
    }


		for (Int i=1; i<MAX_SLOTS; ++i)
		{
			NGMPGameSlot *slot = game->getGameSpySlot(i);
			slot->setState( SLOT_OPEN );
		}

		// TODO_NGMP: preferred map support
		//AsciiString lowerMap = getDefaultOfficialMap();
		AsciiString lowerMap = game->getMap();
		//AsciiString lowerMap = customPref.getPreferredMap();
		lowerMap.toLower();
		std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
		if (it != TheMapCache->end())
		{
			hostSlot->setMapAvailability(TRUE);
			game->setMapCRC( it->second.m_CRC );
			game->setMapSize( it->second.m_filesize );

			game->adjustSlotsForMap(); // BGC- adjust the slots for the new map.
		}


		WOLDisplaySlotList();
		WOLDisplayGameOptions();

		// TheSuperHackers @tweak disable the combo box for the host's player name
		comboBoxPlayer[0]->winEnable(FALSE);
	}
	else
	{
		OptionPreferences natPref;
		CustomMatchPreferences customPref;
		AsciiString options;
		//PeerRequest req;
		UnicodeString uName = hostSlot->getName();
		AsciiString aName;
		aName.translate(uName);

		// TODO_NGMP: Do this on join? and map change
		AsciiString asciiMap = game->getMap();
		asciiMap.toLower();

		/*
		req.peerRequestType = PeerRequest::PEERREQUEST_UTMPLAYER;
		req.UTM.isStagingRoom = TRUE;
		req.id = "REQ/";
		req.nick = aName.str();
		options.format("PlayerTemplate=%d", customPref.getPreferredFaction());
		req.options = options.str();
		TheGameSpyPeerMessageQueue->addRequest(req);
		options.format("Color=%d", customPref.getPreferredColor());
		req.options = options.str();
		TheGameSpyPeerMessageQueue->addRequest(req);
		options.format("NAT=%d", natPref.getFirewallBehavior());
		req.options = options.str();
		TheGameSpyPeerMessageQueue->addRequest(req);
		options.format("Ping=%s", TheGameSpyInfo->getPingString().str());
		req.options = options.str();
		TheGameSpyPeerMessageQueue->addRequest(req);
		*/
		game->setMapCRC( game->getMapCRC() );		// force a recheck
		game->setMapSize( game->getMapSize() ); // of if we have the map

		for (Int i = 0; i < MAX_SLOTS; ++i)
		{
			//I'm a client, disable the controls I can't touch.
			comboBoxPlayer[i]->winEnable(FALSE);

			comboBoxColor[i]->winEnable(FALSE);
			comboBoxPlayerTemplate[i]->winEnable(FALSE);
			comboBoxTeam[i]->winEnable(FALSE);
//			buttonStartPosition[i]->winEnable(FALSE);
			buttonMapStartPosition[i]->winEnable(FALSE);

		}
		buttonStart->winSetText(TheGameText->fetch("GUI:Accept"));
		buttonStart->winEnable( FALSE );
		buttonSelectMap->winEnable( FALSE );
		if (buttonRandomize) buttonRandomize->winEnable( FALSE );
		if (buttonMapVote) buttonMapVote->winEnable( FALSE );
		wolUpdateResumeButton();
		initialAcceptEnable = FALSE;

		WOLDisplaySlotList();
		WOLDisplayGameOptions();
	}

	// Show the Menu
	layout->hide( FALSE );

	// Make sure the text fields are clear
	GadgetListBoxReset( listboxGameSetupChat );
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);

	initDone = true;

	// TODO_NGMP
	//TheGameSpyInfo->setGameOptions();
	//TheShell->registerWithAnimateManager(parentWOLGameSetup, WIN_ANIMATION_SLIDE_TOP, TRUE);
	WOLPositionStartSpots();

	lastSlotlistTime = 0;
	enterTime = timeGetTime();

	// Set Keyboard to chat entry
	raiseMessageBoxes = true;
	TheTransitionHandler->setGroup("GameSpyGameOptionsMenuFade");
	TheWindowManager->winSetFocus(textEntryChat);

#if defined(GENERALS_ONLINE)
// NGMP: Did we just enter a lobby with modified camera height?
	// if (pLobbyInterface->IsInLobby())
	{
		LobbyEntry& theLobby = pLobbyInterface->GetCurrentLobby();

		if (theLobby.max_cam_height != GENERALS_ONLINE_DEFAULT_LOBBY_CAMERA_ZOOM)
		{


			if (!pLobbyInterface->IsHost())
			{
				UnicodeString strInform;
				strInform.format(L"Camera height: The host set the limit to %lu.", theLobby.max_cam_height);
				GadgetListBoxAddEntryText(listboxGameSetupChat, strInform, GameMakeColor(192, 192, 192, 255), -1, -1);
			}
			else
			{
				UnicodeString strInform;
				strInform.format(L"Camera height: Your limit is %lu. Use /maxcameraheight <value> to change it. Default: 310.", theLobby.max_cam_height);
				GadgetListBoxAddEntryText(listboxGameSetupChat, strInform, GameMakeColor(192, 192, 192, 255), -1, -1);
			}

		}
	}

    if (pLobbyInterface != nullptr)
    {
        if (pLobbyInterface->IsHost())
        {
			GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"Lobby access: Anyone can join. Use /friendsonly to limit the lobby to friends."), GameMakeColor(192, 192, 192, 255), -1, -1);
        }
    }
    
    if (TheNGMPGame != nullptr)
	{
		if (!TheNGMPGame->getAllowObservers())
		{
			GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"Observers: Disabled by the host."), GameMakeColor(192, 192, 192, 255), -1, -1);
		}
	}
#endif

#if defined(GENERALS_ONLINE)
    // Update the communicator button anytime we get notifications
    NGMP_OnlineServices_SocialInterface* pSocialInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_SocialInterface>();
    if (pSocialInterface != nullptr)
    {
        // notifiactions callback
        pSocialInterface->RegisterForCallback_OnNumberGlobalNotificationsChanged([](int numNotifications)
            {
                // update communicator button
                GameWindow* buttonBuddy = TheWindowManager->winGetWindowFromId(NULL, NAMEKEY("GameSpyGameOptionsMenu.wnd:ButtonCommunicator"));
                if (buttonBuddy != nullptr)
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
                    buttonBuddy->winSetText(buttonText);
                }
            });
    }

    // And also initialize it
    GameWindow* buttonBuddy = TheWindowManager->winGetWindowFromId(NULL, NAMEKEY("GameSpyGameOptionsMenu.wnd:ButtonCommunicator"));
    if (buttonBuddy != nullptr && pSocialInterface->GetNumTotalNotifications() > 0)
    {
        UnicodeString buttonText;
        buttonText.format(L"%s [%d]", TheGameText->fetch("GUI:Buddies").str(), pSocialInterface->GetNumTotalNotifications());
        buttonBuddy->winSetText(buttonText);
    }
#endif
}

//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{

	isShuttingDown = false;

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout, (nextScreen != NULL) );

	if (nextScreen != NULL)
	{
		// TODO_NGMP: Handle disconnect again
		if (false)
		//if (!TheGameSpyPeerMessageQueue || !TheGameSpyPeerMessageQueue->isConnected())
		{
			DEBUG_LOG(("GameSetup shutdownComplete() - skipping push because we're disconnected"));
		}
		else
		{
			TheShell->push(nextScreen);
		}
	}

	/*
	if (launchGameNext)
	{
		TheNGMPGame->launchGame();
		TheGameSpyInfo->leaveStagingRoom();
	}
	*/

	nextScreen = NULL;

}

//-------------------------------------------------------------------------------------------------
/** GameSpy Game Options menu shutdown method */
//-------------------------------------------------------------------------------------------------
void WOLGameSetupMenuShutdown( WindowLayout *layout, void *userData )
{
	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

	if (pLobbyInterface != nullptr)
	{
		pLobbyInterface->DeregisterForChatCallback();
		pLobbyInterface->DeregisterForCannotConnectToLobbyCallback();
		pLobbyInterface->DeregisterForPlayerDoesntHaveMapCallback();
		pLobbyInterface->DeregisterForRosterNeedsRefreshCallback();
		pLobbyInterface->DeregisterForGameStartPacket();
	}
	
	NetworkMesh* pMesh = NGMP_OnlineServicesManager::GetNetworkMesh();
	if (pMesh != nullptr)
	{
		pMesh->DeregisterForConnectionEvents();
	}

	// drop any in-flight mesh connectivity check so a late reply never fires into this now-dead menu
	std::shared_ptr<WebSocket> pWS = NGMP_OnlineServicesManager::GetWebSocket();
	if (pWS != nullptr)
	{
		pWS->ClearConnectivityCheckCallback();
	}

	//TheGameSpyInfo->unregisterTextWindow(listboxGameSetupChat);

	if( WOLMapSelectLayout )
	{
		WOLMapSelectLayout->destroyWindows();
		deleteInstance(WOLMapSelectLayout);
		WOLMapSelectLayout = NULL;
	}
	parentWOLGameSetup = NULL;
	EnableSlotListUpdates(FALSE);
	DeinitWOLGameGadgets();
	if (TheEstablishConnectionsMenu != NULL)
	{
		TheEstablishConnectionsMenu->endMenu();
	}
	initDone = false;

	isShuttingDown = true;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}

	TheShell->reverseAnimatewindow();

	RaiseGSMessageBox();
	TheTransitionHandler->reverse("GameSpyGameOptionsMenuFade");
}

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu update method */
//-------------------------------------------------------------------------------------------------
void WOLGameSetupMenuUpdate( WindowLayout * layout, void *userData)
{
	// Refresh only the fast-changing connection indicators each frame.
	WOLRefreshConnectionIndicators();

	// Fetch the lobby's map if we do not have it (see wolUpdateMapDownload).
	wolUpdateMapDownload();

	// need to exit?
	if (NGMP_OnlineServicesManager::GetInstance() != nullptr && NGMP_OnlineServicesManager::GetInstance()->IsPendingFullTeardown())
	{
		// Only if not in game and not in anim
		if ((TheNGMPGame == nullptr || !TheNGMPGame->isGameInProgress()) && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
		{
			bool bForceShutdown = true;
			WOLGameSetupMenuShutdown(layout, (void*)&bForceShutdown); // userdata is 'force shutdown'
			TearDownGeneralsOnline();

			TheShell->pop();
			return;
		}
	}
	
	// We'll only be successful if we've requested to
	if(isShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
	{
		shutdownComplete(layout);
		return;
	}

	if (AnticheatPlugInterface::g_bPendingExitLobby)
	{
		AnticheatPlugInterface::g_bPendingExitLobby = false;

        GSMessageBoxOk(TheGameText->fetchOrSubstitute("GUI:ACErrorHeader", L"AntiCheat Error"), TheGameText->fetchOrSubstitute("GUI:ACLobbyIntegrityError", L"Lobby integrity could not be validated. Leaving Lobby."));

        PopBackToLobby();
	}

	if (NGMP_OnlineServicesManager::GetInstance() != nullptr)
	{
		NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		if (pLobbyInterface != nullptr)
		{
			ServiceConfig& serviceConf = NGMP_OnlineServicesManager::GetInstance()->GetServiceConfig();
			bool bHostMigrationEnabledOnService = serviceConf.enable_host_migration;

			if (bHostMigrationEnabledOnService)
			{
				if (pLobbyInterface->m_bHostMigrated)
				{
					pLobbyInterface->m_bHostMigrated = false;

					// If we are in-game, nothing to do here, the game handles it for us
					if (!TheNGMPGame->isGameInProgress()) // in progress is in game, ingame is just in lobby
					{
						// TODO_NGMP: Make sure we did a lobby get first
						// did we become the host?
						bool bIsHost = pLobbyInterface->IsHost();

						if (bIsHost)
						{
							// re init our UI & enable host buttons
							buttonStart->winSetText(TheGameText->fetch("GUI:Start"));
							buttonStart->winEnable(TRUE);
							buttonSelectMap->winEnable(TRUE);
							if (buttonRandomize) buttonRandomize->winEnable(TRUE);
							if (buttonMapVote) buttonMapVote->winEnable(TRUE);
							wolUpdateResumeButton();
							initialAcceptEnable = TRUE;

							comboBoxStartingCash->winEnable(TRUE);
							checkBoxLimitSuperweapons->winEnable(TRUE);
							if (checkBoxEnforceRandom) checkBoxEnforceRandom->winEnable(TRUE);


							NetworkLog(ELogVerbosity::LOG_RELEASE, "Host left and server migrated the host to us...");

							GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"Host: The previous host left. You are now the host."), GameMakeColor(192, 192, 192, 255), -1, -1);

							// NOTE: don't need to mark ourselves ready, the service did it for us upon migration
						}
						else
						{
							GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"Host: The previous host left. A new host was selected."), GameMakeColor(192, 192, 192, 255), -1, -1);
						}

						// re-enable critical buttons for everyone
						if (buttonBack != nullptr)
						{
							buttonBack->winEnable(TRUE);
						}

						if (buttonStart != nullptr)
						{
							buttonStart->winEnable(TRUE);
						}

						GameWindow* buttonBuddy = TheWindowManager->winGetWindowFromId(NULL, NAMEKEY("GameSpyGameOptionsMenu.wnd:ButtonCommunicator"));
						if (buttonBuddy != nullptr)
						{
							buttonBuddy->winEnable(FALSE);
						}
					}

					TheNGMPGame->UpdateSlotsFromCurrentLobby();

					WOLDisplaySlotList();

					// Force a refresh to get latest lobby data
					NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
					if (pLobbyInterface != nullptr)
					{
						pLobbyInterface->UpdateRoomDataCache([](bool bSuccess)
							{

							});
					}
				}
			}

			if (pLobbyInterface->m_bPendingHostHasLeft || pLobbyInterface->m_bHostMigrated)
			{
				pLobbyInterface->m_bHostMigrated = false;
				pLobbyInterface->m_bPendingHostHasLeft = false;

				buttonPushed = true;
				DEBUG_LOG(("Host left lobby\n"));
				if (TheNGMPGame)
					TheNGMPGame->reset();

				GSMessageBoxOk(TheGameText->fetch("GUI:HostLeftTitle"), TheGameText->fetch("GUI:HostLeft"));

				PopBackToLobby();

				return;
			}
		}
	}

#if defined(GENERALS_ONLINE_ENABLE_MATCH_START_COUNTDOWN)
	// is there a countdown in progress?
	if (TheNGMPGame != nullptr)
	{
		if (TheNGMPGame->IsCountdownStarted())
		{
			s_matchStartCountdownWasRunning = true;
			const int64_t timeBetweenChecks = 1000;
			int64_t currTime = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::utc_clock::now().time_since_epoch()).count();

			if (currTime - TheNGMPGame->GetCountdownLastCheckTime() >= timeBetweenChecks)
			{
				int secondsSinceCountdownStart = (currTime - TheNGMPGame->GetCountdownStartTime()) / 1000;
				int secondsRemaining = TheNGMPGame->GetTotalCountdownDuration() - secondsSinceCountdownStart;

				TheNGMPGame->UpdateCountdownLastCheckTime();

				// remote msg
				UnicodeString strInform;
				if (secondsRemaining == 1)
				{
					// Lock all host controlled lobby settings last second of the match start countdown
					// to prevent late local changes not propagating to remote clients in time
					WOLLockSettings();

					strInform.format(TheGameText->fetch("LAN:GameStartTimerSingular"), secondsRemaining);
				}
				else
				{
					strInform.format(TheGameText->fetch("LAN:GameStartTimerPlural"), secondsRemaining);
				}

				NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
				if (pLobbyInterface != nullptr)
				{
					pLobbyInterface->SendAnnouncementMessageToCurrentLobby(strInform, true);
				}

				// are we done?
				if (secondsRemaining <= 0)
				{
					s_matchStartCountdownWasRunning = false;
					// stop countdown
					TheNGMPGame->StopCountdown();

					// send start game packet
					std::shared_ptr<WebSocket>  pWS = NGMP_OnlineServicesManager::GetWebSocket();
					if (pWS != nullptr)
					{
						pWS->SendData_StartGame();
					}
				}
			}
		}
		else
		{
			// countdown is currently NOT running.
			// If it was running before, it just got cancelled or finished.
			if (s_matchStartCountdownWasRunning)
			{
				s_matchStartCountdownWasRunning = false;

				// Re-enable Back and Start buttons when countdown stops
				if (buttonBack != nullptr)
				{
					buttonBack->winEnable(TRUE);
				}

				if (buttonStart != nullptr)
				{
					buttonStart->winEnable(TRUE);
				}
			}

		}
	}
#endif

	if (raiseMessageBoxes)
	{
		RaiseGSMessageBox();
		raiseMessageBoxes = false;
	}

#if defined(GENERALS_ONLINE) // GO needs to tick this, so notifications disappear etc
	HandleBuddyResponses();
#endif

}

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLGameSetupMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	/*
	switch( msg )
	{

		//-------------------------------------------------------------------------------------------------
		case GWM_RIGHT_UP:
		{
			if (buttonPushed)
				break;

			GameWindow *control = (GameWindow *)mData1;
			NameKeyType controlID = (NameKeyType)control->winGetWindowId();
			DEBUG_LOG(("GWM_RIGHT_UP for control %d(%s)", controlID, TheNameKeyGenerator->keyToName(controlID).str()));
			break;
		}

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;
			if (buttonPushed)
				break;

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
																							(WindowMsgData)buttonBack, buttonBackID );
					}
					// don't let key fall through anywhere else
					return MSG_HANDLED;
				}
			}
		}
	}
	*/
	return MSG_IGNORED;
}


// Slash commands -------------------------------------------------------------------------
extern "C" {
int getQR2HostingStatus();
}
extern int isThreadHosting;

Bool handleGameSetupSlashCommands(UnicodeString uText)
{
	AsciiString message;
	message.translate(uText);

	if (message.getCharAt(0) != '/')
	{
		return FALSE; // not a slash command
	}

	AsciiString remainder = message.str() + 1;
	AsciiString token;
	remainder.nextToken(&token);
	token.toLower();

	if (token == "host")
	{
		UnicodeString s;
		s.format(L"Hosting qr2:%d thread:%d", getQR2HostingStatus(), isThreadHosting);
#if !defined(GENERALS_ONLINE)
#else
		GadgetListBoxAddEntryText(listboxGameSetupChat, s, GameSpyColor[GSCOLOR_DEFAULT], -1, -1);
#endif
		return TRUE; // was a slash command
	}
	else if (token == "me" && uText.getLength()>4)
	{
		UnicodeString msg = UnicodeString(uText.str() + 4); // skip the /me
		NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

		if (pLobbyInterface != nullptr)
		{
			pLobbyInterface->SendChatMessageToCurrentLobby(msg, true);
		}
		return TRUE; // was a slash command
	}

#if defined(GENERALS_ONLINE)
	else if (token == "nettest")
	{
		// TheSuperHackers @feature bill-rich 11/10/2026 Network self-test, results uploaded.
		wolLocalSystemChat(NetSelfTest_Start("chat command", TRUE));
		return TRUE; // was a slash command
	}
	else if (token == "resume")
	{
		// TheSuperHackers @feature bill-rich 15/09/2026 resume-from-replay arming lives behind a chat command so no layout change is needed
		AsciiString arg;
		remainder.nextToken(&arg);
		arg.toLower();
		wolTryArmResumeFromReplay(arg == "off");
		return TRUE; // was a slash command
	}
	else if (token == "help" || token == "commands")
	{
		const Color helpColor = GameMakeColor(127, 127, 127, 255);
		GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"/me <message> - Send an emote."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"/resume - Same as the Resume button: continue the host's last recorded match; /resume off cancels (host only)."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"/friendsonly - Let only friends join (host only)."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"/public - Let anyone join (host only)."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"/setpassword <password> - Set a lobby password (host only)."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"/removepassword - Remove the lobby password (host only)."), helpColor, -1, -1);
		GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"/maxcameraheight <value> - Set the camera height limit (host only)."), helpColor, -1, -1);
		// GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"/leave - Return to the main lobby."), helpColor, -1, -1);
		// GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"/quit - Exit the game."), helpColor, -1, -1);
#if !defined(ZULU_GO_SELFHOST_BASE)
		GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"/support - Open the GeneralsOnline Discord."), helpColor, -1, -1);
#endif
		GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"/help - Show these commands. You can also use /commands."), helpColor, -1, -1);
		return TRUE; // was a slash command
	}
#if !defined(ZULU_GO_SELFHOST_BASE)
	else if (token == "support")
	{
		ShellExecuteA(NULL, "open", "https://discord.playgenerals.online", NULL, NULL, SW_SHOWNORMAL);
		return TRUE;
	}
#endif
	else if (token == "friendsonly")
    {
        NGMP_OnlineServicesManager* pOnlineServicesMgr = NGMP_OnlineServicesManager::GetInstance();
		if (pOnlineServicesMgr != nullptr)
		{
			NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

			if (pLobbyInterface != nullptr)
			{
				if (pLobbyInterface->IsInLobby())
				{
					if (pLobbyInterface->IsHost()) // NOTE: this is checked service side too, but we might as well not make the call to reduce resource usage
					{
						GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"Lobby access: Friends only. Use /public to let anyone join."), GameMakeColor(0, 255, 0, 255), -1, -1);
						pLobbyInterface->SetJoinability(ELobbyJoinability::LobbyJoinability_FriendsOnly);
					}
				}
			}
		}
		return TRUE; // was a slash command
	}
    else if (token == "public")
    {
        NGMP_OnlineServicesManager* pOnlineServicesMgr = NGMP_OnlineServicesManager::GetInstance();
        if (pOnlineServicesMgr != nullptr)
        {
            NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

            if (pLobbyInterface != nullptr)
            {
                if (pLobbyInterface->IsInLobby())
                {
					if (pLobbyInterface->IsHost()) // NOTE: this is checked service side too, but we might as well not make the call to reduce resource usage
                    {
						GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"Lobby access: Anyone can join. Use /friendsonly to limit the lobby to friends."), GameMakeColor(0, 255, 0, 255), -1, -1);
						pLobbyInterface->SetJoinability(ELobbyJoinability::LobbyJoinability_Public);
                    }
                }
            }
        }
		return TRUE; // was a slash command
    }
	else if (token == "maxcameraheight" && uText.getLength() > 17)
	{
		NGMP_OnlineServicesManager* pOnlineServicesMgr = NGMP_OnlineServicesManager::GetInstance();
		if (pOnlineServicesMgr != nullptr)
		{
			NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

			if (pLobbyInterface != nullptr)
			{
				if (pLobbyInterface->IsInLobby())
				{
					if (pLobbyInterface->IsHost())
					{
						UnicodeString val = UnicodeString(uText.str() + 17); // skip the command
						
						AsciiString asciiVal;
						asciiVal.translate(val);

						bool bIsNumber = true;

						for (int i = 0; i < asciiVal.getLength(); ++i)
						{
							char thisChar = asciiVal.getCharAt(i);
							if (!std::isdigit((unsigned char)thisChar))
							{
								bIsNumber = false;
								break;
							}
						}

						if (bIsNumber)
						{
							int newCameraHeight = atoi(asciiVal.str());

							if (newCameraHeight < GENERALS_ONLINE_MIN_LOBBY_CAMERA_ZOOM || newCameraHeight > GENERALS_ONLINE_MAX_LOBBY_CAMERA_ZOOM)
							{
								UnicodeString msg;
								msg.format(L"Camera height: Enter a value from %d to %d.", GENERALS_ONLINE_MIN_LOBBY_CAMERA_ZOOM, GENERALS_ONLINE_MAX_LOBBY_CAMERA_ZOOM);
								GadgetListBoxAddEntryText(listboxGameSetupChat, msg, GameMakeColor(255, 0, 0, 255), -1, -1);
								return TRUE; // was a slash command
							}
							else
							{
								// save locally
								NGMP_OnlineServicesManager::Settings.Save_Camera_MaxHeight_WhenLobbyHost((float)newCameraHeight);

								// update lobby
								pLobbyInterface->UpdateCurrentLobbyMaxCameraHeight((uint16_t)newCameraHeight);
							}
						}
						else
						{
							GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"Camera height: Enter a number."), GameMakeColor(255, 0, 0, 255), -1, -1);
							return TRUE; // was a slash command
						}
						
					}
					else
					{
						GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"Camera height: Only the host can change it."), GameMakeColor(255, 0, 0, 255), -1, -1);
						return TRUE; // was a slash command
					}
				}
			}
		}

		return TRUE; // was a slash command
	}
#endif
	// else if (token == "leave")
	// {
	// 	PopBackToLobby();
	// 	return TRUE;
	// }
	// else if (token == "quit")
	// {
	// 	TheGameEngine->setQuitting(TRUE);
	// 	return TRUE;
	// }
	else if (token == "steam" || token == "advnet")
	{
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[ADV NET STATS] Writing advanced networking stats:");

		NetworkMesh* pAdvMesh = NGMP_OnlineServicesManager::GetNetworkMesh();
		if (pAdvMesh == nullptr)
			return TRUE;
		std::map<int64_t, PlayerConnection>& connections = pAdvMesh->GetAllConnections();
		for (auto& kvPair : connections)
		{
			PlayerConnection& conn = kvPair.second;

			NetworkLog(ELogVerbosity::LOG_RELEASE, "[ADV NET STATS] Connection to user %lld: %s", kvPair.first, conn.GetStats().c_str());
		}
		NetworkLog(ELogVerbosity::LOG_RELEASE, "[ADV NET STATS] Advanced networking stats dumped");
		GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"Network debug: Statistics were written to the log file."), GameMakeColor(192, 192, 192, 255), -1, -1);

		return TRUE;
	}
	else if (token == "setpassword" && uText.getLength() > 13)
	{
		UnicodeString newPassword(uText.str() + 13); // skip the /setpassword and space

		NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		NGMP_OnlineServices_AuthInterface* pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
		if (pLobbyInterface != nullptr && pAuthInterface != nullptr)
		{
			LobbyEntry& theLobby = pLobbyInterface->GetCurrentLobby();

			// This is just done clientside to show an error message, server validates it also
			if (theLobby.owner == pAuthInterface->GetUserID())
			{
				if (newPassword.getLength() == 0 || newPassword.getLength() > GENERALS_ONLINE_LOBBY_MAX_PASSWORD_LENGTH)
				{
					UnicodeString errorMsg;
					errorMsg.format(L"Lobby password: Use 1 to %d characters.", GENERALS_ONLINE_LOBBY_MAX_PASSWORD_LENGTH);
					GadgetListBoxAddEntryText(listboxGameSetupChat, errorMsg, GameMakeColor(255, 0, 0, 255), -1, -1);
				}
				else
				{
					std::shared_ptr<WebSocket>  pWS = NGMP_OnlineServicesManager::GetWebSocket();
					if (pWS != nullptr)
					{
						pWS->SendData_ChangeLobbyPassword(newPassword);

						GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"Lobby password: Updated. Use /removepassword to remove it."), GameMakeColor(0, 255, 0, 255), -1, -1);
					}
				}
			}
			else
			{
				GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"Lobby password: Only the host can change it."), GameMakeColor(255, 0, 0, 255), -1, -1);
			}
		}

		return TRUE; // was a slash command
	}
	else if (token == "removepassword" || token == "clearpassword" || token == "resetpassword")
	{
		NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		NGMP_OnlineServices_AuthInterface* pAuthInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
		if (pLobbyInterface != nullptr && pAuthInterface != nullptr)
		{
			LobbyEntry& theLobby = pLobbyInterface->GetCurrentLobby();

			// This is just done clientside to show an error message, server validates it also
			if (theLobby.owner == pAuthInterface->GetUserID())
			{
				if (theLobby.passworded)
				{
					std::shared_ptr<WebSocket>  pWS = NGMP_OnlineServicesManager::GetWebSocket();
					if (pWS != nullptr)
					{
						pWS->SendData_RemoveLobbyPassword();

						GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"Lobby password: Removed. Use /setpassword <password> to add one."), GameMakeColor(0, 255, 0, 255), -1, -1);
					}
				}
				else
				{
					GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"Lobby password: No password is set. Use /setpassword <password> to add one."), GameMakeColor(192, 192, 192, 255), -1, -1);
				}
			}
			else
			{
				GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"Lobby password: Only the host can change it."), GameMakeColor(255, 0, 0, 255), -1, -1);
			}
		}

		return TRUE; // was a slash command
	}
#if defined(RTS_DEBUG)
	else if (token == "slots")
	{
		g_debugSlots = !g_debugSlots;
#if !defined(GENERALS_ONLINE)		
		TheGameSpyInfo->addText(L"Toggled SlotList debug", GameSpyColor[GSCOLOR_DEFAULT], NULL);
#endif
		return TRUE; // was a slash command
	}
	else if (token == "discon")
	{
		PeerRequest req;
		req.peerRequestType = PeerRequest::PEERREQUEST_LOGOUT;
		TheGameSpyPeerMessageQueue->addRequest( req );
		return TRUE;
	}
#endif // defined(RTS_DEBUG)

#if defined(GENERALS_ONLINE)
	GadgetListBoxAddEntryText(listboxGameSetupChat, UnicodeString(L"Unknown command: Use /help to see all commands."), GameSpyColor[GSCOLOR_CHAT_NORMAL], -1, -1);
	return TRUE;
#else
	return FALSE;
#endif
}

static Int getNextSelectablePlayer(Int start)
{
#if !defined(GENERALS_ONLINE)
#else
	NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
	if (pLobbyInterface == nullptr)
	{
		return -1;
	}

	NGMPGame* game = pLobbyInterface->GetCurrentGame();
#endif
	if (game == nullptr || !game->amIHost())
		return -1;
	for (Int j=start; j<MAX_SLOTS; ++j)
	{
#if !defined(GENERALS_ONLINE)
#else
		NGMPGameSlot *slot = game->getGameSpySlot(j);
#endif

		if (slot && slot->getStartPos() == -1 &&
			( (j==game->getLocalSlotNum() && game->getConstSlot(j)->getPlayerTemplate()!=PLAYERTEMPLATE_OBSERVER)
			|| slot->isAI()))
		{
			return j;
		}
	}
	return -1;
}

static Int getFirstSelectablePlayer(const GameInfo *game)
{
	const GameSlot *slot = game->getConstSlot(game->getLocalSlotNum());
	if (!game->amIHost() || slot && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
		return game->getLocalSlotNum();

	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		slot = game->getConstSlot(i);
		if (slot && slot->isAI())
			return i;
	}

	return game->getLocalSlotNum();
}

//-------------------------------------------------------------------------------------------------
/** WOL Game Options menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLGameSetupMenuSystem( GameWindow *window, UnsignedInt msg,
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;

	static int buttonCommunicatorID = NAMEKEY_INVALID;
	switch( msg )
	{
		//-------------------------------------------------------------------------------------------------
		case GWM_CREATE:
			{
				buttonCommunicatorID = NAMEKEY("GameSpyGameOptionsMenu.wnd:ButtonCommunicator");
				break;
			}
		//-------------------------------------------------------------------------------------------------
		case GWM_DESTROY:
			{
				if (windowMap)
					windowMap->winSetUserData(NULL);

				break;
			}
		//-------------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
			{
				// if we're givin the opportunity to take the keyboard focus we must say we want it
				if( mData1 == TRUE )
					*(Bool *)mData2 = TRUE;

				return MSG_HANDLED;
			}
		//-------------------------------------------------------------------------------------------------
		case GCM_SELECTED:
		{
			if (!initDone)
				break;
			if (buttonPushed)
				break;
			GameWindow* control = (GameWindow*)mData1;
			Int controlID = control->winGetWindowId();

			NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
			if (pLobbyInterface == nullptr)
			{
				break;
			}
			
			NGMPGame* myGame = pLobbyInterface->GetCurrentGame();

			if (myGame == nullptr)
			{
				break;
			}

			if (controlID == comboBoxStartingCashID)
			{
				handleStartingCashSelection();
			}
			else
			{
				for (Int i = 0; i < MAX_SLOTS; i++)
				{
					if (controlID == comboBoxColorID[i])
					{
						handleColorSelection(i);
					}
					else if (controlID == comboBoxPlayerTemplateID[i])
					{
						handlePlayerTemplateSelection(i);
					}
					else if (controlID == comboBoxTeamID[i])
					{
						handleTeamSelection(i);
					}
					else if (controlID == comboBoxPlayerID[i] && pLobbyInterface->IsHost())
					{
						// We don't have anything that'll happen if we click on ourselves
						if (i == myGame->getLocalSlotNum())
							break;
						// Get
						Int pos = -1;
						GadgetComboBoxGetSelectedPos(comboBoxPlayer[i], &pos);
						// Combo lays out Open/Closed/Tactical/Hard/Medium/Easy at positions 0..5
						// (AI tiers strongest-first). slotStateFromLobbyComboPos translates the
						// selected position into the corresponding SlotState. The retail guard
						// here was `pos != SLOT_PLAYER`, which only ever kept a five-entry combo
						// from selecting past its end; a sixth entry collided with it and
						// silently dropped every Tactical AI click.
						if (pos >= 0)
						{
							SlotState newState = slotStateFromLobbyComboPos(pos);
							if (myGame->getSlot(i)->getState() == SLOT_PLAYER)
							{
								// TODO_NGMP: Support kick again
								/*
								PeerRequest req;
								req.peerRequestType = PeerRequest::PEERREQUEST_UTMPLAYER;
								req.UTM.isStagingRoom = TRUE;
								AsciiString aName;
								aName.translate(myGame->getSlot(i)->getName());
								req.nick = aName.str();
								req.id = "KICK/";
								req.options = "true";
								TheGameSpyPeerMessageQueue->addRequest(req);
								*/

								UnicodeString name = myGame->getSlot(i)->getName();

								// NOTE: No host check here, service enforces it
								NGMPGameSlot* pSlot = (NGMPGameSlot*)myGame->getSlot(i);
								int64_t userBeingKicked = pSlot->m_userID;

								pLobbyInterface->UpdateCurrentLobby_KickUser(userBeingKicked, name);
								pLobbyInterface->UpdateCurrentLobby_SetSlotState(i, newState);  // use what host selected
								myGame->getSlot(i)->setState(newState);
								myGame->resetAccepted();

								// // TODO_NGMP
								//TheGameSpyInfo->setGameOptions();
								if (TheNGMPGame && TheNGMPGame->IsCountdownStarted())
									TheNGMPGame->StopCountdown();
								WOLDisplaySlotList();
								//TheLAN->OnPlayerLeave(name);
							}
							else if (myGame->getSlot(i)->getState() != newState)
							{
								Bool wasAI = (myGame->getSlot(i)->isAI());
								myGame->getSlot(i)->setState(newState);
								Bool isAI = (myGame->getSlot(i)->isAI());
								myGame->resetAccepted();
								if (TheNGMPGame && TheNGMPGame->IsCountdownStarted())
									TheNGMPGame->StopCountdown();
								if (wasAI ^ isAI)
									PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, myGame, wasAI && myGame->getAllowObservers());

								// inform service
								NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
								if (pLobbyInterface != nullptr)
								{
									pLobbyInterface->UpdateCurrentLobby_SetSlotState(i, newState);
								}

								WOLDisplaySlotList();
							}
						}
						break;
					}
				}
			}
			}
		//-------------------------------------------------------------------------------------------------
		case GBM_SELECTED:
			{
				if (buttonPushed)
					break;

				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				static int buttonCommunicatorID = NAMEKEY("GameSpyGameOptionsMenu.wnd:ButtonCommunicator");

				if ( controlID == buttonBackID )
				{
					savePlayerInfo();
					if( WOLMapSelectLayout )
					{
						WOLMapSelectLayout->destroyWindows();
						deleteInstance(WOLMapSelectLayout);
						WOLMapSelectLayout = NULL;
					}

					
					PopBackToLobby();

				}
				else if ( controlID == buttonCommunicatorID )
				{
					GameSpyToggleOverlay( GSOVERLAY_BUDDY );

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
					{
						NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
						if (pLobbyInterface != nullptr)
						{
							pLobbyInterface->SendChatMessageToCurrentLobby(txtInput, false);
						}
					}
				}
				else if ( controlID == buttonSelectMapID )
				{
					WOLMapSelectLayout = TheWindowManager->winCreateLayout( "Menus/WOLMapSelectMenu.wnd" );
					WOLMapSelectLayout->runInit();
					WOLMapSelectLayout->hide( FALSE );
					WOLMapSelectLayout->bringForward();
				}
				else if ( controlID == buttonMapVoteID )
				{
					handleOnlineMapVoteClick();
				}
				else if ( controlID == buttonResumeID )
				{
					// TheSuperHackers @feature bill-rich 11/10/2026 Same as /resume and /resume off.
					NGMP_OnlineServices_LobbyInterface* pResumeLobby = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
					NGMPGame *resumeGame = pResumeLobby ? pResumeLobby->GetCurrentGame() : nullptr;
					wolTryArmResumeFromReplay(resumeGame != nullptr && resumeGame->isResumeArmed());
				}
				else if ( controlID == buttonRandomizeID )
				{
					// TheSuperHackers @feature bill-rich 15/09/2026 host-only Randomize. The lobby service owns the
					// slots, so the roll is computed on a scratch copy, only the slots it changed are sent, and
					// the room is told once the service has accepted them; the next lobby push shows the result.
					NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
					if (pLobbyInterface != nullptr && pLobbyInterface->IsHost())
					{
						NGMPGame* game = pLobbyInterface->GetCurrentGame();
						if (game)
						{
							GameSlot saved[MAX_SLOTS];
							Int i;
							for (i = 0; i < MAX_SLOTS; ++i)
							{
								if (game->getSlot(i))
									saved[i] = *game->getSlot(i);
							}
							// Balance first, on the same scratch copy, so the roll hands
							// factions to teams that are already settled. A no-op when the
							// teams are set by hand; a network failure is announced and the
							// roll still happens.
							AsciiString balanceErr;
							if (!tryBalanceTeamsViaApi(game, &balanceErr))
							{
								UnicodeString errU;
								errU.translate(balanceErr);
								pLobbyInterface->SendAnnouncementMessageToCurrentLobby(errU, true);
							}

							std::vector<RandomSlotAssignment> changes;
							performRandomAssign(game, &changes);

							// The roll reports what it changed; the balance wrote teams
							// straight into the slots, so fold those in against the
							// snapshot. Without this the service never hears about them and
							// the next lobby push puts everyone back on no team.
							for (i = 0; i < MAX_SLOTS; ++i)
							{
								GameSlot *slot = game->getSlot(i);
								if (slot == NULL || slot->getTeamNumber() == saved[i].getTeamNumber())
									continue;

								size_t c;
								Bool folded = FALSE;
								for (c = 0; c < changes.size(); ++c)
								{
									if (changes[c].slotIndex == i)
									{
										changes[c].team = slot->getTeamNumber();
										folded = TRUE;
										break;
									}
								}
								if (!folded)
								{
									RandomSlotAssignment teamOnly;
									teamOnly.slotIndex = i;
									teamOnly.team = slot->getTeamNumber();
									changes.push_back(teamOnly);
								}
							}

							// The lineup is final here, so this is the picture worth posting.
							// Host-side only, so one image per roll rather than one per
							// client, and a no-op on a build with no webhook baked in. It
							// runs before the slots are restored, so the image shows the roll.
							PostLobbyMapToDiscord(game);

							for (i = 0; i < MAX_SLOTS; ++i)
							{
								if (game->getSlot(i))
									*game->getSlot(i) = saved[i];
							}

							if (changes.empty())
							{
								UnicodeString strNothing = TheGameText->FETCH_OR_SUBSTITUTE("GUI:RandomizeNothingToDo", L"Randomize: every slot already has a faction, color and start position");
								pLobbyInterface->SendAnnouncementMessageToCurrentLobby(strNothing, true);
							}
							else
							{
								// The button is held down only while the request is in flight, and goes back to
								// whatever it was when the roll started: a stats match or a guest must not end
								// up with it enabled.
								const Bool bWasEnabled = (buttonRandomize != NULL) && BitIsSet(buttonRandomize->winGetStatus(), WIN_STATUS_ENABLED);
								if (buttonRandomize)
									buttonRandomize->winEnable(FALSE);
								pLobbyInterface->UpdateCurrentLobby_BulkSlotUpdate(changes, [pLobbyInterface, bWasEnabled](bool bSuccess)
									{
										if (bSuccess)
										{
											++s_randomizeCount;
											UnicodeString strInform;
											strInform.format(TheGameText->FETCH_OR_SUBSTITUTE("GUI:HostRandomizedSlots", L"Randomize: the host resolved the random factions, colors and start positions (roll %d)"), s_randomizeCount);
											pLobbyInterface->SendAnnouncementMessageToCurrentLobby(strInform, true);
										}
										else if (TheNGMPGame != nullptr)
										{
											TheNGMPGame->UpdateSlotsFromCurrentLobby();
											WOLDisplaySlotList();
										}
										if (buttonRandomize)
											buttonRandomize->winEnable(bWasEnabled);
									});
							}
						}
					}
				}
				else if ( controlID == buttonStartID )
				{
					savePlayerInfo();
					
					NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
					if (pLobbyInterface == nullptr)
					{
						break;
					}

					bool bIsHost = pLobbyInterface->IsHost();
					if (bIsHost)
					{
						StartPressed();
					}
					else
					{
						//I'm the Client... send an accept message to the host.
						auto game = pLobbyInterface->GetCurrentGame();
						if (game == nullptr)
						{
							// Oct 17 2026 crash: Accept pressed right after the mesh gave up on a
							// peer and the lobby leave tore the game down (Tytan, 2.0.6).
							break;
						}
						GameSlot *localSlot = game->getSlot(game->getLocalSlotNum());
						if (localSlot)
						{
							localSlot->setAccept();
						}

						// force a refresh of our local lobby properties to sync to remote players
						pLobbyInterface->ApplyLocalUserPropertiesToCurrentNetworkRoom();

						/*
						UnicodeString hostName = game->getSlot(0)->getName();
						AsciiString asciiName;
						asciiName.translate(hostName);
						PeerRequest req;
						req.peerRequestType = PeerRequest::PEERREQUEST_UTMPLAYER;
						req.UTM.isStagingRoom = TRUE;
						req.id = "accept";
						req.nick = asciiName.str();
						req.options = "true";
						TheGameSpyPeerMessageQueue->addRequest(req);
						//peerSetReady( PEER, PEERTrue );
						WOLDisplaySlotList();
						*/
					}
				}
        else if ( controlID == checkBoxEnforceRandomID )
        {
          handleEnforceRandomClick();
        }
        else if ( controlID == checkBoxLimitSuperweaponsID )
        {
          handleLimitSuperweaponsClick();
        }
				else
				{
					NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
					if (pLobbyInterface == nullptr)
					{
						break;
					}

					NGMPGame* game = pLobbyInterface->GetCurrentGame();
					if (game == nullptr)
					{
						break;
					}

					for (Int i = 0; i < MAX_SLOTS; i++)
					{
						if (controlID == buttonMapStartPositionID[i])
						{
							NGMPGame* game = pLobbyInterface->GetCurrentGame();
							if (game == nullptr)
								break;	// lobby torn down while the screen was still up
							Int playerIdxInPos = -1;
							for (Int j=0; j<MAX_SLOTS; ++j)
							{
								NGMPGameSlot *slot = game->getGameSpySlot(j);
								if (slot && slot->getStartPos() == i)
								{
									playerIdxInPos = j;
									break;
								}
							}
							if (playerIdxInPos >= 0)
							{
								NGMPGameSlot *slot = game->getGameSpySlot(playerIdxInPos);
								if (playerIdxInPos == game->getLocalSlotNum() || (game->amIHost() && slot && slot->isAI()))
								{
									// it's one of my type.  Try to change it.
									Int nextPlayer = getNextSelectablePlayer(playerIdxInPos+1);
									handleStartPositionSelection(playerIdxInPos, -1);
									if (nextPlayer >= 0)
									{
										handleStartPositionSelection(nextPlayer, i);
									}
								}
							}
							else
							{
								// nobody in the slot - put us in
								Int nextPlayer = getNextSelectablePlayer(0);
								if (nextPlayer < 0)
									nextPlayer = getFirstSelectablePlayer(game);
								handleStartPositionSelection(nextPlayer, i);
							}
						}
					}
				}


				break;
			}
		//-------------------------------------------------------------------------------------------------
		case GBM_SELECTED_RIGHT:
   		{
   			if (buttonPushed)
   				break;

   			GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
				if (pLobbyInterface == nullptr)
				{
					break;
				}

				NGMPGame* game = pLobbyInterface->GetCurrentGame();
				if (game == nullptr)
				{
					break;
				}

				for (Int i = 0; i < MAX_SLOTS; i++)
				{
					if (controlID == buttonMapStartPositionID[i])
					{
						NGMPGame* game = pLobbyInterface->GetCurrentGame();
						if (game == nullptr)
							break;	// lobby torn down while the screen was still up
						Int playerIdxInPos = -1;
						for (Int j=0; j<MAX_SLOTS; ++j)
						{
							NGMPGameSlot *slot = game->getGameSpySlot(j);
							if (slot && slot->getStartPos() == i)
							{
								playerIdxInPos = j;
								break;
							}
						}
						if (playerIdxInPos >= 0)
						{
							NGMPGameSlot *slot = game->getGameSpySlot(playerIdxInPos);
							if (playerIdxInPos == game->getLocalSlotNum() || (game->amIHost() && slot && slot->isAI()))
							{
								// it's one of my type.  Remove it.
								handleStartPositionSelection(playerIdxInPos, -1);
							}
						}
					}
				}
				break;
			}

		//-------------------------------------------------------------------------------------------------
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
					{
						if (!handleGameSetupSlashCommands(txtInput))
						{
							NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
							if (pLobbyInterface != nullptr)
							{
								pLobbyInterface->SendChatMessageToCurrentLobby(txtInput, false);
							}
						}
					}

				}
				break;
			}
		//-------------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}


void OnKickedFromLobby()
{
	// can't see ourselves
	buttonPushed = true;

	if (TheNGMPGame != nullptr)
	{
		TheNGMPGame->reset();
	}

	GSMessageBoxOk(TheGameText->fetch("GUI:GSErrorTitle"), TheGameText->fetch("GUI:GSKicked"));
	nextScreen = "Menus/WOLCustomLobby.wnd";
	TheShell->pop();
}
