#include "PCH.h"

#include "Movement.h"

#include "Depth.h"
#include "EditorIDs.h"
#include "Effects.h"
#include "Settings.h"
#include "Trails.h"
#include "utils/Logger.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace movement
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		std::mutex snapLock;
		Snapshot snap;  // guarded by snapLock
		std::atomic<bool> started{ false };
		std::atomic<bool> taskPending{ false };
		std::atomic<int> forced{ -1 };
		std::atomic<int> markRequest{ -1 };
		std::atomic<bool> clearTrailsRequest{ false };
		std::atomic<bool> reapplyRequest{ false };
		std::atomic<bool> loadedRequest{ false };
		std::atomic<float> snowfallBoost{ 0.0F };  // also the co-save value

		// Per-actor memory between ticks (game thread only).
		struct Seen
		{
			int band = 0;
			bool road = false;
		};
		std::unordered_map<std::uint32_t, Seen> seen;
		std::size_t npcCursor = 0;
		std::uint64_t lastGeneration = 0;
		Clock::time_point lastTick{};
		float lastDays = -1.0F;
		float drainAccum = 0.0F;
		Clock::time_point drainWindow{};

		float NowDays()
		{
			const RE::Calendar* cal = RE::Calendar::GetSingleton();
			return cal ? cal->GetDaysPassed() : 0.0F;
		}

		bool Snowing()
		{
			const RE::Sky* sky = RE::Sky::GetSingleton();
			return sky && sky->IsSnowing();
		}

		trails::Who WhoOf(RE::Actor* a_actor)
		{
			if (a_actor->IsPlayerRef()) { return trails::Who::kPlayer; }
			const RE::TESRace* race = a_actor->GetRace();
			return race && race->HasKeywordString("ActorTypeNPC") ? trails::Who::kNPC : trails::Who::kCreature;
		}

		struct Result
		{
			surface::Reading reading;
			float packing = 0.0F;
			float depth = 0.0F;
			float sdUnits = -1.0F;
			int groundBand = 0;
			int band = 0;
			bool road = false;
			float drained = 0.0F;
		};

		Result Process(RE::Actor* a_actor, bool a_isPlayer, float a_now, bool a_snowing, float a_seconds)
		{
			Result r;
			r.reading = surface::Read(a_actor, a_isPlayer);
			RE::TESWorldSpace* ws = a_actor->GetWorldspace();
			const RE::NiPoint3 pos = a_actor->GetPosition();
			if (ws && r.reading.kind != surface::Kind::kNone)
			{
				trails::Stamp(a_actor->GetFormID(), ws->GetFormID(), pos.x, pos.y, WhoOf(a_actor), a_now);
				r.packing = trails::PackingAt(ws->GetFormID(), pos.x, pos.y, a_now, a_snowing);
			}
			// Snow Deformation's depth already carries its accumulation, so our snowfall boost applies only to our own model.
			r.depth = depth::PercentFor(a_actor, r.reading, r.packing, snowfallBoost.load(), &r.sdUnits);
			r.groundBand = depth::BandFor(r.depth);
			r.band = r.groundBand;
			r.road = r.reading.kind == surface::Kind::kRoad && settings::roads::bonus;
			if (a_isPlayer && forced.load() >= 0)
			{
				r.band = std::clamp(forced.load(), 0, settings::kBandCount);
				r.road = false;
			}
			if (!a_isPlayer && !settings::general::affectNPCs)
			{
				r.band = 0;
				r.road = false;
			}
			// Forced bands apply even indoors / mounted (it is a test hook); the ground's bands only where the ground was read.
			if (r.reading.kind == surface::Kind::kNone && !(a_isPlayer && forced.load() >= 0)) { r.band = 0; r.road = false; }

			Seen& s = seen[a_actor->GetFormID()];
			if (effects::Set(a_actor, r.band, r.road) || s.band != r.band || s.road != r.road)
			{
				if (a_isPlayer || s.band != r.band || s.road != r.road)
				{
					logger::debug("[tick] {:08X}{}: {} ({}) tex='{}' ltex='{}' roadBy='{}' cover={:.2f} packing={:.2f} sheltered={} sd={:.1f}u depth={:.1f}% -> band {} -> {}{}{}", a_actor->GetFormID(),
						a_isPlayer ? " (player)" : "", surface::Name(r.reading.kind), r.reading.why, r.reading.texture, r.reading.landEditorID, r.reading.roadBy, r.reading.cover, r.packing, r.reading.sheltered,
						r.sdUnits, r.depth, r.groundBand, r.band, r.road ? " +road" : "", a_isPlayer && forced.load() >= 0 ? " (forced)" : "");
				}
				s.band = r.band;
				s.road = r.road;
			}
			if (r.band > 0 && a_actor->IsMoving()) { r.drained = effects::Drain(a_actor, r.band, a_seconds); }
			return r;
		}

		void ClearEveryone()
		{
			if (auto* player = RE::PlayerCharacter::GetSingleton()) { effects::Set(player, 0, false); }
			for (const std::uint32_t id : effects::Applied())
			{
				if (auto* a = RE::TESForm::LookupByID<RE::Actor>(id)) { effects::Set(a, 0, false); }
			}
			seen.clear();
		}

		void DoMark(RE::PlayerCharacter* a_player)
		{
			const int list = markRequest.exchange(-1);
			if (list < 0) { return; }
			const surface::Reading rd = surface::Read(a_player, true);
			std::string item;
			settings::List target = static_cast<settings::List>(std::clamp(list, 0, static_cast<int>(settings::List::kIce)));
			if (rd.onMesh && !rd.model.empty())
			{
				if (target != settings::List::kIce)
				{
					logger::info("[mark] refused: the player stands on a mesh ('{}'); a mesh can only be marked as ice", rd.model);
					return;
				}
				item = surface::Stem(rd.model.c_str());
				if (item.size() > 4 && item.ends_with(".nif")) { item.resize(item.size() - 4); }
				target = settings::List::kIceMesh;
			}
			else
			{
				item = rd.texture;
			}
			if (item.empty()) { logger::info("[mark] refused: nothing readable under the player ({})", rd.why); return; }
			if (settings::Mark(target, item))
			{
				const bool saved = settings::Save();
				logger::info("[mark] '{}' is now {} ({})", item, settings::kListKeys[static_cast<int>(target)], saved ? "saved to the INI" : "NOT saved - see above");
			}
		}

		void Tick()
		{
			taskPending = false;
			const auto t0 = Clock::now();
			const float seconds = lastTick.time_since_epoch().count() == 0 ? 0.0F :
				std::min(2.0F, std::chrono::duration<float>(t0 - lastTick).count());
			lastTick = t0;

			RE::PlayerCharacter* player = RE::PlayerCharacter::GetSingleton();
			const bool havePlayer = player && player->GetParentCell() && player->Is3DLoaded();
			static bool lastHadPlayer = true;
			if (havePlayer != lastHadPlayer)
			{
				logger::info("[tick] {}", havePlayer ? "player is in a loaded cell - ticking" : "no player in a loaded cell (main menu or loading) - waiting");
				lastHadPlayer = havePlayer;
			}
			if (!havePlayer)
			{
				std::scoped_lock l(snapLock);
				snap.havePlayer = false;
				return;
			}

			if (loadedRequest.exchange(false))
			{
				effects::ResetAfterLoad();
				seen.clear();
				npcCursor = 0;
				lastDays = -1.0F;
			}

			const std::uint64_t gen = settings::Generation();
			if (gen != lastGeneration || reapplyRequest.exchange(false))
			{
				effects::ApplyMagnitudes();
				for (const std::uint32_t id : effects::Applied())
				{
					if (auto* a = RE::TESForm::LookupByID<RE::Actor>(id)) { effects::Reapply(a); }
				}
				lastGeneration = gen;
			}
			if (clearTrailsRequest.exchange(false)) { trails::Clear(); }
			DoMark(player);

			const float now = NowDays();
			const bool snowing = Snowing();

			// The snowfall boost rises while it snows and falls back after, fPerHour % per game hour.
			if (lastDays >= 0.0F && settings::snowfall::deepens)
			{
				const float hours = std::clamp((now - lastDays) * 24.0F, 0.0F, 24.0F);
				const float step = settings::snowfall::perHour.load() / 100.0F * hours;
				const float cap = settings::snowfall::max.load() / 100.0F;
				snowfallBoost = std::clamp(snowfallBoost.load() + (snowing ? step : -step), 0.0F, cap);
			}
			else if (!settings::snowfall::deepens)
			{
				snowfallBoost = 0.0F;
			}
			lastDays = now;

			static bool wasEnabled = true;
			const bool enabled = settings::general::enabled.load() && effects::Ready();
			if (!enabled)
			{
				if (wasEnabled) { ClearEveryone(); logger::info("[tick] the mechanism is off ({}) - every Deep Snow ability removed", effects::Ready() ? "bEnabled=0" : "DeepSnow.esp missing"); }
				wasEnabled = false;
				std::scoped_lock l(snapLock);
				snap.havePlayer = true;
				snap.enabled = false;
				snap.effectsReady = effects::Ready();
				return;
			}
			wasEnabled = true;

			const Result pr = Process(player, true, now, snowing, seconds);

			// NPCs and creatures: every nearby one stamps the trail (cheap); uActorsPerTick of them are classified.
			int nearCount = 0, classified = 0, affected = 0;
			if (RE::ProcessLists* pl = RE::ProcessLists::GetSingleton())
			{
				const float radius = settings::general::scanRadius.load();
				const RE::NiPoint3 ppos = player->GetPosition();
				const std::uint32_t budget = settings::general::actorsPerTick.load();
				auto& handles = pl->highActorHandles;
				const std::size_t n = handles.size();
				std::size_t start = n ? npcCursor % n : 0;
				for (std::size_t k = 0; k < n; ++k)
				{
					const std::size_t i = (start + k) % n;
					RE::NiPointer<RE::Actor> sp = handles[static_cast<std::uint32_t>(i)].get();
					RE::Actor* a = sp.get();
					if (!a || a == player || a->IsDead() || a->IsDisabled() || !a->Is3DLoaded()) { continue; }
					if (a->GetPosition().GetDistance(ppos) > radius) { continue; }
					++nearCount;
					if (classified < static_cast<int>(budget))
					{
						const Result r = Process(a, false, now, snowing, seconds * static_cast<float>(std::max<std::size_t>(1, n)) / std::max(1.0F, static_cast<float>(budget)));
						++classified;
						if (r.band > 0 || r.road) { ++affected; }
						npcCursor = i + 1;
					}
					else if (settings::trails::enabled && !a->IsOnMount())
					{
						if (RE::TESWorldSpace* ws = a->GetWorldspace())
						{
							const RE::NiPoint3 p = a->GetPosition();
							trails::Stamp(a->GetFormID(), ws->GetFormID(), p.x, p.y, WhoOf(a), now);
						}
					}
				}
			}

			static int pruneCount = 0;
			if (++pruneCount >= 16) { pruneCount = 0; trails::Prune(now, snowing); }

			// One-second drain window for the page.
			drainAccum += pr.drained;
			if (t0 - drainWindow > std::chrono::seconds(1)) { drainWindow = t0; }

			const float us = std::chrono::duration<float, std::micro>(Clock::now() - t0).count();
			RE::ActorValueOwner* avo = player->AsActorValueOwner();
			std::scoped_lock l(snapLock);
			snap.havePlayer = true;
			snap.enabled = true;
			snap.effectsReady = true;
			snap.reading = pr.reading;
			snap.packing = pr.packing;
			snap.snowfall = snowfallBoost.load();
			snap.depthPercent = pr.depth;
			snap.depthSource = static_cast<int>(depth::ActiveSource());
			snap.sdUnits = pr.sdUnits;
			snap.groundBand = pr.groundBand;
			snap.band = pr.band;
			snap.forcedBand = forced.load();
			snap.road = pr.road;
			snap.snowing = snowing;
			snap.moving = player->IsMoving();
			snap.speedMult = avo ? avo->GetActorValue(RE::ActorValue::kSpeedMult) : -1.0F;
			snap.stamina = avo ? avo->GetActorValue(RE::ActorValue::kStamina) : -1.0F;
			if (drainWindow == t0) { snap.drainedLastSecond = drainAccum; drainAccum = 0.0F; }
			++snap.ticks;
			snap.tickUsAvg = snap.tickUsAvg <= 0.0F ? us : snap.tickUsAvg * 0.95F + us * 0.05F;
			snap.tickUsMax = (snap.ticks % 40 == 0) ? us : std::max(snap.tickUsMax, us);
			snap.npcsNear = nearCount;
			snap.npcsClassified = classified;
			snap.npcsAffected = affected;
			snap.trailSquares = trails::Count();
		}

		void QueueTick()
		{
			bool expected = false;
			if (!taskPending.compare_exchange_strong(expected, true)) { return; }
			const SKSE::TaskInterface* tasks = SKSE::GetTaskInterface();
			if (!tasks)
			{
				static bool warned = false;
				if (!warned) { logger::error("[tick] SKSE task interface is null; nothing can be applied"); warned = true; }
				taskPending = false;
				return;
			}
			tasks->AddTask(&Tick);
		}

		void Loop()
		{
			logger::info("[tick] timer thread started: every {} ms, {} NPCs per tick within {:.0f} units", settings::general::tickMs.load(),
				settings::general::actorsPerTick.load(), settings::general::scanRadius.load());
			for (;;)
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(settings::general::tickMs.load()));
				QueueTick();
			}
		}

		const char* B(bool a_b) { return a_b ? "true" : "false"; }

		std::string Esc(const std::string& a_s)
		{
			std::string o;
			for (const char c : a_s)
			{
				if (c == '\\' || c == '"') { o += '\\'; }
				if (static_cast<unsigned char>(c) >= 0x20) { o += c; }
			}
			return o;
		}
	}

	void Start()
	{
		bool expected = false;
		if (!started.compare_exchange_strong(expected, true)) { return; }
		// Detached on purpose: joining a thread from a static destructor at game exit can hang the shutdown.
		std::thread(&Loop).detach();
	}

	Snapshot Get()
	{
		std::scoped_lock l(snapLock);
		return snap;
	}

	std::string Json()
	{
		const Snapshot s = Get();
		std::string bands;
		for (int i = 0; i < settings::kBandCount; ++i)
		{
			if (i) { bands += ","; }
			bands += std::format("{{\"band\":{},\"depth\":{:.1f},\"speed\":{:.1f},\"drain\":{:.2f}}}", i + 1, settings::bands[i].depth.load(), settings::bands[i].speed.load(),
				settings::bands[i].drain.load());
		}
		const auto& r = s.reading;
		return std::format(
			"{{\"havePlayer\":{},\"enabled\":{},\"effectsReady\":{},\"surface\":\"{}\",\"why\":\"{}\",\"texture\":\"{}\",\"landTexture\":\"{:08X}\",\"landEditorId\":\"{}\",\"textureSetEditorId\":\"{}\",\"roadBy\":\"{}\",\"editorIdsKept\":{},\"model\":\"{}\",\"material\":{},\"cover\":{:.3f},"
			"\"preferred\":{},\"navmeshRead\":{},\"sheltered\":{},\"roofDistance\":{:.1f},\"packing\":{:.3f},\"snowfall\":{:.3f},\"snowing\":{},"
			"\"depthPercent\":{:.2f},\"depthSource\":\"{}\",\"depthSourceSetting\":{},\"sdFound\":{},\"sdActive\":{},\"sdUnits\":{:.2f},\"sdFullDepthUnits\":{:.1f},"
			"\"fullDepthUnits\":{:.1f},\"fullDepthSource\":\"{}\",\"depthUnits\":{:.2f},\"groundBand\":{},\"band\":{},\"forcedBand\":{},\"road\":{},"
			"\"moving\":{},\"speedMult\":{:.2f},\"stamina\":{:.2f},\"drainedLastSecond\":{:.3f},\"ticks\":{},\"tickUsAvg\":{:.1f},\"tickUsMax\":{:.1f},"
			"\"npcsNear\":{},\"npcsClassified\":{},\"npcsAffected\":{},\"affectNPCs\":{},\"trailSquares\":{},\"bands\":[{}]}}",
			B(s.havePlayer), B(s.enabled), B(s.effectsReady), surface::Name(r.kind), Esc(r.why ? r.why : ""), Esc(r.texture), r.landTexture, Esc(r.landEditorID), Esc(r.textureSetEditorID), Esc(r.roadBy), editor_ids::Count(), Esc(r.model), r.material, r.cover, B(r.preferred),
			B(r.navmeshRead), B(r.sheltered), r.roofDistance, s.packing, s.snowfall, B(s.snowing), s.depthPercent, depth::SourceName(static_cast<depth::Source>(s.depthSource)), settings::depth::source.load(),
			B(depth::SnowDeformationFound()), B(depth::SnowDeformationActive()), s.sdUnits, settings::depth::sdFullDepthUnits.load(), depth::FullDepthUnits(), depth::FullDepthSource(),
			s.depthPercent / 100.0F * depth::FullDepthUnits(), s.groundBand, s.band, s.forcedBand, B(s.road), B(s.moving), s.speedMult, s.stamina, s.drainedLastSecond,
			s.ticks, s.tickUsAvg, s.tickUsMax, s.npcsNear, s.npcsClassified, s.npcsAffected, B(settings::general::affectNPCs), s.trailSquares, bands);
	}

	void ForceBand(int a_band)
	{
		const int v = a_band < 0 ? -1 : std::min(a_band, settings::kBandCount);
		forced = v;
		logger::info("[tick] forced band on the player: {}", v < 0 ? std::string("off (the ground decides)") : std::to_string(v));
	}

	int ForcedBand() { return forced.load(); }

	void RequestMark(int a_list) { markRequest = a_list; }
	void RequestClearTrails() { clearTrailsRequest = true; }
	void RequestReapply() { reapplyRequest = true; }
	void OnLoaded() { loadedRequest = true; }

	void Save(const SKSE::SerializationInterface* a_intfc)
	{
		if (!a_intfc) { return; }
		const float v = snowfallBoost.load();
		if (!a_intfc->WriteRecord('SNFL', 1, v)) { logger::error("[co-save] could not write the snowfall record"); return; }
		logger::debug("[co-save] saved snowfall boost {:.3f} (record SNFL v1)", v);
	}

	void Load(const SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length)
	{
		if (!a_intfc) { return; }
		float v = 0.0F;
		if (a_version != 1 || a_length != sizeof(float) || a_intfc->ReadRecordData(v) != sizeof(float) || !std::isfinite(v))
		{
			logger::warn("[co-save] snowfall record v{} ({} bytes) not readable; the boost starts at 0", a_version, a_length);
			return;
		}
		snowfallBoost = std::clamp(v, 0.0F, 2.0F);
		logger::info("[co-save] loaded snowfall boost {:.3f}", snowfallBoost.load());
	}

	void Revert()
	{
		snowfallBoost = 0.0F;
	}
}
