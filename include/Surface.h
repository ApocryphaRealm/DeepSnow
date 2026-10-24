#pragma once

// Deep Snow - what an actor stands on (the surface table). Game thread only.
//
// The answer comes from the same readings the milestone-1 probe proved in test 78b3 (2026-10-24): the landscape
// texture the engine picks at the feet (TES::GetLandTexture, the diffuse file's stem), the LAND quadrant's blend
// layers at the nearest vertex (alpha 0..255, confirmed), a havok ray straight down (a hit on a reference = standing on
// a mesh: rock, bridge, dock, ice floe) and one straight up (a roof within fShelterHeight = sheltered, no snow), and for
// the player the navmesh triangle's "preferred path" flag (roads; world coordinates, confirmed).
//
// Which surface a texture is lives in five lists in the INI ([Surfaces] sSnow / sTrail / sRoad / sIce / sIceMeshes),
// so the table is changed without a rebuild - from the page's "mark the ground under me" buttons or by hand.

#include <cstdint>
#include <string>

namespace surface
{
	enum class Kind : std::uint8_t
	{
		kNone = 0,    // not classified: interior, mounted, swimming, no 3D, the mod off
		kSnow,        // SNOW row - deep snow
		kTrail,       // TRAIL row - a snowy path, already packed
		kRoad,        // ROAD row - a road (texture list, or the navmesh preferred flag)
		kIce,         // ICE row - not ours: Ice is Slick owns ice; no slow, no drain
		kObject,      // standing on a mesh that is not ice
		kOther,       // landscape no list names
	};
	const char* Name(Kind a_kind);  // "snow", "trail", ... (English: tools and logs read it, rule 66's exception)

	struct Reading
	{
		Kind kind = Kind::kNone;
		const char* why = "not read";   // the branch that decided (a static string)
		std::string texture;            // the engine pick's diffuse stem ("snow01"), empty if none
		std::string model;              // the mesh under the feet when standing on one
		float cover = 1.0F;             // snow cover under the feet from the blend layers, 0..1
		bool preferred = false;         // navmesh preferred-path flag (player only)
		bool navmeshRead = false;
		bool sheltered = false;         // a roof within fShelterHeight
		float roofDistance = -1.0F;
	};

	// Reads the ground under a_actor. a_wantNavmesh: the triangle walk is the expensive part, so only the player asks.
	Reading Read(RE::Actor* a_actor, bool a_wantNavmesh);

	// The surface table on its own (for DevBench and the page): which list a texture stem falls in.
	Kind ClassifyTexture(const std::string& a_stem);
	bool IsIceMesh(const std::string& a_model);
	std::string Stem(const char* a_path);  // "PBR\\Landscape\\Snow01.dds" -> "snow01"
}
