#include "PCH.h"

#include "Probe.h"

#include "Settings.h"
#include "utils/Logger.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

// Everything in this file that touches game state runs on the game thread (Sample(), queued through SKSE's task
// interface with no captures). The sampler thread and the DevBench handler only ever read the finished strings
// under stateLock - they never reach into game objects (rule 31: hand DevBench already-computed state).

namespace probe
{
	namespace
	{
		constexpr float kCellSize = 4096.0F;   // an exterior cell, game units
		constexpr float kQuadSize = 2048.0F;   // a LAND quadrant: 0 = SW, 1 = SE, 2 = NW, 3 = NE
		constexpr float kVertexStep = 128.0F;  // 17 x 17 vertices per quadrant
		constexpr std::size_t kHistory = 240;  // two minutes at 2 Hz

		std::mutex stateLock;
		std::string latestJson;              // guarded by stateLock
		std::deque<std::string> history;     // guarded by stateLock
		std::atomic<std::uint64_t> samples{ 0 };
		std::atomic<bool> taskPending{ false };
		std::atomic<bool> started{ false };

		std::string Escape(std::string_view a_in)
		{
			std::string out;
			out.reserve(a_in.size() + 8);
			for (const char c : a_in)
			{
				switch (c)
				{
				case '\\': out += "\\\\"; break;
				case '"': out += "\\\""; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default:
					if (static_cast<unsigned char>(c) < 0x20) { out += std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(c))); }
					else { out += c; }
					break;
				}
			}
			return out;
		}

		std::string Str(const char* a_s) { return a_s ? std::string(a_s) : std::string(); }

		std::string NowStamp()
		{
			const auto now = std::chrono::system_clock::now();
			return std::format("{:%Y-%m-%d %H:%M:%S}", std::chrono::floor<std::chrono::milliseconds>(now));
		}

		std::string PluginOf(const RE::TESForm* a_form)
		{
			if (!a_form) { return {}; }
			const RE::TESFile* file = a_form->GetFile(0);
			return file ? std::string(file->GetFilename()) : std::string("(none)");
		}

		// --- landscape -------------------------------------------------------------------------------------------

		struct TexInfo
		{
			std::string text;  // for the log line
			std::string json;  // a JSON object
		};

		TexInfo Describe(const RE::TESLandTexture* a_lt)
		{
			if (!a_lt)
			{
				// A null texture on a LAND quad is the engine's default landscape texture, not a missing read.
				return { "default", "{\"form\":null,\"note\":\"null - the default landscape texture\"}" };
			}
			std::string diffuse;
			std::string tsEdid;
			if (const RE::BGSTextureSet* ts = a_lt->textureSet)
			{
				diffuse = Str(ts->textures[0].textureName.c_str());
				tsEdid = Str(ts->GetFormEditorID());
			}
			std::uint32_t matId = 0;
			std::string matName;
			if (const RE::BGSMaterialType* mt = a_lt->materialType)
			{
				matId = static_cast<std::uint32_t>(mt->materialID);
				matName = Str(mt->materialName.c_str());
			}
			const std::string edid = Str(a_lt->GetFormEditorID());
			const std::string plugin = PluginOf(a_lt);
			TexInfo t;
			t.text = std::format("{:08X}[{}]{}{} tex='{}' mat={}", a_lt->GetFormID(), plugin, edid.empty() ? "" : " ", edid, diffuse, matName.empty() ? std::format("0x{:08X}", matId) : matName);
			t.json = std::format(
				"{{\"form\":\"{:08X}\",\"plugin\":\"{}\",\"editorId\":\"{}\",\"textureSet\":\"{:08X}\",\"textureSetEditorId\":\"{}\",\"diffuse\":\"{}\",\"material\":\"{}\",\"materialId\":\"0x{:08X}\",\"friction\":{},\"restitution\":{}}}",
				a_lt->GetFormID(), Escape(plugin), Escape(edid), a_lt->textureSet ? a_lt->textureSet->GetFormID() : 0u, Escape(tsEdid), Escape(diffuse), Escape(matName), matId,
				a_lt->havokData.friction, a_lt->havokData.restitution);
			return t;
		}

		// The quad method: which quadrant of the cell's LAND the player stands in, its base texture, and every
		// additional layer with its blend weight at the nearest of the quadrant's 17 x 17 vertices.
		void LandQuad(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_pos, std::string& a_log, std::string& a_json)
		{
			a_log = "quad=n/a";
			a_json = "null";
			if (!a_cell || a_cell->IsInteriorCell()) { return; }
			const RE::EXTERIOR_DATA* ext = a_cell->GetCoordinates();
			if (!ext) { a_log = "quad=no-coords"; return; }
			RE::TESObjectLAND* land = a_cell->GetRuntimeData().cellLand;
			if (!land) { a_log = "quad=no-land"; a_json = "{\"error\":\"the cell has no LAND\"}"; return; }
			const RE::TESObjectLAND::LoadedLandData* ld = land->loadedData;
			if (!ld) { a_log = "quad=land-not-loaded"; a_json = std::format("{{\"land\":\"{:08X}\",\"error\":\"LAND has no loaded data\"}}", land->GetFormID()); return; }

			const float lx = a_pos.x - static_cast<float>(ext->cellX) * kCellSize;
			const float ly = a_pos.y - static_cast<float>(ext->cellY) * kCellSize;
			const int qx = lx >= kQuadSize ? 1 : 0;
			const int qy = ly >= kQuadSize ? 1 : 0;
			const int quad = qy * 2 + qx;
			const int col = std::clamp(static_cast<int>(std::lround((lx - static_cast<float>(qx) * kQuadSize) / kVertexStep)), 0, 16);
			const int row = std::clamp(static_cast<int>(std::lround((ly - static_cast<float>(qy) * kQuadSize) / kVertexStep)), 0, 16);
			const int vtx = row * 17 + col;

			const TexInfo base = Describe(ld->defQuadTextures[quad]);
			std::string layersLog;
			std::string layersJson;
			int bestLayer = -1;
			int bestAlpha = -1;
			for (int i = 0; i < 6; ++i)
			{
				const RE::TESLandTexture* lt = ld->quadTextures[quad][i];
				const int alpha = static_cast<int>(static_cast<std::uint8_t>(ld->percents[quad][vtx][i]));
				if (!lt) { continue; }
				const TexInfo li = Describe(lt);
				if (!layersLog.empty()) { layersLog += ", "; layersJson += ","; }
				layersLog += std::format("L{}:{}@{}", i, li.text, alpha);
				layersJson += std::format("{{\"layer\":{},\"alpha\":{},\"texture\":{}}}", i, alpha, li.json);
				if (alpha > bestAlpha) { bestAlpha = alpha; bestLayer = i; }
			}
			a_log = std::format("quad={} v=({},{}) base={} layers=[{}]", quad, col, row, base.text, layersLog);
			a_json = std::format("{{\"land\":\"{:08X}\",\"quad\":{},\"vertex\":{{\"col\":{},\"row\":{}}},\"base\":{},\"layers\":[{}],\"strongestLayer\":{},\"strongestAlpha\":{}}}",
				land->GetFormID(), quad, col, row, base.json, layersJson, bestLayer, bestAlpha);
		}

		// --- navmesh ---------------------------------------------------------------------------------------------

		struct NavHit
		{
			bool found = false;
			std::uint32_t navmesh = 0;
			std::uint32_t triangle = 0;
			std::uint16_t flags = 0;
			std::uint16_t traversal = 0;
			float dz = 0.0F;
			const char* basis = "world";
			int meshes = 0;
			int triangles = 0;
		};

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

		void FindTriangle(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_p, const char* a_basis, NavHit& a_hit)
		{
			RE::NavMeshArray* arr = a_cell->GetRuntimeData().navMeshes;
			if (!arr) { return; }
			const float maxDz = settings::probe::navmeshMaxDz.load();
			float best = maxDz;
			a_hit.meshes = 0;
			a_hit.triangles = 0;
			for (auto& sp : arr->navMeshes)
			{
				RE::NavMesh* nm = sp.get();
				if (!nm) { continue; }
				++a_hit.meshes;
				const auto& verts = nm->vertices;
				const auto& tris = nm->triangles;
				const std::uint32_t nv = verts.size();
				a_hit.triangles += static_cast<int>(tris.size());
				for (std::uint32_t i = 0; i < tris.size(); ++i)
				{
					const RE::BSNavmeshTriangle& t = tris[i];
					if (t.vertices[0] >= nv || t.vertices[1] >= nv || t.vertices[2] >= nv) { continue; }
					float z = 0.0F;
					if (!InTriangle2D(a_p, verts[t.vertices[0]].location, verts[t.vertices[1]].location, verts[t.vertices[2]].location, z)) { continue; }
					const float dz = std::fabs(z - a_p.z);
					if (dz > best) { continue; }
					best = dz;
					a_hit.found = true;
					a_hit.navmesh = nm->GetFormID();
					a_hit.triangle = i;
					a_hit.flags = t.triangleFlags.underlying();
					a_hit.traversal = t.traversalFlags.underlying();
					a_hit.dz = z - a_p.z;
					a_hit.basis = a_basis;
				}
			}
		}

		NavHit Navmesh(RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_pos)
		{
			NavHit hit;
			if (!a_cell) { return hit; }
			FindTriangle(a_cell, a_pos, "world", hit);
			// NVNM vertices are expected in world coordinates. If nothing matches, the cell-local reading is tried once so
			// the spike log says which basis the data really uses (rule 30: ask the object, do not infer it).
			if (!hit.found && a_cell->IsExteriorCell())
			{
				if (const RE::EXTERIOR_DATA* ext = a_cell->GetCoordinates())
				{
					const RE::NiPoint3 local{ a_pos.x - static_cast<float>(ext->cellX) * kCellSize, a_pos.y - static_cast<float>(ext->cellY) * kCellSize, a_pos.z };
					FindTriangle(a_cell, local, "cell-local", hit);
				}
			}
			return hit;
		}

		// --- havok rays ------------------------------------------------------------------------------------------

		struct RayHit
		{
			bool ran = false;
			bool hit = false;
			float distance = 0.0F;
			int layer = -1;
			std::string text;
			std::string json;
		};

		RayHit CastRay(RE::PlayerCharacter* a_player, RE::TESObjectCELL* a_cell, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
		{
			RayHit r;
			r.text = "no-world";
			r.json = "{\"ran\":false,\"error\":\"the cell has no havok world\"}";
			RE::bhkWorld* world = a_cell ? a_cell->GetbhkWorld() : nullptr;
			if (!world) { return r; }

			const float s = RE::bhkWorld::GetWorldScale();
			RE::bhkPickData pick;
			pick.rayInput.from = RE::hkVector4(a_from.x * s, a_from.y * s, a_from.z * s, 0.0F);
			pick.rayInput.to = RE::hkVector4(a_to.x * s, a_to.y * s, a_to.z * s, 0.0F);
			std::uint32_t filter = 0;
			a_player->GetCollisionFilterInfo(filter);
			// The player's own collision group (the high 16 bits) so the ray ignores the player's capsule; the LOS layer.
			pick.rayInput.filterInfo = (filter & 0xFFFF0000u) | static_cast<std::uint32_t>(RE::COL_LAYER::kLOS);
			world->PickObject(pick);
			r.ran = true;

			const float length = a_from.GetDistance(a_to);
			if (!pick.rayOutput.HasHit())
			{
				r.text = std::format("none(<{:.0f})", length);
				r.json = std::format("{{\"ran\":true,\"hit\":false,\"length\":{:.1f}}}", length);
				return r;
			}
			r.hit = true;
			r.distance = pick.rayOutput.hitFraction * length;
			const RE::hkpCollidable* col = pick.rayOutput.rootCollidable;
			r.layer = col ? static_cast<int>(col->GetCollisionLayer()) : -1;
			RE::TESObjectREFR* ref = col ? RE::TESHavokUtilities::FindCollidableRef(*col) : nullptr;
			std::string refText = "landscape-or-none";
			std::string refJson = "null";
			if (ref)
			{
				const RE::TESBoundObject* base = ref->GetBaseObject();
				std::string model;
				if (base)
				{
					if (const auto* m = base->As<RE::TESModel>()) { model = Str(m->GetModel()); }
				}
				const std::string name = Str(ref->GetName());
				refText = std::format("ref={:08X} base={:08X}[{}] '{}' model='{}'", ref->GetFormID(), base ? base->GetFormID() : 0u, PluginOf(base), name, model);
				refJson = std::format("{{\"ref\":\"{:08X}\",\"base\":\"{:08X}\",\"basePlugin\":\"{}\",\"baseType\":{},\"name\":\"{}\",\"model\":\"{}\"}}", ref->GetFormID(),
					base ? base->GetFormID() : 0u, Escape(PluginOf(base)), base ? static_cast<int>(base->GetFormType()) : -1, Escape(name), Escape(model));
			}
			r.text = std::format("{:.0f}u layer={} {}", r.distance, r.layer, refText);
			r.json = std::format("{{\"ran\":true,\"hit\":true,\"distance\":{:.1f},\"length\":{:.1f},\"layer\":{},\"hitForm\":{}}}", r.distance, length, r.layer, refJson);
			return r;
		}

		// --- the sample --------------------------------------------------------------------------------------------

		void Store(std::string a_json)
		{
			std::scoped_lock l(stateLock);
			latestJson = a_json;
			history.push_back(std::move(a_json));
			while (history.size() > kHistory) { history.pop_front(); }
		}

		void Sample()
		{
			taskPending = false;
			static bool lastHadPlayer = true;
			RE::PlayerCharacter* player = RE::PlayerCharacter::GetSingleton();
			RE::TESObjectCELL* cell = player ? player->GetParentCell() : nullptr;
			if (!player || !cell || !player->Is3DLoaded())
			{
				if (lastHadPlayer)
				{
					logger::info("[probe] no player in a loaded cell (main menu or loading) - sampling waits");
					lastHadPlayer = false;
				}
				Store(std::format("{{\"ok\":false,\"time\":\"{}\",\"reason\":\"no player in a loaded cell\"}}", NowStamp()));
				return;
			}
			if (!lastHadPlayer) { logger::info("[probe] player is in a loaded cell - sampling resumes"); lastHadPlayer = true; }

			const std::uint64_t n = ++samples;
			const RE::NiPoint3 pos = player->GetPosition();

			// Where.
			RE::TESWorldSpace* ws = player->GetWorldspace();
			const std::string wsEdid = ws ? Str(ws->GetFormEditorID()) : std::string();
			const std::string wsName = ws ? Str(ws->GetName()) : std::string();
			const bool interior = cell->IsInteriorCell();
			const std::string cellEdid = Str(cell->GetFormEditorID());
			const std::string cellName = Str(cell->GetFullName());
			int cx = 0, cy = 0;
			if (const RE::EXTERIOR_DATA* ext = interior ? nullptr : cell->GetCoordinates()) { cx = ext->cellX; cy = ext->cellY; }
			const RE::BGSLocation* loc = player->GetCurrentLocation();
			const std::string locName = loc ? Str(loc->GetName()) : std::string();

			// State.
			const bool mounted = player->IsOnMount();
			const RE::ActorState* as = player->AsActorState();
			const bool swimming = as && as->IsSwimming();
			const bool inWater = player->IsInWater();
			RE::ActorValueOwner* avo = player->AsActorValueOwner();
			const float speedMult = avo ? avo->GetActorValue(RE::ActorValue::kSpeedMult) : -1.0F;

			// Landscape texture: the engine's own pick (what footsteps use) and the quad breakdown.
			std::string engineLog = "n/a";
			std::string engineJson = "null";
			std::string landMatJson = "null";
			if (!interior)
			{
				if (RE::TES* tes = RE::TES::GetSingleton())
				{
					const TexInfo e = Describe(tes->GetLandTexture(pos));
					engineLog = e.text;
					engineJson = e.json;
					landMatJson = std::format("\"0x{:08X}\"", static_cast<std::uint32_t>(tes->GetLandMaterialType(pos)));
				}
				else
				{
					static bool warned = false;
					if (!warned) { logger::warn("[probe] TES singleton is null; the engine landscape pick is skipped"); warned = true; }
				}
			}
			std::string quadLog, quadJson;
			LandQuad(cell, pos, quadLog, quadJson);

			// Navmesh triangle under the player.
			const NavHit nav = Navmesh(cell, pos);
			const bool preferred = nav.found && (nav.flags & static_cast<std::uint16_t>(RE::BSNavmeshTriangle::TriangleFlag::kPreferred)) != 0;
			const std::string navLog = nav.found ?
				std::format("{:08X}#{} flags=0x{:04X} preferred={} dz={:.0f} basis={}", nav.navmesh, nav.triangle, nav.flags, preferred ? 1 : 0, nav.dz, nav.basis) :
				std::format("none (meshes={} tris={})", nav.meshes, nav.triangles);
			const std::string navJson = nav.found ?
				std::format("{{\"found\":true,\"navmesh\":\"{:08X}\",\"triangle\":{},\"flags\":\"0x{:04X}\",\"traversal\":\"0x{:04X}\",\"preferred\":{},\"dz\":{:.1f},\"basis\":\"{}\",\"meshes\":{},\"triangles\":{}}}",
					nav.navmesh, nav.triangle, nav.flags, nav.traversal, preferred ? "true" : "false", nav.dz, nav.basis, nav.meshes, nav.triangles) :
				std::format("{{\"found\":false,\"meshes\":{},\"triangles\":{}}}", nav.meshes, nav.triangles);

			// Rays: up (roof / overhang) and down (what the feet stand on).
			const float up0 = settings::probe::roofRayStart.load();
			const RE::NiPoint3 roofFrom{ pos.x, pos.y, pos.z + up0 };
			const RE::NiPoint3 roofTo{ pos.x, pos.y, pos.z + up0 + settings::probe::roofRayLength.load() };
			const RayHit roof = CastRay(player, cell, roofFrom, roofTo);
			const RE::NiPoint3 downFrom{ pos.x, pos.y, pos.z + 32.0F };
			const RE::NiPoint3 downTo{ pos.x, pos.y, pos.z + 32.0F - settings::probe::groundRayLength.load() };
			const RayHit ground = CastRay(player, cell, downFrom, downTo);

			const std::string time = NowStamp();
			std::string json = std::format(
				"{{\"ok\":true,\"sample\":{},\"time\":\"{}\",\"position\":{{\"x\":{:.1f},\"y\":{:.1f},\"z\":{:.1f}}},"
				"\"worldspace\":{{\"form\":\"{:08X}\",\"editorId\":\"{}\",\"name\":\"{}\"}},"
				"\"cell\":{{\"form\":\"{:08X}\",\"editorId\":\"{}\",\"name\":\"{}\",\"interior\":{},\"x\":{},\"y\":{}}},\"location\":\"{}\","
				"\"mounted\":{},\"swimming\":{},\"inWater\":{},\"speedMult\":{:.1f},"
				"\"landEngine\":{},\"landMaterial\":{},\"landQuad\":{},\"navmesh\":{},\"roof\":{},\"ground\":{}}}",
				n, time, pos.x, pos.y, pos.z,
				ws ? ws->GetFormID() : 0u, Escape(wsEdid), Escape(wsName),
				cell->GetFormID(), Escape(cellEdid), Escape(cellName), interior ? "true" : "false", cx, cy, Escape(locName),
				mounted ? "true" : "false", swimming ? "true" : "false", inWater ? "true" : "false", speedMult,
				engineJson, landMatJson, quadJson, navJson, roof.json, ground.json);

			if (settings::probe::logSamples)
			{
				logger::info("[sample {}] pos=({:.0f},{:.0f},{:.0f}) ws={} cell={:08X} '{}'{} {} loc='{}' interior={} mounted={} swimming={} inWater={} speedMult={:.0f} | land: engine={} | {} | navmesh: {} | roof: {} | ground: {}",
					n, pos.x, pos.y, pos.z, wsEdid.empty() ? "-" : wsEdid, cell->GetFormID(), cellName, cellEdid.empty() ? "" : " " + cellEdid,
					interior ? "interior" : std::format("({},{})", cx, cy), locName, interior ? 1 : 0, mounted ? 1 : 0, swimming ? 1 : 0, inWater ? 1 : 0, speedMult,
					engineLog, quadLog, navLog, roof.text, ground.text);
			}
			Store(std::move(json));
		}

		void QueueSample()
		{
			// One sample in flight at a time: while the game thread is busy (a load) the queue never piles up.
			bool expected = false;
			if (!taskPending.compare_exchange_strong(expected, true)) { return; }
			const SKSE::TaskInterface* tasks = SKSE::GetTaskInterface();
			if (!tasks)
			{
				static bool warned = false;
				if (!warned) { logger::error("[probe] SKSE task interface is null; no samples can be taken"); warned = true; }
				taskPending = false;
				return;
			}
			tasks->AddTask(&Sample);
		}

		void SamplerLoop()
		{
			logger::info("[probe] sampler thread started: every {} ms", settings::probe::intervalMs.load());
			for (;;)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(settings::probe::intervalMs.load()));
				if (settings::probe::enabled) { QueueSample(); }
			}
		}
	}

	void Start()
	{
		bool expected = false;
		if (!started.compare_exchange_strong(expected, true)) { return; }
		if (!settings::probe::enabled)
		{
			logger::info("[probe] bEnabled=0 - the probe does not sample");
			return;
		}
		// Detached on purpose: joining a thread from a static destructor at game exit can hang the shutdown.
		std::thread(&SamplerLoop).detach();
	}

	std::string LatestJson()
	{
		std::scoped_lock l(stateLock);
		return latestJson.empty() ? std::string("{\"ok\":false,\"reason\":\"no sample yet\"}") : latestJson;
	}

	std::string HistoryJson(std::size_t a_count)
	{
		std::scoped_lock l(stateLock);
		const std::size_t n = std::min(a_count, history.size());
		std::string out = "[";
		for (std::size_t i = history.size() - n; i < history.size(); ++i)
		{
			if (out.size() > 1) { out += ","; }
			out += history[i];
		}
		out += "]";
		return out;
	}

	std::uint64_t SampleCount() { return samples.load(); }
}
