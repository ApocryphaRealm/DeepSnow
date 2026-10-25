#include "PCH.h"

#include "Surface.h"

#include "EditorIDs.h"
#include "Settings.h"
#include "utils/Logger.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <unordered_map>
#include <vector>

// Game thread only: every function here reads game objects (TES, the cell's LAND and navmesh, havok).

namespace surface
{
	namespace
	{
		constexpr float kCellSize = 4096.0F;
		constexpr float kQuadSize = 2048.0F;
		constexpr float kVertexStep = 128.0F;

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}

		// The lists, copied out of settings once per change (ListsGeneration), so a tick never takes the settings lock
		// more than once.
		struct Table
		{
			std::uint64_t generation = 0;
			std::array<std::vector<std::string>, static_cast<std::size_t>(settings::List::kCount)> lists;
			std::vector<std::uint32_t> iceMaterials;  // sIceMaterials parsed once (decimal IDs; a bad entry is skipped)
		};
		const Table& CurrentTable()
		{
			static Table t;
			const std::uint64_t g = settings::ListsGeneration();
			if (t.generation != g)
			{
				for (int i = 0; i < static_cast<int>(settings::List::kCount); ++i) { t.lists[i] = settings::GetList(static_cast<settings::List>(i)); }
				t.iceMaterials.clear();
				for (const auto& s : t.lists[static_cast<std::size_t>(settings::List::kIceMaterial)])
				{
					try { t.iceMaterials.push_back(static_cast<std::uint32_t>(std::stoul(s))); }
					catch (...) { logger::warn("[surface] sIceMaterials: '{}' is not a decimal material ID; skipped", s); }
				}
				t.generation = g;
				logger::debug("[surface] table re-read: snow={} trail={} road={} ice={} iceMeshes={} iceMaterials={}", settings::ListText(settings::List::kSnow),
					settings::ListText(settings::List::kTrail), settings::ListText(settings::List::kRoad), settings::ListText(settings::List::kIce),
					settings::ListText(settings::List::kIceMesh), settings::ListText(settings::List::kIceMaterial));
			}
			return t;
		}

		using rules::InList;

		rules::Lists ListsOf(const Table& a_t)
		{
			return { a_t.lists[static_cast<std::size_t>(settings::List::kIce)], a_t.lists[static_cast<std::size_t>(settings::List::kRoad)],
				a_t.lists[static_cast<std::size_t>(settings::List::kTrail)], a_t.lists[static_cast<std::size_t>(settings::List::kSnow)] };
		}

		std::string StemOf(const RE::TESLandTexture* a_lt)
		{
			if (!a_lt || !a_lt->textureSet) { return {}; }
			return Stem(a_lt->textureSet->textures[0].textureName.c_str());
		}

		const char* PathOf(const RE::BGSTextureSet* a_ts, int a_slot)
		{
			const char* p = a_ts ? a_ts->textures[a_slot].textureName.c_str() : nullptr;
			return p ? p : "";
		}

		// The diffuse paths an object's loaded mesh draws say "road" (or "path", with a_pathsAreRoads - [Roads]
		// bPathsAreRoads)? Asked once per mesh path (the answer is the same for every placed copy of it) and remembered -
		// both the first road texture and the first path texture, so the setting can change without asking again. The
		// scene graph walk stops at the first road texture.
		bool MeshDrawsRoad(RE::TESObjectREFR* a_ref, const std::string& a_model, std::string& a_by, bool a_pathsAreRoads)
		{
			struct Found { std::string road, path; };
			static std::unordered_map<std::string, Found> known;  // model (lower case) -> its road / path texture, or ""
			if (a_model.empty()) { return false; }  // nothing to remember the answer by
			std::string full = a_model;
			for (char& c : full) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			const auto pick = [&](const Found& f) {
				a_by = !f.road.empty() ? f.road : (a_pathsAreRoads ? f.path : std::string());
				return !a_by.empty();
			};
			if (const auto it = known.find(full); it != known.end()) { return pick(it->second); }
			RE::NiAVObject* root = a_ref ? a_ref->Get3D() : nullptr;
			if (!root) { return false; }  // not loaded yet: ask again next time rather than remembering a miss (rule 17)
			Found found;
			RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* a_geom) -> RE::BSVisit::BSVisitControl {
				RE::BSLightingShaderProperty* lsp = a_geom ? a_geom->lightingShaderProp_cast() : nullptr;
				auto* mat = lsp ? static_cast<RE::BSLightingShaderMaterialBase*>(lsp->material) : nullptr;
				RE::BSTextureSet* ts = mat ? mat->textureSet.get() : nullptr;
				const char* diffuse = ts ? ts->GetTexturePath(RE::BSTextureSet::Texture::kDiffuse) : nullptr;
				if (diffuse && rules::HasRoad(diffuse))
				{
					found.road = diffuse;
					return RE::BSVisit::BSVisitControl::kStop;
				}
				if (diffuse && found.path.empty() && rules::HasPath(diffuse)) { found.path = diffuse; }
				return RE::BSVisit::BSVisitControl::kContinue;
			});
			known.emplace(full, found);
			logger::debug("[surface] mesh '{}' textures: road '{}', path '{}'", a_model, found.road, found.path);
			return pick(found);
		}

		// Snow cover at the nearest LAND vertex: the base texture under the blend layers, each layer laid over the last
		// with its alpha (0..255, read in test 78b3). 1 = all snow-list textures, 0 = none. -1 = the LAND is not readable.
		float Cover(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_pos, const Table& a_t)
		{
			if (!a_cell || a_cell->IsInteriorCell()) { return -1.0F; }
			const RE::EXTERIOR_DATA* ext = a_cell->GetCoordinates();
			RE::TESObjectLAND* land = a_cell->GetRuntimeData().cellLand;
			const RE::TESObjectLAND::LoadedLandData* ld = land ? land->loadedData : nullptr;
			if (!ext || !ld) { return -1.0F; }
			const float lx = a_pos.x - static_cast<float>(ext->cellX) * kCellSize;
			const float ly = a_pos.y - static_cast<float>(ext->cellY) * kCellSize;
			const int qx = lx >= kQuadSize ? 1 : 0;
			const int qy = ly >= kQuadSize ? 1 : 0;
			const int quad = qy * 2 + qx;
			const int col = std::clamp(static_cast<int>(std::lround((lx - static_cast<float>(qx) * kQuadSize) / kVertexStep)), 0, 16);
			const int row = std::clamp(static_cast<int>(std::lround((ly - static_cast<float>(qy) * kQuadSize) / kVertexStep)), 0, 16);
			const int vtx = row * 17 + col;
			const auto& snow = a_t.lists[static_cast<std::size_t>(settings::List::kSnow)];
			float c = InList(snow, StemOf(ld->defQuadTextures[quad])) ? 1.0F : 0.0F;
			for (int i = 0; i < 6; ++i)
			{
				const RE::TESLandTexture* lt = ld->quadTextures[quad][i];
				if (!lt) { continue; }
				const float a = static_cast<float>(static_cast<std::uint8_t>(ld->percents[quad][vtx][i])) / 255.0F;
				const float s = InList(snow, StemOf(lt)) ? 1.0F : 0.0F;
				c = c * (1.0F - a) + s * a;
			}
			return std::clamp(c, 0.0F, 1.0F);
		}

		bool InTriangle2D(const RE::NiPoint3& p, const RE::NiPoint3& a, const RE::NiPoint3& b, const RE::NiPoint3& c, float& z)
		{
			const float d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
			if (std::fabs(d) < 1e-6F) { return false; }
			const float w1 = ((b.y - c.y) * (p.x - c.x) + (c.x - b.x) * (p.y - c.y)) / d;
			const float w2 = ((c.y - a.y) * (p.x - c.x) + (a.x - c.x) * (p.y - c.y)) / d;
			const float w3 = 1.0F - w1 - w2;
			constexpr float e = -1e-4F;
			if (w1 < e || w2 < e || w3 < e) { return false; }
			z = w1 * a.z + w2 * b.z + w3 * c.z;
			return true;
		}

		// The navmesh triangle under a_pos (world coordinates - 78b3 never saw a cell-local hit) and its preferred flag.
		bool Preferred(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_pos, bool& a_found)
		{
			a_found = false;
			RE::NavMeshArray* arr = a_cell ? a_cell->GetRuntimeData().navMeshes : nullptr;
			if (!arr) { return false; }
			float best = settings::probe::navmeshMaxDz.load();
			bool preferred = false;
			for (auto& sp : arr->navMeshes)
			{
				RE::NavMesh* nm = sp.get();
				if (!nm) { continue; }
				const auto& verts = nm->vertices;
				const auto& tris = nm->triangles;
				const std::uint32_t nv = verts.size();
				for (std::uint32_t i = 0; i < tris.size(); ++i)
				{
					const RE::BSNavmeshTriangle& t = tris[i];
					if (t.vertices[0] >= nv || t.vertices[1] >= nv || t.vertices[2] >= nv) { continue; }
					float z = 0.0F;
					if (!InTriangle2D(a_pos, verts[t.vertices[0]].location, verts[t.vertices[1]].location, verts[t.vertices[2]].location, z)) { continue; }
					const float dz = std::fabs(z - a_pos.z);
					if (dz > best) { continue; }
					best = dz;
					a_found = true;
					preferred = (t.triangleFlags.underlying() & static_cast<std::uint16_t>(RE::BSNavmeshTriangle::TriangleFlag::kPreferred)) != 0;
				}
			}
			return preferred;
		}

		struct Ray
		{
			bool hit = false;
			float distance = -1.0F;
			RE::TESObjectREFR* ref = nullptr;
		};

		Ray CastRay(RE::Actor* a_actor, RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
		{
			Ray r;
			RE::bhkWorld* world = a_cell ? a_cell->GetbhkWorld() : nullptr;
			if (!world)
			{
				static bool warned = false;
				if (!warned) { logger::debug("[surface] a cell has no havok world; rays are skipped there"); warned = true; }
				return r;
			}
			const float s = RE::bhkWorld::GetWorldScale();
			RE::bhkPickData pick;
			pick.rayInput.from = RE::hkVector4(a_from.x * s, a_from.y * s, a_from.z * s, 0.0F);
			pick.rayInput.to = RE::hkVector4(a_to.x * s, a_to.y * s, a_to.z * s, 0.0F);
			std::uint32_t filter = 0;
			a_actor->GetCollisionFilterInfo(filter);
			// The actor's own collision group (high 16 bits) so the ray ignores its capsule; the LOS layer (as the probe).
			pick.rayInput.filterInfo = (filter & 0xFFFF0000u) | static_cast<std::uint32_t>(RE::COL_LAYER::kLOS);
			world->PickObject(pick);
			if (!pick.rayOutput.HasHit()) { return r; }
			r.hit = true;
			r.distance = pick.rayOutput.hitFraction * a_from.GetDistance(a_to);
			if (const RE::hkpCollidable* col = pick.rayOutput.rootCollidable) { r.ref = RE::TESHavokUtilities::FindCollidableRef(*col); }
			return r;
		}
	}

	const char* Name(Kind a_kind)
	{
		switch (a_kind)
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

	std::string Stem(const char* a_path)
	{
		if (!a_path) { return {}; }
		std::string p = Lower(a_path);
		const auto slash = p.find_last_of("\\/");
		if (slash != std::string::npos) { p = p.substr(slash + 1); }
		if (p.size() >= 4 && p.ends_with(".dds")) { p.resize(p.size() - 4); }
		return p;
	}

	Kind ClassifyTexture(const std::string& a_stem)
	{
		// ICE first: Ice is Slick owns ice, and a texture in two lists must never be slowed as snow (frozenmarshice01 is
		// in DTD's snow list; it is ICE here on purpose). Then the road rule on the stem, the lists (SurfaceRules.h).
		return rules::ClassifyLand(a_stem, {}, ListsOf(CurrentTable()), nullptr, settings::roads::pathsAreRoads.load());
	}

	bool IsIceMaterial(std::uint32_t a_material)
	{
		const auto& v = CurrentTable().iceMaterials;
		return a_material != 0 && std::find(v.begin(), v.end(), a_material) != v.end();
	}

	bool IsIceMesh(const std::string& a_model)
	{
		// Forward slashes and no leading "meshes/", as Ice is Slick writes its paths (and as the INI list does).
		std::string m = Lower(a_model);
		std::replace(m.begin(), m.end(), '\\', '/');
		if (m.starts_with("meshes/")) { m.erase(0, 7); }
		for (const auto& frag : CurrentTable().lists[static_cast<std::size_t>(settings::List::kIceMesh)])
		{
			if (!frag.empty() && m.find(frag) != std::string::npos) { return true; }
		}
		return false;
	}

	Reading Read(RE::Actor* a_actor, bool a_wantNavmesh)
	{
		Reading r;
		if (!a_actor) { r.why = "no actor"; return r; }
		RE::TESObjectCELL* cell = a_actor->GetParentCell();
		if (!cell || !a_actor->Is3DLoaded()) { r.why = "not loaded"; return r; }
		if (cell->IsInteriorCell()) { r.why = "interior"; return r; }
		if (a_actor->IsOnMount()) { r.why = "mounted"; return r; }
		const RE::ActorState* as = a_actor->AsActorState();
		if (as && as->IsSwimming()) { r.why = "swimming"; return r; }

		const Table& t = CurrentTable();
		const RE::NiPoint3 pos = a_actor->GetPosition();

		// ICE first, by Ice is Slick's own test: the collision material the character controller stands on (landscape
		// and meshes alike - 78b3 read LFrozenMarshIce01 as material Ice). A match is ICE whatever the texture says.
		if (const RE::bhkCharacterController* cc = a_actor->GetCharController())
		{
			r.material = static_cast<std::uint32_t>(cc->surfaceMaterial);
			if (IsIceMaterial(r.material)) { r.kind = Kind::kIce; r.why = "ice material"; return r; }
		}

		// Roof: one ray straight up (78b3: 238 units under Windhelm's gatehouse, none in open snow).
		const float up0 = settings::probe::roofRayStart.load();
		const float shelter = settings::depth::shelterHeight.load();
		if (shelter > 0.0F)
		{
			const Ray roof = CastRay(a_actor, cell, { pos.x, pos.y, pos.z + up0 }, { pos.x, pos.y, pos.z + up0 + shelter });
			r.roofDistance = roof.distance;
			r.sheltered = roof.hit;
		}

		// Feet: one ray straight down. A reference under the feet means a mesh (78b3: on Windhelm's bridge the ray hit
		// Windhelmbridge2.nif while the land texture below was the river bed - so a mesh wins over the land).
		const Ray down = CastRay(a_actor, cell, { pos.x, pos.y, pos.z + 32.0F }, { pos.x, pos.y, pos.z + 32.0F - settings::probe::groundRayLength.load() });
		if (down.hit && down.ref)
		{
			const RE::TESBoundObject* base = down.ref->GetBaseObject();
			if (base)
			{
				if (const auto* m = base->As<RE::TESModel>(); m && m->GetModel()) { r.model = m->GetModel(); }
			}
			if (!r.model.empty() && IsIceMesh(r.model)) { r.kind = Kind::kIce; r.why = "ice mesh"; r.onMesh = true; return r; }
			if (down.ref != a_actor && down.distance < 96.0F)
			{
				// The road rule on the object: its mesh path, its base object's editor ID, then the textures its mesh draws.
				r.onMesh = true;
				const std::string baseEdid = editor_ids::Get(base);
				const bool paths = settings::roads::pathsAreRoads.load();  // [Roads] bPathsAreRoads: "path" counts as "road"
				r.kind = rules::ClassifyObject({ r.model, baseEdid }, &r.roadBy, paths);
				if (r.kind != Kind::kRoad && MeshDrawsRoad(down.ref, r.model, r.roadBy, paths)) { r.kind = Kind::kRoad; }
				r.why = r.kind == Kind::kRoad ? "road mesh" : "on a mesh";
				return r;
			}
		}

		// Land: the engine's own pick (what footsteps use).
		RE::TES* tes = RE::TES::GetSingleton();
		if (!tes)
		{
			static bool warned = false;
			if (!warned) { logger::warn("[surface] TES singleton is null; the land texture is not read"); warned = true; }
			r.why = "no TES";
			return r;
		}
		const RE::TESLandTexture* lt = tes->GetLandTexture(pos);
		r.landTexture = lt ? lt->GetFormID() : 0;
		r.texture = StemOf(lt);
		if (lt)
		{
			// The road rule on the ground: the LTEX's editor ID, its texture set's editor ID, the diffuse and normal paths.
			const RE::BGSTextureSet* ts = lt->textureSet;
			r.landEditorID = editor_ids::Get(lt);
			r.textureSetEditorID = editor_ids::Get(ts);
			r.kind = rules::ClassifyLand(r.texture, { r.landEditorID, r.textureSetEditorID, PathOf(ts, 0), PathOf(ts, 1) }, ListsOf(t), &r.roadBy,
				settings::roads::pathsAreRoads.load());
			r.why = r.kind == Kind::kRoad ? (r.roadBy == "sRoad" ? "road texture list" : "road in the name") : "land texture";
		}
		else
		{
			r.kind = Kind::kOther;
			r.why = "default land texture";
		}
		if (r.kind == Kind::kIce) { return r; }

		const float cover = Cover(cell, pos, t);
		r.cover = cover < 0.0F ? 1.0F : cover;

		if (a_wantNavmesh && settings::roads::useNavmesh)
		{
			bool found = false;
			r.preferred = Preferred(cell, pos, found);
			r.navmeshRead = found;
			if (r.preferred && r.kind != Kind::kRoad)
			{
				r.kind = Kind::kRoad;
				r.why = "navmesh preferred path";
				r.roadBy = "navmesh";
			}
		}
		return r;
	}
}
