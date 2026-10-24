#include "PCH.h"

#include "Effects.h"

#include "Settings.h"
#include "utils/Logger.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <mutex>
#include <unordered_set>

namespace effects
{
	namespace
	{
		constexpr const char* kPlugin = "DeepSnow.esp";
		constexpr RE::FormID kSlowEffect = 0x801;
		constexpr RE::FormID kFastEffect = 0x802;
		constexpr RE::FormID kBandSpell[settings::kBandCount] = { 0x803, 0x804, 0x805, 0x806, 0x807 };
		constexpr RE::FormID kRoadSpell = 0x808;

		std::array<RE::SpellItem*, settings::kBandCount> bandSpells{};
		RE::SpellItem* roadSpell = nullptr;
		std::atomic<bool> ready{ false };

		std::mutex appliedLock;
		std::unordered_set<std::uint32_t> applied;  // guarded by appliedLock

		RE::SpellItem* Find(RE::TESDataHandler* a_dh, RE::FormID a_id, const char* a_what)
		{
			auto* spell = a_dh->LookupForm<RE::SpellItem>(a_id, kPlugin);
			if (!spell) { logger::error("[effects] {} ({:03X}) not found in {}", a_what, a_id, kPlugin); return nullptr; }
			if (spell->effects.empty() || !spell->effects[0] || !spell->effects[0]->baseEffect)
			{
				logger::error("[effects] {} ({:08X}) has no magic effect; the plugin is damaged", a_what, spell->GetFormID());
				return nullptr;
			}
			logger::info("[effects] {} = {:08X} '{}' (effect {:08X}, magnitude {:.1f})", a_what, spell->GetFormID(), spell->GetFullName(),
				spell->effects[0]->baseEffect->GetFormID(), spell->effects[0]->effectItem.magnitude);
			return spell;
		}

		void SetMagnitude(RE::SpellItem* a_spell, float a_value)
		{
			if (!a_spell || a_spell->effects.empty() || !a_spell->effects[0]) { return; }
			a_spell->effects[0]->effectItem.magnitude = a_value;
		}

		// A SpeedMult change only reaches the character's movement when the game recalculates it, which a CarryWeight
		// change does. +x then -x in the same call: the net permanent change is zero.
		void RefreshSpeed(RE::Actor* a_actor)
		{
			if (!settings::general::refreshSpeed) { return; }
			RE::ActorValueOwner* avo = a_actor->AsActorValueOwner();
			if (!avo) { return; }
			avo->ModActorValue(RE::ActorValue::kCarryWeight, 0.01F);
			avo->ModActorValue(RE::ActorValue::kCarryWeight, -0.01F);
		}

		bool Holds(RE::Actor* a_actor)
		{
			for (auto* s : bandSpells) { if (s && a_actor->HasSpell(s)) { return true; } }
			return roadSpell && a_actor->HasSpell(roadSpell);
		}
	}

	bool Init()
	{
		RE::TESDataHandler* dh = RE::TESDataHandler::GetSingleton();
		if (!dh) { logger::error("[effects] TESDataHandler is null at data load; the effects stay off"); return false; }
		if (!dh->LookupModByName(kPlugin) && !dh->LookupLoadedLightModByName(kPlugin))
		{
			logger::error("[effects] {} is not active: no speed or stamina effect can be applied. Enable it in the load order.", kPlugin);
			return false;
		}
		bool ok = true;
		for (int i = 0; i < settings::kBandCount; ++i)
		{
			bandSpells[i] = Find(dh, kBandSpell[i], std::format("band {} ability", i + 1).c_str());
			ok &= bandSpells[i] != nullptr;
		}
		roadSpell = Find(dh, kRoadSpell, "road ability");
		ok &= roadSpell != nullptr;
		const auto* slow = dh->LookupForm<RE::EffectSetting>(kSlowEffect, kPlugin);
		const auto* fast = dh->LookupForm<RE::EffectSetting>(kFastEffect, kPlugin);
		if (slow && fast)
		{
			logger::info("[effects] slow effect {:08X} (archetype {}, actor value {}), fast effect {:08X} (archetype {}, actor value {})", slow->GetFormID(),
				static_cast<int>(slow->data.archetype), static_cast<int>(slow->data.primaryAV), fast->GetFormID(), static_cast<int>(fast->data.archetype),
				static_cast<int>(fast->data.primaryAV));
		}
		else
		{
			logger::error("[effects] the magic effects are missing from {}", kPlugin);
			ok = false;
		}
		ready = ok;
		if (ok) { ApplyMagnitudes(); }
		logger::info("[effects] {}", ok ? "ready" : "NOT ready - nothing will be applied");
		return ok;
	}

	bool Ready() { return ready.load(); }

	void ApplyMagnitudes()
	{
		if (!ready) { return; }
		std::string log;
		for (int i = 0; i < settings::kBandCount; ++i)
		{
			const float m = std::clamp(100.0F - settings::bands[i].speed.load(), 0.0F, 90.0F);
			SetMagnitude(bandSpells[i], m);
			log += std::format(" band{}=-{:.0f}", i + 1, m);
		}
		const float road = std::clamp(settings::roads::speed.load() - 100.0F, 0.0F, 50.0F);
		SetMagnitude(roadSpell, road);
		logger::debug("[effects] SpeedMult magnitudes:{} road=+{:.0f}", log, road);
	}

	bool Set(RE::Actor* a_actor, int a_band, bool a_road)
	{
		if (!ready || !a_actor) { return false; }
		bool changed = false;
		for (int i = 0; i < settings::kBandCount; ++i)
		{
			RE::SpellItem* s = bandSpells[i];
			if (!s) { continue; }
			const bool want = a_band == i + 1;
			const bool has = a_actor->HasSpell(s);
			if (want && !has) { a_actor->AddSpell(s); changed = true; }
			else if (!want && has) { a_actor->RemoveSpell(s); changed = true; }
		}
		if (roadSpell)
		{
			const bool has = a_actor->HasSpell(roadSpell);
			if (a_road && !has) { a_actor->AddSpell(roadSpell); changed = true; }
			else if (!a_road && has) { a_actor->RemoveSpell(roadSpell); changed = true; }
		}
		if (changed)
		{
			RefreshSpeed(a_actor);
			std::scoped_lock l(appliedLock);
			if (a_band > 0 || a_road) { applied.insert(a_actor->GetFormID()); }
			else { applied.erase(a_actor->GetFormID()); }
		}
		return changed;
	}

	void Reapply(RE::Actor* a_actor)
	{
		if (!ready || !a_actor) { return; }
		for (int i = 0; i < settings::kBandCount; ++i)
		{
			if (bandSpells[i] && a_actor->HasSpell(bandSpells[i])) { a_actor->RemoveSpell(bandSpells[i]); a_actor->AddSpell(bandSpells[i]); }
		}
		if (roadSpell && a_actor->HasSpell(roadSpell)) { a_actor->RemoveSpell(roadSpell); a_actor->AddSpell(roadSpell); }
		RefreshSpeed(a_actor);
	}

	float Drain(RE::Actor* a_actor, int a_band, float a_seconds)
	{
		if (!a_actor || a_band < 1 || a_band > settings::kBandCount || !settings::stamina::drain || a_seconds <= 0.0F) { return 0.0F; }
		RE::ActorValueOwner* avo = a_actor->AsActorValueOwner();
		if (!avo) { return 0.0F; }
		const float perSecond = settings::bands[a_band - 1].drain.load();
		if (perSecond <= 0.0F) { return 0.0F; }
		const float max = avo->GetPermanentActorValue(RE::ActorValue::kStamina) + a_actor->GetActorValueModifier(RE::ACTOR_VALUE_MODIFIER::kTemporary, RE::ActorValue::kStamina);
		const float now = avo->GetActorValue(RE::ActorValue::kStamina);
		const float floor = max * settings::stamina::floor.load() / 100.0F;
		const float take = std::min(perSecond * a_seconds, now - floor);
		if (take <= 0.0F) { return 0.0F; }
		avo->RestoreActorValue(RE::ACTOR_VALUE_MODIFIER::kDamage, RE::ActorValue::kStamina, -take);
		return take;
	}

	std::vector<std::uint32_t> Applied()
	{
		std::scoped_lock l(appliedLock);
		return { applied.begin(), applied.end() };
	}

	std::size_t AppliedCount()
	{
		std::scoped_lock l(appliedLock);
		return applied.size();
	}

	void ForgetAll()
	{
		std::scoped_lock l(appliedLock);
		applied.clear();
	}

	void Remember(std::uint32_t a_formId)
	{
		std::scoped_lock l(appliedLock);
		applied.insert(a_formId);
	}

	void ResetAfterLoad()
	{
		if (!ready) { return; }
		std::vector<std::uint32_t> ids = Applied();
		if (auto* player = RE::PlayerCharacter::GetSingleton()) { ids.push_back(player->GetFormID()); }
		int cleared = 0, missing = 0;
		for (const std::uint32_t id : ids)
		{
			auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
			if (!actor) { ++missing; continue; }
			if (Holds(actor) && Set(actor, 0, false)) { ++cleared; }
		}
		ForgetAll();
		logger::info("[effects] after load: our abilities taken off {} actor(s) ({} remembered, {} no longer exist); the next tick re-applies by the ground",
			cleared, ids.size(), missing);
	}

	std::string Json()
	{
		std::string mags;
		for (int i = 0; i < settings::kBandCount; ++i)
		{
			if (i) { mags += ","; }
			const RE::SpellItem* s = bandSpells[i];
			mags += std::format("{{\"band\":{},\"spell\":\"{:08X}\",\"magnitude\":{:.1f}}}", i + 1, s ? s->GetFormID() : 0u,
				s && !s->effects.empty() && s->effects[0] ? s->effects[0]->effectItem.magnitude : -1.0F);
		}
		return std::format("{{\"ready\":{},\"plugin\":\"{}\",\"bands\":[{}],\"road\":{{\"spell\":\"{:08X}\",\"magnitude\":{:.1f}}},\"applied\":{}}}", ready ? "true" : "false",
			kPlugin, mags, roadSpell ? roadSpell->GetFormID() : 0u,
			roadSpell && !roadSpell->effects.empty() && roadSpell->effects[0] ? roadSpell->effects[0]->effectItem.magnitude : -1.0F, AppliedCount());
	}

	void Save(const SKSE::SerializationInterface* a_intfc)
	{
		if (!a_intfc) { return; }
		const std::vector<std::uint32_t> ids = Applied();
		const auto n = static_cast<std::uint32_t>(ids.size());
		if (!a_intfc->OpenRecord('APLD', 1) || !a_intfc->WriteRecordData(n))
		{
			logger::error("[co-save] could not write the applied-actor record");
			return;
		}
		for (const std::uint32_t id : ids) { a_intfc->WriteRecordData(id); }
		logger::info("[co-save] saved {} actor(s) holding a Deep Snow ability (record APLD v1)", n);
	}

	void Load(const SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length)
	{
		if (!a_intfc) { return; }
		if (a_version != 1 || a_length < 4)
		{
			logger::warn("[co-save] applied-actor record version {} / {} bytes is not known (expects v1); only the player is cleaned after load", a_version, a_length);
			return;
		}
		std::uint32_t n = 0;
		a_intfc->ReadRecordData(n);
		if (4 + static_cast<std::uint64_t>(n) * 4 != a_length) { logger::error("[co-save] applied-actor record says {} but holds {} bytes", n, a_length); return; }
		int kept = 0;
		for (std::uint32_t i = 0; i < n; ++i)
		{
			std::uint32_t id = 0;
			RE::FormID resolved = 0;
			if (a_intfc->ReadRecordData(id) != 4) { break; }
			if (a_intfc->ResolveFormID(id, resolved) && resolved) { Remember(resolved); ++kept; }
		}
		logger::info("[co-save] loaded {} remembered actor(s) of {}", kept, n);
	}
}
