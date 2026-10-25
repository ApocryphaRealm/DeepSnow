// Offline check of Deep Snow's road rule (1.1.0; the owner, 2026-10-25: any road or texture with "road" in its name is
// exempt from the slowdown, so road mods need no patch). Compiles the plugin's own include/SurfaceRules.h - the same
// classifier Surface.cpp calls - with the [Surfaces] lists read from the shipped dist/SKSE/Plugins/DeepSnow.ini.
//   tools\surface_rules_check.bat [names.tsv]   -> one line per check and "surface_rules_check: N of N passed"; exit 1 on
//   any failure. names.tsv (optional): one landscape texture per line, "form<TAB>LTEX edid<TAB>TXST edid<TAB>diffuse<TAB>
//   normal", e.g. dumped from a load order; every row is classified and the roads are listed.
// What it cannot: whether the game hands over these names at run time (EditorIDs.cpp keeps the editor IDs the game
// drops; the in-game step on a Northern Roads stone road covers that).
#include "SurfaceRules.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	using surface::Kind;
	int passed = 0;
	int failed = 0;

	const char* Name(Kind k)
	{
		switch (k)
		{
		case Kind::kSnow: return "snow";
		case Kind::kTrail: return "trail";
		case Kind::kRoad: return "road";
		case Kind::kIce: return "ice";
		case Kind::kObject: return "object";
		case Kind::kOther: return "other";
		default: return "none";
		}
	}

	void Check(const char* a_what, Kind a_got, Kind a_want, const std::string& a_by = {})
	{
		const bool ok = a_got == a_want;
		(ok ? passed : failed)++;
		std::printf("%s  %-72s -> %-6s (want %s)%s%s\n", ok ? "PASS" : "FAIL", a_what, Name(a_got), Name(a_want), a_by.empty() ? "" : "  by: ", a_by.c_str());
	}

	std::vector<std::string> Split(const std::string& a_text)
	{
		std::vector<std::string> out;
		std::string cur;
		for (char c : a_text)
		{
			if (c == ',')
			{
				if (!cur.empty()) { out.push_back(cur); }
				cur.clear();
			}
			else if (c != ' ' && c != '\r') { cur += static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
		}
		if (!cur.empty()) { out.push_back(cur); }
		return out;
	}
}

int main(int argc, char** argv)
{
	// The shipped INI's lists (the bat passes its path first).
	std::vector<std::string> ice, road, trail, snow;
	{
		std::ifstream ini(argc > 1 ? argv[1] : "dist/SKSE/Plugins/DeepSnow.ini");
		if (!ini) { std::printf("surface_rules_check: cannot read the INI\n"); return 1; }
		std::string line;
		while (std::getline(ini, line))
		{
			auto take = [&](const char* key, std::vector<std::string>& into) {
				const std::string k = std::string(key) + "=";
				if (line.rfind(k, 0) == 0) { into = Split(line.substr(k.size())); }
			};
			take("sIce", ice);
			take("sRoad", road);
			take("sTrail", trail);
			take("sSnow", snow);
		}
	}
	const surface::rules::Lists lists{ ice, road, trail, snow };
	std::string by;

	// 1. The case that started it: RoadsToCobblestone points Northern Roads' dirt roads at COTN_LRoadStone01, which draws
	//    groundstone01.dds - in no list. The editor IDs say road.
	by.clear();
	Check("COTN_LRoadStone01 / COTN_LandscapeRoadStone01 / groundstone01.dds",
		surface::rules::ClassifyLand("groundstone01", { "COTN_LRoadStone01", "COTN_LandscapeRoadStone01", "pbr\\COTN\\landscape\\groundstone01.dds", "pbr\\COTN\\landscape\\groundstone01_n.dds" }, lists, &by),
		Kind::kRoad, by);
	by.clear();
	Check("COTN_LRoadStone01 alone (texture set editor ID unknown)", surface::rules::ClassifyLand("groundstone01", { "COTN_LRoadStone01", "", "pbr\\COTN\\landscape\\groundstone01.dds", "" }, lists, &by),
		Kind::kRoad, by);
	// 1b. Control: the same ground with no editor IDs at all is NOT found - why EditorIDs.cpp keeps them.
	Check("control: groundstone01.dds with no editor IDs", surface::rules::ClassifyLand("groundstone01", { "", "", "pbr\\COTN\\landscape\\groundstone01.dds", "pbr\\COTN\\landscape\\groundstone01_n.dds" }, lists),
		Kind::kOther);

	// 2. A plain road landscape texture by its editor ID.
	by.clear();
	Check("LRoad01 (editor ID only)", surface::rules::ClassifyLand("", { "LRoad01", "", "", "" }, lists, &by), Kind::kRoad, by);
	by.clear();
	Check("Road01Snow01.dds path (vanilla road texture set)", surface::rules::ClassifyLand("road01snow01", { "", "TextureSetRoad01Snow01", "PBR\\Landscape\\Roads\\Road01Snow01.dds", "" }, lists, &by),
		Kind::kRoad, by);

	// 3. Snow stays snow.
	Check("LSnow01 / LandscapeSnow01 / snow01.dds", surface::rules::ClassifyLand("snow01", { "LSnow01", "LandscapeSnow01", "PBR\\Landscape\\Snow01.dds", "PBR\\Landscape\\Snow01_n.dds" }, lists), Kind::kSnow);
	Check("LSnowRocks01 / LandscapeSnowCobble01 / SnowRocks01.dds", surface::rules::ClassifyLand("snowrocks01", { "LSnowRocks01", "LandscapeSnowCobble01", "PBR\\Landscape\\SnowRocks01.dds", "" }, lists),
		Kind::kSnow);

	// 4. Vanilla road meshes (Skyrim.esm: Landscape\Roads\RoadChunkL01.nif and family; Whiterun's main road; Dwemer roads).
	by.clear();
	Check("mesh Landscape\\Roads\\RoadChunkL01.nif", surface::rules::ClassifyObject({ "Landscape\\Roads\\RoadChunkL01.nif", "" }, &by), Kind::kRoad, by);
	by.clear();
	Check("mesh Architecture\\WhiteRun\\WRTerrain\\WRMainRoadMarket.nif", surface::rules::ClassifyObject({ "Architecture\\WhiteRun\\WRTerrain\\WRMainRoadMarket.nif", "" }, &by), Kind::kRoad, by);
	by.clear();
	Check("mesh with a road diffuse only (Landscape\\Roads\\Road01.dds)", surface::rules::ClassifyObject({ "Landscape\\Bridge01.nif", "", "Landscape\\Roads\\Road01.dds" }, &by), Kind::kRoad, by);
	Check("mesh Landscape\\Rocks\\RockCliff01.nif (not a road)", surface::rules::ClassifyObject({ "Landscape\\Rocks\\RockCliff01.nif", "RockCliff01" }), Kind::kObject);

	// 5. A dirt path without "road" in any name is not a road; vanilla's snowy dirt path stays a trail.
	Check("LDirtPath01 / LandscapeDirtPath01 / DirtPath01.dds", surface::rules::ClassifyLand("dirtpath01", { "LDirtPath01", "LandscapeDirtPath01", "PBR\\Landscape\\DirtPath01.dds", "PBR\\Landscape\\DirtPath01_N.dds" }, lists),
		Kind::kOther);
	Check("LDirtSnowPath01 / DirtSnowPath01.dds", surface::rules::ClassifyLand("dirtsnowpath01", { "LDirtSnowPath01", "LandscapeDirtSnowPath01", "PBR\\Landscape\\DirtSnowPath01.dds", "" }, lists),
		Kind::kTrail);

	// 6. Ice still comes first, whatever a name says.
	Check("ice list texture with \"road\" in its set's name", surface::rules::ClassifyLand("frozenmarshice01", { "LFrozenMarshIce01", "IceRoadSet", "Landscape\\FrozenMarshIce01.dds", "" }, lists), Kind::kIce);

	// 7. Any letter case, and the words that only contain the letters (accepted: no snow ground in the load order has them).
	Check("upper case ROAD", surface::rules::ClassifyLand("x", { "MYROADTEX", "", "", "" }, lists), Kind::kRoad);
	Check("Crossroads", surface::rules::ClassifyLand("x", { "LCrossroads01", "", "", "" }, lists), Kind::kRoad);
	Check("Railroad", surface::rules::ClassifyLand("x", { "", "", "textures\\railroad01.dds", "" }, lists), Kind::kRoad);
	Check("no name at all", surface::rules::ClassifyLand("", { "", "", "", "" }, lists), Kind::kOther);
	Check("\"rod\" / \"roa d\" are not road", surface::rules::ClassifyLand("x", { "LRod01", "roa d", "", "" }, lists), Kind::kOther);

	// 8. The optional sRoad list still works for a road named otherwise.
	by.clear();
	Check("sRoad entry grounddirt01 with no names", surface::rules::ClassifyLand("grounddirt01", { "", "", "", "" }, lists, &by), Kind::kRoad, by);

	// 9. Optional: every landscape texture of a load order.
	if (argc > 2)
	{
		std::ifstream tsv(argv[2]);
		std::string line;
		int rows = 0, roads = 0;
		while (std::getline(tsv, line))
		{
			std::vector<std::string> f;
			std::stringstream ss(line);
			std::string cell;
			while (std::getline(ss, cell, '\t')) { f.push_back(cell); }
			while (f.size() < 5) { f.emplace_back(); }
			std::string stem = f[3];
			for (char& c : stem) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			if (const auto s = stem.find_last_of("\\/"); s != std::string::npos) { stem = stem.substr(s + 1); }
			if (stem.size() > 4 && stem.ends_with(".dds")) { stem.resize(stem.size() - 4); }
			std::string rb;
			const Kind k = surface::rules::ClassifyLand(stem, { f[1], f[2], f[3], f[4] }, lists, &rb);
			++rows;
			if (k == Kind::kRoad)
			{
				++roads;
				std::printf("  road: %-40s %-34s by '%s'\n", f[0].c_str(), f[1].c_str(), rb.c_str());
			}
		}
		std::printf("load order: %d landscape textures, %d roads\n", rows, roads);
	}

	std::printf("surface_rules_check: %d of %d passed\n", passed, passed + failed);
	return failed == 0 ? 0 : 1;
}
