#pragma once

// Deep Snow - the depth model. Two sources, one scale (% of a full blanket, which the band sliders are measured in):
//
//   * Snow Deformation (Community Shaders) - its public API reports the real snow depth under an actor in game units
//     (SnowDeformationAPI.h; resolved after kPostPostLoad). Converted to % by DepthMath.h. Used when [Depth] uSource is
//     0 (Auto) and the feature is installed and on, or 2 (Snow Deformation) - which falls back to our model while it is off.
//   * OUR OWN model (uSource 1, or Auto without Snow Deformation) - DTD's deformed height cannot be read (it lives in GPU
//     textures and DTD exports nothing), so we estimate it:
//
//   depth % = 100 x row   x cover x (1 - packing x (1 - packedRemain)) x (1 + snowfall)      and 0 under a roof
//
//   row      the surface row's depth fraction (SNOW 1.0, TRAIL 0.35, OBJECT 0, OTHER 0 - [Depth] in the INI)
//   cover    snow cover under the feet from the land's blend layers (SNOW rows only; 0..1)
//   packing  the trail packing at that square from every actor's passes (Trails.h; 0..1)
//   snowfall the snowfall boost (0..fMax/100) while bDeepens is on
//
// 100 % is "a full untouched blanket", which in DTD's own terms is SnowRaiseHeight units (read from DTD's INI when it
// is installed, else fFullDepthUnits). ICE and ROAD are never deep: Ice is Slick owns ice,
// and roads are always exempt (no band, no drain; the optional road bonus is separate).

#include "Surface.h"

namespace depth
{
	enum class Source : int { kAuto = 0, kModel = 1, kSnowDeformation = 2 };  // uSource:Depth
	float RowFraction(surface::Kind a_kind);
	// The model, in % of a full blanket.
	float Percent(const surface::Reading& a_r, float a_packing, float a_snowfall);
	// 0 = no band, 1..5 = the deepest band whose threshold the depth reaches.
	int BandFor(float a_percent);
	// Units of a full blanket (DTD's SnowRaiseHeight when read, else fFullDepthUnits) - for showing units beside %.
	float FullDepthUnits();
	const char* FullDepthSource();  // "DTD", "INI" or "SnowDeformation"
	void ReadDTD();                 // once at data load: DTD's SnowRaiseHeight, if its INI is there

	// Snow Deformation's API: resolved at kPostPostLoad, retried at kDataLoaded (rule 17). Read-only, any thread.
	void ResolveSnowDeformation(const char* a_when);
	bool SnowDeformationFound();   // the API resolved (Community Shaders carries the feature)
	bool SnowDeformationActive();  // found AND switched on right now
	Source ActiveSource();         // kModel or kSnowDeformation: what the next reading uses
	const char* SourceName(Source a_source);
	// The depth under an actor from the active source, in % of a full blanket. a_rawUnits (optional) gets Snow
	// Deformation's raw reading in game units, or -1 when our model was used.
	float PercentFor(RE::Actor* a_actor, const surface::Reading& a_r, float a_packing, float a_snowfall, float* a_rawUnits = nullptr);
}
