#pragma once

// Deep Snow - the ICE row's defaults: what Ice is Slick (Ckw25, Nexus 190881) treats as ice, so the two mods never act
// on the same ground. ICE means "not ours": no snow band, no stamina drain, no slowdown.
//
// TAKEN FROM: Ice is Slick 1.0.3.2, its published source archive ("Ice is Slick 1.0.3.2 - Source", GPL-3.0-or-later,
// read 2026-10-24). These are facts about game data - collision-material IDs, texture and mesh file names - listed
// here as plain data; none of Ice is Slick's code is used. When Ice is Slick updates, re-read the same four places in
// its source and update the three lists below (and the matching lines in dist\SKSE\Plugins\DeepSnow.ini):
//   materials  src/core/SurfaceClassifier.h   kSkyrimIceMaterial, kSkyrimIceFormMaterial
//   textures   src/core/SurfaceCoatingModel.h IsIceSurfaceTexture (stems: no folder, no .dds)
//   meshes     src/core/CompatibilityRules.h  IsFrozenWaterModel (paths under meshes\, matched as path fragments)
// Not copied, because it changes nothing here: its list of about 190 official ice base forms (SurfaceClassifier.h).
// Those are all meshes, and standing on any mesh already gives no snow effect in Deep Snow (the OBJECT row).

namespace ice_is_slick
{
	inline constexpr const char* kSourceVersion = "1.0.3.2";

	// Collision materials (decimal, as the engine's MATERIAL_ID): MaterialIce and MaterialIceForm. Ice is Slick's main
	// test, on landscape and meshes alike: the character controller's current support material.
	inline constexpr const char* kMaterials = "873356572,2431524493";

	// Landscape texture stems. Ice is Slick matches these names on a mesh's exposed surface (ice coatings); on landscape
	// it goes by the material above, so these are the fallback when no material is read. frozenmarshice02 is ours:
	// Skyrim.esm's LFrozenMarshIce01 (material Ice) draws frozenmarshice02.dds under PBR (test 78b3's readings).
	inline constexpr const char* kTextures =
		"frozenmarshice01,frozenmarshice02,glacierslab,glaciersubsurface,glacierparallax,icefloes,athglaciersubsurface,"
		"icefloesunderparallax,icefloesunderparallax_l,icefloes_new4,icefloes_new4_l,icelakelower,icelakesurface,icewall01,"
		"icesurface,icefrozen03";

	// Frozen-water meshes (path under meshes\, forward slashes, lower case).
	inline constexpr const char* kMeshes =
		"landscape/ice/icefloes01.nif,landscape/ice/icefloessolid01.nif,landscape/ice/icefloessolid02.nif,"
		"landscape/ice/frozenpooll01.nif,landscape/ice/frozenmarshicefloel01.nif,landscape/ice/frozenmarshicefloem01.nif,"
		"landscape/ice/frozenmarshicefloes01.nif,landscape/ice/frozenmarshicefloes02.nif,landscape/ice/icebergsmall01_anim.nif,"
		"landscape/ice/icebergsmall02_anim.nif,landscape/ice/iceberglarge_anim.nif,landscape/ice/icefloessolid01_anim.nif,"
		"landscape/ice/icefloes01_anim.nif,landscape/ice/icefloessolid02_anim.nif,landscape/ice/frozenmarshicefloechunks01_anim.nif,"
		"landscape/ice/frozenmarshicefloechunks02_anim.nif,landscape/ice/frozenmarshicefloechunks03_anim.nif,"
		"dlc01/landscape/dlc01frozenlake01.nif,dlc01/landscape/dlc01frozenlakebreak01.nif,dlc01/landscape/dlc01frozenlakebreak02.nif,"
		"dlc01/landscape/dlc01frozenlakebreakcap.nif,dlc01/landscape/dlc01frozenlakebreakcap02.nif,"
		"dlc01/landscape/dlc01frozenlakeedgene.nif,dlc01/landscape/dlc1icefloel01.nif,dlc01/landscape/dlc1icefloem01.nif,"
		"dlc01/landscape/dlc1icefloes01.nif,dlc01/landscape/dlc1icefloes02.nif,dlc01/landscape/dlc1icefloechunks01.nif,"
		"dlc01/landscape/dlc1icefloechunks02.nif,dlc01/landscape/dlc1icefloechunks03.nif,dlc01/landscape/dlc1icelandbroken01.nif,"
		"dlc01/landscape/dlc1icelandbroken02.nif,dlc01/landscape/dlc1icefloechunks01_anim.nif,"
		"dlc01/landscape/dlc1icefloechunks02_anim.nif,dlc01/landscape/dlc1icefloechunks03_anim.nif";
}
