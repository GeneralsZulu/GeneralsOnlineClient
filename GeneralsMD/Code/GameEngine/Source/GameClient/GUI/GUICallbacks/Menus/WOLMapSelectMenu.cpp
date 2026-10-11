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

// FILE: WOLMapSelectMenu.cpp ////////////////////////////////////////////////////////////////////////
// Author: Matt Campbell, December 2001
// Description: MapSelect menu window callbacks
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/CustomMatchPreferences.h"
#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/MessageStream.h"
#include "Common/StatsUploader.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetRadioButton.h"
#include "GameClient/Shell.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GameText.h"
#include "GameNetwork/GameSpy/PeerDefs.h"
#include "GameNetwork/GameSpyOverlay.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/GUIUtil.h"


// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static NameKeyType buttonBack = NAMEKEY_INVALID;
static NameKeyType buttonOK = NAMEKEY_INVALID;
static NameKeyType listboxMap = NAMEKEY_INVALID;
static GameWindow *parent = nullptr;
static Bool raiseMessageBoxes = FALSE;
static GameWindow *winMapPreview = nullptr;
static NameKeyType winMapPreviewID = NAMEKEY_INVALID;

static NameKeyType radioButtonSystemMapsID = NAMEKEY_INVALID;
static NameKeyType radioButtonUserMapsID = NAMEKEY_INVALID;

extern WindowLayout *WOLMapSelectLayout;			///< Map selection overlay
static GameWindow *mapList = nullptr;

// TheSuperHackers @feature bill-rich 27/09/2026 Search box and player-count
// dropdown, the same pair the LAN map select has had. The online list is the
// longer of the two -- every official map plus everything the map downloader
// has ever dropped in -- so scrolling it blind was the worse of the two.
static NameKeyType textEntrySearchID = NAMEKEY_INVALID;
static NameKeyType comboBoxFilterPlayersID = NAMEKEY_INVALID;
static GameWindow *textEntrySearch = nullptr;
static GameWindow *comboBoxFilterPlayers = nullptr;

// Index 0 is "Any" (filter value 0), entries 1..7 are 2..8 players. The value
// rides on each entry as item data so the callback never parses display text.
static const Int FILTER_PLAYER_COUNT_MIN = 2;
static const Int FILTER_PLAYER_COUNT_MAX = 8;

static Int getCurrentPlayerCountFilter()
{
	if (!comboBoxFilterPlayers)
		return 0;
	Int selIndex = -1;
	GadgetComboBoxGetSelectedPos(comboBoxFilterPlayers, &selIndex);
	if (selIndex < 0)
		return 0;
	return (Int)GadgetComboBoxGetItemData(comboBoxFilterPlayers, selIndex);
}

static UnicodeString getCurrentNameFilter()
{
	if (!textEntrySearch)
		return UnicodeString::TheEmptyString;
	return GadgetTextEntryGetText(textEntrySearch);
}

static GameWindow *buttonMapStartPosition[MAX_SLOTS] = {0};
static NameKeyType buttonMapStartPositionID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
																									NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID,
																										NAMEKEY_INVALID,NAMEKEY_INVALID };

static GameWindow *winMapWindow = nullptr;

#include "GameNetwork/GeneralsOnline/NGMP_interfaces.h"
#include "GameNetwork/GeneralsOnline/NGMPGame.h"
extern NGMPGame* TheNGMPGame;

// TheSuperHackers @feature bill-rich 02/10/2026 One list, like the LAN screen:
// official and user maps together, searched together. The screen used to show one
// side of the System/User split at a time, which made a search miss any map on the
// other side -- and the split only ever existed to express "a stats match is
// official maps only", a restriction this build does not have. The two radio
// buttons are hidden at init; the code below still answers them so an older
// layout that shows them keeps working (both then mean "show everything").
static void repopulateMapList()
{
	if (!mapList)
		return;
	if (TheMapCache)
		TheMapCache->updateCache();
	populateMapListboxFiltered( mapList, TRUE, getCurrentNameFilter(),
	                            getCurrentPlayerCountFilter(),
	                            TheNGMPGame->getMap(),
	                            MAPORIGIN_ANY );
}


static void NullifyControls(void)
{
	parent = nullptr;
	mapList = nullptr;
	textEntrySearch = nullptr;
	comboBoxFilterPlayers = nullptr;
	if (winMapPreview)
	{
		winMapPreview->winSetUserData(nullptr);
		winMapPreview = nullptr;
	}
	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		buttonMapStartPosition[i] = nullptr;
	}
}

static const char *layoutFilename = "GameSpyGameOptionsMenu.wnd";
static const char *parentName = "GameSpyGameOptionsMenuParent";
static const char *gadgetsToHide[] =
{
	"MapWindow",
	//"StaticTextGameName",
	"StaticTextTeam",
	"StaticTextFaction",
	"StaticTextColor",
	"TextEntryMapDisplay",
	"ButtonSelectMap",
	"ButtonStart",
	"StaticTextMapPreview",

	// TheSuperHackers @bugfix bill-rich 27/09/2026 The panel grew downward to
	// hold the search row, so it now covers the lobby's options row; and
	// Randomize moved into the bottom button row, where it used to be Start
	// alone. Both would otherwise show through the panel and still take clicks.
	"CheckboxLimitSuperweapons",
	"StartingCashLabel",
	"ComboBoxStartingCash",
	"CheckboxEnforceRandom",
	"CheckBoxLimitArmies",
	"ButtonRandomize",
	// Radarvan Pick took Record Stats' place in that row, so it is covered too.
	"ButtonMapVote",

	nullptr
};
static const char *perPlayerGadgetsToHide[] =
{
	"ComboBoxTeam",
	"ComboBoxColor",
	"ComboBoxPlayerTemplate",
	//"ButtonStartPosition",
	nullptr
};
void positionStartSpots( AsciiString mapName, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);

static void showGameSpyGameOptionsUnderlyingGUIElements( Bool show )
{
	ShowUnderlyingGUIElements( show, layoutFilename, parentName, gadgetsToHide, perPlayerGadgetsToHide );
	GameWindow *win	= TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey("GameSpyGameOptionsMenu.wnd:ButtonBack") );
	if(win)
		win->winEnable( show );
}

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////


//-------------------------------------------------------------------------------------------------
/** Initialize the MapSelect menu */
//-------------------------------------------------------------------------------------------------
void WOLMapSelectMenuInit( WindowLayout *layout, void *userData )
{

	// set keyboard focus to main parent
	NameKeyType parentID = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:WOLMapSelectMenuParent" );
	parent = TheWindowManager->winGetWindowFromId( nullptr, parentID );

	TheWindowManager->winSetFocus( parent );

	winMapPreviewID = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:WinMapPreview" );
	winMapPreview = TheWindowManager->winGetWindowFromId(parent, winMapPreviewID);

#if !defined(GENERALS_ONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES)
	// Which side of the System/User split to preselect. Only the stats-match build
	// still has a split to preselect; the list this build shows holds both.
	CustomMatchPreferences pref;
	Bool usesSystemMapDir = pref.usesSystemMapDir();
	const MapMetaData *mmd = TheMapCache->findMap(TheNGMPGame->getMap());
	if (mmd)
	{
		usesSystemMapDir = mmd->m_isOfficial;
	}
	bool bUseStats = TheNGMPGame->getUseStats();
	//if stats are enabled, only official maps can be used
	if(bUseStats)
		usesSystemMapDir = true;
#endif

	buttonBack = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:ButtonBack" );
	buttonOK = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:ButtonOK" );
	listboxMap = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:ListboxMap" );
	radioButtonSystemMapsID = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:RadioButtonSystemMaps" );
	radioButtonUserMapsID = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:RadioButtonUserMaps" );
	textEntrySearchID = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:TextEntrySearch" );
	comboBoxFilterPlayersID = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:ComboBoxFilterPlayers" );
	winMapWindow = TheWindowManager->winGetWindowFromId( parent, listboxMap );

	// Both are absent from an older layout, so every use is guarded.
	textEntrySearch = TheWindowManager->winGetWindowFromId(parent, textEntrySearchID);
	if (textEntrySearch)
		GadgetTextEntrySetText(textEntrySearch, UnicodeString::TheEmptyString);

	comboBoxFilterPlayers = TheWindowManager->winGetWindowFromId(parent, comboBoxFilterPlayersID);
	if (comboBoxFilterPlayers)
	{
		GadgetComboBoxReset(comboBoxFilterPlayers);
		const Color white = GameMakeColor(255, 255, 255, 255);

		// Index 0: "Any" -- filter value 0 disables the player-count check.
		Int idx = GadgetComboBoxAddEntry(comboBoxFilterPlayers,
			TheGameText->fetch("GUI:FilterPlayersAny"), white);
		GadgetComboBoxSetItemData(comboBoxFilterPlayers, idx, (void *)0);

		const UnicodeString fmt = TheGameText->fetch("GUI:FilterPlayersFmt");
		Int n;
		for (n = FILTER_PLAYER_COUNT_MIN; n <= FILTER_PLAYER_COUNT_MAX; ++n)
		{
			UnicodeString label;
			label.format(fmt.str(), n);
			idx = GadgetComboBoxAddEntry(comboBoxFilterPlayers, label, white);
			GadgetComboBoxSetItemData(comboBoxFilterPlayers, idx, (void *)n);
		}
		GadgetComboBoxSetSelectedPos(comboBoxFilterPlayers, 0);
	}

#if !defined(GENERALS_ONLINE_ALLOW_ALL_SETTINGS_FOR_STATS_MATCHES)
	GameWindow* radioButtonSystemMaps = TheWindowManager->winGetWindowFromId(parent, radioButtonSystemMapsID);
	GameWindow* radioButtonUserMaps = TheWindowManager->winGetWindowFromId(parent, radioButtonUserMapsID);

	if (bUseStats)
	{	//disable unofficial maps if stats are being recorded
		GadgetRadioSetSelection( radioButtonSystemMaps, FALSE );
		radioButtonUserMaps->winEnable( FALSE );
	}
	else if (usesSystemMapDir)
		GadgetRadioSetSelection( radioButtonSystemMaps, FALSE );
	else
		GadgetRadioSetSelection( radioButtonUserMaps, FALSE );
#endif
	AsciiString tmpString;
	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		tmpString.format("WOLMapSelectMenu.wnd:ButtonMapStartPosition%d", i);
		buttonMapStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId( winMapPreview, buttonMapStartPositionID[i] );
		DEBUG_ASSERTCRASH(buttonMapStartPosition[i], ("Could not find the ButtonMapStartPosition[%d]",i ));
		buttonMapStartPosition[i]->winHide(TRUE);
		buttonMapStartPosition[i]->winEnable(FALSE);
	}

	raiseMessageBoxes = TRUE;
	showGameSpyGameOptionsUnderlyingGUIElements( FALSE );

	// get the listbox window
	NameKeyType mapListID = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:ListboxMap" );
	mapList = TheWindowManager->winGetWindowFromId( parent, mapListID );

#if defined(GENERALS_ONLINE)
	// NGMP_CHANGE: Show all the maps in the folder, not just the first 1200. This change could be made in the .WND instead, but code is cleaner.
	// NGMP_BACKPORT: Port this to SH once verified
	GadgetListBoxSetListLength(mapList, TheMapCache->size());
#endif
	
	// One merged list, so the split control is noise: hide it rather than leave two
	// buttons that look like they filter and do not. winEnable too, so a hidden
	// button cannot still take a click.
	{
		GameWindow *radioSystem = TheWindowManager->winGetWindowFromId(parent, radioButtonSystemMapsID);
		GameWindow *radioUser = TheWindowManager->winGetWindowFromId(parent, radioButtonUserMapsID);
		if (radioSystem)
		{
			radioSystem->winHide(TRUE);
			radioSystem->winEnable(FALSE);
		}
		if (radioUser)
		{
			radioUser->winHide(TRUE);
			radioUser->winEnable(FALSE);
		}
	}

	if( mapList )
	{
		repopulateMapList();
	}

}

//-------------------------------------------------------------------------------------------------
/** MapSelect menu shutdown method */
//-------------------------------------------------------------------------------------------------
void WOLMapSelectMenuShutdown( WindowLayout *layout, void *userData )
{
	NullifyControls();

	// hide menu
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout );

}

//-------------------------------------------------------------------------------------------------
/** MapSelect menu update method */
//-------------------------------------------------------------------------------------------------
void WOLMapSelectMenuUpdate( WindowLayout *layout, void *userData )
{

	if (raiseMessageBoxes)
	{
		RaiseGSMessageBox();
		raiseMessageBoxes = false;
	}

	// No update because the game setup screen is up at the same
	// time and it does the update for us...
}

//-------------------------------------------------------------------------------------------------
/** Map select menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLMapSelectMenuInput( GameWindow *window, UnsignedInt msg,
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
						NameKeyType buttonID = TheNameKeyGenerator->nameToKey( "WOLMapSelectMenu.wnd:ButtonBack" );
						GameWindow *button = TheWindowManager->winGetWindowFromId( window, buttonID );

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED,
																								(WindowMsgData)button, buttonID );

					}

					// don't let key fall through anywhere else
					return MSG_HANDLED;

				}

			}

		}

	}

	return MSG_IGNORED;

}
void WOLPositionStartSpots();

//-------------------------------------------------------------------------------------------------
/** MapSelect menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType WOLMapSelectMenuSystem( GameWindow *window, UnsignedInt msg,
																				  WindowMsgData mData1, WindowMsgData mData2 )
{

	switch( msg )
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CREATE:
		{
			break;

		}

		//---------------------------------------------------------------------------------------------
		case GWM_DESTROY:
		{
			NullifyControls();
			break;

		}

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{

			// if we're givin the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			return MSG_HANDLED;

		}

		//---------------------------------------------------------------------------------------------
		case GLM_DOUBLE_CLICKED:
			{
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if( controlID == listboxMap )
				{
					int rowSelected = mData2;

					if (rowSelected >= 0)
					{
						GadgetListBoxSetSelected( control, rowSelected );
						GameWindow *button = TheWindowManager->winGetWindowFromId( window, buttonOK );

						TheWindowManager->winSendSystemMsg( window, GBM_SELECTED,
																								(WindowMsgData)button, buttonOK );
					}
				}
				break;
			}
		//---------------------------------------------------------------------------------------------



			case GLM_SELECTED:
			{

				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				if( controlID == listboxMap )
				{
					int rowSelected = mData2;
					if( rowSelected < 0 )
					{
						positionStartSpots( AsciiString::TheEmptyString, buttonMapStartPosition, winMapPreview);
//						winMapPreview->winClearStatus(WIN_STATUS_IMAGE);
						break;
					}
					winMapPreview->winSetStatus(WIN_STATUS_IMAGE);
					UnicodeString map;
					// get text of the map to load
					map = GadgetListBoxGetText( winMapWindow, rowSelected, 0 );

					// set the map name in the global data map name
					AsciiString asciiMap;
					const char *mapFname = (const char *)GadgetListBoxGetItemData( winMapWindow, rowSelected );
					DEBUG_ASSERTCRASH(mapFname, ("No map item data"));
					if (mapFname)
						asciiMap = mapFname;
					else
						asciiMap.translate( map );
					asciiMap.toLower();
					Image *image = getMapPreviewImage(asciiMap);
					winMapPreview->winSetUserData((void *)TheMapCache->findMap(asciiMap));
					if(image)
					{
						winMapPreview->winSetEnabledImage(0, image);
					}
					else
					{
						winMapPreview->winClearStatus(WIN_STATUS_IMAGE);
					}
					positionStartSpots( asciiMap, buttonMapStartPosition, winMapPreview);
				}
				break;
			}
		// --------------------------------------------------------------------------------------------
		case GEM_UPDATE_TEXT:
		{
			GameWindow *control = (GameWindow *)mData1;
			if (control != nullptr && mapList != nullptr &&
			    control->winGetWindowId() == (Int)textEntrySearchID)
			{
				repopulateMapList();
			}
			break;
		}

		// --------------------------------------------------------------------------------------------
		case GCM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			if (control != nullptr && mapList != nullptr &&
			    control->winGetWindowId() == (Int)comboBoxFilterPlayersID)
			{
				repopulateMapList();
			}
			break;
		}

		//---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();

			if( controlID == buttonBack )
			{
				showGameSpyGameOptionsUnderlyingGUIElements( TRUE );

				if (WOLMapSelectLayout)
				{
					WOLMapSelectLayout->destroyWindows();
					deleteInstance(WOLMapSelectLayout);
					WOLMapSelectLayout = nullptr;
				}

				WOLPositionStartSpots();
			}
			else if ( controlID == radioButtonSystemMapsID )
			{
				// Hidden now (the list always holds both), so this only fires on an
				// older layout. The list does not change; the preference still does.
				CustomMatchPreferences pref;
				pref.setUsesSystemMapDir(TRUE);
				pref.write();
			}
			else if ( controlID == radioButtonUserMapsID )
			{
				CustomMatchPreferences pref;
				pref.setUsesSystemMapDir(FALSE);
				pref.write();
			}
			else if( controlID == buttonOK )
			{
				Int selected;
				UnicodeString map;

				// get the selected index
				GadgetListBoxGetSelected( winMapWindow, &selected );

				if( selected != -1 )
				{

					// get text of the map to load
					map = GadgetListBoxGetText( winMapWindow, selected, 0 );


					// set the map name in the global data map name
					AsciiString asciiMap;
					const char *mapFname = (const char *)GadgetListBoxGetItemData( winMapWindow, selected );
					DEBUG_ASSERTCRASH(mapFname, ("No map item data"));
					if (mapFname)
						asciiMap = mapFname;
					else
						asciiMap.translate( map );

					int newMaxPlayers = -1;
					AsciiString strMapName;
					bool bOfficialMap = false;

					TheNGMPGame->setMap(asciiMap);
					asciiMap.toLower();
					std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(asciiMap);
					if (it != TheMapCache->end())
					{
						TheNGMPGame->getGameSpySlot(0)->setMapAvailability(TRUE);
						TheNGMPGame->setMapCRC( it->second.m_CRC );
						TheNGMPGame->setMapSize( it->second.m_filesize );

						newMaxPlayers = it->second.m_numPlayers;
						strMapName.translate(it->second.m_displayName);

						bOfficialMap = it->second.m_isOfficial;
					}

					TheNGMPGame->adjustSlotsForMap(); // BGC- adjust the slots for the new map.
					TheNGMPGame->resetAccepted();
					TheNGMPGame->resetStartSpots();

					// TheSuperHackers @feature bill-rich 02/10/2026 Push the .map + sidecars
					// to the cncstats store so a lobby member who does not have this map can
					// pull it over HTTP instead of waiting for the P2P transfer at launch
					// (which only covers user maps). No-ops when the URLs are unset or the
					// store already holds this CRC, and the sidecar helpers skip whatever is
					// not on disk here. After setMap, which computes the contents mask.
					if (it != TheMapCache->end() && TheGlobalData != nullptr)
					{
						UploadAllMapAssetsIfMissing(TheGlobalData->m_mapCheckUrl,
							TheGlobalData->m_mapAssetsUrl,
							TheGlobalData->m_mapUploadUrl,
							it->second.m_CRC,
							it->second.m_fileName,
							TheNGMPGame->getMapContentsMask(),
							0 /* no per-game seed in the lobby */);
					}

					// TODO_NGMP:
					//TheGameSpyInfo->setGameOptions();

					WOLDisplaySlotList();
					WOLDisplayGameOptions();

					// NGMP: Update lobby
					NGMP_OnlineServices_LobbyInterface* pLobbyInterface = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();

					if (pLobbyInterface != nullptr)
					{
						pLobbyInterface->UpdateCurrentLobby_Map(strMapName, TheNGMPGame->getMap(), bOfficialMap, newMaxPlayers, TheNGMPGame->getMapCRC());
					}
					

					if (WOLMapSelectLayout)
					{
						WOLMapSelectLayout->destroyWindows();
						deleteInstance(WOLMapSelectLayout);
						WOLMapSelectLayout = nullptr;
					}

					showGameSpyGameOptionsUnderlyingGUIElements( TRUE );

					WOLPositionStartSpots();

				}

			}

			break;

		}

		default:
			return MSG_IGNORED;

	}

	return MSG_HANDLED;

}
