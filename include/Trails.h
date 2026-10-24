#pragma once

// Deep Snow - packed trails, from ANY actor (the owner, 2026-10-24: "Packed trails should come from anyone").
//
// The world is cut into squares fCellSize units on a side (128 = one LAND vertex step) per worldspace. Each time an
// actor LEAVES a square it adds fPackPerPass to that square's packing (0..1) - never to the square it stands in, so an
// actor never reads its own pass under its feet, and an actor first seen (after a load) adds nothing. Packing fills back in linearly: a full
// packing is gone fLifeHours game hours after the last pass (fSnowRefill times faster while it snows). The packing
// lowers the modelled depth there (Depth.h). The list is kept in the SKSE co-save (record 'TRLS', versioned) so a trail
// survives a save and load; a revert (new game, load of another save) clears it.
//
// Each square also keeps WHO last passed (player / NPC / creature) and when - the data a later feature needs for
// time-limited animal and NPC tracks (PLAN.md, phase 5).
//
// Game thread for Stamp / Decay / the co-save callbacks (SKSE runs those on the game thread); every public function
// takes the lock, so the DevBench thread may read PackingAt / Json too.

#include <cstdint>
#include <string>

namespace trails
{
	enum class Who : std::uint8_t { kPlayer = 0, kNPC = 1, kCreature = 2 };

	// An actor at a_pos in a_worldspace. When a_actorId has moved to a new square since its last stamp, the square it
	// left gets one pass; the square it stands in gets nothing until it leaves.
	void Stamp(std::uint32_t a_actorId, std::uint32_t a_worldspace, float a_x, float a_y, Who a_who, float a_nowDays);
	// a_passes passes on the square at a_pos at once (DevBench stamp - the co-save test needs trail data without walking).
	void AddPasses(std::uint32_t a_worldspace, float a_x, float a_y, Who a_who, float a_nowDays, int a_passes);
	// 0..1 at a position, after fill-in up to a_nowDays.
	float PackingAt(std::uint32_t a_worldspace, float a_x, float a_y, float a_nowDays, bool a_snowing);
	// Drops squares that have filled in, and the oldest beyond uMaxCells. Cheap to call once a second.
	void Prune(float a_nowDays, bool a_snowing);
	void Clear();
	std::size_t Count();
	std::string Json(std::size_t a_max);  // {"count":N,"cells":[...]} newest first, at most a_max

	// Co-save (Serialization.cpp calls these).
	void Save(const SKSE::SerializationInterface* a_intfc);
	void Load(const SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length);
}
