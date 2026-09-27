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

// TheSuperHackers @feature bill-rich 15/09/2026 Lobby "Randomize" button. The placement mirrors
// populateRandomSideAndColor / populateRandomStartPosition in GameLogic.cpp so the
// lobby result looks like what a match start would have produced, but it is drawn
// from an independent, clock-seeded RNG: nothing here consumes or predicts the
// game-logic RNG that match start seeds from the lobby seed.

#include "PreRTS.h"

#include "GameNetwork/RandomAssign.h"
#include "GameNetwork/GameInfo.h"
#include "Common/PlayerTemplate.h"
#include "Common/MultiplayerSettings.h"
#include "Common/GlobalData.h"
#include "Common/StatsUploader.h"
#include "GameClient/MapUtil.h"
#include "GameClient/ChallengeGenerals.h"

#include <cfloat>
#include <cmath>
#include <random>
#include <vector>

// Private RNG for the roll: seeded from the clock, never the game-logic RNG, and
// not the process CRT RNG either so the roll has no side effect on anything else.
static Int rollBelow(Int n)
{
	static std::mt19937 s_rng((unsigned int)std::random_device{}() ^ (unsigned int)timeGetTime());
	return (n > 0) ? (Int)(s_rng() % (unsigned int)n) : 0;
}

// Build the list of valid template indices for random assignment.
// Same filtering as populateRandomSideAndColor (GameLogic.cpp).
static void buildValidTemplates(const GameInfo *game, std::vector<Int> &out)
{
	Int count = ThePlayerTemplateStore->getPlayerTemplateCount();
	for (Int c = 0; c < count; ++c)
	{
		const PlayerTemplate *fac = ThePlayerTemplateStore->getNthPlayerTemplate(c);
		if (!fac)
			continue;

		// Must have a starting building (filters out civilian etc)
		if (fac->getStartingBuilding().isEmpty())
			continue;

		// Respect old factions only mode
		if (game->oldFactionsOnly() && !fac->isOldFaction())
			continue;

		// Skip generals that are not unlocked on this client, the same test the
		// lobby faction combo applies (GUIUtil.cpp) and match start enforces.
		const GeneralPersona *general = TheChallengeGenerals->getGeneralByTemplateName(fac->getName());
		if (general && !general->isStartingEnabled())
			continue;

		out.push_back(c);
	}
}

// Phase 1: Assign random factions and colors.
// Mirrors populateRandomSideAndColor (GameLogic.cpp:691-781).
// Only touches slots that have PLAYERTEMPLATE_RANDOM / color == -1.
static void assignRandomFactions(GameInfo *game, const std::vector<Int> &validTemplates)
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		GameSlot *slot = game->getSlot(i);
		if (!slot || !slot->isOccupied() || slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			continue;

		// Assign faction if random
		Int playerTemplateIdx = slot->getPlayerTemplate();
		if (playerTemplateIdx == PLAYERTEMPLATE_RANDOM && !validTemplates.empty())
		{
			playerTemplateIdx = validTemplates[rollBelow((Int)validTemplates.size())];
			slot->setPlayerTemplate(playerTemplateIdx);
		}

		// Assign color if random (-1)
		Int colorIdx = slot->getColor();
		if (colorIdx < 0 || colorIdx >= TheMultiplayerSettings->getNumColors())
		{
			// Pick uniformly among the colors nobody holds yet, so a full lobby
			// never ends up with a slot left at -1 because a blind retry loop
			// kept landing on taken colors.
			std::vector<Int> freeColors;
			Int numColors = TheMultiplayerSettings->getNumColors();
			for (Int c = 0; c < numColors; ++c)
			{
				if (!game->isColorTaken(c))
					freeColors.push_back(c);
			}
			if (!freeColors.empty())
				slot->setColor(freeColors[rollBelow((Int)freeColors.size())]);
		}
	}
}

// ------------------------------------------------------------------------------------------------
// Global start-position solver.
//
// Instead of placing players one at a time (which can strand the last player
// next to the enemy team), enumerate every distinct team-to-spot assignment
// and score the whole arrangement. There are at most 8 spots and players on
// the same team are interchangeable, so the search space is tiny (a 4v4 on an
// 8-spot map is 70 groupings; a full 8-player FFA is 8! = 40320).
//
// Scoring is lexicographic: first maximize the smallest distance between any
// two players on different teams (no one gets stranded in enemy territory),
// then minimize the summed distance between teammates (allies group up).
// Distances come from the map cache's ground-path matrix when the cache build
// computed one (so a river or cliff between two spots makes them "far" even
// when they look adjacent), falling back to straight-line otherwise.
//
// Assignments within 10% of the best on both criteria are treated as
// equivalent and one is picked at random, keeping spawn variety on symmetric
// maps. All randomness flows through the caller-supplied randFunc, so the
// deterministic game-start path (GameLogicRandomValue) and the host-only
// lobby path (rand) both work.
// ------------------------------------------------------------------------------------------------

struct SpotSolverState
{
	Int numSpots;
	Real dist[MAX_SLOTS][MAX_SLOTS];

	Int numPlayers;               // combatants (fixed and unfixed)
	Int playerTeam[MAX_SLOTS];    // -1 = no team
	Int playerSlot[MAX_SLOTS];    // lobby slot index
	Int playerSpot[MAX_SLOTS];    // current assignment, -1 = unassigned
	Bool playerFixed[MAX_SLOTS];  // spot was already chosen deliberately

	Int unfixed[MAX_SLOTS];       // player indices needing a spot, teammates adjacent
	Int numUnfixed;

	Bool spotUsed[MAX_SLOTS];

	// pass thresholds
	Real bestMinCross;
	Real bestIntra;
	Real crossThresh;
	Real intraThresh;

	// reservoir pick among equivalent assignments
	StartSpotRandomFunc randFunc;
	Int tieCount;
	Int chosenSpot[MAX_SLOTS];

	Int pass; // 0: max minCross, 1: min intra among cross-eligible, 2: pick
};

static void scoreCurrentAssignment(const SpotSolverState *st, Real *outMinCross, Real *outIntra)
{
	Real minCross = FLT_MAX;
	Real intra = 0.0f;
	Int i, j;
	for (i = 0; i < st->numPlayers; ++i)
	{
		if (st->playerSpot[i] < 0)
			continue;
		for (j = i + 1; j < st->numPlayers; ++j)
		{
			if (st->playerSpot[j] < 0)
				continue;
			Real d = st->dist[st->playerSpot[i]][st->playerSpot[j]];
			if (st->playerTeam[i] >= 0 && st->playerTeam[i] == st->playerTeam[j])
				intra += d;
			else if (d < minCross)
				minCross = d;
		}
	}
	*outMinCross = minCross;
	*outIntra = intra;
}

static void visitAssignment(SpotSolverState *st)
{
	Real minCross, intra;
	scoreCurrentAssignment(st, &minCross, &intra);
	switch (st->pass)
	{
	case 0:
		if (minCross > st->bestMinCross)
			st->bestMinCross = minCross;
		break;
	case 1:
		if (minCross >= st->crossThresh && intra < st->bestIntra)
			st->bestIntra = intra;
		break;
	case 2:
		if (minCross >= st->crossThresh && intra <= st->intraThresh)
		{
			// reservoir sampling: the nth eligible assignment survives with
			// probability 1/n, giving a uniform pick without storing them all
			++st->tieCount;
			if (st->randFunc(0, st->tieCount - 1) == 0)
			{
				Int i;
				for (i = 0; i < st->numPlayers; ++i)
					st->chosenSpot[i] = st->playerSpot[i];
			}
		}
		break;
	}
}

static void enumerateAssignments(SpotSolverState *st, Int depth)
{
	if (depth == st->numUnfixed)
	{
		visitAssignment(st);
		return;
	}
	Int p = st->unfixed[depth];
	Int minSpot = 0;
	if (depth > 0)
	{
		// Teammates are interchangeable for scoring, so force ascending spot
		// order within a team to visit each grouping exactly once.
		Int q = st->unfixed[depth - 1];
		if (st->playerTeam[p] >= 0 && st->playerTeam[p] == st->playerTeam[q])
			minSpot = st->playerSpot[q] + 1;
	}
	Int s;
	for (s = minSpot; s < st->numSpots; ++s)
	{
		if (st->spotUsed[s])
			continue;
		st->playerSpot[p] = s;
		st->spotUsed[s] = TRUE;
		enumerateAssignments(st, depth + 1);
		st->spotUsed[s] = FALSE;
		st->playerSpot[p] = -1;
	}
}

void assignStartPositionsGlobal(GameInfo *game, StartSpotRandomFunc randFunc)
{
	if (!game || !randFunc)
		return;

	Int numSpots = MAX_SLOTS;
	const MapMetaData *md = TheMapCache ? TheMapCache->findMap(game->getMap()) : NULL;
	if (md)
		numSpots = md->m_numPlayers;
	if (numSpots <= 0 || numSpots > MAX_SLOTS)
		return;

	SpotSolverState st;
	st.numSpots = numSpots;
	st.randFunc = randFunc;

	// Distance matrix: ground-path distance from the map cache when
	// available, straight-line between start waypoints otherwise.
	static const WaypointMap s_emptyWaypoints = WaypointMap();
	const WaypointMap &waypoints = md ? md->m_waypoints : s_emptyWaypoints;
	Int i, j;
	for (i = 0; i < MAX_SLOTS; ++i)
		for (j = 0; j < MAX_SLOTS; ++j)
			st.dist[i][j] = 0.0f;
	for (i = 0; i < numSpots; ++i)
	{
		for (j = i + 1; j < numSpots; ++j)
		{
			Real d = 0.0f;
			if (md && md->m_startSpotPathDist[i][j] > 0.0f)
			{
				d = md->m_startSpotPathDist[i][j];
			}
			else
			{
				AsciiString w1, w2;
				w1.format("Player_%d_Start", i + 1);
				w2.format("Player_%d_Start", j + 1);
				WaypointMap::const_iterator c1 = waypoints.find(w1);
				WaypointMap::const_iterator c2 = waypoints.find(w2);
				if (c1 == waypoints.end() || c2 == waypoints.end())
				{
					d = 1000000.0f;
				}
				else
				{
					Coord3D p1 = c1->second;
					Coord3D p2 = c2->second;
					d = sqrt(sqr(p1.x - p2.x) + sqr(p1.y - p2.y));
				}
			}
			st.dist[i][j] = st.dist[j][i] = d;
		}
	}

	// Collect combatants; spots that were deliberately chosen stay fixed and
	// still participate in scoring.
	Int s;
	for (s = 0; s < MAX_SLOTS; ++s)
		st.spotUsed[s] = (s < numSpots) ? FALSE : TRUE;
	st.numPlayers = 0;
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		GameSlot *slot = game->getSlot(i);
		if (!slot || !slot->isOccupied() || slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			continue;
		Int p = st.numPlayers++;
		st.playerSlot[p] = i;
		st.playerTeam[p] = slot->getTeamNumber();
		st.playerSpot[p] = -1;
		st.playerFixed[p] = FALSE;
		Int posIdx = slot->getStartPos();
		if (posIdx >= 0 && posIdx < numSpots && !st.spotUsed[posIdx])
		{
			st.playerSpot[p] = posIdx;
			st.playerFixed[p] = TRUE;
			st.spotUsed[posIdx] = TRUE;
		}
	}

	// Unfixed players, with teammates adjacent (required by the ascending
	// spot-order pruning in enumerateAssignments).
	Bool added[MAX_SLOTS];
	for (i = 0; i < MAX_SLOTS; ++i)
		added[i] = FALSE;
	st.numUnfixed = 0;
	for (i = 0; i < st.numPlayers; ++i)
	{
		if (st.playerFixed[i] || added[i])
			continue;
		st.unfixed[st.numUnfixed++] = i;
		added[i] = TRUE;
		if (st.playerTeam[i] >= 0)
		{
			for (j = i + 1; j < st.numPlayers; ++j)
			{
				if (!st.playerFixed[j] && !added[j] && st.playerTeam[j] == st.playerTeam[i])
				{
					st.unfixed[st.numUnfixed++] = j;
					added[j] = TRUE;
				}
			}
		}
	}
	if (st.numUnfixed == 0)
		return;

	Int freeSpots = 0;
	for (s = 0; s < numSpots; ++s)
	{
		if (!st.spotUsed[s])
			++freeSpots;
	}
	if (st.numUnfixed > freeSpots)
	{
		// More players than start spots shouldn't happen; fill sequentially
		// rather than leave anyone without a position.
		s = 0;
		for (i = 0; i < st.numUnfixed && s < numSpots; ++i)
		{
			while (s < numSpots && st.spotUsed[s])
				++s;
			if (s < numSpots)
			{
				GameSlot *slot = game->getSlot(st.playerSlot[st.unfixed[i]]);
				if (slot)
					slot->setStartPos(s);
				st.spotUsed[s] = TRUE;
			}
		}
		return;
	}

	// Pass 0: widest achievable separation between opposing players.
	st.pass = 0;
	st.bestMinCross = -1.0f;
	enumerateAssignments(&st, 0);
	if (st.bestMinCross >= FLT_MAX)
		st.crossThresh = FLT_MAX; // no opposing pairs at all (everyone allied)
	else
		st.crossThresh = st.bestMinCross * 0.90f;

	// Pass 1: tightest team grouping among well-separated assignments.
	st.pass = 1;
	st.bestIntra = FLT_MAX;
	enumerateAssignments(&st, 0);
	st.intraThresh = st.bestIntra * 1.10f + 1.0f;

	// Pass 2: pick uniformly among assignments that are (near-)optimal on
	// both criteria, for spawn variety on symmetric maps.
	st.pass = 2;
	st.tieCount = 0;
	for (i = 0; i < MAX_SLOTS; ++i)
		st.chosenSpot[i] = -1;
	enumerateAssignments(&st, 0);
	if (st.tieCount == 0)
		return;

	// The ascending-spot pruning in enumerateAssignments hands a team's spots
	// to its members in slot order, so without this the lowest slot on a team
	// always lands on the lowest-numbered spot. Which teammate gets which spot
	// doesn't affect the score, so Fisher-Yates within each team for variety.
	// Unteamed players don't need it: all their permutations tie, so the
	// reservoir pick above is already uniform across them.
	{
		Bool inTeamDone[MAX_SLOTS];
		for (i = 0; i < st.numPlayers; ++i)
			inTeamDone[i] = FALSE;
		for (i = 0; i < st.numPlayers; ++i)
		{
			if (inTeamDone[i] || st.playerFixed[i] || st.playerTeam[i] < 0 || st.chosenSpot[i] < 0)
				continue;
			Int members[MAX_SLOTS];
			Int numMembers = 0;
			for (j = i; j < st.numPlayers; ++j)
			{
				if (!inTeamDone[j] && !st.playerFixed[j] && st.chosenSpot[j] >= 0 && st.playerTeam[j] == st.playerTeam[i])
				{
					members[numMembers++] = j;
					inTeamDone[j] = TRUE;
				}
			}
			Int k;
			for (k = numMembers - 1; k > 0; --k)
			{
				Int r = randFunc(0, k);
				Int tmp = st.chosenSpot[members[k]];
				st.chosenSpot[members[k]] = st.chosenSpot[members[r]];
				st.chosenSpot[members[r]] = tmp;
			}
		}
	}

	for (i = 0; i < st.numPlayers; ++i)
	{
		if (st.playerFixed[i] || st.chosenSpot[i] < 0)
			continue;
		GameSlot *slot = game->getSlot(st.playerSlot[i]);
		if (slot)
			slot->setStartPos(st.chosenSpot[i]);
	}
}

// Phase 2: Assign start positions using distance-based placement.
// Mirrors populateRandomStartPosition (GameLogic.cpp:787-1053).
// Different teams are placed far apart, teammates close together.
// Only touches slots that have startPos == -1.
static void assignRandomPositions(GameInfo *game)
{
	Int i;
	const MapMetaData *md = TheMapCache ? TheMapCache->findMap(game->getMap()) : nullptr;
	if (!md)
		return; // unknown map: no start spots to hand out; match start will do it
	Int numPlayers = md->m_numPlayers;

	if (numPlayers <= 0)
		return;
	// The per-spot tables below are MAX_SLOTS wide; never index past them.
	if (numPlayers > MAX_SLOTS)
		numPlayers = MAX_SLOTS;

	// Build distance matrix between all start positions using map waypoints
	const WaypointMap &waypoints = md->m_waypoints;
	Real startSpotDistance[MAX_SLOTS][MAX_SLOTS];
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		for (Int j = 0; j < MAX_SLOTS; ++j)
		{
			if (i != j && i < numPlayers && j < numPlayers)
			{
				AsciiString w1, w2;
				w1.format("Player_%d_Start", i + 1);
				w2.format("Player_%d_Start", j + 1);
				WaypointMap::const_iterator c1 = waypoints.find(w1);
				WaypointMap::const_iterator c2 = waypoints.find(w2);
				if (c1 == waypoints.end() || c2 == waypoints.end())
				{
					startSpotDistance[i][j] = 1000000.0f;
				}
				else
				{
					Coord3D p1 = c1->second;
					Coord3D p2 = c2->second;
					startSpotDistance[i][j] = sqrt(sqr(p1.x - p2.x) + sqr(p1.y - p2.y));
				}
			}
			else
			{
				startSpotDistance[i][j] = 0.0f;
			}
		}
	}

	// Track which positions are already taken (deliberately assigned)
	Bool taken[MAX_SLOTS];
	for (i = 0; i < MAX_SLOTS; ++i)
		taken[i] = (i < numPlayers) ? FALSE : TRUE;

	Bool hasStartSpotBeenPicked = FALSE;
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		GameSlot *slot = game->getSlot(i);
		if (!slot || !slot->isOccupied() || slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			continue;

		Int posIdx = slot->getStartPos();
		if (posIdx >= 0 && posIdx < numPlayers)
		{
			hasStartSpotBeenPicked = TRUE;
			taken[posIdx] = TRUE;
		}
	}

	// Track first position per team for teammate clustering
	Int teamPosIdx[MAX_SLOTS];
	for (i = 0; i < MAX_SLOTS; ++i)
		teamPosIdx[i] = -1;

	// Seed teamPosIdx from already-assigned slots
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		const GameSlot *slot = game->getConstSlot(i);
		if (!slot || !slot->isOccupied() || slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			continue;
		Int posIdx = slot->getStartPos();
		if (posIdx >= 0 && posIdx < numPlayers)
		{
			Int team = slot->getTeamNumber();
			if (team >= 0 && teamPosIdx[team] == -1)
				teamPosIdx[team] = posIdx;
		}
	}

	// Assign positions for non-observer slots that don't have one yet
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		GameSlot *slot = game->getSlot(i);
		if (!slot || !slot->isOccupied() || slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			continue;

		Int posIdx = slot->getStartPos();
		if (posIdx >= 0 && posIdx < numPlayers)
			continue; // already assigned

		Int team = slot->getTeamNumber();

		if (!hasStartSpotBeenPicked)
		{
			// First player: pick randomly
			posIdx = -1;
			for (Int attempt = 0; attempt < numPlayers * 2 && posIdx == -1; ++attempt)
			{
				Int candidate = rollBelow(numPlayers);
				if (!taken[candidate])
					posIdx = candidate;
			}
			if (posIdx < 0)
				continue;
			hasStartSpotBeenPicked = TRUE;
			slot->setStartPos(posIdx);
			taken[posIdx] = TRUE;
			if (team >= 0)
				teamPosIdx[team] = posIdx;
		}
		else if (team < 0 || teamPosIdx[team] == -1)
		{
			// New team or no team: pick position farthest from all taken positions
			Real farthestDistance = 0.0f;
			Int farthestIndex = -1;
			for (posIdx = 0; posIdx < numPlayers; ++posIdx)
			{
				if (taken[posIdx])
					continue;

				Real dist = 0.0f;
				for (Int n = 0; n < numPlayers; ++n)
				{
					if (taken[n] && n != posIdx)
						dist += startSpotDistance[posIdx][n];
				}
				if (farthestIndex < 0 || dist > farthestDistance)
				{
					farthestDistance = dist;
					farthestIndex = posIdx;
				}
			}

			if (farthestIndex >= 0)
			{
				slot->setStartPos(farthestIndex);
				taken[farthestIndex] = TRUE;
				if (team >= 0)
					teamPosIdx[team] = farthestIndex;
			}
		}
		else
		{
			// Teammate: pick position closest to team's existing position
			Real closestDist = FLT_MAX;
			Int closestIdx = -1;
			for (Int n = 0; n < numPlayers; ++n)
			{
				if (!taken[n] && startSpotDistance[teamPosIdx[team]][n] < closestDist)
				{
					closestDist = startSpotDistance[teamPosIdx[team]][n];
					closestIdx = n;
				}
			}
			if (closestIdx >= 0)
			{
				slot->setStartPos(closestIdx);
				taken[closestIdx] = TRUE;
			}
		}
	}

	// Observer slots are left alone: they take a player's spot at match start.
}

void performRandomAssign(GameInfo *game, std::vector<RandomSlotAssignment> *outChanges)
{
	if (!game)
		return;

	Int before[MAX_SLOTS][3];
	Int i;
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		const GameSlot *slot = game->getConstSlot(i);
		before[i][0] = slot ? slot->getPlayerTemplate() : -1;
		before[i][1] = slot ? slot->getColor() : -1;
		before[i][2] = slot ? slot->getStartPos() : -1;
	}

	// Phase 1: Assign factions and colors for random slots
	std::vector<Int> validTemplates;
	buildValidTemplates(game, validTemplates);
	assignRandomFactions(game, validTemplates);

	// Phase 2: Assign start positions using distance-based placement
	assignRandomPositions(game);

	if (outChanges)
	{
		outChanges->clear();
		for (i = 0; i < MAX_SLOTS; ++i)
		{
			const GameSlot *slot = game->getConstSlot(i);
			if (!slot)
				continue;
			RandomSlotAssignment change;
			change.slotIndex = i;
			if (slot->getPlayerTemplate() != before[i][0])
				change.side = slot->getPlayerTemplate();
			if (slot->getColor() != before[i][1])
				change.color = slot->getColor();
			if (slot->getStartPos() != before[i][2])
				change.startPos = slot->getStartPos();
			if (change.side != -1 || change.color != -1 || change.startPos != -1)
				outChanges->push_back(change);
		}
	}

	// Reset accepted state since we changed settings
	game->resetAccepted();
}

//-----------------------------------------------------------------------------
// Team balancing via the radarvan balance_teams API. Ported back from the Zulu
// client: the Randomize button asks the server for a fair split before it rolls
// the factions, so a pick-up game starts even instead of whoever-sits-where.
//-----------------------------------------------------------------------------

// Translate a slot's display name into the form expected by the
// balance_teams API. AI slot names ("Easy Army", "Hard Army", ...) are
// joined to a single token ("EasyArmy", "HardArmy") so the server's name
// table doesn't have to care about spacing. Human names pass through.
static AsciiString slotNameForApi(const GameSlot *slot)
{
	AsciiString out;
	out.translate(slot->getName());
	if (slot->isAI())
	{
		AsciiString joined;
		for (const char *p = out.str(); *p != '\0'; ++p)
		{
			if (*p != ' ')
				joined.concat(*p);
		}
		out = joined;
	}
	return out;
}

// Hand-maintained aliases for slot-display-name <-> server-canonical-name
// pairs that prefix matching can't bridge (e.g. a numeric in-game handle vs.
// a spelled-out canonical). Either form may appear on either side of the
// comparison.
struct BalanceNameAlias { const char *a; const char *b; };
static const BalanceNameAlias g_balanceNameAliases[] = {
	{ "131", "OneThree111" },
};
static const size_t g_balanceNameAliasCount =
	sizeof(g_balanceNameAliases) / sizeof(g_balanceNameAliases[0]);

static Bool aliasMatch(const AsciiString &x, const AsciiString &y)
{
	for (size_t k = 0; k < g_balanceNameAliasCount; ++k)
	{
		const BalanceNameAlias &al = g_balanceNameAliases[k];
		if ((x.compareNoCase(al.a) == 0 && y.compareNoCase(al.b) == 0) ||
		    (x.compareNoCase(al.b) == 0 && y.compareNoCase(al.a) == 0))
		{
			return TRUE;
		}
	}
	return FALSE;
}

// Case-insensitive prefix compare of two char arrays for `n` chars.
static Bool prefixMatchNoCase(const char *a, const char *b, Int n)
{
	for (Int i = 0; i < n; ++i)
	{
		char ca = a[i];
		char cb = b[i];
		if (ca == 0 || cb == 0) return FALSE;
		if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
		if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
		if (ca != cb) return FALSE;
	}
	return TRUE;
}

// The balance-teams server canonicalizes the names it receives ("Pan" comes
// back as "Pancake", "Wild" as "WildCard"), so a slot's display name will
// often differ from the form the server returns. Treat exact-equal-no-case as
// the strongest signal, with case-insensitive prefix-match-in-either-direction
// as the fallback. The 2-character minimum guards against single-letter
// collisions.
static Bool slotMatchesCanonical(const AsciiString &slotName, const AsciiString &canonical)
{
	if (slotName.compareNoCase(canonical) == 0) return TRUE;
	if (aliasMatch(slotName, canonical)) return TRUE;
	Int slen = slotName.getLength();
	Int clen = canonical.getLength();
	if (slen < 2 || clen < 2) return FALSE;
	Int shortLen = (slen < clen) ? slen : clen;
	return prefixMatchNoCase(slotName.str(), canonical.str(), shortLen);
}

struct BalanceSlotPlan { Int slotIdx; Int team; };

// True if every occupied non-observer slot has m_teamNumber == -1.
static Bool allTeamsUnset(const GameInfo *game)
{
	for (Int i = 0; i < MAX_SLOTS; ++i)
	{
		const GameSlot *slot = game->getConstSlot(i);
		if (!slot || !slot->isOccupied())
			continue;
		if (slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			continue;
		if (slot->getTeamNumber() != -1)
			return FALSE;
	}
	return TRUE;
}

bool tryBalanceTeamsViaApi(GameInfo *game, AsciiString *outError)
{
	if (!game)
		return true;

	if (!allTeamsUnset(game))
		return true;

	// Collect names from occupied non-observer slots in slot order.
	std::vector<AsciiString> names;
	Int i;
	for (i = 0; i < MAX_SLOTS; ++i)
	{
		const GameSlot *slot = game->getConstSlot(i);
		if (!slot || !slot->isOccupied())
			continue;
		if (slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			continue;

		AsciiString asciiName = slotNameForApi(slot);
		if (asciiName.isEmpty())
			continue;
		names.push_back(asciiName);
	}

	if (names.size() < 2)
		return true; // nothing to balance

	BalanceTeamsResult res = BalanceTeamsFromServer(TheGlobalData->m_balanceTeamsUrl, names);
	if (!res.success)
	{
		if (outError)
			*outError = res.errorMessage;
		return false;
	}

	// Decide a target team per slot:
	//   - matches a name in res.team1                   -> team 0
	//   - matches a recognized name not in res.team1    -> team 1
	//   - matches no canonical name (server didn't recognize) -> defer
	// Deferred slots are then distributed to whichever team has fewer members,
	// so a 4-player lobby with one unrecognized name still ends up 2v2 instead
	// of dumping everything unmatched onto team 1.
	std::vector<BalanceSlotPlan> plans;
	plans.reserve(MAX_SLOTS);

	for (i = 0; i < MAX_SLOTS; ++i)
	{
		GameSlot *slot = game->getSlot(i);
		if (!slot || !slot->isOccupied())
			continue;
		if (slot->getPlayerTemplate() == PLAYERTEMPLATE_OBSERVER)
			continue;

		AsciiString asciiName = slotNameForApi(slot);

		Int team = -1;
		size_t j;
		for (j = 0; j < res.team1.size() && team == -1; ++j)
		{
			if (slotMatchesCanonical(asciiName, res.team1[j]))
				team = 0;
		}
		if (team == -1)
		{
			for (j = 0; j < res.allKnown.size() && team == -1; ++j)
			{
				Bool isInTeam1 = FALSE;
				size_t a;
				for (a = 0; a < res.team1.size(); ++a)
				{
					if (res.allKnown[j].compareNoCase(res.team1[a]) == 0)
					{
						isInTeam1 = TRUE;
						break;
					}
				}
				if (isInTeam1) continue;
				if (slotMatchesCanonical(asciiName, res.allKnown[j]))
					team = 1;
			}
		}

		BalanceSlotPlan plan;
		plan.slotIdx = i;
		plan.team = team;
		plans.push_back(plan);
	}

	Int t0count = 0, t1count = 0;
	size_t pi;
	for (pi = 0; pi < plans.size(); ++pi)
	{
		if (plans[pi].team == 0) ++t0count;
		else if (plans[pi].team == 1) ++t1count;
	}
	for (pi = 0; pi < plans.size(); ++pi)
	{
		if (plans[pi].team != -1) continue;
		if (t0count <= t1count)
		{
			plans[pi].team = 0;
			++t0count;
		}
		else
		{
			plans[pi].team = 1;
			++t1count;
		}
	}

	// Ensure the host (slot 0) ends up on team 0 (UI "Team 1"). If the host
	// landed on team 1 because of how the server split names, flip both teams
	// so the host's side is always Team 1.
	Int hostTeam = -1;
	for (pi = 0; pi < plans.size(); ++pi)
	{
		if (plans[pi].slotIdx == 0)
		{
			hostTeam = plans[pi].team;
			break;
		}
	}
	if (hostTeam == 1)
	{
		for (pi = 0; pi < plans.size(); ++pi)
		{
			if (plans[pi].team == 0)
				plans[pi].team = 1;
			else if (plans[pi].team == 1)
				plans[pi].team = 0;
		}
	}

	for (pi = 0; pi < plans.size(); ++pi)
	{
		GameSlot *slot = game->getSlot(plans[pi].slotIdx);
		if (!slot) continue;
		slot->setTeamNumber(plans[pi].team);
	}

	return true;
}
