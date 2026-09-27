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
// FILE: LanGameOptionsMenu.cpp
// Author: Chris Huybregts, October 2001
// Description: Lan Game Options Menu
///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine


#include "Common/PlayerTemplate.h"
#include "Common/GameEngine.h"
#include "Common/UserPreferences.h"
#include "Common/QuotedPrintable.h"
#include "Common/StatsUploader.h"
#include "GameClient/AnimateWindowManager.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/Shell.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetCheckBox.h"
#include "GameClient/MapUtil.h"
#include "GameClient/Mouse.h"
#include "GameClient/GameWindowTransitions.h"
#include "GameClient/ChallengeGenerals.h"
#include "GameNetwork/GameSpy/LobbyUtils.h"

#include "GameNetwork/FirewallHelper.h"
#include "GameNetwork/LANAPI.h"
#include "GameNetwork/IPEnumeration.h"
#include "GameNetwork/LANAPICallbacks.h"
#include "Common/MultiplayerSettings.h"
#include "GameClient/GameText.h"
#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/RandomAssign.h"
#include "GameClient/MapUtil.h"
#include "GameNetwork/ResumeFromReplay.h"


extern char *LANnextScreen;
extern Bool LANisShuttingDown;
extern Bool LANbuttonPushed;
extern void MapSelectorTooltip(GameWindow *window, WinInstanceData *instData,	UnsignedInt mouse);
extern void gameAcceptTooltip(GameWindow *window, WinInstanceData *instData, UnsignedInt mouse);
Color white = GameMakeColor( 255, 255, 255, 255 );
static bool s_isIniting = FALSE;

// TRUE while updateGameOptions() pushes game state into the gadgets.
// GadgetCheckBoxSetChecked/GadgetComboBoxSetSelectedPos re-fire the same
// GBM_SELECTED/combo messages a user click does, so without this guard every
// received options string made joiners re-run the click handlers against
// their own copy of the game (2026-08-05: the game started with host and
// joiners holding different settings, and a joiner's copy of an armed resume
// was disarmed by its own redraw).
static bool s_isSyncingUI = FALSE;
// window ids ------------------------------------------------------------------------------
static NameKeyType parentLanGameOptionsID = NAMEKEY_INVALID;

static NameKeyType comboBoxPlayerID[MAX_SLOTS] = { NAMEKEY_INVALID,NAMEKEY_INVALID,
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

static NameKeyType textEntryChatID = NAMEKEY_INVALID;
static NameKeyType textEntryMapDisplayID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;
static NameKeyType buttonStartID = NAMEKEY_INVALID;
static NameKeyType buttonRandomizeID = NAMEKEY_INVALID;
static NameKeyType buttonEmoteID = NAMEKEY_INVALID;
static NameKeyType buttonSelectMapID = NAMEKEY_INVALID;
static NameKeyType buttonMapVoteID = NAMEKEY_INVALID;
static NameKeyType checkboxLimitSuperweaponsID = NAMEKEY_INVALID;
static NameKeyType checkboxEnforceRandomID = NAMEKEY_INVALID;
static NameKeyType comboBoxStartingCashID = NAMEKEY_INVALID;
static NameKeyType windowMapID = NAMEKEY_INVALID;
// Window Pointers ------------------------------------------------------------------------
static GameWindow *parentLanGameOptions = nullptr;
static GameWindow *buttonBack = nullptr;
static GameWindow *buttonStart = nullptr;
static GameWindow *buttonRandomize = nullptr;
// TheSuperHackers @feature bill-rich 15/09/2026 How many times the host pressed Randomize in this
// lobby; announced with every roll so re-rolling is visible to everyone present.
static Int s_randomizeCount = 0;
static GameWindow *buttonSelectMap = nullptr;
static GameWindow *buttonMapVote = nullptr;
static GameWindow *buttonEmote = nullptr;
static GameWindow *textEntryChat = nullptr;
static GameWindow *textEntryMapDisplay = nullptr;
static GameWindow *checkboxLimitSuperweapons = nullptr;
static GameWindow *checkboxEnforceRandom = nullptr;
static GameWindow *comboBoxStartingCash = nullptr;
static GameWindow *windowMap = nullptr;

static GameWindow *comboBoxPlayer[MAX_SLOTS] = {0};
static GameWindow *buttonAccept[MAX_SLOTS] = {0};

static GameWindow *comboBoxColor[MAX_SLOTS] = {0};

static GameWindow *comboBoxPlayerTemplate[MAX_SLOTS] = {0};

static GameWindow *comboBoxTeam[MAX_SLOTS] = {0};

//static GameWindow *buttonStartPosition[MAX_SLOTS] = {0};
//
static GameWindow *buttonMapStartPosition[MAX_SLOTS] = {0};

//external declarations of the Gadgets the callbacks can use
GameWindow *listboxChatWindowLanGame = nullptr;
NameKeyType listboxChatWindowLanGameID = NAMEKEY_INVALID;
WindowLayout *mapSelectLayout = nullptr;

static Int getNextSelectablePlayer(Int start)
{
	LANGameInfo *game = TheLAN->GetMyGame();
	if (!game->amIHost())
		return -1;
	for (Int j=start; j<MAX_SLOTS; ++j)
	{
		LANGameSlot *slot = game->getLANSlot(j);
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
	if (!game->amIHost() || (slot && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER))
		return game->getLocalSlotNum();

	for (Int i=0; i<MAX_SLOTS; ++i)
	{
		slot = game->getConstSlot(i);
		if (slot && slot->isAI())
			return i;
	}

	return game->getLocalSlotNum();
}

void updateMapStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], Bool onLoadScreen = FALSE );
void positionStartSpots( GameInfo *myGame, GameWindow *buttonMapStartPositions[], GameWindow *mapWindow);
void LanPositionStartSpots()
{

	positionStartSpots( TheLAN->GetMyGame(), buttonMapStartPosition, windowMap);
}
static void playerTooltip(GameWindow *window,
													WinInstanceData *instData,
													UnsignedInt mouse)
{
	Int idx = -1;
	Int i=0;
	for (; i<MAX_SLOTS; ++i)
	{
		if (window && window == GadgetComboBoxGetEditBox(comboBoxPlayer[i]))
		{
			idx = i;
			break;
		}
	}
	if (idx == -1)
		return;

	LANGameSlot *slot = TheLAN->GetMyGame()->getLANSlot(idx);
	if (!slot)
		return;

	LANPlayer *player = slot->getUser();
	if (!player)
	{
		DEBUG_ASSERTCRASH(TheLAN->GetMyGame()->getIP(idx) == 0, ("No player info in listbox!"));
		TheMouse->setCursorTooltip( UnicodeString::TheEmptyString );
		return;
	}

	setLANPlayerTooltip(player);
}

static Bool lanArmedResumeStillMatches(LANGameInfo *game);
static void lanDisarmResumeForSettingsChange();
static void lanCheckResumeArmChange(LANGameInfo *game);
void StartPressed()
{
	LANGameInfo *myGame = TheLAN->GetMyGame();

	Bool isReady = true;
	Bool allHaveMap = true;
	Int playerCount = 0;
	if (!myGame)
	{
		return;
	}
	myGame->getLANSlot(0)->setAccept(); // cause we are, of course!

	int i;

	int numUsers = 0;
	int numHumans = 0;
	for (i=0; i<MAX_SLOTS; ++i)
	{
		GameSlot *slot = myGame->getSlot(i);
		if (slot && slot->isOccupied() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER)
		{
			if (slot && slot->isHuman())
				numHumans++;
			numUsers++;
		}
	}

	// Check for too many players
	const MapMetaData *md = TheMapCache->findMap( myGame->getMap() );
	if (!md || md->m_numPlayers < numUsers)
	{
		if (TheLAN->AmIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:TooManyPlayers"), (md)?md->m_numPlayers:0);
			TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
		}
		return;
	}

	// TheSuperHackers @feature bill-rich 15/09/2026 An armed resume-from-replay only works if the lobby still matches the recording.
	if (TheLAN->AmIHost() && myGame->isResumeArmed() && !lanArmedResumeStillMatches(myGame))
	{
		return;
	}

	// Check for observer + AI players
	if (TheGlobalData->m_netMinPlayers && !numHumans)
	{
		if (TheLAN->AmIHost())
		{
			UnicodeString text = TheGameText->fetch("GUI:NeedHumanPlayers");
			TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
		}
		return;
	}

	// Check for too few players
	if (numUsers < TheGlobalData->m_netMinPlayers)
	{
		if (TheLAN->AmIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:NeedMorePlayers"),numUsers);
			TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
		}
		return;
	}

	// Check for too few teams
	int numRandom = 0;
	std::set<Int> teams;
	for (i=0; i<MAX_SLOTS; ++i)
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
		if (TheLAN->AmIHost())
		{
			UnicodeString text;
			text.format(TheGameText->fetch("LAN:NeedMoreTeams"));
			TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
		}
		return;
	}

	if (numRandom + teams.size() < 2)
	{
		UnicodeString text;
		text.format(TheGameText->fetch("GUI:SandboxMode"));
			TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
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
		mapDisplayName.format(L"%hs", myGame->getMap().str());
		willTransfer = WouldMapTransfer(myGame->getMap());
	}
	for( i = 0; i < MAX_SLOTS; i++ )
	{
		LANGameSlot *slot = myGame->getLANSlot(i);
		if( slot->isHuman() && !slot->isAccepted())
		{
			isReady = false;
			if (!willTransfer)
			{
				if (!slot->hasMap())
				{
					UnicodeString msg;
					msg.format(TheGameText->fetch("GUI:PlayerNoMap"), slot->getName().str(), mapDisplayName.str());
					GadgetListBoxAddEntryText(listboxChatWindowLanGame, msg , chatSystemColor, -1, 0);
					allHaveMap = false;
				}
			}
		}
		if( slot->isHuman() && slot->getPlayerTemplate() != PLAYERTEMPLATE_OBSERVER )
			playerCount++;
	}

	if(isReady)
	{
		for( i = 0; i < MAX_SLOTS; i++ )
		{
			LANGameSlot *slot = myGame->getLANSlot(i);
			if (slot && slot->isOpen())
			{
				slot->setState( SLOT_CLOSED );
				GadgetComboBoxSetSelectedPos(comboBoxPlayer[i], SLOT_CLOSED);
			}
		}
		Int seconds = TheMultiplayerSettings->getStartCountdownTimerSeconds();
		if (seconds)
			TheLAN->RequestGameStartTimer(seconds);
		else
			TheLAN->RequestGameStart();
		LANEnableStartButton(false);
	}
	else
	{
		// Does everyone have the map?
		if (allHaveMap)
		{
			GadgetListBoxAddEntryText(listboxChatWindowLanGame, TheGameText->fetch("GUI:NotifiedStartIntent") , chatSystemColor, -1, 0);
			TheLAN->RequestAccept();
		}
	}

}

void LANEnableStartButton(Bool enabled)
{
	buttonStart->winEnable(enabled);
	buttonSelectMap->winEnable(enabled);
}

static void handleColorSelection(int index)
{
	GameWindow *combo = comboBoxColor[index];
	Int color, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	color = (Int)GadgetComboBoxGetItemData(combo, selIndex);

	LANGameInfo *myGame = TheLAN->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(index);
		if (color == slot->getColor())
			return;

		if (color >= -1 && color < TheMultiplayerSettings->getNumColors())
		{
			Bool colorAvailable = TRUE;
			if(color != -1 )
			{
				for(Int i=0; i <MAX_SLOTS; i++)
				{
					LANGameSlot *checkSlot = myGame->getLANSlot(i);
					if(color == checkSlot->getColor() && slot != checkSlot)
					{
						colorAvailable = FALSE;
						break;
					}
				}
			}
			if(!colorAvailable)
				return;
		}

		slot->setColor(color);

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the color from the host
			if (!slot->isLocalPlayer() || !AreSlotListUpdatesEnabled())
				return;

			AsciiString options;
			options.format("Color=%d", color);
			TheLAN->RequestGameOptions(options, true);
		}
	}
}

static void handlePlayerTemplateSelection(int index)
{
	GameWindow *combo = comboBoxPlayerTemplate[index];
	Int playerTemplate, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	playerTemplate = (Int)GadgetComboBoxGetItemData(combo, selIndex);
	LANGameInfo *myGame = TheLAN->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(index);
		if (playerTemplate == slot->getPlayerTemplate())
			return;

		Int oldTemplate = slot->getPlayerTemplate();
		slot->setPlayerTemplate(playerTemplate);

		if (oldTemplate == PLAYERTEMPLATE_OBSERVER)
		{
			// was observer, so populate color & team with all, and enable
			GadgetComboBoxSetSelectedPos(comboBoxColor[index], 0);
			GadgetComboBoxSetSelectedPos(comboBoxTeam[index], 0);
			slot->setStartPos(-1);
		}
		else if (playerTemplate == PLAYERTEMPLATE_OBSERVER)
		{
			// is becoming observer, so populate color & team with random only, and disable
			GadgetComboBoxSetSelectedPos(comboBoxColor[index], 0);
			GadgetComboBoxSetSelectedPos(comboBoxTeam[index], 0);
			slot->setStartPos(-1);
		}

		myGame->resetAccepted();

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the playerTemplate from the host
			if (AreSlotListUpdatesEnabled())
			{
				AsciiString options;
				options.format("PlayerTemplate=%d", playerTemplate);
				TheLAN->RequestGameOptions(options, true);
			}
		}
	}
}

static void handleStartPositionSelection(Int player, int startPos)
{
	LANGameInfo *myGame = TheLAN->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(player);
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
		slot->setStartPos(startPos);

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				myGame->resetAccepted();
				TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the color from the host
			if (AreSlotListUpdatesEnabled())
			{
				AsciiString options;
				options.format("StartPos=%d", slot->getStartPos());
				TheLAN->RequestGameOptions(options, true);
			}
		}
	}
}



static void handleTeamSelection(int index)
{
	GameWindow *combo = comboBoxTeam[index];
	Int team, selIndex;
	GadgetComboBoxGetSelectedPos(combo, &selIndex);
	team = (Int)GadgetComboBoxGetItemData(combo, selIndex);
	LANGameInfo *myGame = TheLAN->GetMyGame();

	if (myGame)
	{
		LANGameSlot * slot = myGame->getLANSlot(index);
		if (team == slot->getTeamNumber())
			return;

		slot->setTeamNumber(team);
		myGame->resetAccepted();

		if (myGame->amIHost())
		{
			if (!s_isIniting)
			{
				// send around a new slotlist
				myGame->resetAccepted();
				TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
				lanUpdateSlotList();
			}
		}
		else
		{
			// request the team from the host
			if (AreSlotListUpdatesEnabled())
			{
				AsciiString options;
				options.format("Team=%d", team);
				TheLAN->RequestGameOptions(options, true);
			}
		}
	}
}

static void handleStartingCashSelection()
{
  if (s_isSyncingUI)
    return;

  LANGameInfo *myGame = TheLAN->GetMyGame();

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
      if (!s_isIniting)
      {
        lanDisarmResumeForSettingsChange();
        // send around the new data
        TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
        lanUpdateSlotList(); // Update the accepted button UI
      }
    }
  }
}

static void handleLimitSuperweaponsClick()
{
  if (s_isSyncingUI)
    return;

  LANGameInfo *myGame = TheLAN->GetMyGame();

  if (myGame)
  {
    // At the moment, 1 and 0 are the only choices supported in the GUI, though the system could
    // support more.
    if ( GadgetCheckBoxIsChecked( checkboxLimitSuperweapons ) )
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
      if (!s_isIniting)
      {
        lanDisarmResumeForSettingsChange();
        // send around a new slotlist
        TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
        lanUpdateSlotList(); // Update the accepted button UI
      }
    }
  }
}

static void handleEnforceRandomClick()
{
  LANGameInfo *myGame = TheLAN->GetMyGame();

  if (myGame)
  {
    Bool checked = GadgetCheckBoxIsChecked( checkboxEnforceRandom );
    myGame->setEnforceRandom( checked );

    if (checked)
    {
      // Snap every occupied, non-observer slot to random faction and random
      // team. Color is left alone. The Randomize button may still assign
      // concrete values afterwards; the dropdowns stay locked until the
      // host unchecks the box.
      for (Int i = 0; i < MAX_SLOTS; i++)
      {
        GameSlot *slot = myGame->getSlot(i);
        if (!slot || !slot->isOccupied())
          continue;
        if (slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
          continue;
        slot->setPlayerTemplate( PLAYERTEMPLATE_RANDOM );
        slot->setTeamNumber( -1 );
      }
    }
    myGame->resetAccepted();

    if (myGame->amIHost())
    {
      if (!s_isIniting)
      {
        // send around a new slotlist
        TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
        lanUpdateSlotList(); // Update the accepted button UI
      }
    }
  }
}

void lanUpdateSlotList()
{
	if(!AreSlotListUpdatesEnabled() || s_isIniting)
		return;
	UpdateSlotList( TheLAN->GetMyGame(), comboBoxPlayer, comboBoxColor,
		comboBoxPlayerTemplate, comboBoxTeam, buttonAccept, buttonStart, buttonMapStartPosition);

	updateMapStartSpots(TheLAN->GetMyGame(), buttonMapStartPosition);
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Gadgets Options Menu */
//-------------------------------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// TheSuperHackers @feature bill-rich 15/09/2026 Resume-from-replay arming (host only).
//
// /resume copies the host's last recording to Resume.rep (ResumeFromReplay::prepareHostSource),
// aligns the lobby with it (slot order, factions, colors, start spots, teams, map, seed,
// starting cash and superweapon limit) and broadcasts the handoff frame in the game options
// (RH=). The file itself is pushed to every guest over the map transfer path when the game
// starts (LANAPI::OnGameStart), where every client validates its copy against the lobby
// before the recorder replays it in lockstep (see RecorderClass).
// -----------------------------------------------------------------------------

static UnsignedInt s_lanLastSeenResumeHandoff = 0; ///< the arm the last game options carried, to notice changes

// Everyone in the lobby sees resume messages: the player who has to fix
// something (wrong name, wrong slot) is usually not the host.
static void lanSystemChat(const UnicodeString &text)
{
	if (TheLAN)
		TheLAN->RequestChat(text, LANAPIInterface::LANCHAT_SYSTEM);
}

static void lanLocalSystemChat(const UnicodeString &text)
{
	if (TheLAN)
		TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), text, LANAPI::LANCHAT_SYSTEM);
}

// Called from StartPressed while a resume is armed. Re-reads the resume source and refuses
// the start when the lobby has drifted from it since arming (someone left, rejoined under
// another name, joined into a recorded slot, or a setting changed).
static Bool lanArmedResumeStillMatches(LANGameInfo *game)
{
	ResumeFromReplay::SourceReplay replay;
	UnicodeString why;
	if (ResumeFromReplay::readSource(replay, why)
		&& ResumeFromReplay::sourceMatchesGame(replay, game, game->getResumeHandoffFrame(), why))
		return TRUE;
	lanSystemChat(TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:ResumeStartRefused",
		L"Resume is armed and the lobby no longer matches the recording (%ls). Fix it, or type /resume off to start a fresh game.", why.str()));
	return FALSE;
}

// Put every human back into the slot they held in the replay and restore the
// recorded faction, color, start position and team on every occupied slot whose kind
// (human, or AI at the recorded difficulty) already matches. Recorded commands are
// indexed by slot position, so the lobby must line up with the recording exactly.
// Slot 0 is never swapped: the LAN protocol treats it as the host's slot and validation
// has already checked the host matches. Slot kinds are never changed here: setState()
// has destructive side effects on LAN slot identity, so a missing or wrong AI is
// reported by the full check afterwards for the host to fix.
static void reorderLobbyForResume(const ReplayGameInfo &replayInfo, LANGameInfo *lobby)
{
	Int i;
	for (i = 1; i < MAX_SLOTS; ++i)
	{
		const GameSlot *rs = replayInfo.getConstSlot(i);
		if (!rs || !rs->isHuman())
			continue;
		Int currentIdx = -1;
		for (Int j = 1; j < MAX_SLOTS; ++j)
		{
			const GameSlot *ls = lobby->getConstSlot(j);
			if (ls && ls->isHuman() && ls->getName().compare(rs->getName()) == 0)
			{
				currentIdx = j;
				break;
			}
		}
		if (currentIdx < 0 || currentIdx == i)
			continue;
		LANGameSlot saved = *lobby->getLANSlot(i);
		lobby->setSlot(i, *lobby->getLANSlot(currentIdx));
		lobby->setSlot(currentIdx, saved);
	}
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		const GameSlot *rs = replayInfo.getConstSlot(i);
		LANGameSlot *ls = lobby->getLANSlot(i);
		if (!rs || !rs->isOccupied() || !ls || !ls->isOccupied() || rs->getState() != ls->getState())
			continue;
		ls->setColor(rs->getColor());
		ls->setPlayerTemplate(rs->getPlayerTemplate());
		ls->setStartPos(rs->getStartPos());
		ls->setTeamNumber(rs->getTeamNumber());
	}
}

static void lanClearResumeArm()
{
	if (TheLAN && TheLAN->GetMyGame())
		TheLAN->GetMyGame()->setResumeHandoffFrame(0);
	s_lanLastSeenResumeHandoff = 0;
}

// "/resume off": disarm and tell the lobby the next start is a fresh game.
static void disarmResumeFromReplay()
{
	if (!TheLAN || !TheLAN->GetMyGame() || !TheLAN->AmIHost())
		return;
	const Bool wasArmed = TheLAN->GetMyGame()->isResumeArmed();
	lanClearResumeArm();
	if (wasArmed)
	{
		TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
		lanSystemChat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeDisarmed", L"Resume disarmed: the next start is a fresh game"));
	}
}

// A host change to a setting the recording fixes (map, starting cash, superweapon limit)
// disarms; the caller sends the game options right after.
static void lanDisarmResumeForSettingsChange()
{
	if (!TheLAN || !TheLAN->GetMyGame() || !TheLAN->AmIHost() || !TheLAN->GetMyGame()->isResumeArmed())
		return;
	lanClearResumeArm();
	lanSystemChat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeDisarmedSettings", L"Resume disarmed: the game settings changed"));
}

// Guests: the arm arrives in the host's game options (RH=). The file itself is sent when the
// game starts, so all a guest can do here is say so, and note a disarm.
static void lanCheckResumeArmChange(LANGameInfo *game)
{
	if (!TheLAN || !game || TheLAN->AmIHost())
		return;
	const UnsignedInt handoff = game->getResumeHandoffFrame();
	if (handoff == s_lanLastSeenResumeHandoff)
		return;
	s_lanLastSeenResumeHandoff = handoff;
	if (handoff != 0)
		lanLocalSystemChat(TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:ResumeArmedGuest",
			L"Resume armed by the host: the replay is sent when the game starts and replays to %ls", ResumeFromReplay::formatGameTime(handoff).str()));
	else
		lanLocalSystemChat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeDisarmed", L"Resume disarmed: the next start is a fresh game"));
}

// Copies the last recorded replay to the resume source, validates it against the current
// lobby, aligns the lobby with it and arms. Reports success or the reason for refusal in
// the lobby chat.
static void tryArmResumeFromReplay()
{
	if (!TheLAN || !TheLAN->GetMyGame() || !TheLAN->AmIHost())
		return;
	LANGameInfo *game = TheLAN->GetMyGame();

	ResumeFromReplay::SourceReplay replay;
	UnsignedInt handoff = 0;
	UnicodeString why;
	if (!ResumeFromReplay::prepareHostSource(replay, handoff, why)
		|| !ResumeFromReplay::validateRoster(replay.game, game, why))
	{
		lanSystemChat(TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:ResumeRefused", L"Resume: %ls", why.str()));
		return;
	}

	reorderLobbyForResume(replay.game, game);
	game->setMap(replay.game.getMap());
	game->setMapCRC(replay.game.getMapCRC());
	game->setMapSize(replay.game.getMapSize());
	game->setSeed(replay.game.getSeed());
	game->setStartingCash(replay.game.getStartingCash());
	game->setSuperweaponRestriction(replay.game.getSuperweaponRestriction());
	game->setResumeHandoffFrame(handoff);

	// The alignment above cannot add or change AI slots; the full check reports what is left for the host to fix.
	if (!ResumeFromReplay::sourceMatchesGame(replay, game, handoff, why))
	{
		lanClearResumeArm();
		TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
		lanUpdateSlotList();
		lanSystemChat(TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:ResumeRefused", L"Resume: %ls", why.str()));
		return;
	}

	// The options travel in one LAN packet; refuse to arm rather than let the field be cut off.
	AsciiString options = GenerateGameOptionsString();
	if (options.getLength() >= m_lanMaxOptionsLength)
	{
		lanClearResumeArm();
		lanSystemChat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeOptionsTooLong", L"Resume: the game options are too long to carry the resume, shorten the player names"));
		return;
	}
	TheLAN->RequestGameOptions(options, true);
	lanUpdateSlotList();
	updateGameOptions();
	s_lanLastSeenResumeHandoff = handoff;

	lanSystemChat(TheGameText->FETCH_OR_SUBSTITUTE_FORMAT("GUI:ResumeArmed",
		L"Resume armed: the next start replays the last match to %ls, then hands control back", ResumeFromReplay::formatGameTime(handoff).str()));
}

void InitLanGameGadgets()
{
	//Initialize the gadget IDs
	parentLanGameOptionsID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:LanGameOptionsMenuParent" );
	buttonBackID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ButtonBack" );
	buttonStartID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ButtonStart" );
	buttonRandomizeID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ButtonRandomize" );
	textEntryChatID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:TextEntryChat" );
	textEntryMapDisplayID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:TextEntryMapDisplay" );
	listboxChatWindowLanGameID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ListboxChatWindowLanGame" );
	buttonEmoteID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ButtonEmote" );
	buttonSelectMapID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ButtonSelectMap" );
	buttonMapVoteID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ButtonMapVote" );
  checkboxLimitSuperweaponsID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:CheckboxLimitSuperweapons" );
  checkboxEnforceRandomID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:CheckboxEnforceRandom" );
  comboBoxStartingCashID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:ComboBoxStartingCash" );
	windowMapID = TheNameKeyGenerator->nameToKey( "LanGameOptionsMenu.wnd:MapWindow" );

	// Initialize the pointers to our gadgets
	parentLanGameOptions = TheWindowManager->winGetWindowFromId( nullptr, parentLanGameOptionsID );
	DEBUG_ASSERTCRASH(parentLanGameOptions, ("Could not find the parentLanGameOptions"));
	buttonEmote = TheWindowManager->winGetWindowFromId( parentLanGameOptions,buttonEmoteID  );
	DEBUG_ASSERTCRASH(buttonEmote, ("Could not find the buttonEmote"));
	buttonSelectMap = TheWindowManager->winGetWindowFromId( parentLanGameOptions,buttonSelectMapID  );
	DEBUG_ASSERTCRASH(buttonSelectMap, ("Could not find the buttonSelectMap"));
	buttonMapVote = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonMapVoteID );
	// Button is optional: only present in builds of the .wnd that include the new entry. Guard access with nullptr checks.
	if (buttonMapVote)
		buttonMapVote->winEnable( TheLAN && TheLAN->AmIHost() );
	buttonStart = TheWindowManager->winGetWindowFromId( parentLanGameOptions,buttonStartID  );
	DEBUG_ASSERTCRASH(buttonStart, ("Could not find the buttonStart"));
	buttonRandomize = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonRandomizeID ); // absent from older .wnd layouts
	if (buttonRandomize)
		buttonRandomize->winSetText(TheGameText->FETCH_OR_SUBSTITUTE("GUI:Randomize", L"Randomize")); // no .csf change needed
	buttonBack = TheWindowManager->winGetWindowFromId( parentLanGameOptions,  buttonBackID);
	DEBUG_ASSERTCRASH(buttonBack, ("Could not find the buttonBack"));
	listboxChatWindowLanGame = TheWindowManager->winGetWindowFromId( parentLanGameOptions, listboxChatWindowLanGameID );
	DEBUG_ASSERTCRASH(listboxChatWindowLanGame, ("Could not find the listboxChatWindowLanGame"));
	textEntryChat = TheWindowManager->winGetWindowFromId( parentLanGameOptions, textEntryChatID );
	DEBUG_ASSERTCRASH(textEntryChat, ("Could not find the textEntryChat"));
	textEntryMapDisplay = TheWindowManager->winGetWindowFromId( parentLanGameOptions, textEntryMapDisplayID );
	DEBUG_ASSERTCRASH(textEntryMapDisplay, ("Could not find the textEntryMapDisplay"));
  checkboxLimitSuperweapons = TheWindowManager->winGetWindowFromId( parentLanGameOptions, checkboxLimitSuperweaponsID );
  DEBUG_ASSERTCRASH(checkboxLimitSuperweapons, ("Could not find the checkboxLimitSuperweapons"));
  // Checkbox is optional: only present in builds of the .wnd that include the new entry. Guard access with nullptr checks.
  checkboxEnforceRandom = TheWindowManager->winGetWindowFromId( parentLanGameOptions, checkboxEnforceRandomID );
  comboBoxStartingCash = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxStartingCashID );
  DEBUG_ASSERTCRASH(comboBoxStartingCash, ("Could not find the comboBoxStartingCash"));
	PopulateStartingCashComboBox(comboBoxStartingCash, TheLAN->GetMyGame());

	windowMap = TheWindowManager->winGetWindowFromId( parentLanGameOptions,windowMapID  );
	DEBUG_ASSERTCRASH(windowMap, ("Could not find the LanGameOptionsMenu.wnd:MapWindow" ));

	Int localSlotNum = TheLAN->GetMyGame()->getLocalSlotNum();
	DEBUG_ASSERTCRASH(localSlotNum >= 0, ("Bad slot number!"));

	//Tooltip function is being set for techBuildings, and supplyDocks
	windowMap->winSetTooltipFunc(MapSelectorTooltip);

	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		AsciiString tmpString;
		tmpString.format("LanGameOptionsMenu.wnd:ComboBoxPlayer%d", i);
		comboBoxPlayerID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxPlayer[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxPlayerID[i] );
		GadgetComboBoxReset(comboBoxPlayer[i]);
		GadgetComboBoxGetEditBox(comboBoxPlayer[i])->winSetTooltipFunc(playerTooltip);

		if(localSlotNum != i)
		{
			// AI tiers run strongest-first, matching the Skirmish lobby.
			// slotStateFromLobbyComboPos / lobbyComboPosFromSlotState (GameInfo.h)
			// are the only translation between a position here and a SlotState.
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:Open"),white);
			GadgetComboBoxSetItemData(comboBoxPlayer[i], 0, (void *)SLOT_OPEN);
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:Closed"),white);
			GadgetComboBoxSetItemData(comboBoxPlayer[i], 1, (void *)SLOT_CLOSED);
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:TacticalAI"),white);
			GadgetComboBoxSetItemData(comboBoxPlayer[i], 2, (void *)SLOT_TACTICAL_AI);
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:HardAI"),white);
			GadgetComboBoxSetItemData(comboBoxPlayer[i], 3, (void *)SLOT_BRUTAL_AI);
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:MediumAI"),white);
			GadgetComboBoxSetItemData(comboBoxPlayer[i], 4, (void *)SLOT_MED_AI);
			GadgetComboBoxAddEntry(comboBoxPlayer[i],TheGameText->fetch("GUI:EasyAI"),white);
			GadgetComboBoxSetItemData(comboBoxPlayer[i], 5, (void *)SLOT_EASY_AI);
			GadgetComboBoxSetSelectedPos(comboBoxPlayer[i],0);
		}
		/*
		if(i != 0)
		{
			TheLAN->GetMyGame()->getLANSlot(i)->setState(SLOT_OPEN);
		}
		*/

		tmpString.format("LanGameOptionsMenu.wnd:ComboBoxColor%d", i);
		comboBoxColorID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxColor[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxColorID[i] );
		DEBUG_ASSERTCRASH(comboBoxColor[i], ("Could not find the comboBoxColor[%d]",i ));
		PopulateColorComboBox(i, comboBoxColor, TheLAN->GetMyGame());
		GadgetComboBoxSetSelectedPos(comboBoxColor[i], 0);

		tmpString.format("LanGameOptionsMenu.wnd:ComboBoxPlayerTemplate%d", i);
		comboBoxPlayerTemplateID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxPlayerTemplate[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxPlayerTemplateID[i] );
		DEBUG_ASSERTCRASH(comboBoxPlayerTemplate[i], ("Could not find the comboBoxPlayerTemplate[%d]",i ));
		PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, TheLAN->GetMyGame(), TRUE);

		// add tooltips to the player template combobox and listbox
		comboBoxPlayerTemplate[i]->winSetTooltipFunc(playerTemplateComboBoxTooltip);
		GadgetComboBoxGetListBox(comboBoxPlayerTemplate[i])->winSetTooltipFunc(playerTemplateListBoxTooltip);

		tmpString.format("LanGameOptionsMenu.wnd:ComboBoxTeam%d", i);
		comboBoxTeamID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		comboBoxTeam[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, comboBoxTeamID[i] );
		DEBUG_ASSERTCRASH(comboBoxTeam[i], ("Could not find the comboBoxTeam[%d]",i ));
		PopulateTeamComboBox(i, comboBoxTeam, TheLAN->GetMyGame());

		tmpString.clear();
		tmpString.format("LanGameOptionsMenu.wnd:ButtonAccept%d", i);
		buttonAcceptID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonAccept[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonAcceptID[i] );
		DEBUG_ASSERTCRASH(buttonAccept[i], ("Could not find the buttonAccept[%d]",i ));
		buttonAccept[i]->winSetTooltipFunc(gameAcceptTooltip);
//
//		tmpString.format("LanGameOptionsMenu.wnd:ButtonStartPosition%d", i);
//		buttonStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
//		buttonStartPosition[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonStartPositionID[i] );
//		DEBUG_ASSERTCRASH(buttonStartPosition[i], ("Could not find the ButtonStartPosition[%d]",i ));

		tmpString.format("LanGameOptionsMenu.wnd:ButtonMapStartPosition%d", i);
		buttonMapStartPositionID[i] = TheNameKeyGenerator->nameToKey( tmpString );
		buttonMapStartPosition[i] = TheWindowManager->winGetWindowFromId( parentLanGameOptions, buttonMapStartPositionID[i] );
		DEBUG_ASSERTCRASH(buttonMapStartPosition[i], ("Could not find the ButtonMapStartPosition[%d]",i ));

		if(i !=0 && buttonAccept[i])
			buttonAccept[i]->winHide(TRUE);
	}
	if( buttonAccept[0] )
		GadgetButtonSetEnabledColor(buttonAccept[0], acceptTrueColor );

}

void DeinitLanGameGadgets()
{
	// Disarm any pending resume so it cannot survive into a future lobby session.
	lanClearResumeArm();
	parentLanGameOptions = nullptr;
	buttonEmote = nullptr;
	buttonMapVote = nullptr;
	buttonSelectMap = nullptr;
	buttonStart = nullptr;
	buttonBack = nullptr;
	buttonRandomize = nullptr;
	listboxChatWindowLanGame = nullptr;
	textEntryChat = nullptr;
	textEntryMapDisplay = nullptr;
  checkboxLimitSuperweapons = nullptr;
  checkboxEnforceRandom = nullptr;
  comboBoxStartingCash = nullptr;
	if (windowMap)
	{
		windowMap->winSetUserData(nullptr);
		windowMap = nullptr;
	}
	for (Int i = 0; i < MAX_SLOTS; i++)
	{
		comboBoxPlayer[i] = nullptr;
		comboBoxColor[i] = nullptr;
		comboBoxPlayerTemplate[i] = nullptr;
		comboBoxTeam[i] = nullptr;
		buttonAccept[i] = nullptr;
//		buttonStartPosition[i] = nullptr;
		buttonMapStartPosition[i] = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
/** Initialize the Lan Game Options Menu */
//-------------------------------------------------------------------------------------------------
void LanGameOptionsMenuInit( WindowLayout *layout, void *userData )
{
	s_randomizeCount = 0;
	if (TheLAN->GetMyGame() && TheLAN->GetMyGame()->isGameInProgress())
	{
		// If we init while the game is in progress, we are really returning to the menu
		// after the game.  So, we pop the menu and go back to the lobby.  Whee!
		DEBUG_LOG(("Popping to lobby after a game!"));
		TheShell->popImmediate();
		return;
	}
	s_isIniting = TRUE;

	LANbuttonPushed = false;
	LANisShuttingDown = false;

	//initialize the gadgets
	EnableSlotListUpdates(FALSE);
	InitLanGameGadgets();
	EnableSlotListUpdates(TRUE);
	Int start = 0;

	// Make sure the text fields are clear
	GadgetListBoxReset( listboxChatWindowLanGame );
	GadgetTextEntrySetText(textEntryChat, UnicodeString::TheEmptyString);

	//The dialog needs to react differently depending on whether it's the host or not.
	TheMapCache->updateCache();
	if (TheLAN->AmIHost())
	{
		// read in some prefs
		LANGameInfo *game = TheLAN->GetMyGame();
		LANGameSlot *slot = game->getLANSlot(0);
		LANPreferences pref;
		slot->setColor( pref.getPreferredColor() );
		slot->setPlayerTemplate( pref.getPreferredFaction() );
		slot->setNATBehavior(FirewallHelperClass::FIREWALL_TYPE_SIMPLE);
		game->setMap( pref.getPreferredMap() );
    game->setStartingCash( pref.getStartingCash() );
    game->setSuperweaponRestriction( pref.getSuperweaponRestricted() ? 1 : 0 );
		AsciiString lowerMap = pref.getPreferredMap();
		lowerMap.toLower();
		std::map<AsciiString, MapMetaData>::iterator it = TheMapCache->find(lowerMap);
		if (it != TheMapCache->end())
		{
			TheLAN->GetMyGame()->getSlot(0)->setMapAvailability(true);
			TheLAN->GetMyGame()->setMapCRC( it->second.m_CRC );
			TheLAN->GetMyGame()->setMapSize( it->second.m_filesize );

			TheLAN->GetMyGame()->adjustSlotsForMap(); // BGC- adjust the slots for the selected map.
		}

		//GadgetTextEntrySetText(comboBoxPlayer[0], TheLAN->GetMyName());
		lanUpdateSlotList();
		updateGameOptions();
		start = 1; // leave my combo boxes usable

		// TheSuperHackers @tweak disable the combo box for the host's player name
		comboBoxPlayer[0]->winEnable(FALSE);
	}
	else
	{

		//DEBUG_LOG(("LanGameOptionsMenuInit(): map is %s", TheLAN->GetMyGame()->getMap().str()));
		buttonStart->winSetText(TheGameText->fetch("GUI:Accept"));
		buttonSelectMap->winEnable( FALSE );
		if (buttonMapVote)
			buttonMapVote->winEnable( FALSE );
		if (buttonRandomize)
			buttonRandomize->winEnable( FALSE );
    checkboxLimitSuperweapons->winEnable( FALSE ); // Can look but only host can touch
    if (checkboxEnforceRandom)
      checkboxEnforceRandom->winEnable( FALSE );   // Ditto
    comboBoxStartingCash->winEnable( FALSE );      // Ditto
		TheLAN->GetMyGame()->setMapCRC( TheLAN->GetMyGame()->getMapCRC() );		// force a recheck
		TheLAN->GetMyGame()->setMapSize( TheLAN->GetMyGame()->getMapSize() ); // of if we have the map
		TheLAN->RequestHasMap();
		lanUpdateSlotList();
		updateGameOptions();
	}
	for (Int i = start; i < MAX_SLOTS; ++i)
	{
		//I'm a client, disable the controls I can't touch.
		if (!TheLAN->AmIHost())
			comboBoxPlayer[i]->winEnable(FALSE);

		comboBoxColor[i]->winEnable(FALSE);
		comboBoxPlayerTemplate[i]->winEnable(FALSE);
		comboBoxTeam[i]->winEnable(FALSE);
//		buttonStartPosition[i]->winEnable(FALSE);
	}

//	for (i = 0; i < MAX_SLOTS; ++i)
//	{
//		if (buttonStartPosition[i])
//			buttonStartPosition[i]->winHide(TRUE); // not picking start spots this way any more
//	}
//
	// Show the Menu
	layout->hide( FALSE );

	// Set Keyboard to Main Parent
	TheWindowManager->winSetFocus( parentLanGameOptions );

	s_isIniting = FALSE;

	if (TheLAN->AmIHost())
	{
		TheLAN->RequestGameOptions(GenerateGameOptionsString(),true);
		TheLAN->RequestGameAnnounce();
	}
	lanUpdateSlotList();
	LanPositionStartSpots();
	TheTransitionHandler->setGroup("LanGameOptionsFade");

	// animate controls
	//TheShell->registerWithAnimateManager(buttonBack, WIN_ANIMATION_SLIDE_RIGHT, TRUE, 1);

}

//-------------------------------------------------------------------------------------------------
/** Update options on screen */
//-------------------------------------------------------------------------------------------------
void updateGameOptions()
{
	LANGameInfo *theGame = TheLAN->GetMyGame();
	UnicodeString mapDisplayName;
	lanCheckResumeArmChange(theGame);
	if (theGame && AreSlotListUpdatesEnabled())
	{
		const GameSlot *localSlot = theGame->getConstSlot(theGame->getLocalSlotNum());
		const MapMetaData *mapData = TheMapCache->findMap( TheLAN->GetMyGame()->getMap() );
		if (mapData && localSlot && localSlot->hasMap())
		{
			mapDisplayName.format(L"%ls", mapData->m_displayName.str());
		}
		else
		{
			AsciiString s = TheLAN->GetMyGame()->getMap();
			if (s.reverseFind('\\'))
			{
				s = s.reverseFind('\\') + 1;
			}
			mapDisplayName.format(L"%hs", s.str());
		}
		UnicodeString old = GadgetStaticTextGetText(textEntryMapDisplay);
		if(old.compare(mapDisplayName) != 0)
			LanPositionStartSpots();
		GadgetStaticTextSetText(textEntryMapDisplay, mapDisplayName);

    // These setters re-fire the gadgets' click messages; the guard keeps the
    // click handlers from mutating the game state we are displaying.
    s_isSyncingUI = TRUE;
    GadgetCheckBoxSetChecked( checkboxLimitSuperweapons, theGame->getSuperweaponRestriction() != 0 );
    if (checkboxEnforceRandom)
      GadgetCheckBoxSetChecked( checkboxEnforceRandom, theGame->getEnforceRandom() );
		Int itemCount = GadgetComboBoxGetLength(comboBoxStartingCash);
    Int index = 0;
    for ( ; index < itemCount; index++ )
    {
      Int value  = (Int)GadgetComboBoxGetItemData(comboBoxStartingCash, index);
      if ( value == theGame->getStartingCash().countMoney() )
      {
        GadgetComboBoxSetSelectedPos(comboBoxStartingCash, index, TRUE);
        break;
      }
    }
    s_isSyncingUI = FALSE;

    DEBUG_ASSERTCRASH( index < itemCount, ("Could not find new starting cash amount %d in list", theGame->getStartingCash().countMoney() ) );
	}
}


//-------------------------------------------------------------------------------------------------
//-------------------------------------------------------------------------------------------------
void setLANPlayerTooltip(LANPlayer* player)
{
	UnicodeString tooltip;

	if (!player->getLogin().isEmpty() || !player->getHost().isEmpty())
	{
		tooltip.format(TheGameText->fetch("TOOLTIP:LANPlayer"), player->getLogin().str(), player->getHost().str());
	}

#if defined(RTS_DEBUG)
	UnicodeString ip;
	ip.format(L" - %d.%d.%d.%d", PRINTF_IP_AS_4_INTS(player->getIP()));
	tooltip.concat(ip);
#endif

	if (!tooltip.isEmpty())
	{
		TheMouse->setCursorTooltip( tooltip );
	}
}


//-------------------------------------------------------------------------------------------------
/** This is called when a shutdown is complete for this menu */
//-------------------------------------------------------------------------------------------------
static void shutdownComplete( WindowLayout *layout )
{
	DeinitLanGameGadgets();
	textEntryMapDisplay = nullptr;
	LANisShuttingDown = false;

	// hide the layout
	layout->hide( TRUE );

	// our shutdown is complete
	TheShell->shutdownComplete( layout, (LANnextScreen != nullptr) );

	if (LANnextScreen != nullptr)
	{
		TheShell->push(LANnextScreen);
	}

	LANnextScreen = nullptr;

}

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu shutdown method */
//-------------------------------------------------------------------------------------------------
void LanGameOptionsMenuShutdown( WindowLayout *layout, void *userData )
{
	TheMouse->setCursor(Mouse::ARROW);
	TheMouse->setMouseText(UnicodeString::TheEmptyString,nullptr,nullptr);
	EnableSlotListUpdates(FALSE);
	LANisShuttingDown = true;

	// if we are shutting down for an immediate pop, skip the animations
	Bool popImmediate = *(Bool *)userData;
	if( popImmediate )
	{

		shutdownComplete( layout );
		return;

	}

	TheShell->reverseAnimatewindow();
	TheTransitionHandler->reverse("LanGameOptionsFade");
	if (TheLAN)
		TheLAN->ResetGameStartTimer();

	/*
	// hide menu
	layout->hide( TRUE );

	// Reset the LAN singleton
//	TheLAN->reset();

	// our shutdown is complete
	TheShell->shutdownComplete( layout );
	*/
}

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu update method */
//-------------------------------------------------------------------------------------------------
void LanGameOptionsMenuUpdate( WindowLayout * layout, void *userData)
{
	if(LANisShuttingDown && TheShell->isAnimFinished() && TheTransitionHandler->isFinished())
		shutdownComplete(layout);
	//TheLAN->update(); // this is handled in the lobby
}

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanGameOptionsMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{

		// --------------------------------------------------------------------------------------------
		case GWM_CHAR:
		{
			UnsignedByte key = mData1;
			UnsignedByte state = mData2;
			if (LANbuttonPushed)
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
	return MSG_IGNORED;
}


// -----------------------------------------------------------------------------
// Radarvan "Map Vote" button helpers
// -----------------------------------------------------------------------------

// Normalize a candidate display name for matching against the radarvan
// response: lowercase + drop spaces. Spaces have to go because radarvan
// stores some legacy entries with the folder name ("TournamentA") while
// the engine's m_displayName uses the CSF-translated form with spaces
// ("Tournament A"). Case folding handles "[RANK]" vs "[rank]" drift.
static AsciiString normalizeMapNameForMatch(const UnicodeString &src)
{
	AsciiString out;
	out.translate(src);
	AsciiString stripped;
	for (const char *p = out.str(); *p != '\0'; ++p)
	{
		if (*p != ' ')
			stripped.concat(*p);
	}
	stripped.toLower();
	return stripped;
}

// Locate a map in MapCache whose m_displayName matches the given
// AsciiString. Strips two engine-added suffixes from m_displayName
// before comparing:
//   1) " (N)" / " (NN)" player-count suffix (appended for any
//      multiplayer-eligible map).
//   2) Trailing ".map" extension (unofficial maps derive
//      m_displayName from the bare filename, official maps don't).
// Comparison is case-insensitive and space-insensitive so the
// server's "TournamentA" matches the engine's "Tournament A".
// Returns the metadata pointer on match and writes the MapCache
// iterator key (lowercase filename path the engine uses internally)
// into *outKey. Returns nullptr when nothing matches.
static const MapMetaData *findMapByDisplayName(const AsciiString &chosen, AsciiString *outKey)
{
	if (!TheMapCache || chosen.isEmpty())
		return nullptr;
	UnicodeString chosenU;
	chosenU.translate(chosen);
	AsciiString chosenNorm = normalizeMapNameForMatch(chosenU);
	if (chosenNorm.isEmpty())
		return nullptr;

	for (MapCache::iterator it = TheMapCache->begin(); it != TheMapCache->end(); ++it)
	{
		UnicodeString display = it->second.m_displayName;

		// Strip a trailing " (N)" / " (NN)" player-count suffix.
		Int dlen = display.getLength();
		if (dlen >= 4)
		{
			const WideChar *ds = display.str();
			if (ds[dlen - 1] == L')')
			{
				Int di = dlen - 2;
				while (di > 0 && ds[di] >= L'0' && ds[di] <= L'9')
					--di;
				if (di >= 1 && ds[di] == L'(' && ds[di - 1] == L' ' && di != dlen - 2)
				{
					UnicodeString trimmed;
					Int j;
					for (j = 0; j < di - 1; ++j)
						trimmed.concat(ds[j]);
					display = trimmed;
					dlen = display.getLength();
				}
			}
		}

		// Strip a trailing ".map" extension (case-insensitive).
		// Unofficial maps bake the filename into m_displayName so
		// the extension survives into the visible name; official
		// maps fetch a localized title and have no extension.
		if (dlen >= 4)
		{
			const WideChar *ds = display.str();
			WideChar c0 = ds[dlen - 4];
			WideChar c1 = ds[dlen - 3];
			WideChar c2 = ds[dlen - 2];
			WideChar c3 = ds[dlen - 1];
			if (c0 == L'.' &&
				(c1 == L'm' || c1 == L'M') &&
				(c2 == L'a' || c2 == L'A') &&
				(c3 == L'p' || c3 == L'P'))
			{
				UnicodeString trimmed;
				Int j;
				for (j = 0; j < dlen - 4; ++j)
					trimmed.concat(ds[j]);
				display = trimmed;
			}
		}

		AsciiString displayNorm = normalizeMapNameForMatch(display);
		if (displayNorm == chosenNorm)
		{
			if (outKey)
				*outKey = it->first;
			return &it->second;
		}
	}
	return nullptr;
}

// Host-only handler for the "Radarvan Pick" button. POSTs the lobby
// roster to the configured map_vote endpoint, applies the chosen map
// locally, and propagates via RequestGameOptions. All error states
// surface as local SYSTEM chat lines so the host sees what went wrong
// without aborting the lobby. The body sends humans-only names; the
// URL path segment carries the total occupied-slot count (humans + AI
// + observers) so the server picks from a pool sized to the actual
// match.
//
// Forward-compat: when the server starts returning map_filename /
// map_crc / map_contents_mask alongside chosen_map, the marked block
// below auto-installs an unknown map via DownloadAndInstallMap
// before the local-cache fallback fails. Until then, an unknown
// chosen_map results in a chat error showing the picked name.
static void handleMapVoteClick()
{
	if (!TheLAN || !TheLAN->AmIHost())
		return;

	LANGameInfo *game = TheLAN->GetMyGame();
	if (!game)
		return;

	std::vector<AsciiString> humanNames;
	unsigned int occupied = 0;
	Int i;
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		const GameSlot *slot = game->getConstSlot(i);
		if (!slot || !slot->isOccupied())
			continue;
		++occupied;
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
		TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), errU, LANAPIInterface::LANCHAT_SYSTEM);
		return;
	}

	ChooseMapResult res = ChooseMapFromServer(
		TheGlobalData->m_mapVoteUrl, humanNames, occupied);
	if (!res.success)
	{
		UnicodeString errU;
		errU.format(TheGameText->fetch("GUI:MapVoteFailed"),
			res.errorMessage.str());
		TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), errU, LANAPIInterface::LANCHAT_SYSTEM);
		return;
	}

	AsciiString mapKey;
	const MapMetaData *md = findMapByDisplayName(res.chosenMap, &mapKey);

	// Forward-compat CDN-download hook. The server doesn't return
	// these fields today (mapCRC == 0, mapFileName empty), so this
	// branch is dormant. Once it does, an unknown chosen_map will be
	// fetched + CRC-verified by DownloadAndInstallMap before the
	// local-cache lookup retries, and the user-visible "not found"
	// chat error stays as the final fallback for truly missing maps.
	if (md == nullptr && !res.mapFileName.isEmpty() && res.mapCRC != 0)
	{
		if (DownloadAndInstallMap(res.mapFileName, res.mapCRC, res.contentsMask))
		{
			md = findMapByDisplayName(res.chosenMap, &mapKey);
			if (md == nullptr && TheMapCache)
			{
				AsciiString key = res.mapFileName;
				key.toLower();
				md = TheMapCache->findMap(key);
				if (md)
					mapKey = key;
			}
		}
	}

	if (md == nullptr || mapKey.isEmpty())
	{
		UnicodeString errU;
		errU.format(TheGameText->fetch("GUI:MapVoteNotFound"),
			res.chosenMap.str());
		TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), errU, LANAPIInterface::LANCHAT_SYSTEM);
		return;
	}

	game->setMap(mapKey);
	game->getSlot(0)->setMapAvailability(true);
	game->setMapCRC(md->m_CRC);
	game->setMapSize(md->m_filesize);
	game->resetStartSpots();
	game->adjustSlotsForMap();

	// Mirror the SelectMap confirm path: push the .map + sidecars to
	// the cncstats CDN so joining peers without the map can pull it
	// over HTTP instead of waiting for the P2P transfer at launch.
	// Helper no-ops when URLs are empty or the server already has
	// the map for this CRC.
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

	// Drives updateGameOptions() (refreshes the map-name static text),
	// lanUpdateSlotList() (refreshes player slot widgets), resets
	// start positions, and re-sends RequestGameOptions to clients.
	// Calling RequestGameOptions on its own updates engine state but
	// leaves the lobby UI showing the previous map until the next
	// re-init.
	PostToLanGameOptions(SEND_GAME_OPTS);

	UnicodeString okU;
	okU.format(TheGameText->fetch("GUI:MapVoteApplied"), res.chosenMap.str());
	TheLAN->RequestChat(okU, LANAPIInterface::LANCHAT_SYSTEM);
}

//-------------------------------------------------------------------------------------------------
/** Lan Game Options menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType LanGameOptionsMenuSystem( GameWindow *window, UnsignedInt msg,
														 WindowMsgData mData1, WindowMsgData mData2 )
{
	UnicodeString txtInput;
	switch( msg )
	{
		//-------------------------------------------------------------------------------------------------
		case GWM_CREATE:
			{
				break;
			}
		//-------------------------------------------------------------------------------------------------
		case GWM_DESTROY:
			{
				if (windowMap)
					windowMap->winSetUserData(nullptr);

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
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();
				LANGameInfo *myGame = TheLAN->GetMyGame();

        if ( controlID == comboBoxStartingCashID )
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
					  else if( controlID == comboBoxPlayerID[i] && myGame->amIHost() )
					  {
						  // We don't have anything that'll happen if we click on ourselves
						  if(i == myGame->getLocalSlotNum())
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
						  if( pos >= 0 )
						  {
							  SlotState newState = slotStateFromLobbyComboPos(pos);
							  if( myGame->getLANSlot(i)->getState() == SLOT_PLAYER )
							  {
								  UnicodeString name = myGame->getPlayerName(i);
								  myGame->getLANSlot(i)->setState(newState);
								  myGame->resetAccepted();
								  TheLAN->OnPlayerLeave(name);
							  }
							  else if( myGame->getLANSlot(i)->getState() != newState )
							  {
								  Bool wasAI = (myGame->getLANSlot(i)->isAI());
								  myGame->getLANSlot(i)->setState(newState);
								  Bool isAI = (myGame->getLANSlot(i)->isAI());
								  if (wasAI || isAI)
									  myGame->resetAccepted();
								  if (wasAI ^ isAI)
									  PopulatePlayerTemplateComboBox(i, comboBoxPlayerTemplate, myGame, wasAI);
								  if (!s_isIniting)
								  {
									  TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
									  lanUpdateSlotList();
								  }
							  }
						  }
              break;
            }
          }
				}
        break;
			}
		//-------------------------------------------------------------------------------------------------
		case GBM_SELECTED:
			{
				if (LANbuttonPushed)
					break;
				GameWindow *control = (GameWindow *)mData1;
				Int controlID = control->winGetWindowId();

				if ( controlID == buttonBackID )
				{
					if( mapSelectLayout )
						{
							mapSelectLayout->destroyWindows();
							deleteInstance(mapSelectLayout);
							mapSelectLayout = nullptr;
						}
					TheLAN->RequestGameLeave();
					//TheShell->pop();

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
						TheLAN->RequestChat(txtInput, LANAPIInterface::LANCHAT_EMOTE);
				}
				else if ( controlID == buttonMapVoteID )
				{
					handleMapVoteClick();
				}
				else if ( controlID == buttonSelectMapID )
				{
					//buttonBack->winEnable( false );

					mapSelectLayout = TheWindowManager->winCreateLayout( "Menus/LanMapSelectMenu.wnd" );
					mapSelectLayout->runInit();
					mapSelectLayout->hide( FALSE );
					mapSelectLayout->bringForward();

				}
				else if ( controlID == buttonStartID )
				{
					if (TheLAN->AmIHost())
					{
						StartPressed();
						//TheLAN->RequestGameStart();
					}
					else
					{
						//I'm the Client... send an accept message to the host.
						TheLAN->RequestAccept();

						// Disable the accept button
						EnableAcceptControls(TRUE, TheLAN->GetMyGame(), comboBoxPlayer, comboBoxColor, comboBoxPlayerTemplate,
							comboBoxTeam, buttonAccept, buttonStart, buttonMapStartPosition);

					}
				}
				else if ( controlID == buttonRandomizeID )
				{
					// TheSuperHackers @feature bill-rich 15/09/2026 host-only Randomize: resolve the random slots,
					// broadcast the new options and tell the room it happened.
					if (TheLAN->AmIHost())
					{
						std::vector<RandomSlotAssignment> changes;
						performRandomAssign(TheLAN->GetMyGame(), &changes);
						if (changes.empty())
						{
							TheLAN->RequestChat(TheGameText->FETCH_OR_SUBSTITUTE("GUI:RandomizeNothingToDo", L"Randomize: every slot already has a faction, color and start position"), LANAPIInterface::LANCHAT_SYSTEM);
						}
						else
						{
							TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
							lanUpdateSlotList();

							++s_randomizeCount;
							UnicodeString strInform;
							strInform.format(TheGameText->FETCH_OR_SUBSTITUTE("GUI:HostRandomizedSlots", L"Randomize: the host resolved the random factions, colors and start positions (roll %d)"), s_randomizeCount);
							TheLAN->RequestChat(strInform, LANAPIInterface::LANCHAT_SYSTEM);
						}
					}
				}
        else if ( controlID == checkboxLimitSuperweaponsID )
        {
          handleLimitSuperweaponsClick();
        }
        else if ( controlID == checkboxEnforceRandomID )
        {
          handleEnforceRandomClick();
        }
				else
				{
					for (Int i = 0; i < MAX_SLOTS; i++)
					{
						if (controlID == buttonMapStartPositionID[i])
						{
							LANGameInfo *game = TheLAN->GetMyGame();
							Int playerIdxInPos = -1;
							for (Int j=0; j<MAX_SLOTS; ++j)
							{
								LANGameSlot *slot = game->getLANSlot(j);
								if (slot && slot->getStartPos() == i)
								{
									playerIdxInPos = j;
									break;
								}
							}
							if (playerIdxInPos >= 0)
							{
								LANGameSlot *slot = game->getLANSlot(playerIdxInPos);
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
			if (LANbuttonPushed)
				break;

			GameWindow *control = (GameWindow *)mData1;
			Int controlID = control->winGetWindowId();
			for (Int i = 0; i < MAX_SLOTS; i++)
			{
				if (controlID == buttonMapStartPositionID[i])
				{
					LANGameInfo *game = TheLAN->GetMyGame();
					Int playerIdxInPos = -1;
					for (Int j=0; j<MAX_SLOTS; ++j)
					{
						LANGameSlot *slot = game->getLANSlot(j);
						if (slot && slot->getStartPos() == i)
						{
							playerIdxInPos = j;
							break;
						}
					}
					if (playerIdxInPos >= 0)
					{
						LANGameSlot *slot = game->getLANSlot(playerIdxInPos);
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
				if (LANbuttonPushed)
					break;
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
						// TheSuperHackers @feature bill-rich 15/09/2026 "/resume" arms resume-from-replay, "/resume off" disarms
						if (txtInput.compareNoCase(L"/resume") == 0 || txtInput.compareNoCase(L"/resume off") == 0)
						{
							if (!TheLAN->AmIHost())
								TheLAN->OnChat(L"SYSTEM", TheLAN->GetLocalIP(), TheGameText->FETCH_OR_SUBSTITUTE("GUI:ResumeHostOnly", L"Only the host can resume a game"), LANAPI::LANCHAT_SYSTEM);
							else if (txtInput.compareNoCase(L"/resume off") == 0)
								disarmResumeFromReplay();
							else
								tryArmResumeFromReplay();
						}
						else
						TheLAN->RequestChat(txtInput, LANAPIInterface::LANCHAT_NORMAL);

				}
				break;
			}
		//-------------------------------------------------------------------------------------------------
		default:
			return MSG_IGNORED;
	}
	return MSG_HANDLED;
}

//-------------------------------------------------------------------------------------------------
/** Utility FUnction used as a bridge from other windows to this one */
//-------------------------------------------------------------------------------------------------
void PostToLanGameOptions( PostToLanGameType post )
{
	if (post >= POST_TO_LAN_GAME_TYPE_COUNT)
		return;
	LanPositionStartSpots();
	switch (post)
	{
			//-------------------------------------------------------------------------------------------------
		case SEND_GAME_OPTS:
		{
			LANGameInfo *game = TheLAN->GetMyGame();
			game->resetAccepted();
			lanDisarmResumeForSettingsChange(); // a new map is not the recording's
			updateGameOptions();
			lanUpdateSlotList();

			//buttonBack->winEnable( true );

			for(Int i = 0; i < MAX_SLOTS; ++i)
			{
				game->getSlot(i)->setStartPos(-1);
			}

			TheLAN->RequestGameOptions(GenerateGameOptionsString(), true);
			break;
		}
		case MAP_BACK:
		{
				//buttonBack->winEnable( true );
		}
		default:
			return;
	}
}
