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

// TheSuperHackers @feature bill-rich 15/09/2026 Lobby "Randomize" button: resolves every slot still set
// to Random (faction, color, start position) in the lobby, so a group no longer has
// to start and abandon a match just to learn the engine's random draw.

#pragma once

#include "Lib/BaseType.h"
#include "Common/AsciiString.h"

#include <vector>

class GameInfo;

// One slot the roll changed. A field left at -1 was not touched by the roll.
struct RandomSlotAssignment
{
	Int slotIndex = -1;
	Int side = -1;
	Int color = -1;
	Int startPos = -1;
	Int team = -1;   // only resume-from-replay restores teams; a roll never changes them
};

// Resolve random factions, colors and start positions on every occupied player
// slot of the lobby that still has them unset. Slots with explicit choices and
// observer slots are left alone (observers are resolved at match start, as in
// retail). Draws from a private RNG seeded from the clock, deliberately NOT from
// the game-logic RNG, so the result says nothing about the draw the engine will
// make from the lobby seed at match start. outChanges, when given, receives the
// slots that changed and what changed, for callers that must push the roll
// through an authoritative service instead of applying it locally.
void performRandomAssign(GameInfo *game, std::vector<RandomSlotAssignment> *outChanges = nullptr);

// If every occupied non-observer slot has team == -1, query the balance_teams
// API (TheGlobalData->m_balanceTeamsUrl) with the lobby's player names and
// assign team 0 / team 1 from the answer. Returns true on success or no-op
// (teams already set, or fewer than two candidates). Returns false on an API or
// network failure; *outError is then a player-facing message fit for chat, and
// the slots are left untouched.
bool tryBalanceTeamsViaApi(GameInfo *game, AsciiString *outError);

// Source of randomness for assignStartPositionsGlobal. Returns a value in
// [lo, hi] inclusive. Game start passes a GameLogicRandomValue wrapper so
// every peer computes the same assignment.
typedef Int (*StartSpotRandomFunc)(Int lo, Int hi);

// Assign start positions to every occupied non-observer slot that doesn't
// have one, by enumerating all team-to-spot assignments and scoring the
// arrangement as a whole: maximize the smallest opposing-player distance,
// then minimize teammate spread, then pick randomly among near-ties.
// Distances are ground-path based when the map cache has them (rivers and
// cliffs separate spots that look adjacent), straight-line otherwise.
// Deliberately chosen spots are honored and score as fixed points.
// Which teammate gets which of the team's spots is then shuffled (the
// enumeration itself hands them out in slot order).
void assignStartPositionsGlobal(GameInfo *game, StartSpotRandomFunc randFunc);
