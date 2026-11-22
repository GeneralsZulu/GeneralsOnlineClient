// Self-check for run-ahead latency model 5 (95th percentile). No engine needed. Run from the repo root:
//   g++ -std=c++17 -IGeneralsMD/Code/GameEngine/Include/GameNetwork/GeneralsOnline tools/tests/runahead_latency_test.cpp -o /tmp/ra_test && /tmp/ra_test
#include <cassert>
#include <cstdio>
#include <cmath>
#include <vector>
#include "LatencyPercentile.h"

// mirrors updateRunAhead with default slack, clamped to MAX_FRAMES_AHEAD/2
static int framesFor(int latencyMs)
{
	int frames = (int)std::ceil(latencyMs / 1000.0 * 1.3 * 60.0);
	return frames > 64 ? 64 : frames;
}

// model 2 stand-in
static int peakOf(const std::vector<int>& v)
{
	int peak = 0;
	size_t i;
	for (i = 0; i < v.size(); ++i)
	{
		if (v[i] > peak)
		{
			peak = v[i];
		}
	}
	return peak;
}

// total samples with `spikes` of 900 spread evenly, the rest 450
static std::vector<int> makeHistory(int total, int spikes)
{
	std::vector<int> v(total, 450);
	int k;
	for (k = 0; k < spikes; ++k)
	{
		v[(size_t)k * total / spikes] = 900;
	}
	return v;
}

int main()
{
	std::vector<int> a = makeHistory(1200, 30);
	assert(LatencyPercentile::nearestRank(a, 95) == 450);
	assert(peakOf(a) == 900);
	assert(framesFor(LatencyPercentile::nearestRank(a, 95)) == 36);
	assert(framesFor(peakOf(a)) == 64);

	std::vector<int> b = makeHistory(1200, 100);
	assert(LatencyPercentile::nearestRank(b, 95) == 900);

	assert(LatencyPercentile::nearestRank(a, 100) == 900);

	std::vector<int> empty;
	assert(LatencyPercentile::nearestRank(empty, 95) == 0);
	std::vector<int> single(1, 123);
	assert(LatencyPercentile::nearestRank(single, 95) == 123);

	std::vector<int> flat(1200, 80);
	assert(LatencyPercentile::nearestRank(flat, 95) == 80);

	std::vector<int> neg;
	neg.push_back(-1);
	neg.push_back(-1);
	neg.push_back(200);
	assert(LatencyPercentile::nearestRank(neg, 95) == 200);
	std::vector<int> onlyNeg(1, -1);
	assert(LatencyPercentile::nearestRank(onlyNeg, 95) == 0);

	std::printf("runahead latency: ok\n");
	return 0;
}
