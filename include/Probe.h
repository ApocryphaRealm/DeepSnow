#pragma once

// Deep Snow spike - the ground probe. Read-only: it changes nothing in the game.
//
// A sampler thread queues one sample to the game thread every uIntervalMs (500 ms = 2 Hz). The sample reads, for the
// player: position, worldspace, cell, interior / mounted / swimming; the landscape texture under the player (the
// engine's own pick, TES::GetLandTexture, and the per-quad layer breakdown of the cell's LAND at the nearest vertex);
// the navmesh triangle under the player and its flags (the "preferred path" bit); a havok ray straight up (roof or
// overhang) and one straight down (what the feet stand on). Each sample is one log line and a JSON snapshot the
// DevBench tool hands back.

#include <cstdint>
#include <string>
#include <vector>

namespace probe
{
	void Start();            // starts the sampler thread once (kDataLoaded)
	std::string LatestJson();  // the newest snapshot, or a "no sample yet" object
	std::string HistoryJson(std::size_t a_count);  // the newest a_count snapshots, oldest first
	std::uint64_t SampleCount();
}
