// Offline check of how Snow Deformation's depth (game units) maps onto Deep Snow's depth bands, against the SAME
// header the plugin compiles (include/DepthMath.h). No game, no CommonLib:
//   cl /nologo /std:c++20 /EHsc /I include tools\check-band-mapping.cpp /Fe:<out>.exe && <out>.exe
// Exit code 0 = every case passed.
#include "DepthMath.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
	int failures = 0;
	const std::array<float, depth::math::kBands> kDefaults{ 10.0F, 30.0F, 50.0F, 70.0F, 90.0F };  // fBand1..5Depth shipped

	void Case(const char* a_name, const depth::math::SDInput& a_in, float a_wantPct, int a_wantBand,
		const std::array<float, depth::math::kBands>& a_thresholds = kDefaults)
	{
		const float pct = depth::math::PercentFromSD(a_in);
		const int band = depth::math::BandFor(pct, a_thresholds);
		const bool ok = std::fabs(pct - a_wantPct) < 0.05F && band == a_wantBand;
		std::printf("%s  %-62s units=%6.2f full=%5.1f pack=%.2f -> %7.2f%% band %d (want %.2f%% band %d)\n", ok ? "ok  " : "FAIL", a_name, a_in.units,
			a_in.fullUnits, a_in.packing, pct, band, a_wantPct, a_wantBand);
		if (!ok) { ++failures; }
	}

	depth::math::SDInput Snow(float a_units, float a_full = 30.0F, float a_packing = 0.0F)
	{
		depth::math::SDInput in;
		in.units = a_units;
		in.fullUnits = a_full;
		in.packing = a_packing;
		return in;
	}
}

int main()
{
	// Snow Deformation's default class depths at rest are 14, 18 and 30 units; its accumulation peak is 1.5x.
	Case("deepest class at rest = a full blanket", Snow(30.0F), 100.0F, 5);
	Case("deepest class at the accumulation peak (x1.5)", Snow(45.0F), 150.0F, 5);
	Case("18-unit class at rest", Snow(18.0F), 60.0F, 3);
	Case("14-unit class at rest", Snow(14.0F), 46.67F, 2);
	Case("14-unit class at the peak (21 units)", Snow(21.0F), 70.0F, 4);
	Case("band 1 threshold is inclusive (3 units = 10%)", Snow(3.0F), 10.0F, 1);
	Case("just under band 1", Snow(2.9F), 9.67F, 0);
	Case("bare ground (0 units)", Snow(0.0F), 0.0F, 0);
	Case("negative reading treated as none", Snow(-4.0F), 0.0F, 0);
	Case("NaN reading treated as none", Snow(std::numeric_limits<float>::quiet_NaN()), 0.0F, 0);
	Case("full blanket set to 0 (guard)", Snow(30.0F, 0.0F), 0.0F, 0);
	Case("full blanket 45: the peak reads 100%", Snow(45.0F, 45.0F), 100.0F, 5);
	Case("full blanket 60: deepest class at rest reads 50%", Snow(30.0F, 60.0F), 50.0F, 3);
	Case("fully packed trail keeps fPackedRemain 0.25", Snow(30.0F, 30.0F, 1.0F), 25.0F, 1);
	Case("half packed trail", Snow(30.0F, 30.0F, 0.5F), 62.5F, 3);
	Case("packing above 1 is clamped", Snow(30.0F, 30.0F, 3.0F), 25.0F, 1);

	depth::math::SDInput road = Snow(30.0F);
	road.exempt = true;
	Case("road / ice / roof / unread ground: exempt whatever the units", road, 0.0F, 0);

	depth::math::SDInput object = Snow(30.0F);
	object.landscape = false;
	Case("standing on an object: OBJECT row 0", object, 0.0F, 0);
	object.objectRow = 0.5F;
	Case("standing on an object: OBJECT row 0.5", object, 50.0F, 3);

	depth::math::SDInput remain = Snow(30.0F, 30.0F, 1.0F);
	remain.packedRemain = 0.0F;
	Case("fully packed, fPackedRemain 0: no snow left", remain, 0.0F, 0);

	const std::array<float, depth::math::kBands> unordered{ 90.0F, 10.0F, 50.0F, 30.0F, 70.0F };
	Case("thresholds out of order: deepest reached wins", Snow(18.0F), 60.0F, 3, unordered);

	std::printf("\n%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	return failures ? 1 : 0;
}
