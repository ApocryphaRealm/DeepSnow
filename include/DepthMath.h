#pragma once

// Deep Snow - the pure arithmetic of the depth bands, with no game or CommonLib headers, so tools\check-band-mapping.cpp
// can compile it on its own and check the mapping offline. Depth.cpp calls exactly these functions.
//
// Snow Deformation's depth (Community Shaders) is in GAME UNITS and already includes its accumulation (snowfall); it
// does not subtract existing tracks (an actor carves its own trench as it walks) and reads 0 when the reference stands
// more than 70 units above the land. Deep Snow turns it into the same "% of a full blanket" its bands are measured in:
//
//   depth % = 100 x units / fullUnits x (1 - packing x (1 - packedRemain))
//
//   fullUnits     fSDFullDepthUnits:Depth - the depth that counts as a full blanket (default 30, Snow Deformation's
//                 own deepest default class depth at rest; its accumulation peak of 1.5 then reads 150 %)
//   packing       Deep Snow's own trail packing (other actors' passes, and your own earlier ones) - a gameplay layer on
//                 top, switched off with [Trails] bEnabled=0
//
// Deep Snow's own snowfall boost is NOT applied on top (Snow Deformation's accumulation already is the snowfall).
// Roads, ice, a roof overhead and unread ground stay exempt (0) whatever the source says; standing on an object uses the
// OBJECT row (default 0) as it always did.

#include <algorithm>
#include <array>

namespace depth::math
{
	inline constexpr int kBands = 5;

	struct SDInput
	{
		bool exempt = false;       // NONE, ROAD, ICE, or sheltered: always 0
		bool landscape = true;     // SNOW, TRAIL or OTHER ground: Snow Deformation's depth applies
		float objectRow = 0.0F;    // fObject:Depth, for OBJECT ground (not landscape)
		float units = 0.0F;        // GetSnowDepthAtRef, game units
		float fullUnits = 30.0F;   // fSDFullDepthUnits:Depth
		float packing = 0.0F;      // 0..1
		float packedRemain = 0.25F;  // fPackedRemain:Trails, 0..1
	};

	inline float PackFactor(float a_packing, float a_packedRemain)
	{
		const float remain = std::clamp(a_packedRemain, 0.0F, 1.0F);
		return 1.0F - std::clamp(a_packing, 0.0F, 1.0F) * (1.0F - remain);
	}

	inline float PercentFromSD(const SDInput& a_in)
	{
		if (a_in.exempt) { return 0.0F; }
		if (!a_in.landscape) { return std::max(0.0F, 100.0F * a_in.objectRow); }
		if (!(a_in.units > 0.0F) || !(a_in.fullUnits > 0.0F)) { return 0.0F; }  // also catches NaN
		return std::max(0.0F, 100.0F * a_in.units / a_in.fullUnits * PackFactor(a_in.packing, a_in.packedRemain));
	}

	// 0 = no band, 1..5 = the deepest band whose threshold the depth reaches; thresholds need not be in order.
	inline int BandFor(float a_percent, const std::array<float, kBands>& a_thresholds)
	{
		int best = 0;
		float bestDepth = -1.0F;
		for (int i = 0; i < kBands; ++i)
		{
			const float d = a_thresholds[i];
			if (a_percent >= d && d > bestDepth && a_percent > 0.0F)
			{
				best = i + 1;
				bestDepth = d;
			}
		}
		return best;
	}
}
