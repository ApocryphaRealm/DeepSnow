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
#include <utility>
#include <vector>

namespace effects
{
	bool Init();      // kDataLoaded: find DeepSnow.esp's forms; false = the plugin is missing, the mechanism stays off
	bool Ready();
	void ApplyMagnitudes();  // settings -> the abilities' effect magnitudes

	// Gives a_actor exactly the ability for a_band (0 = none) and, with a_road, the road ability; removes the rest.
	// Returns true when anything was added or removed.
	bool Set(RE::Actor* a_actor, int a_band, bool a_road);
	// Stamina regen penalty (1.3.0, [Bands] fBandNRegen, 100 = no change): gives a_actor the regen ability for a_band
	// when that band's regen is below 100 % (0 = none) and removes the others. Its own abilities (0x80A-0x80E, effect
	// 0x809 on StaminaRateMult), apart from the speed ones, so movement.cpp can limit it to NPCs within fRegenRadius.
	bool SetRegen(RE::Actor* a_actor, int a_band);
	bool RegenWanted(int a_band);  // the band's regen is below 100 % and the forms were found
	bool RegenReady();
	// Actors holding a regen ability and its band (form id, band), for the radius check and deepsnow.state.
	std::vector<std::pair<std::uint32_t, int>> RegenHolders();
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
