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

/** FrameMetrics.cpp */

#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include <numeric>

#include "GameNetwork/FrameMetrics.h"
#include "GameClient/Display.h"
#include "Common/Recorder.h"
#include "GameNetwork/networkutil.h"
#include "../NGMP_include.h"
#include "../NetworkMesh.h"
#include "../NGMP_interfaces.h"

FrameMetrics::FrameMetrics()
{
	m_averageFps = 0.0f;
	m_averageLatency = 0.0f;
	m_cushionIndex = 0;
	m_fpsListIndex = 0;
	m_lastFpsTimeThing = 0;
	m_minimumCushion = 0;

#if !defined(USE_NEW_FRAMEMETRIC_LOGIC)
    m_pendingLatencies = NEW time_t[MAX_FRAMES_AHEAD];
    for (Int i = 0; i < MAX_FRAMES_AHEAD; i++)
        m_pendingLatencies[i] = 0;

	m_latencyList = NEW Real[TheGlobalData->m_networkLatencyHistoryLength];
#else
	oldestLatencyInMap = -1;
	oldestPendingLatencyInMap = -1;
	m_mapLatenciesLookup.clear();
	m_mapLatenciesSorted.clear();
	m_mapPendingLatenciesLookup.clear();
	m_mapPendingLatenciesSorted.clear();
#endif


	m_fpsList = NEW Real[TheGlobalData->m_networkFPSHistoryLength];
}

FrameMetrics::~FrameMetrics() {
	delete m_fpsList;
	m_fpsList = NULL;

#if !defined(USE_NEW_FRAMEMETRIC_LOGIC)
    delete m_latencyList;
    m_latencyList = NULL;

    delete[] m_pendingLatencies;
    m_pendingLatencies = NULL;
#else
    m_mapLatenciesLookup.clear();
    m_mapLatenciesSorted.clear();
    m_mapPendingLatenciesLookup.clear();
    m_mapPendingLatenciesSorted.clear();
#endif
	
	
}

void FrameMetrics::init() {
#if defined(GENERALS_ONLINE_HIGH_FPS_SERVER)
	m_averageFps = GENERALS_ONLINE_HIGH_FPS_LIMIT;
#else
	m_averageFps = 30;
#endif

#if defined(GENERALS_ONLINE)
	// NGMP_NOTE: Don't start with the assumption that we have latency. Connections are now formed earlier, so we have latency data earlier too.

	NetworkMesh* pMesh = NGMP_OnlineServicesManager::GetNetworkMesh();
	if (TheNGMPGame != nullptr && pMesh != nullptr)
	{
		int totalLatency = 0;
		std::map<int64_t, PlayerConnection>& connections = pMesh->GetAllConnections();
		for (auto& kvPair : connections)
		{
			PlayerConnection& conn = kvPair.second;
			totalLatency += conn.GetLatency();
		}

		m_averageLatency = (Real)((Real)totalLatency / 1000.f) / (Real)connections.size();
	}
	else
	{
		m_averageLatency = (Real)0.2;
	}
#else
	m_averageLatency = (Real)0.2;
#endif
	m_minimumCushion = -1;

	UnsignedInt i = 0;
	for (; i < TheGlobalData->m_networkFPSHistoryLength; ++i) {
#if defined(GENERALS_ONLINE_HIGH_FPS_SERVER)
		m_fpsList[i] = GENERALS_ONLINE_HIGH_FPS_LIMIT;
#else
		m_fpsList[i] = 30.0;
#endif
	}
	m_fpsListIndex = 0;
	for (i = 0; i < TheGlobalData->m_networkLatencyHistoryLength; ++i)
	{
#if defined(USE_NEW_FRAMEMETRIC_LOGIC)
		m_mapLatenciesLookup[i] = 0.2;
		m_mapLatenciesSorted[i] = 0.2;
#else
		m_latencyList[i] = (Real)0.2;
#endif
	}
	m_cushionIndex = 0;
}

void FrameMetrics::reset() {
	init();
}

void FrameMetrics::doPerFrameMetrics(UnsignedInt frame) {
	//NetworkLog(ELogVerbosity::LOG_DEBUG, "doPerFrameMetrics for frame %d", frame);
	// Do the measurement of the fps.
	time_t curTime = timeGetTime();
	if ((curTime - m_lastFpsTimeThing) >= 1000) {
		// TheSuperHackers @feature bill-rich 15/09/2026 This history is the input to
		// ConnectionManager::updateRunAhead, i.e. how this machine tells the rest of
		// the game how fast it can run. During resume-from-replay catchup the renderer
		// is deliberately throttled while logic fast-forwards, so display FPS measures
		// the throttle, not the machine; sampling it would collapse the run-ahead
		// negotiation. Hold the history steady through catchup and resume sampling
		// for real on handoff.
		const Bool inCatchup = (TheRecorder != nullptr) && TheRecorder->isResumeCatchupMode();
		if (!inCatchup) {
			m_averageFps -= ((m_fpsList[m_fpsListIndex])) / TheGlobalData->m_networkFPSHistoryLength; // subtract out the old value from the average.
			m_fpsList[m_fpsListIndex] = TheDisplay->getAverageFPS();
			m_averageFps += ((Real)(m_fpsList[m_fpsListIndex])) / TheGlobalData->m_networkFPSHistoryLength; // add the new value to the average.
			++m_fpsListIndex;
			m_fpsListIndex %= TheGlobalData->m_networkFPSHistoryLength;
		}
		m_lastFpsTimeThing = curTime;
	}

#if defined(USE_NEW_FRAMEMETRIC_LOGIC)
	m_mapPendingLatenciesSorted[frame] = curTime;
	m_mapPendingLatenciesLookup[frame] = curTime;
	//NetworkLog(ELogVerbosity::LOG_DEBUG, "doPerFrameMetrics for frame %d, index %d", frame, pendingLatenciesIndex);

	// initialize
	if (oldestPendingLatencyInMap == -1)
	{
		oldestPendingLatencyInMap = frame;
	}

	// beyond size limit? remove the oldest
	//NetworkLog(ELogVerbosity::LOG_DEBUG, "SIZE of pending latencies: %ld and %ld", m_mapPendingLatenciesSorted.size(), m_mapPendingLatenciesLookup.size());
	if (m_mapPendingLatenciesSorted.size() > TheGlobalData->m_networkLatencyHistoryLength)
	{
        // remove oldest frame
        UnsignedInt oldestFrame = m_mapPendingLatenciesSorted.begin()->first;
		m_mapPendingLatenciesSorted.erase(m_mapPendingLatenciesSorted.begin());
		m_mapPendingLatenciesLookup.erase(oldestFrame);
		//NetworkLog(ELogVerbosity::LOG_DEBUG, "Removed one pending latency for frame %ld", oldestFrame);
	}
#else
	Int pendingLatenciesIndex = frame % MAX_FRAMES_AHEAD;
	m_pendingLatencies[pendingLatenciesIndex] = curTime;
#endif
}

void FrameMetrics::processLatencyResponse(UnsignedInt frame)
{
#if defined(USE_NEW_FRAMEMETRIC_LOGIC)
	if (!m_mapPendingLatenciesLookup.contains(frame))
	{
		NetworkLog(ELogVerbosity::LOG_DEBUG, "WARNING: Frame hasn't been requested yet, frame %ld", frame);
		return;
	}
#endif

	time_t curTime = timeGetTime();

#if defined(USE_NEW_FRAMEMETRIC_LOGIC)
    //NetworkLog(ELogVerbosity::LOG_DEBUG, "processLatencyResponse for frame %d, pending index is %d", frame, currentLatencyMapIndex);
    time_t timeDiff = curTime - m_mapPendingLatenciesLookup[frame];

    // initialize
    if (oldestLatencyInMap == -1)
    {
		oldestLatencyInMap = frame;
    }

    // beyond size limit? remove the oldest
    //NetworkLog(ELogVerbosity::LOG_DEBUG, "SIZE of actual latencies: %ld and %ld/%ld", m_mapLatenciesLookup.size(), m_mapLatenciesSorted.size(), TheGlobalData->m_networkLatencyHistoryLength);
    if (m_mapLatenciesLookup.size() > TheGlobalData->m_networkLatencyHistoryLength)
    {
        // remove oldest frame
        UnsignedInt oldestFrame = m_mapLatenciesSorted.begin()->first;
		m_mapLatenciesSorted.erase(m_mapLatenciesSorted.begin());
		m_mapLatenciesLookup.erase(oldestFrame);
        //NetworkLog(ELogVerbosity::LOG_DEBUG, "Removed one actual latency for frame %ld", oldestFrame);
    }

	m_mapLatenciesLookup[frame] = (Real)timeDiff / (Real)1000; // convert to seconds from milliseconds.
	m_mapLatenciesSorted[frame] = (Real)timeDiff / (Real)1000; // convert to seconds from milliseconds.


    // calculate average
    m_averageLatency = 0.0f;
    for (auto kvPaAir : m_mapLatenciesLookup)
    {
        m_averageLatency += kvPaAir.second;
    }
	NetworkLog(ELogVerbosity::LOG_DEBUG, "Avg latency is: %f / %d = %f", m_averageLatency, m_mapLatenciesLookup.size(), m_averageLatency /= m_mapLatenciesLookup.size());
    m_averageLatency /= m_mapLatenciesLookup.size();


    if (timeDiff > 1000)
    {
        NetworkLog(ELogVerbosity::LOG_DEBUG, "WARNING: HIGH processLatencyResponse");
    }

    NetworkLog(ELogVerbosity::LOG_DEBUG, "processLatencyResponse timediff was %lld which is %f ms latency for frame %ld", timeDiff, (Real)timeDiff / (Real)1000, frame);
#else
    Int pendingIndex = frame % MAX_FRAMES_AHEAD;
    time_t timeDiff = curTime - m_pendingLatencies[pendingIndex];

    Int latencyListIndex = frame % TheGlobalData->m_networkLatencyHistoryLength;
    m_averageLatency -= m_latencyList[latencyListIndex] / TheGlobalData->m_networkLatencyHistoryLength;
    m_latencyList[latencyListIndex] = (Real)timeDiff / (Real)1000; // convert to seconds from milliseconds.
    NetworkLog(ELogVerbosity::LOG_DEBUG, "processLatencyResponse timediff was %lld which is %f ms latency", timeDiff, (Real)timeDiff / (Real)1000);
    m_averageLatency += m_latencyList[latencyListIndex] / TheGlobalData->m_networkLatencyHistoryLength;
#endif

	if (frame % 16 == 0) {
//		DEBUG_LOG(("ConnectionManager::processFrameInfoAck - average latency = %f", m_averageLatency));
	}
}

void FrameMetrics::addCushion(Int cushion) {
	++m_cushionIndex;
	m_cushionIndex %= TheGlobalData->m_networkCushionHistoryLength;
	if (m_cushionIndex == 0) {
		m_minimumCushion = -1;
	}
	if ((cushion < m_minimumCushion) || (m_minimumCushion == -1)) {
		m_minimumCushion = cushion;
	}
}

Int FrameMetrics::getAverageFPS() {
	return (Int)m_averageFps;
}

Real FrameMetrics::getAverageLatency() {
	return m_averageLatency;
}

// TheSuperHackers @feature bill-rich 24/09/2026 Percentile of the latency
// history, in seconds. The history is small (m_networkLatencyHistoryLength),
// so a copy and a sort on a metrics tick is cheap; the scratch buffer is a
// member so it does not reallocate every time.
Real FrameMetrics::getLatencyPercentile(Int pct) {
#if defined(USE_NEW_FRAMEMETRIC_LOGIC)
	if (m_mapLatenciesLookup.empty()) {
		return m_averageLatency;
	}
	m_latencyScratch.clear();
	m_latencyScratch.reserve(m_mapLatenciesLookup.size());
	for (auto& kvPair : m_mapLatenciesLookup) {
		m_latencyScratch.push_back(kvPair.second);
	}
#else
	const Int historyLength = (Int)TheGlobalData->m_networkLatencyHistoryLength;
	if (historyLength <= 0) {
		return m_averageLatency;
	}
	m_latencyScratch.assign(m_latencyList, m_latencyList + historyLength);
#endif
	std::sort(m_latencyScratch.begin(), m_latencyScratch.end());
	const Int n = (Int)m_latencyScratch.size();
	Int idx = (n * pct) / 100;
	if (idx < 0) {
		idx = 0;
	}
	if (idx > (n - 1)) {
		idx = n - 1;
	}
	return m_latencyScratch[idx];
}

/**
 * TheSuperHackers @feature bill-rich 24/09/2026 The mean is blind to jitter: a
 * link that averages 80ms but spikes to 300ms every few seconds gets a window
 * sized for 80ms and stalls on every spike. So add the spread between the mean
 * and a high percentile of the history.
 *
 * Two guards keep this from feeding on itself. Packets go out every batchSec
 * (the frame grouping, itself derived from run-ahead), so every sample already
 * carries up to a batch interval of random wait at each end; that spread is
 * subtracted before it counts as jitter, otherwise a bigger window makes bigger
 * "jitter" and run-ahead ratchets to its cap even on a perfect LAN. And the
 * allowance is capped at NET_RUNAHEAD_JITTER_CAP_MS so a few retry-inflated
 * samples cannot buy the whole game a second of input lag.
 */
Real FrameMetrics::getRunAheadLatency(Real batchSec) {
	if (NET_LEGACY_TIMING) {
		return m_averageLatency;
	}
	Real spread = getLatencyPercentile(NET_RUNAHEAD_LAT_PERCENTILE) - m_averageLatency - batchSec;
	if (spread < 0.0f) {
		spread = 0.0f;
	}
	const Real cap = (Real)NET_RUNAHEAD_JITTER_CAP_MS / 1000.0f;
	if (spread > cap) {
		spread = cap;
	}
	return m_averageLatency + spread;
}

Int FrameMetrics::getMinimumCushion() {
	return m_minimumCushion;
}
void FrameMetrics::SeedLatencyData(int latency)
{
	m_averageFps = GENERALS_ONLINE_HIGH_FPS_LIMIT;
	m_averageLatency = latency / 1000.f;
}
