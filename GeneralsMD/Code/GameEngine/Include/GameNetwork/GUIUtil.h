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

// FILE: GUIUtil.h //////////////////////////////////////////////////////
// Author: Matthew D. Campbell, Sept 2002

#pragma once

#include "GameClient/Color.h"

class GameWindow;
class GameInfo;
class MapMetaData;

void ShowUnderlyingGUIElements( Bool show, const char *layoutFilename, const char *parentName,
															 const char **gadgetsToHide, const char **perPlayerGadgetsToHide );

void PopulateColorComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver = FALSE);
void PopulatePlayerTemplateComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool allowObservers );
void PopulateTeamComboBox(Int comboBox, GameWindow *comboArray[], GameInfo *myGame, Bool isObserver = FALSE);
void PopulateStartingCashComboBox(GameWindow *comboBox, GameInfo *myGame);

// Lobby team-number text color, shared by the team dropdown and the map
// preview's start-position markers. teamNumber is 0-based (team 1 = 0, etc.);
// negative values (no team) return white.
Color GetTeamUiColor( Int teamNumber );

// Locate a map in MapCache by the display name a server handed us (radarvan's
// map pick returns a human-readable name, not a path). Matching is
// case-insensitive and space-insensitive, and strips the two suffixes the
// engine adds to m_displayName -- the " (N)" player count and a trailing
// ".map" -- so "TournamentA" finds "Tournament A (2)". Returns the metadata and
// writes the MapCache key (the lowercase path the engine uses internally) into
// *outMapCacheKey; nullptr when nothing matches.
const MapMetaData *FindMapByDisplayName( const AsciiString &chosenDisplayName, AsciiString *outMapCacheKey );

// Install a map this client does not have, by the CRC a server handed us, from
// the cncstats map endpoint (which is keyed by CRC, so the name only decides
// where the bytes land). serverMapFileName is the server's own relative path when
// it sends one; otherwise a path is derived from chosenDisplayName under the user
// maps directory. Blocking: the caller has already made one blocking request to
// get here. Returns the installed map's metadata and writes its MapCache key into
// *outMapCacheKey, or nullptr when there is nothing to fetch, the download fails,
// the CRC does not verify, or the installed file still does not show up in the
// cache.
const MapMetaData *InstallMapByCRC( const AsciiString &chosenDisplayName,
                                    const AsciiString &serverMapFileName,
                                    UnsignedInt mapCRC,
                                    UnsignedInt contentsMask,
                                    AsciiString *outMapCacheKey );

void EnableSlotListUpdates( Bool val );
Bool AreSlotListUpdatesEnabled();

void UpdateSlotList( GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[],
										GameWindow *buttonStart, GameWindow *buttonMapStartPosition[] );

void EnableAcceptControls(Bool Enabled, GameInfo *myGame, GameWindow *comboPlayer[],
										GameWindow *comboColor[], GameWindow *comboPlayerTemplate[],
										GameWindow *comboTeam[], GameWindow *buttonAccept[], GameWindow *buttonStart,
										GameWindow *buttonMapStartPosition[], Int slotNum = -1);
