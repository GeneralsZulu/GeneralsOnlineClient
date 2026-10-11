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

// NetworkSelfTest.cpp - see NetworkSelfTest.h for the rationale.

// winsock2 must precede any <windows.h> (PreRTS pulls one in), else winsock.h
// gets in first and the two headers collide. Same order udp.cpp uses.
#include <winsock2.h>
#include <ws2tcpip.h>

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameNetwork/GeneralsOnline/NetworkSelfTest.h"

#include "Common/GlobalData.h"
#include "Common/ReleaseLog.h"
#include "Common/StatsUploader.h"
#include "GameNetwork/LANObserverStream.h"
#include "GameNetwork/GeneralsOnline/NGMP_include.h"
#include "GameNetwork/GeneralsOnline/OnlineServices_Init.h"
#include "GameNetwork/GeneralsOnline/OnlineServices_Auth.h"
#include "GameNetwork/GeneralsOnline/OnlineServices_LobbyInterface.h"

#include <windows.h>
#include <process.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <atomic>

// -----------------------------------------------------------------------------
// Shared helpers
// -----------------------------------------------------------------------------

// The player's name for the upload path: the online display name once logged
// in, else "anonymous". The crash-on-launch path waits for the login so the
// bucket is attributable.
static std::string diagNick()
{
	NGMP_OnlineServicesManager *mgr = NGMP_OnlineServicesManager::GetInstance();
	if (mgr != nullptr)
	{
		NGMP_OnlineServices_AuthInterface *auth = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_AuthInterface>();
		if (auth != nullptr && auth->GetUserID() != -1)
		{
			std::string n = auth->GetDisplayName();
			if (!n.empty())
				return n;
		}
	}
	return "anonymous";
}

// connfail-YYYYMMDD / <nick>-HHMMSS-<phase>, UTC so one game night lands in
// one bucket for every time zone.
static void diagLabels(const char *phase, std::string &seedLabel, std::string &playerId)
{
	SYSTEMTIME st;
	GetSystemTime(&st);
	char buf[128];
	snprintf(buf, sizeof(buf), "connfail-%04u%02u%02u", (unsigned)st.wYear, (unsigned)st.wMonth, (unsigned)st.wDay);
	seedLabel = buf;
	snprintf(buf, sizeof(buf), "%s-%02u%02u%02u-%s", diagNick().c_str(),
		(unsigned)st.wHour, (unsigned)st.wMinute, (unsigned)st.wSecond, phase ? phase : "diag");
	playerId = buf;
}

static const Int DIAG_MAX_UPLOADS_PER_SESSION = 6;
static std::atomic<int> s_diagUploads(0);

// The files every diagnostic upload carries. `prev` adds the previous
// session's logs (crash reports) on top of the current ones.
static void diagUpload(const std::string &logsUrl, const std::string &seedLabel,
	const std::string &playerId, Bool prev, Bool crashInfo)
{
	std::vector<AsciiString> paths;
	paths.push_back(AsciiString(ReleaseGetLogFileName()));
	if (NetworkLogGetFileName()[0] != '\0')
		paths.push_back(AsciiString(NetworkLogGetFileName()));
	if (prev)
	{
		paths.push_back(AsciiString(ReleaseGetPrevLogFileName()));
		if (NetworkLogGetPrevFileName()[0] != '\0')
			paths.push_back(AsciiString(NetworkLogGetPrevFileName()));
		paths.push_back(AsciiString(LANObsGetPrevLogFileName()));
	}
	if (crashInfo && TheGlobalData != nullptr)
	{
		AsciiString p = TheGlobalData->getPath_UserData();
		p.concat("ReleaseCrashInfo.txt");
		paths.push_back(p);
	}
	StartDiagnosticLogUpload(AsciiString(logsUrl.c_str()), AsciiString(seedLabel.c_str()),
		AsciiString(playerId.c_str()), paths);
}

void NetDiag_UploadLogs(const char *phase)
{
	if (TheGlobalData == nullptr || TheGlobalData->m_logsUrl.isEmpty())
		return;
	if (s_diagUploads.fetch_add(1) >= DIAG_MAX_UPLOADS_PER_SESSION)
		return;
	std::string seedLabel, playerId;
	diagLabels(phase, seedLabel, playerId);
	ReleaseLog("NETDIAG uploading logs as %s/%s", seedLabel.c_str(), playerId.c_str());
	diagUpload(TheGlobalData->m_logsUrl.str(), seedLabel, playerId, FALSE, FALSE);
}

// -----------------------------------------------------------------------------
// STUN / TURN probes (RFC 5389 binding, RFC 5766 allocate), bare minimum.
// -----------------------------------------------------------------------------

static const UnsignedInt STUN_MAGIC = 0x2112A442u;

struct StunResult
{
	bool   ok;
	bool   isError;      // an error response (TURN 401 is the expected one)
	int    errorCode;    // from ERROR-CODE, 0 if none
	std::string mapped;  // "ip:port" from XOR-MAPPED-ADDRESS / MAPPED-ADDRESS
	unsigned short mappedPort;
	std::string realm;
	std::string software;
	int    rttMs;
};

static void putU16(unsigned char *p, unsigned short v) { p[0] = (unsigned char)(v >> 8); p[1] = (unsigned char)v; }
static void putU32(unsigned char *p, UnsignedInt v) { p[0] = (unsigned char)(v >> 24); p[1] = (unsigned char)(v >> 16); p[2] = (unsigned char)(v >> 8); p[3] = (unsigned char)v; }
static unsigned short getU16(const unsigned char *p) { return (unsigned short)((p[0] << 8) | p[1]); }
static UnsignedInt getU32(const unsigned char *p) { return ((UnsignedInt)p[0] << 24) | ((UnsignedInt)p[1] << 16) | ((UnsignedInt)p[2] << 8) | p[3]; }

static bool resolveV4(const char *host, unsigned short port, sockaddr_in &out)
{
	addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_DGRAM;
	addrinfo *res = nullptr;
	if (getaddrinfo(host, nullptr, &hints, &res) != 0 || res == nullptr)
		return false;
	out = *(sockaddr_in *)res->ai_addr;
	out.sin_port = htons(port);
	freeaddrinfo(res);
	return true;
}

// One request/response exchange on `sock` toward `dst`. `msgType` 0x0001 =
// Binding, 0x0003 = Allocate (with REQUESTED-TRANSPORT=UDP). Two retries.
static StunResult stunExchange(SOCKET sock, const sockaddr_in &dst, unsigned short msgType)
{
	StunResult r;
	r.ok = false; r.isError = false; r.errorCode = 0; r.mappedPort = 0; r.rttMs = -1;

	unsigned char req[32];
	memset(req, 0, sizeof(req));
	int len = 20;
	if (msgType == 0x0003)
	{
		putU16(req + 20, 0x0019);	// REQUESTED-TRANSPORT
		putU16(req + 22, 4);
		req[24] = 17;				// UDP
		len = 28;
	}
	putU16(req, msgType);
	putU16(req + 2, (unsigned short)(len - 20));
	putU32(req + 4, STUN_MAGIC);
	unsigned char txid[12];
	UnsignedInt t = (UnsignedInt)GetTickCount() ^ (UnsignedInt)(size_t)&r;
	for (int i = 0; i < 12; ++i) { t = t * 1103515245u + 12345u; txid[i] = (unsigned char)(t >> 16); }
	memcpy(req + 8, txid, 12);

	for (int attempt = 0; attempt < 3 && !r.ok; ++attempt)
	{
		const DWORD t0 = GetTickCount();
		if (sendto(sock, (const char *)req, len, 0, (const sockaddr *)&dst, sizeof(dst)) != len)
			return r;
		for (;;)
		{
			fd_set fds; FD_ZERO(&fds); FD_SET(sock, &fds);
			timeval tv; tv.tv_sec = 1; tv.tv_usec = 500000;
			const int sel = select(0, &fds, nullptr, nullptr, &tv);
			if (sel <= 0)
				break;
			unsigned char buf[1500];
			sockaddr_in from; int fromLen = sizeof(from);
			const int n = recvfrom(sock, (char *)buf, sizeof(buf), 0, (sockaddr *)&from, &fromLen);
			if (n < 20 || getU32(buf + 4) != STUN_MAGIC || memcmp(buf + 8, txid, 12) != 0)
				continue;	// not ours (or a late reply to an earlier attempt)
			r.rttMs = (int)(GetTickCount() - t0);
			const unsigned short rtype = getU16(buf);
			r.isError = ((rtype & 0x0110) == 0x0110);
			int pos = 20;
			const int end = 20 + getU16(buf + 2);
			while (pos + 4 <= end && pos + 4 <= n)
			{
				const unsigned short atype = getU16(buf + pos);
				const unsigned short alen = getU16(buf + pos + 2);
				const unsigned char *av = buf + pos + 4;
				if (pos + 4 + alen > n)
					break;
				if ((atype == 0x0020 || atype == 0x0001) && alen >= 8 && av[1] == 0x01)
				{
					unsigned short port = getU16(av + 2);
					UnsignedInt ip = getU32(av + 4);
					if (atype == 0x0020) { port ^= (unsigned short)(STUN_MAGIC >> 16); ip ^= STUN_MAGIC; }
					char tmp[48];
					snprintf(tmp, sizeof(tmp), "%u.%u.%u.%u:%u", (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff, (unsigned)port);
					if (atype == 0x0020 || r.mapped.empty())
					{
						r.mapped = tmp;
						r.mappedPort = port;
					}
				}
				else if (atype == 0x0009 && alen >= 4)
				{
					r.errorCode = (av[2] & 0x07) * 100 + av[3];
				}
				else if (atype == 0x0014)
				{
					r.realm.assign((const char *)av, alen);
				}
				else if (atype == 0x8022)
				{
					r.software.assign((const char *)av, alen);
				}
				pos += 4 + ((alen + 3) & ~3);
			}
			r.ok = true;
			break;
		}
	}
	return r;
}

static const char *classifyV4(UnsignedInt ip)
{
	const unsigned a = (ip >> 24) & 0xff, b = (ip >> 16) & 0xff;
	if (a == 10) return "private";
	if (a == 172 && b >= 16 && b <= 31) return "private";
	if (a == 192 && b == 168) return "private";
	if (a == 100 && b >= 64 && b <= 127) return "CGNAT";
	if (a == 26) return "radmin-vpn";
	if (a == 169 && b == 254) return "link-local";
	if (a == 127) return "loopback";
	return "public";
}

// -----------------------------------------------------------------------------
// The test itself (worker thread)
// -----------------------------------------------------------------------------

struct SelfTestJob
{
	char reason[64];
	bool upload;
	char logsUrl[512];
	char seedLabel[64];
	char playerId[128];
	char turnUser[8];	// "present"/"NONE": only the fact, never the credential
};

static std::atomic<bool> s_selfTestRunning(false);

static unsigned __stdcall selfTestThreadProc(void *arg)
{
	SelfTestJob *job = (SelfTestJob *)arg;
	const DWORD tStart = GetTickCount();
	ReleaseLog("NETTEST start reason=\"%s\" turnCreds=%s", job->reason, job->turnUser);

	WSADATA wsa;
	const bool wsaOk = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);

	// Local addresses.
	{
		char host[256] = "";
		gethostname(host, sizeof(host));
		addrinfo hints; memset(&hints, 0, sizeof(hints)); hints.ai_family = AF_INET;
		addrinfo *res = nullptr;
		std::string addrs;
		if (getaddrinfo(host, nullptr, &hints, &res) == 0)
		{
			for (addrinfo *ai = res; ai != nullptr; ai = ai->ai_next)
			{
				const UnsignedInt ip = ntohl(((sockaddr_in *)ai->ai_addr)->sin_addr.s_addr);
				char tmp[64];
				snprintf(tmp, sizeof(tmp), "%s%u.%u.%u.%u(%s)", addrs.empty() ? "" : " ",
					(ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff, classifyV4(ip));
				addrs += tmp;
			}
			freeaddrinfo(res);
		}
		ReleaseLog("NETTEST local=[%s]", addrs.c_str());
	}

	// STUN from one socket toward several servers. Same public port everywhere
	// = endpoint-independent mapping (punchable). Different ports = symmetric.
	static const struct { const char *host; unsigned short port; } STUN_SERVERS[] = {
		{ "stun.l.google.com", 19302 },
		{ "stun1.l.google.com", 19302 },
		{ "stun.playgenerals.online", 3478 },
		{ "stun.cloudflare.com", 3478 },
	};
	std::vector<unsigned short> ports;
	std::string publicIp;
	SOCKET sockA = INVALID_SOCKET;
	if (wsaOk)
	{
		sockA = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sockA != INVALID_SOCKET)
		{
			sockaddr_in local; memset(&local, 0, sizeof(local)); local.sin_family = AF_INET;
			bind(sockA, (sockaddr *)&local, sizeof(local));
			for (size_t i = 0; i < sizeof(STUN_SERVERS) / sizeof(STUN_SERVERS[0]); ++i)
			{
				sockaddr_in dst;
				if (!resolveV4(STUN_SERVERS[i].host, STUN_SERVERS[i].port, dst))
				{
					ReleaseLog("NETTEST stun %s:%u dns=FAILED", STUN_SERVERS[i].host, STUN_SERVERS[i].port);
					continue;
				}
				const StunResult r = stunExchange(sockA, dst, 0x0001);
				if (!r.ok)
					ReleaseLog("NETTEST stun %s:%u no response", STUN_SERVERS[i].host, STUN_SERVERS[i].port);
				else
				{
					ReleaseLog("NETTEST stun %s:%u mapped=%s rtt=%dms", STUN_SERVERS[i].host, STUN_SERVERS[i].port,
						r.mapped.c_str(), r.rttMs);
					if (!r.mapped.empty())
					{
						ports.push_back(r.mappedPort);
						const size_t colon = r.mapped.find(':');
						const std::string ip = r.mapped.substr(0, colon);
						if (publicIp.empty()) publicIp = ip;
						else if (publicIp != ip) publicIp = "MULTIPLE";
					}
				}
			}
		}
	}
	// A second socket: does the NAT hand out ports near the first one, or
	// anywhere? (Informational; some symmetric NATs allocate sequentially.)
	if (wsaOk)
	{
		SOCKET sockB = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sockB != INVALID_SOCKET)
		{
			sockaddr_in dst;
			if (resolveV4(STUN_SERVERS[0].host, STUN_SERVERS[0].port, dst))
			{
				const StunResult r = stunExchange(sockB, dst, 0x0001);
				ReleaseLog("NETTEST stun second-socket mapped=%s", r.ok ? r.mapped.c_str() : "(no response)");
			}
			closesocket(sockB);
		}
	}
	if (ports.size() >= 2)
	{
		bool same = true;
		for (size_t i = 1; i < ports.size(); ++i) if (ports[i] != ports[0]) same = false;
		ReleaseLog("NETTEST verdict: mapping=%s publicIp=%s (%u servers answered)",
			same ? "endpoint-independent (cone, punchable)" : "SYMMETRIC (a different public port per destination; hole punching cannot reach this client, a relay is required)",
			publicIp.c_str(), (unsigned)ports.size());
	}
	else
	{
		ReleaseLog("NETTEST verdict: mapping=unknown (%u servers answered; UDP to the internet may be blocked)", (unsigned)ports.size());
	}

	// TURN reachability: an Allocate without credentials must come back 401
	// with the realm. A timeout means the relay is unreachable from here.
	static const struct { const char *host; unsigned short port; } TURN_SERVERS[] = {
		{ "turn.cloudflare.com", 3478 },	// what the self-hosted service hands out credentials for
		{ "turn.cloudflare.com", 443 },
		{ "turn.playgenerals.online", 3478 },	// GeneralsOnline's own relay, for comparison
	};
	if (sockA != INVALID_SOCKET)
	{
		for (size_t i = 0; i < sizeof(TURN_SERVERS) / sizeof(TURN_SERVERS[0]); ++i)
		{
			sockaddr_in dst;
			if (!resolveV4(TURN_SERVERS[i].host, TURN_SERVERS[i].port, dst))
			{
				ReleaseLog("NETTEST turn %s:%u dns=FAILED", TURN_SERVERS[i].host, TURN_SERVERS[i].port);
				continue;
			}
			const StunResult r = stunExchange(sockA, dst, 0x0003);
			if (!r.ok)
				ReleaseLog("NETTEST turn %s:%u no response (relay unreachable)", TURN_SERVERS[i].host, TURN_SERVERS[i].port);
			else
				ReleaseLog("NETTEST turn %s:%u reachable code=%d realm=\"%s\" software=\"%s\" rtt=%dms",
					TURN_SERVERS[i].host, TURN_SERVERS[i].port, r.errorCode, r.realm.c_str(), r.software.c_str(), r.rttMs);
		}
		closesocket(sockA);
	}

	if (wsaOk)
		WSACleanup();
	ReleaseLog("NETTEST done in %ums", (unsigned)(GetTickCount() - tStart));

	if (job->upload && job->logsUrl[0] != '\0')
	{
		ReleaseLog("NETTEST uploading logs as %s/%s", job->seedLabel, job->playerId);
		diagUpload(job->logsUrl, job->seedLabel, job->playerId, FALSE, FALSE);
	}

	s_selfTestRunning.store(false);
	free(job);
	return 0;
}

UnicodeString NetSelfTest_Start(const char *reason, Bool uploadWhenDone)
{
	if (s_selfTestRunning.exchange(true))
		return UnicodeString(L"Network test: already running");

	SelfTestJob *job = (SelfTestJob *)calloc(1, sizeof(SelfTestJob));
	if (job == nullptr)
	{
		s_selfTestRunning.store(false);
		return UnicodeString(L"Network test: could not start");
	}
	strncpy(job->reason, reason ? reason : "", sizeof(job->reason) - 1);
	job->upload = (uploadWhenDone != FALSE) && TheGlobalData != nullptr && !TheGlobalData->m_logsUrl.isEmpty();
	if (job->upload)
	{
		strncpy(job->logsUrl, TheGlobalData->m_logsUrl.str(), sizeof(job->logsUrl) - 1);
		std::string seedLabel, playerId;
		diagLabels("nettest", seedLabel, playerId);
		strncpy(job->seedLabel, seedLabel.c_str(), sizeof(job->seedLabel) - 1);
		strncpy(job->playerId, playerId.c_str(), sizeof(job->playerId) - 1);
		if (s_diagUploads.fetch_add(1) >= DIAG_MAX_UPLOADS_PER_SESSION)
			job->upload = false;
	}
	strcpy(job->turnUser, "NONE");
	if (NGMP_OnlineServicesManager::GetInstance() != nullptr)
	{
		NGMP_OnlineServices_LobbyInterface *lobby = NGMP_OnlineServicesManager::GetInterface<NGMP_OnlineServices_LobbyInterface>();
		if (lobby != nullptr && !lobby->GetLobbyTurnUsername().empty() && !lobby->GetLobbyTurnToken().empty())
			strcpy(job->turnUser, "present");
	}

	// The worker frees the job; read what the reply needs before it starts.
	const bool willUpload = job->upload;
	unsigned threadId = 0;
	HANDLE h = (HANDLE)_beginthreadex(nullptr, 0, selfTestThreadProc, job, 0, &threadId);
	if (h == NULL)
	{
		free(job);
		s_selfTestRunning.store(false);
		return UnicodeString(L"Network test: could not start");
	}
	CloseHandle(h);
	return willUpload
		? UnicodeString(L"Network test started; the results are uploaded to cncstats when it finishes (about 10 seconds)")
		: UnicodeString(L"Network test started; see ReleaseLog.txt in the game's user data folder");
}

// -----------------------------------------------------------------------------
// Mesh-check failure hook
// -----------------------------------------------------------------------------

void NetDiag_OnMeshIncomplete(const char *who, Int connected, Int expected)
{
	// Once per two minutes: the service retries the check and re-signals, and
	// every attempt would otherwise fire this again.
	static DWORD s_lastMs = 0;
	const DWORD now = GetTickCount();
	if (s_lastMs != 0 && (now - s_lastMs) < 120000)
		return;
	s_lastMs = now;
	ReleaseLog("NETDIAG mesh incomplete (%s): connected=%d expected=%d, running the self-test", who ? who : "", connected, expected);
	NetSelfTest_Start("mesh incomplete", TRUE);
}

// -----------------------------------------------------------------------------
// Crash report on the next launch
// -----------------------------------------------------------------------------

static bool fileMTime(const char *path, time_t &out)
{
	struct _stat st;
	if (_stat(path, &st) != 0)
		return false;
	out = st.st_mtime;
	return true;
}

void NetDiag_CheckForCrashOnLaunch()
{
	static Bool s_done = FALSE;
	static DWORD s_firstMs = 0;
	if (s_done || TheGlobalData == nullptr)
		return;
	if (s_firstMs == 0)
		s_firstMs = GetTickCount();

	AsciiString crashPath = TheGlobalData->getPath_UserData();
	crashPath.concat("ReleaseCrashInfo.txt");
	AsciiString markerPath = TheGlobalData->getPath_UserData();
	markerPath.concat("ReleaseCrashInfo.uploaded");

	time_t crashTime = 0;
	if (!fileMTime(crashPath.str(), crashTime))
	{
		s_done = TRUE;	// nothing to report
		return;
	}
	// Already shipped this one?
	{
		FILE *f = fopen(markerPath.str(), "r");
		if (f != nullptr)
		{
			long long seen = 0;
			if (fscanf(f, "%lld", &seen) == 1 && (time_t)seen == crashTime)
			{
				fclose(f);
				s_done = TRUE;
				return;
			}
			fclose(f);
		}
	}
	// Give the online login up to 20 s so the upload carries a name, then go
	// regardless: an unattributed report still beats none.
	if (diagNick() == "anonymous" && (GetTickCount() - s_firstMs) < 20000)
		return;

	s_done = TRUE;
	if (TheGlobalData->m_logsUrl.isEmpty())
		return;
	FILE *f = fopen(markerPath.str(), "w");
	if (f != nullptr)
	{
		fprintf(f, "%lld\n", (long long)crashTime);
		fclose(f);
	}
	std::string seedLabel, playerId;
	diagLabels("crash", seedLabel, playerId);
	ReleaseLog("NETDIAG previous session crashed (ReleaseCrashInfo.txt mtime %lld); uploading it with the previous logs as %s/%s",
		(long long)crashTime, seedLabel.c_str(), playerId.c_str());
	s_diagUploads.fetch_add(1);
	diagUpload(TheGlobalData->m_logsUrl.str(), seedLabel, playerId, TRUE, TRUE);
}
