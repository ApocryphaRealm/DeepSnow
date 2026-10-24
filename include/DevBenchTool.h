#pragma once

namespace DevBenchTool
{
	// Registers the read-only "deepsnow.state" tool with DevBench when present. Call with false at kPostLoad and
	// true at kDataLoaded (the retry, rule 17).
	void Init(bool a_lastAttempt);
}
