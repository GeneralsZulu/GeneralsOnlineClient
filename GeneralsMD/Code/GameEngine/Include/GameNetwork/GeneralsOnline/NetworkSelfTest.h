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

// NetworkSelfTest.h
// -----------------------------------------------------------------------------
// TheSuperHackers @feature bill-rich 11/10/2026
//
// Client-side network diagnostics for the GeneralsOnline port, and the
// "ship the logs now" path for failures that never reach the end-of-match
// telemetry (a lobby that will not start, a crash, a player who asks).
//
// The self-test runs on a worker thread and writes NETTEST lines to the
// release log:
//   - local IPv4 addresses, classified (private / CGNAT / VPN / public)
//   - STUN binding results from one socket toward several servers, which is
//     what tells an endpoint-independent (cone) NAT from a symmetric one: a
//     symmetric NAT hands every destination a different public port, and no
//     amount of hole punching reaches it
//   - a second socket's mapping, to show the port allocation pattern
//   - TURN reachability (an unauthenticated Allocate must come back 401 with
//     the server's realm; a timeout means the relay cannot be reached at all)
// and then, if asked, uploads the release log and the GeneralsOnline network
// log to the cncstats /logs endpoint under a per-day "connfail" bucket.
// -----------------------------------------------------------------------------

#pragma once

#include "Lib/BaseType.h"
#include "Common/UnicodeString.h"

// Start the self-test on a worker thread. `reason` is logged. Returns a short
// status line for the lobby chat ("started", "already running").
UnicodeString NetSelfTest_Start(const char *reason, Bool uploadWhenDone);

// Upload this client's diagnostic logs right now under
// connfail-YYYYMMDD/<nick>-HHMMSS-<phase>. Non-blocking, best-effort, and
// capped per session so a stuck lobby cannot spam the server.
void NetDiag_UploadLogs(const char *phase);

// A guest's answer to the service's full-mesh check came back with peers it
// could not reach, or the host was told the mesh is incomplete. Logs every
// time; runs the self-test and uploads (rate limited) only when attempt is the
// service's last one, since the first attempt usually completes on the retry.
void NetDiag_OnMeshIncomplete(const char *who, Int connected, Int expected, Int attempt);

// Called once from the engine's orderly shutdown. Leaves the marker that tells
// the next launch the previous session ended on purpose; a launch that finds
// the marker missing uploads the previous session's logs.
void NetDiag_NoteCleanShutdown();

// Called from the main menu update. Once per launch: if the previous session
// left a crash record (ReleaseCrashInfo.txt newer than the last upload
// marker), ship it with the previous session's logs. Waits briefly for the
// online login so the upload carries the player's name.
void NetDiag_CheckForCrashOnLaunch();
