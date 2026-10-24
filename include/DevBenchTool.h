#pragma once

namespace DevBenchTool
{
	// Registers "deepsnow.state" (read-only) and "deepsnow.control" (driving) with DevBench when present. Call with false at kPostLoad and
	// true at kDataLoaded (the retry, rule 17).
	void Init(bool a_lastAttempt);
}
