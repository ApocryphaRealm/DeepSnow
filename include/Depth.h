#pragma once

// Deep Snow - the depth model. OUR OWN model of how deep the snow is where an actor stands; no plugin can read DTD's
// real deformed height (it lives in GPU textures and DTD exports nothing). The depth sliders on the page are measured
// against exactly this number:
//
//   depth % = 100 x row   x cover x (1 - packing x (1 - packedRemain)) x (1 + snowfall)      and 0 under a roof
//
//   row      the surface row's depth fraction (SNOW 1.0, TRAIL 0.35, ROAD 0, OBJECT 0, OTHER 0 - [Depth] in the INI)
//   cover    snow cover under the feet from the land's blend layers (SNOW rows only; 0..1)
//   packing  the trail packing at that square from every actor's passes (Trails.h; 0..1)
//   snowfall the snowfall boost (0..fMax/100) while bDeepens is on
//
// 100 % is "a full untouched blanket", which in DTD's own terms is SnowRaiseHeight units (read from DTD's INI when it
// is installed, else fFullDepthUnits). ICE is never deep: Ice is Slick owns ice.

#include "Surface.h"

namespace depth
{
	float RowFraction(surface::Kind a_kind);
	// The model, in % of a full blanket.
	float Percent(const surface::Reading& a_r, float a_packing, float a_snowfall);
	// 0 = no band, 1..5 = the deepest band whose threshold the depth reaches.
	int BandFor(float a_percent);
	// Units of a full blanket (DTD's SnowRaiseHeight when read, else fFullDepthUnits) - for showing units beside %.
	float FullDepthUnits();
	const char* FullDepthSource();  // "DTD" or "INI"
	void ReadDTD();                 // once at data load: DTD's SnowRaiseHeight, if its INI is there
}
