#pragma once

// Run-ahead latency model 5: nearest rank percentile of a link's latency history.
// Pure function, no engine dependency, so it can be unit tested with plain g++.

#include <vector>
#include <algorithm>

namespace LatencyPercentile
{
	// Negative samples mean "no measurement" and are ignored. Returns 0 if nothing is left.
	inline int nearestRank(const std::vector<int>& samples, int percent)
	{
		std::vector<int> sorted;
		sorted.reserve(samples.size());
		for (size_t i = 0; i < samples.size(); ++i)
		{
			if (samples[i] >= 0)
			{
				sorted.push_back(samples[i]);
			}
		}

		if (sorted.empty())
		{
			return 0;
		}

		if (percent < 1) percent = 1;
		if (percent > 100) percent = 100;

		std::sort(sorted.begin(), sorted.end());

		const int n = (int)sorted.size();
		int rank = (percent * n + 99) / 100;
		if (rank < 1) rank = 1;
		if (rank > n) rank = n;
		return sorted[rank - 1];
	}
}
