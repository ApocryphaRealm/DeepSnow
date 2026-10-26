#pragma once

// Deep Snow - editor IDs for the forms the road rule reads (SurfaceRules.h).
//
// The game drops most forms' editor IDs at load (TESForm::GetFormEditorID returns ""), unless powerofthree's Tweaks
// "Load EditorIDs" is on. The road rule needs them - Northern Roads' stone road is named COTN_LRoadStone01 but draws
// groundstone01.dds - so Deep Snow keeps its own copy. 1.1.0's first build wrapped SetFormEditorID (TESForm virtual
// 0x33) on the LTEX / TXST / STAT vtables; on 1.5.97 that wrapper was never called (0 names in six runs). Now, once the
// data has loaded, a background thread reads the active plugins' own records (PluginEditorIDs.h): only the LTEX, TXST
// and STAT top-level groups, and only names with "road" or "path" in them are kept (path names serve
// [Roads] bPathsAreRoads, which can change while the game runs). No game code is hooked.

namespace editor_ids
{
	// Starts the background read of the active plugins. Call at kDataLoaded (the load order is known then).
	void Start();

	// The form's editor ID: Deep Snow's copy, else the game's own answer (powerofthree's Tweaks), else "".
	std::string Get(const RE::TESForm* a_form);

	// How many names were kept, for the log and the DevBench state.
	std::size_t Count();
}
