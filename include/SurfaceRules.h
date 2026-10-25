#pragma once

// Deep Snow - the surface rules on their own: plain strings in, a surface kind out. No game types, so
// tools\surface_rules_check.cpp compiles this same header offline (as tools\check-band-mapping.cpp does DepthMath.h).
//
// THE ROAD RULE (the owner, 2026-10-25): "any road or texture that has the word road in it is exempt from the speed
// debuff", so no patch per road mod is ever needed. A name containing "road" in any letter case is a road - for the
// ground: the landscape texture's editor ID (LTEX, e.g. COTN_LRoadStone01), its texture set's editor ID (TXST) and the
// diffuse and normal paths; for an object under the feet: its mesh path, its base object's editor ID and the diffuse
// paths its mesh draws. Words that merely contain the letters ("crossroads", "railroad", "roadside") count as road too:
// a load order's names were checked and no snow ground carries them. sRoad stays as an optional extra list for a road
// whose names never say "road".
//
// PATHS AS ROADS (optional, [Roads] bPathsAreRoads, off by default; the owner, 2026-10-25): some road mods name their
// roads "path" - Northern Roads' Seasons winter swaps its dirt roads to vanilla LDirtSnowPath01, which says "path" and
// sits on the trail list. With the setting on, a name containing "path" (so "footpath" too) in any letter case counts
// exactly as "road" does, in the same names, and wins over the trail and snow lists. Ice still comes first.
//
// The order (unchanged from 1.0.0 except the name test): ICE first (Ice is Slick owns ice, whatever else a name says),
// then ROAD (the name rule, then the sRoad list), then TRAIL, then SNOW, else OTHER.

#include <cctype>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace surface
{
	enum class Kind : std::uint8_t
	{
		kNone = 0,    // not classified: interior, mounted, swimming, no 3D, the mod off
		kSnow,        // SNOW row - deep snow
		kTrail,       // TRAIL row - a snowy path, already packed
		kRoad,        // ROAD row - a road (a name with "road" in it, the sRoad list, or the navmesh preferred flag)
		kIce,         // ICE row - not ours: Ice is Slick owns ice; no slow, no drain
		kObject,      // standing on a mesh that is neither ice nor a road
		kOther,       // landscape no list names
	};

	namespace rules
	{
		// True when a_name contains a_word (lower case) in any letter case.
		inline bool HasWord(std::string_view a_name, std::string_view word)
		{
			if (a_name.size() < word.size()) { return false; }
			for (std::size_t i = 0; i + word.size() <= a_name.size(); ++i)
			{
				std::size_t k = 0;
				while (k < word.size() && std::tolower(static_cast<unsigned char>(a_name[i + k])) == word[k]) { ++k; }
				if (k == word.size()) { return true; }
			}
			return false;
		}

		// True when a_name contains "road" in any letter case.
		inline bool HasRoad(std::string_view a_name) { return HasWord(a_name, "road"); }

		// True when a_name contains "path" in any letter case ("footpath" included).
		inline bool HasPath(std::string_view a_name) { return HasWord(a_name, "path"); }

		// The name test of the road rule: "road", or "path" too when a_pathsAreRoads ([Roads] bPathsAreRoads).
		inline bool IsRoadName(std::string_view a_name, bool a_pathsAreRoads) { return HasRoad(a_name) || (a_pathsAreRoads && HasPath(a_name)); }

		// The first of a_names that passes the name test, or an empty view when none does (the reading reports which name
		// decided). A name saying "road" is preferred over one saying only "path".
		inline std::string_view RoadName(std::initializer_list<std::string_view> a_names, bool a_pathsAreRoads = false)
		{
			for (const std::string_view n : a_names)
			{
				if (HasRoad(n)) { return n; }
			}
			if (a_pathsAreRoads)
			{
				for (const std::string_view n : a_names)
				{
					if (HasPath(n)) { return n; }
				}
			}
			return {};
		}

		// '*' matches any run of characters; everything else literally (both sides already lower case).
		inline bool Glob(const char* p, const char* s)
		{
			if (*p == '\0') { return *s == '\0'; }
			if (*p == '*')
			{
				for (const char* t = s;; ++t)
				{
					if (Glob(p + 1, t)) { return true; }
					if (*t == '\0') { return false; }
				}
			}
			return *s != '\0' && *p == *s && Glob(p + 1, s + 1);
		}

		inline bool InList(const std::vector<std::string>& a_list, const std::string& a_stem)
		{
			for (const auto& p : a_list)
			{
				if (Glob(p.c_str(), a_stem.c_str())) { return true; }
			}
			return false;
		}

		// The four texture lists of [Surfaces], lower case, as Surface.cpp copies them out of settings.
		struct Lists
		{
			const std::vector<std::string>& ice;
			const std::vector<std::string>& road;
			const std::vector<std::string>& trail;
			const std::vector<std::string>& snow;
		};

		// Landscape: a_stem is the diffuse file's lower-case stem (what the lists hold); a_names are every name the
		// road rule reads (LTEX editor ID, TXST editor ID, diffuse path, normal path - an empty one is skipped).
		// a_roadBy (optional) receives the name that made it a road, or "sRoad" for a list match. a_pathsAreRoads:
		// [Roads] bPathsAreRoads - "path" in a name counts as "road".
		inline Kind ClassifyLand(const std::string& a_stem, std::initializer_list<std::string_view> a_names, const Lists& a_lists, std::string* a_roadBy = nullptr,
			bool a_pathsAreRoads = false)
		{
			if (InList(a_lists.ice, a_stem)) { return Kind::kIce; }
			if (const std::string_view by = RoadName(a_names, a_pathsAreRoads); !by.empty())
			{
				if (a_roadBy) { *a_roadBy = by; }
				return Kind::kRoad;
			}
			const bool stemSays = IsRoadName(a_stem, a_pathsAreRoads);
			if (stemSays || InList(a_lists.road, a_stem))
			{
				if (a_roadBy) { *a_roadBy = stemSays ? a_stem : std::string("sRoad"); }
				return Kind::kRoad;
			}
			if (InList(a_lists.trail, a_stem)) { return Kind::kTrail; }
			if (InList(a_lists.snow, a_stem)) { return Kind::kSnow; }
			return Kind::kOther;
		}

		// An object under the feet that is not ice: a road when its mesh path, its base object's editor ID or any
		// diffuse path its mesh draws says "road" (vanilla's road meshes: landscape\roads\road3way01.nif and family).
		// With a_pathsAreRoads ([Roads] bPathsAreRoads) a name saying "path" counts too.
		inline Kind ClassifyObject(std::initializer_list<std::string_view> a_names, std::string* a_roadBy = nullptr, bool a_pathsAreRoads = false)
		{
			if (const std::string_view by = RoadName(a_names, a_pathsAreRoads); !by.empty())
			{
				if (a_roadBy) { *a_roadBy = by; }
				return Kind::kRoad;
			}
			return Kind::kObject;
		}
	}
}
