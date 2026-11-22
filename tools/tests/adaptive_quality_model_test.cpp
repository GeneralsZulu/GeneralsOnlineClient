// Self-check for the adaptive quality step rules. No engine needed:
//   g++ -std=c++17 -ICore/Libraries/Include -IGeneralsMD/Code/GameEngine/Include/Common tools/tests/adaptive_quality_model_test.cpp -o /tmp/aq_test && /tmp/aq_test
#include <cassert>
#include <cstdio>
#include "AdaptiveQualityModel.h"

// level after two minutes of identical seconds at 60 frames each
static int levelAfter(float p50, float p95, unsigned over)
{
	AdaptiveQualityModel model;
	AdaptiveSecond s = { 60, over, p50, p95, p50 };
	for (int t = 0; t < 120; ++t)
	{
		model.onSecond(s, (double)t);
	}
	return model.level();
}

int main()
{
	using namespace AdaptiveQualityLevels;
	// 2026-11-18: medians of 13.4 to 14.9 ms with no missed frames put three players on half frame rate
	assert(levelAfter(14.2f, 17.5f, 0) == LEVEL_SHADOW_VOLUMES);
	assert(levelAfter(14.9f, 18.3f, 0) == LEVEL_SHADOW_VOLUMES);
	// a median over budget with missed frames still skips renders
	assert(levelAfter(17.5f, 22.0f, 3) >= LEVEL_RENDER_SKIP);
	// and so do frames that really miss their deadline
	assert(levelAfter(6.0f, 30.0f, 12) >= LEVEL_RENDER_SKIP);
	// a machine with room stays at full quality
	assert(levelAfter(6.0f, 9.0f, 0) == LEVEL_NONE);
	// 2026-11-21: Pancake, median 15.6 to 16.9 ms with no missed frames, must not reach render skip
	{
		AdaptiveQualityModel model;
		int t;
		for (t = 0; t < 60; ++t)
		{
			float p50 = (t % 2) ? 16.9f : 15.6f;
			AdaptiveSecond s = { 60, 0, p50, 20.6f, p50 };
			model.onSecond(s, (double)t);
		}
		assert(model.level() == LEVEL_SHADOW_VOLUMES);
		for (t = 60; t < 120; ++t)
		{
			float p50 = (t % 2) ? 16.9f : 15.6f;
			AdaptiveSecond s = { 60, 0, p50, 20.6f, p50 };
			model.onSecond(s, (double)t);
		}
		assert(model.level() == LEVEL_SHADOW_VOLUMES);
	}
	// the over rule still steps down on missed frames
	{
		AdaptiveQualityModel model;
		AdaptiveSecond s = { 60, 9, 6.0f, 30.0f, 6.0f };
		model.onSecond(s, 0.0);
		model.onSecond(s, 1.0);
		assert(model.onSecond(s, 2.0) == AdaptiveQualityModel::DECISION_DOWN);
		assert(model.reason() == AdaptiveQualityModel::REASON_OVER);
		assert(model.level() == LEVEL_PARTICLES);
	}
	puts("adaptive quality model: ok");
	return 0;
}
