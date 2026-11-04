#pragma once

// DevBench tool "mml.control" (rules 31 and 64): the panel's live state and the ops that drive it.
namespace DevBenchTool
{
	// DevBench answers its interface request from kPostLoad on; retried at kDataLoaded (rule 17).
	void Init(bool a_lastAttempt);
}
