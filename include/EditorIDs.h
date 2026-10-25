#pragma once

// Deep Snow - editor IDs for the forms the road rule reads (SurfaceRules.h).
//
// The game drops most forms' editor IDs at load (TESForm::GetFormEditorID returns ""), unless powerofthree's Tweaks
// "Load EditorIDs" is on. The road rule needs them - Northern Roads' stone road is named COTN_LRoadStone01 but draws
// groundstone01.dds - so Deep Snow keeps its own copy: it wraps SetFormEditorID (TESForm virtual 0x33) on the landscape
// texture (LTEX), texture set (TXST) and static (STAT) vtables before the data loads, storing the name and calling the
// original (so powerofthree's Tweaks, which wraps the same function, still sees every call). LTEX and TXST names are
// kept whole (about a thousand forms, the probe prints them); for statics only a name with "road" (or "path", for
// [Roads] bPathsAreRoads) in it is kept.

namespace editor_ids
{
	// Wraps the three vtables. Call from SKSEPluginLoad, before the game loads its data files.
	void Install();

	// The form's editor ID: Deep Snow's copy, else the game's own answer (powerofthree's Tweaks), else "".
	std::string Get(const RE::TESForm* a_form);

	// How many names were kept, for the log and the DevBench state.
	std::size_t Count();
}
