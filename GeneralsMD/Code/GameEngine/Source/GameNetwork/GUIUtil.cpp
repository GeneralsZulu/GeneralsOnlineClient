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

// FILE: GUIUtil.cpp //////////////////////////////////////////////////////
// Author: Matthew D. Campbell, Sept 2002

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/GUIUtil.h"
#include "GameNetwork/NetworkDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/MapUtil.h"
#include "Common/NameKeyGenerator.h"

#include "Common/MultiplayerSettings.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetComboBox.h"
#include "GameClient/GadgetTextEntry.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GameText.h"
#include "GameLogic/GameLogic.h" // SUPERWEAPON_RESTRICT_COUNT
#include "GameNetwork/GameInfo.h"
#include "Common/PlayerTemplate.h"
#include "GameNetwork/LANAPICallbacks.h" // for acceptTrueColor, etc
#include "GameClient/ChallengeGenerals.h"
#include "Common/GlobalData.h"
#include "Common/StatsUploader.h" // DownloadAndInstallMap (map install by CRC)


// -----------------------------------------------------------------------------
// Map lookup by display name, shared by the LAN and online "Radarvan Pick"
// buttons. Moved here from LanGameOptionsMenu.cpp when the online lobby got the
// same button: one copy, one set of matching rules.
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

const MapMetaData *FindMapByDisplayName(const AsciiString &chosenDisplayName, AsciiString *outMapCacheKey)
{
	if (!TheMapCache || chosenDisplayName.isEmpty())
		return nullptr;
	UnicodeString chosenU;
	chosenU.translate(chosenDisplayName);
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
			if (outMapCacheKey)
				*outMapCacheKey = it->first;
			return &it->second;
		}
	}
	return nullptr;
}

// Keep only characters that are safe in a map directory/file name. The name
// comes from a server response, and it becomes a path, so anything that could
// walk out of the maps directory has to go. Spaces are kept: real map names have
// them, and the engine is happy with them.
static AsciiString sanitizeMapNameForPath(const AsciiString &src)
{
	AsciiString out;
	for (const char *p = src.str(); *p != '\0'; ++p)
	{
		const char c = *p;
		const Bool ok = (c >= 'a' && c <= 'z')
			|| (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9')
			|| c == ' ' || c == '_' || c == '-' || c == '.'
			|| c == '[' || c == ']' || c == '(' || c == ')' || c == '\'';
		if (ok)
			out.concat(c);
	}
	out.trim();
	// A name that is nothing but dots would make a path that means "the parent
	// directory"; refuse it rather than write there.
	const char *q = out.str();
	while (*q == '.')
		++q;
	if (*q == '\0')
		return AsciiString::TheEmptyString;
	return out;
}

const MapMetaData *InstallMapByCRC(const AsciiString &chosenDisplayName,
                                   const AsciiString &serverMapFileName,
                                   UnsignedInt mapCRC,
                                   UnsignedInt contentsMask,
                                   AsciiString *outMapCacheKey)
{
	// A zero CRC is the server saying it has no pick, or an older schema that
	// never sent one: there is nothing to fetch by.
	if (mapCRC == 0 || TheMapCache == nullptr)
		return nullptr;

	// Where the bytes land. The server's own relative path wins when it sends
	// one; otherwise derive <user maps>\<name>\<name>.map, which is the layout
	// the engine requires (directory and file must share a name) and the one
	// unofficial maps are matched by, so the retry below finds it by name.
	AsciiString localPath;
	if (!serverMapFileName.isEmpty())
	{
		localPath = serverMapFileName;
	}
	else
	{
		const AsciiString safeName = sanitizeMapNameForPath(chosenDisplayName);
		if (safeName.isEmpty())
			return nullptr;
		localPath.format("%s\\%s\\%s.map", TheMapCache->getUserMapDir().str(),
			safeName.str(), safeName.str());
	}

	if (!DownloadAndInstallMap(localPath, mapCRC, contentsMask))
		return nullptr;

	// DownloadAndInstallMap refreshes MapCache, so the map should be findable
	// now. Try the name the server used first -- for an unofficial map the cache
	// derives the display name from the file name we just wrote, so they agree --
	// then the path we wrote, for the case where they do not.
	AsciiString key;
	const MapMetaData *md = FindMapByDisplayName(chosenDisplayName, &key);
	if (md == nullptr)
	{
		key = localPath;
		key.toLower();
		md = TheMapCache->findMap(key);
	}
	if (md != nullptr && outMapCacheKey != nullptr)
		*outMapCacheKey = key;
	return md;
}

// -----------------------------------------------------------------------------

static Bool winInitialized = FALSE;

void EnableSlotListUpdates( Bool val )
{
	winInitialized = val;
}

Bool AreSlotListUpdatesEnabled()
{
	return winInitialized;
}

// -----------------------------------------------------------------------------

void EnableAcceptControls(Bool Enabled, GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[], GameWindow *buttonStart,
										GameWindow *buttonMapStartPosition[], Int slotNum)
{
	if(slotNum == -1 || slotNum >= MAX_SLOTS )
		slotNum = myGame->getLocalSlotNum();

	// An options parse can leave us temporarily unmatched in the slot list, so
	// getLocalSlotNum() can be -1 here; getConstSlot(-1) returns null and the
	// dereference below crashed the client. Skip the refresh; the next update
	// after the slot is restored redraws everything.
	if (slotNum < 0 || myGame->getConstSlot(slotNum) == nullptr)
		return;

	Bool isObserver = myGame->getConstSlot(slotNum)->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER;

	if( !myGame->amIHost() && (buttonStart != nullptr) )
		buttonStart->winEnable(Enabled);
	if(comboColor[slotNum])
	{
		if (isObserver)
		{
			GadgetComboBoxHideList(comboColor[slotNum]);
		}
		comboColor[slotNum]->winEnable(Enabled && !isObserver);
	}
	// While the host has enforce-random set, faction and team stay locked to
	// Random for everyone (color remains selectable).
	Bool enforceRandom = myGame->getEnforceRandom();
	if(comboPlayerTemplate[slotNum])
	{
		if (enforceRandom)
		{
			GadgetComboBoxHideList(comboPlayerTemplate[slotNum]);
		}
		comboPlayerTemplate[slotNum]->winEnable(Enabled && !enforceRandom);
	}
	if(comboTeam[slotNum])
	{
		if (isObserver || enforceRandom)
		{
			GadgetComboBoxHideList(comboTeam[slotNum]);
		}
		comboTeam[slotNum]->winEnable(Enabled && !isObserver && !enforceRandom);
	}

	Bool canChooseStartSpot = FALSE;
	if (!isObserver)
		canChooseStartSpot = TRUE;
	for (Int i=0; i<MAX_SLOTS && !canChooseStartSpot && myGame->amIHost(); ++i)
	{
		if (myGame->getConstSlot(i) && myGame->getConstSlot(i)->isAI())
			canChooseStartSpot = TRUE;
	}

	if (slotNum == myGame->getLocalSlotNum())
	{
		if (myGame->getConstSlot(myGame->getLocalSlotNum())->hasMap())
		{
			for (Int i=0; i<MAX_SLOTS; ++i)
			{
				if (buttonMapStartPosition[i])
				{
					buttonMapStartPosition[i]->winEnable(Enabled && canChooseStartSpot);
				}
			}
		}
		else
		{
			for (Int i=0; i<MAX_SLOTS; ++i)
			{
				if (buttonMapStartPosition[i])
					buttonMapStartPosition[i]->winEnable(FALSE);
			}
		}
	}
}

// -----------------------------------------------------------------------------

void ShowUnderlyingGUIElements( Bool show, const char *layoutFilename, const char *parentName,
															 const char **gadgetsToHide, const char **perPlayerGadgetsToHide )
{
	AsciiString parentNameStr;
	parentNameStr.format("%s:%s", layoutFilename, parentName);
	NameKeyType parentID = NAMEKEY(parentNameStr);
	GameWindow *parent = TheWindowManager->winGetWindowFromId( nullptr, parentID );
	if (!parent)
	{
		DEBUG_CRASH(("Window %s not found", parentNameStr.str()));
		return;
	}

	// hide some GUI elements of the screen underneath
	GameWindow *win;

	Int player;
	const char **text;

	text = gadgetsToHide;
	while (*text)
	{
		AsciiString gadgetName;
		gadgetName.format("%s:%s", layoutFilename, *text);
		win	= TheWindowManager->winGetWindowFromId( parent, NAMEKEY(gadgetName) );
		//DEBUG_ASSERTCRASH(win, ("Cannot find %s to show/hide it", gadgetName.str()));
		if (win)
		{
			win->winHide( !show );
		}
		++text;
	}

	text = perPlayerGadgetsToHide;
	while (*text)
	{
		for (player = 0; player < MAX_SLOTS; ++player)
		{
			AsciiString gadgetName;
			gadgetName.format("%s:%s%d", layoutFilename, *text, player);
			win	= TheWindowManager->winGetWindowFromId( parent, NAMEKEY(gadgetName) );
			//DEBUG_ASSERTCRASH(win, ("Cannot find %s to show/hide it", gadgetName.str()));
			if (win)
			{
				win->winHide( !show );
			}
		}
		++text;
	}
}

// -----------------------------------------------------------------------------

void PopulateColorComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver)
{
	Int numColors = TheMultiplayerSettings->getNumColors();

	UnicodeString colorName;
	std::vector<bool> availableColors;

	Int i = 0;
	for (; i < numColors; i++)
		availableColors.push_back(true);

	for (i = 0; i < MAX_SLOTS; i++)
	{
		GameSlot *slot = myGame->getSlot(i);
		if( slot && (i != comboBox) && (slot->getColor() >= 0 )&& (slot->getColor() < numColors))
		{
			DEBUG_ASSERTCRASH(slot->getColor() >= 0,("We've tried to access array %d and that ain't good",slot->getColor()));
			availableColors[slot->getColor()] = false;
		}
	}

	Bool wasObserver = (GadgetComboBoxGetLength(comboArray[comboBox]) == 1);
	GadgetComboBoxReset(comboArray[comboBox]);

	MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_RANDOM);
	Int newIndex = GadgetComboBoxAddEntry(comboArray[comboBox],
		(isObserver)?TheGameText->fetch("GUI:None"):TheGameText->fetch("GUI:???"), def->getColor());
	GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, (void *)-1);

	if (isObserver)
	{
		GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);
		return;
	}

	for (Int c=0; c<numColors; ++c)
	{
		def = TheMultiplayerSettings->getColor(c);
		if (!def || availableColors[c] == false)
			continue;

		bool bFoundColorName = false;
		colorName = TheGameText->fetch(def->getTooltipName().str(), &bFoundColorName);

		if (!bFoundColorName) // use raw instead
		{
			colorName.format(L"%hs", def->getTooltipName().str());
		}
		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], colorName, def->getColor());
		GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, (void *)c);
	}
	if (wasObserver)
		GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);
}

// -----------------------------------------------------------------------------

void PopulatePlayerTemplateComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool allowObservers)
{
	Int numPlayerTemplates = ThePlayerTemplateStore->getPlayerTemplateCount();
	UnicodeString playerTemplateName;

	GadgetComboBoxReset(comboArray[comboBox]);

	MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_RANDOM);
	Int newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], TheGameText->fetch("GUI:Random"), def->getColor());
	GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, (void *)PLAYERTEMPLATE_RANDOM);

	std::set<AsciiString> seenSides;

	for (Int c=0; c<numPlayerTemplates; ++c)
	{
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(c);
		if (!fac)
			continue;

		if (fac->getStartingBuilding().isEmpty())
			continue;

		if ( myGame->oldFactionsOnly() && !fac->isOldFaction() )
		  continue;

		// Prevent players from selecting the disabled Generals for use.
		// This is also enforced at game loading (GameLogic.cpp and UserPreferences.cpp).
		// @todo: unlock these when something rad happens
		Bool disallowLockedGenerals = TRUE;
		const GeneralPersona *general = TheChallengeGenerals->getGeneralByTemplateName(fac->getName());
		Bool startsLocked = general ? !general->isStartingEnabled() : FALSE;
		if (disallowLockedGenerals && startsLocked)
			continue;


		AsciiString side;
		side.format("SIDE:%s", fac->getSide().str());
		if (seenSides.find(side) != seenSides.end())
			continue;

		seenSides.insert(side);

		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], TheGameText->fetch(side), def->getColor());
		GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, (void *)c);
	}
	seenSides.clear();

	// disabling observers for Multiplayer test
	if (allowObservers)
	{
		def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_OBSERVER);
		newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], TheGameText->fetch("GUI:Observer"), def->getColor());
		GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, (void *)PLAYERTEMPLATE_OBSERVER);
	}
	GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);

}

// -----------------------------------------------------------------------------

// team colors for UI (team combo + minimap start positions).
// Declared in GUIUtil.h as Color (a signed Int) so every caller agrees on the
// type; GameMakeColor already produces a Color.
//
// TheSuperHackers @bugfix bill-rich 02/10/2026 These are Zulu 1.6.x's four team
// colors, not GO's. Players read a lobby by team color, so a port that quietly
// renumbered them (GO had Red/Green/Blue/Yellow) made every screenshot and every
// "I'm on yellow" disagree with what Zulu players already know.
Color GetTeamUiColor(Int teamNumber)
{
    switch (teamNumber)
    {
        case 0:
            return GameMakeColor(255, 220,  60, 255); // Team 1: Yellow

        case 1:
            return GameMakeColor(255,  60,  60, 255); // Team 2: Red

        case 2:
            return GameMakeColor(200, 100, 255, 255); // Team 3: Purple

        case 3:
            return GameMakeColor( 60, 120, 255, 255); // Team 4: Blue
    }

    // Default: white (none) 
    return GameMakeColor(255, 255, 255, 255);
}

void PopulateTeamComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver)
{
	Int numTeams = MAX_SLOTS/2;
	UnicodeString teamName;

	GadgetComboBoxReset(comboArray[comboBox]);

	MultiplayerColorDefinition *def = TheMultiplayerSettings->getColor(PLAYERTEMPLATE_RANDOM);
	Int newIndex = GadgetComboBoxAddEntry(comboArray[comboBox], TheGameText->fetch("Team:0"), def->getColor());
	GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, (void *)-1);

	if (isObserver)
	{
		GadgetComboBoxSetSelectedPos(comboArray[comboBox], 0);
		return;
	}

	for (Int c=0; c<numTeams; ++c)
	{
		AsciiString teamStr;
		teamStr.format("Team:%d", c + 1);
		teamName = TheGameText->fetch(teamStr.str());
        Color teamColor = GetTeamUiColor(c);
        newIndex = GadgetComboBoxAddEntry(comboArray[comboBox],teamName,teamColor);
        GadgetComboBoxSetItemData(comboArray[comboBox], newIndex, (void *)c);
    }
}

// -----------------------------------------------------------------------------
static UnicodeString formatMoneyForStartingCashComboBox( const Money & moneyAmount )
{
  UnicodeString rtn;
  rtn.format( TheGameText->fetch( "GUI:StartingMoneyFormat" ), moneyAmount.countMoney() );
  return rtn;
}

void PopulateStartingCashComboBox(GameWindow *comboBox, GameInfo *myGame)
{
  GadgetComboBoxReset(comboBox);

  const MultiplayerStartingMoneyList & startingCashMap = TheMultiplayerSettings->getStartingMoneyList();
  Int currentSelectionIndex = -1;

  for (MultiplayerStartingMoneyList::const_iterator it = startingCashMap.begin(); it != startingCashMap.end(); it++ )
  {
    Int newIndex = GadgetComboBoxAddEntry(comboBox, formatMoneyForStartingCashComboBox( *it ),
                                          comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor());
    GadgetComboBoxSetItemData(comboBox, newIndex, (void *)it->countMoney());

    if ( myGame->getStartingCash().amountEqual( *it ) )
    {
      currentSelectionIndex = newIndex;
    }
  }

  // NGMP: safety
  // TODO_NGMP: Why can we get in here with no data during lobby creation? async?
  if (myGame->getStartingCash().countMoney() == 0)
  {
	  currentSelectionIndex = 0;
  }

  if ( currentSelectionIndex == -1 )
  {
    DEBUG_CRASH( ("Current selection for starting cash not found in list") );
    currentSelectionIndex = GadgetComboBoxAddEntry(comboBox, formatMoneyForStartingCashComboBox( myGame->getStartingCash() ),
                                          comboBox->winGetEnabled() ? comboBox->winGetEnabledTextColor() : comboBox->winGetDisabledTextColor());
    GadgetComboBoxSetItemData(comboBox, currentSelectionIndex, (void *)myGame->getStartingCash().countMoney() );
  }

  GadgetComboBoxSetSelectedPos(comboBox, currentSelectionIndex);
}

// -----------------------------------------------------------------------------

//  -----------------------------------------------------------------------------------------
// The slot list displaying function
//-------------------------------------------------------------------------------------------------
void UpdateSlotList( GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[],
										GameWindow *buttonStart, GameWindow *buttonMapStartPosition[] )
{
	if(!AreSlotListUpdatesEnabled())
		return;
	//LANGameInfo *myGame = TheLAN->GetMyGame();

	const MapMetaData *mapData = TheMapCache->findMap( myGame->getMap() );
	Bool willTransfer = TRUE;
	if (mapData)
	{
		willTransfer = !mapData->m_isOfficial;
	}
	else
	{
		willTransfer = WouldMapTransfer(myGame->getMap());
	}

	if (myGame)
	{
		for( int i =0; i < MAX_SLOTS; i++ )
		{
			GameSlot * slot = myGame->getSlot(i);

			// if i'm host, enable the controls for AI
			if(myGame->amIHost() && slot->isAI())
			{
				EnableAcceptControls(TRUE, myGame, comboPlayer, comboColor, comboPlayerTemplate,
					comboTeam, buttonAccept, buttonStart, buttonMapStartPosition, i);
			}
			else if (myGame->getLocalSlotNum() == i)
			{
				if(slot->isAccepted() && !myGame->amIHost())
				{
					EnableAcceptControls(FALSE, myGame, comboPlayer, comboColor, comboPlayerTemplate,
						comboTeam, buttonAccept, buttonStart, buttonMapStartPosition);
				}
				else
				{
					if (slot->hasMap()) {
						EnableAcceptControls(TRUE, myGame, comboPlayer, comboColor, comboPlayerTemplate,
							comboTeam, buttonAccept, buttonStart, buttonMapStartPosition);
					}
					else
					{
						EnableAcceptControls(willTransfer, myGame, comboPlayer, comboColor, comboPlayerTemplate,
							comboTeam, buttonAccept, buttonStart, buttonMapStartPosition);
					}
				}

			}
			else if(myGame->amIHost())
			{
				EnableAcceptControls(FALSE, myGame, comboPlayer, comboColor, comboPlayerTemplate,
					comboTeam, buttonAccept, buttonStart, buttonMapStartPosition, i);
			}
			if(slot->isHuman())
			{
				UnicodeString newName = slot->getName();
				UnicodeString oldName = GadgetComboBoxGetText(comboPlayer[i]);
				if (comboPlayer[i] && newName.compare(oldName))
				{
					GadgetComboBoxSetText(comboPlayer[i], newName);
				}
				if(i!= 0 && buttonAccept && buttonAccept[i])
				{
					buttonAccept[i]->winHide(FALSE);
				//Color In the little accepted boxes
					if(slot->isAccepted())
					{
						if(BitIsSet(buttonAccept[i]->winGetStatus(), WIN_STATUS_IMAGE	))
							buttonAccept[i]->winEnable(TRUE);
						else
							GadgetButtonSetEnabledColor(buttonAccept[i], acceptTrueColor );
					}
					else
					{
						if(BitIsSet(buttonAccept[i]->winGetStatus(), WIN_STATUS_IMAGE	))
							buttonAccept[i]->winEnable(FALSE);
						else
							GadgetButtonSetEnabledColor(buttonAccept[i], acceptFalseColor );
					}
				}
			}
			else
			{
				// Combo position is not the SlotState value: the list runs
				// Open/Closed/Tactical/Hard/Medium/Easy, and SLOT_PLAYER and
				// SLOT_TACTICAL_AI both sit past its end.
				GadgetComboBoxSetSelectedPos(comboPlayer[i], lobbyComboPosFromSlotState(slot->getState()), TRUE);
				if (buttonAccept && buttonAccept[i])
					buttonAccept[i]->winHide(TRUE);

				// NGMP: Support host migration, names can change for non-human occupied slots during migration
				GadgetComboBoxSetText(comboPlayer[i], slot->getName());
			}
/*
			if (myGame->getLocalSlotNum() == i && i!=0)
			{
				if (comboPlayer[i])
					comboPlayer[i]->winEnable( TRUE );
			}
			else*/ if (!myGame->amIHost())
			{
				if (comboPlayer[i])
					comboPlayer[i]->winEnable( FALSE );
			}
			//if( i == myGame->getLocalSlotNum())
      if((comboColor[i] != nullptr) && BitIsSet(comboColor[i]->winGetStatus(), WIN_STATUS_ENABLED))
				PopulateColorComboBox(i, comboColor, myGame, myGame->getConstSlot(i)->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER);
			Int max, idx, attempt;
			if (comboColor[i] != nullptr) {
				Bool bFound = FALSE;
				for (attempt = 0; attempt < 2 && !bFound; ++attempt)
				{
					max = GadgetComboBoxGetLength(comboColor[i]);
					for (idx=0; idx<max; ++idx)
					{
						Int color = (Int)GadgetComboBoxGetItemData(comboColor[i], idx);
						if (color == slot->getColor())
						{
							GadgetComboBoxSetSelectedPos(comboColor[i], idx, TRUE);
							bFound = TRUE;
							break;
						}
					}
					// TheSuperHackers @bugfix bill-rich 24/09/2026 A slot whose colour combo is disabled (every
					// remote slot, on the host) is skipped by the populate above, so a colour it has not
					// listed before is missing and the cell keeps showing whatever it showed last. Rebuild
					// it once and look again, rather than leaving the host's lobby disagreeing with the
					// service and with every other client.
					if (!bFound && attempt == 0)
						PopulateColorComboBox(i, comboColor, myGame, myGame->getConstSlot(i)->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER);
				}
			}

			if (comboTeam[i] != nullptr) {
				max = GadgetComboBoxGetLength(comboTeam[i]);
				for (idx=0; idx<max; ++idx)
				{
					Int team = (Int)GadgetComboBoxGetItemData(comboTeam[i], idx);
					if (team == slot->getTeamNumber())
					{
						GadgetComboBoxSetSelectedPos(comboTeam[i], idx, TRUE);
						break;
					}
				}
			}

			if (comboPlayerTemplate[i] != nullptr) {
				max = GadgetComboBoxGetLength(comboPlayerTemplate[i]);
				for (idx=0; idx<max; ++idx)
				{
					Int playerTemplate = (Int)GadgetComboBoxGetItemData(comboPlayerTemplate[i], idx);
					if (playerTemplate == slot->getPlayerTemplate())
					{
						GadgetComboBoxSetSelectedPos(comboPlayerTemplate[i], idx, TRUE);
						break;
					}
				}
			}
		}
	}
}

// -----------------------------------------------------------------------------

