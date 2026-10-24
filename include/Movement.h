#pragma once

// Deep Snow - the tick. A timer thread queues one task to the game thread every uTickMs (250 ms); the task reads the
// ground under the player and up to uActorsPerTick nearby actors (round robin), stamps trails for every nearby actor,
// models the depth, picks the band and sets the abilities and the stamina drain (Effects.h).
//
// Everything that touches the game runs inside that task. The page and the DevBench tool read the snapshot below
// (copied under a lock) and send requests back through Request*(), which the next tick carries out.

#include "Surface.h"

#include <cstdint>
#include <string>

namespace movement
{
	struct Snapshot
	{
		bool havePlayer = false;
		bool enabled = true;
		bool effectsReady = false;
		surface::Reading reading;
		float packing = 0.0F;
		float snowfall = 0.0F;   // the boost, 0..fMax/100
		float depthPercent = 0.0F;
		int band = 0;            // what is applied to the player
		int groundBand = 0;      // what the ground alone calls for
		int forcedBand = -1;
		bool road = false;
		bool snowing = false;
		bool moving = false;
		float speedMult = -1.0F;
		float stamina = -1.0F;
		float drainedLastSecond = 0.0F;
		std::uint64_t ticks = 0;
		float tickUsAvg = 0.0F;  // microseconds per tick, smoothed
		float tickUsMax = 0.0F;  // the slowest tick in the last ~10 s
		int npcsNear = 0;
		int npcsClassified = 0;
		int npcsAffected = 0;
		std::size_t trailSquares = 0;
	};

	void Start();  // kDataLoaded, once
	Snapshot Get();
	std::string Json();

	// -1 = off (the ground decides), 0 = no band, 1..5 = that band on the player whatever the ground says. A DevBench
	// test hook - not saved, not on the page.
	void ForceBand(int a_band);
	int ForcedBand();

	// Requests the page / DevBench make; the next tick on the game thread carries them out.
	void RequestMark(int a_list);   // settings::List: add what is under the player to that list, then save the INI
	void RequestClearTrails();
	void RequestReapply();          // settings changed: magnitudes and every holder re-applied
	void OnLoaded();                // kPostLoadGame / kNewGame: clean up, re-apply by the ground

	// Co-save record 'SNFL' v1: the snowfall boost.
	void Save(const SKSE::SerializationInterface* a_intfc);
	void Load(const SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length);
	void Revert();
}
