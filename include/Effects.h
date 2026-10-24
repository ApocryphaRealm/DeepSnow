#pragma once

// Deep Snow - the movement and stamina effects (the owner, 2026-10-24: "we can just use movement speed modifiers.
// Which would basically just be a spell effect on the character while in contact with that thing").
//
// Speed: one constant-effect ABILITY per depth band (DeepSnow.esp, 0x803-0x807) and one for the road bonus (0x808).
// Each carries a Value Modifier magic effect on SpeedMult (0x801 slows, detrimental; 0x802 speeds). An ability is added
// to the actor when it enters the band and removed when it leaves - the engine keeps the effect as a modifier of its
// own, so it adds to whatever Inertia, Dynamic Sprint and Survival Mode do to SpeedMult and never overwrites them. No
// absolute SpeedMult write exists anywhere in this mod. A band's magnitude is "100 - its speed %" SpeedMult points,
// written into the ability's effect from the INI at data load and whenever a slider moves (then re-applied).
//
// Stamina: a small damage per second while moving in a band (the band's drain), taken as a damage modifier - it stacks
// with Survival Mode's cold and the game's own regeneration like any other stamina cost, and it never takes stamina
// below fFloor % of the maximum.
//
// Game thread only, except Ready() / AppliedCount().

#include <cstdint>
#include <string>
#include <vector>

namespace effects
{
	bool Init();      // kDataLoaded: find DeepSnow.esp's forms; false = the plugin is missing, the mechanism stays off
	bool Ready();
	void ApplyMagnitudes();  // settings -> the abilities' effect magnitudes

	// Gives a_actor exactly the ability for a_band (0 = none) and, with a_road, the road ability; removes the rest.
	// Returns true when anything was added or removed.
	bool Set(RE::Actor* a_actor, int a_band, bool a_road);
	// Removes and re-adds whatever a_actor holds of ours, so a changed magnitude takes hold.
	void Reapply(RE::Actor* a_actor);
	// Stamina damage for one tick; returns the points taken.
	float Drain(RE::Actor* a_actor, int a_band, float a_seconds);

	// Actors known to hold one of our abilities (form ids), for the co-save and for clean-up after a load.
	std::vector<std::uint32_t> Applied();
	std::size_t AppliedCount();
	void ForgetAll();
	void Remember(std::uint32_t a_formId);
	// After a load: take every ability of ours off every remembered actor and the player; the next tick puts back what
	// the ground calls for (the game's own reload of ability effects is then never trusted - logic library entry 36).
	void ResetAfterLoad();

	std::string Json();  // forms found, magnitudes, applied count

	// Co-save record 'APLD' v1: the remembered actors.
	void Save(const SKSE::SerializationInterface* a_intfc);
	void Load(const SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length);
}
