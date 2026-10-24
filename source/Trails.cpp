#include "PCH.h"

#include "Trails.h"

#include "Settings.h"
#include "utils/Logger.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <format>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace trails
{
	namespace
	{
		struct Key
		{
			std::uint32_t ws;
			std::int32_t gx;
			std::int32_t gy;
			bool operator==(const Key&) const = default;
		};
		struct KeyHash
		{
			std::size_t operator()(const Key& k) const noexcept
			{
				std::uint64_t h = (static_cast<std::uint64_t>(k.ws) * 0x9E3779B97F4A7C15ull) ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(k.gx)) << 32) ^
					static_cast<std::uint32_t>(k.gy);
				h ^= h >> 29;
				return static_cast<std::size_t>(h * 0xBF58476D1CE4E5B9ull);
			}
		};
		struct Cell
		{
			float packing = 0.0F;   // as of asOfDays
			float asOfDays = 0.0F;  // game days (Calendar::GetDaysPassed) the packing value was last brought up to date
			float lastDays = 0.0F;  // game days of the last pass (for tracks: who passed, and when)
			Who who = Who::kPlayer;
			std::uint16_t passes = 0;
		};

		std::mutex lock;
		std::unordered_map<Key, Cell, KeyHash> cells;           // guarded by lock
		std::unordered_map<std::uint32_t, Key> lastSquare;     // actor -> the square it last stamped, guarded by lock

		Key KeyFor(std::uint32_t a_ws, float a_x, float a_y)
		{
			const float size = std::max(32.0F, settings::trails::cellSize.load());
			return { a_ws, static_cast<std::int32_t>(std::floor(a_x / size)), static_cast<std::int32_t>(std::floor(a_y / size)) };
		}

		// Packing left after filling in from lastDays to a_now.
		float Current(const Cell& a_c, float a_now, bool a_snowing)
		{
			const float life = std::max(0.1F, settings::trails::lifeHours.load());
			const float rate = (a_snowing ? std::max(1.0F, settings::trails::snowRefill.load()) : 1.0F) / life;  // packing per game hour
			const float hours = std::max(0.0F, (a_now - a_c.asOfDays) * 24.0F);
			return std::max(0.0F, a_c.packing - hours * rate);
		}

		// Co-save record v1, one per square: worldspace form id (resolved on load), grid x, grid y, packing, last pass
		// (game days), packing as of (game days), who, passes. Packed by hand so the layout never depends on padding.
		constexpr std::uint32_t kRecordSize = 4 + 4 + 4 + 4 + 4 + 4 + 1 + 2;
	}

	namespace
	{
		// One pass over square a_k (lock held).
		void Credit(const Key& a_k, Who a_who, float a_nowDays)
		{
			Cell& c = cells[a_k];
			const float before = Current(c, a_nowDays, false);
			c.packing = std::min(1.0F, before + settings::trails::packPerPass.load());
			c.asOfDays = a_nowDays;
			c.lastDays = a_nowDays;
			c.who = a_who;
			if (c.passes < 0xFFFF) { ++c.passes; }
			if (a_who == Who::kPlayer)
			{
				logger::trace("[trails] player pass at ({},{}) ws {:08X}: packing {:.2f} -> {:.2f} (passes {})", a_k.gx, a_k.gy, a_k.ws, before, c.packing, c.passes);
			}
		}
	}

	void Stamp(std::uint32_t a_actorId, std::uint32_t a_worldspace, float a_x, float a_y, Who a_who, float a_nowDays)
	{
		if (!settings::trails::enabled || a_worldspace == 0) { return; }
		const Key k = KeyFor(a_worldspace, a_x, a_y);
		std::scoped_lock l(lock);
		auto it = lastSquare.find(a_actorId);
		if (it != lastSquare.end() && it->second == k) { return; }  // still in the same square: not a new pass
		// The pass counts on the square the actor LEFT, never on the one it stands in (ff5b: counting on entry put the
		// actor's own pass under its feet, so untouched snow read 85 %). An actor seen for the first time - after a load,
		// a cell change or a fresh start - only records where it stands; a load no longer adds a pass to every square
		// someone is standing on (ff5b: +1 pass per occupied square after loading).
		if (it != lastSquare.end()) { Credit(it->second, a_who, a_nowDays); }
		lastSquare[a_actorId] = k;
	}

	void AddPasses(std::uint32_t a_worldspace, float a_x, float a_y, Who a_who, float a_nowDays, int a_passes)
	{
		if (!settings::trails::enabled || a_worldspace == 0) { return; }
		const Key k = KeyFor(a_worldspace, a_x, a_y);
		std::scoped_lock l(lock);
		for (int i = 0; i < a_passes; ++i) { Credit(k, a_who, a_nowDays); }
	}

	float PackingAt(std::uint32_t a_worldspace, float a_x, float a_y, float a_nowDays, bool a_snowing)
	{
		if (!settings::trails::enabled) { return 0.0F; }
		const Key k = KeyFor(a_worldspace, a_x, a_y);
		std::scoped_lock l(lock);
		const auto it = cells.find(k);
		return it == cells.end() ? 0.0F : Current(it->second, a_nowDays, a_snowing);
	}

	void Prune(float a_nowDays, bool a_snowing)
	{
		std::scoped_lock l(lock);
		std::size_t filled = 0;
		for (auto it = cells.begin(); it != cells.end();)
		{
			// Fold the fill-in into the stored value so snowfall's faster refill is kept, then drop empty squares.
			const float p = Current(it->second, a_nowDays, a_snowing);
			if (p <= 0.0F) { it = cells.erase(it); ++filled; continue; }
			it->second.packing = p;
			it->second.asOfDays = a_nowDays;
			++it;
		}
		const std::size_t cap = settings::trails::maxCells.load();
		std::size_t dropped = 0;
		if (cells.size() > cap)
		{
			std::vector<std::pair<float, Key>> order;
			order.reserve(cells.size());
			for (const auto& [k, c] : cells) { order.emplace_back(c.packing, k); }
			std::sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
			for (std::size_t i = 0; i < order.size() - cap; ++i) { cells.erase(order[i].second); ++dropped; }
		}
		if (lastSquare.size() > 4096) { lastSquare.clear(); }
		if (filled || dropped) { logger::debug("[trails] prune: {} filled in, {} over the cap of {} dropped, {} kept", filled, dropped, cap, cells.size()); }
	}

	void Clear()
	{
		std::scoped_lock l(lock);
		logger::info("[trails] cleared ({} squares)", cells.size());
		cells.clear();
		lastSquare.clear();
	}

	std::size_t Count()
	{
		std::scoped_lock l(lock);
		return cells.size();
	}

	std::string Json(std::size_t a_max)
	{
		std::scoped_lock l(lock);
		std::vector<std::pair<Key, Cell>> v(cells.begin(), cells.end());
		std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second.lastDays > b.second.lastDays; });
		std::string out = std::format("{{\"count\":{},\"cellSize\":{:.0f},\"cells\":[", cells.size(), settings::trails::cellSize.load());
		for (std::size_t i = 0; i < v.size() && i < a_max; ++i)
		{
			if (i) { out += ","; }
			const auto& [k, c] = v[i];
			out += std::format("{{\"ws\":\"{:08X}\",\"gx\":{},\"gy\":{},\"packing\":{:.3f},\"lastDays\":{:.4f},\"who\":{},\"passes\":{}}}", k.ws, k.gx, k.gy, c.packing,
				c.lastDays, static_cast<int>(c.who), c.passes);
		}
		out += "]}";
		return out;
	}

	void Save(const SKSE::SerializationInterface* a_intfc)
	{
		if (!a_intfc) { return; }
		std::scoped_lock l(lock);
		const auto n = static_cast<std::uint32_t>(cells.size());
		std::vector<std::uint8_t> buf;
		buf.reserve(4 + static_cast<std::size_t>(n) * kRecordSize);
		auto put = [&buf](const void* a_p, std::size_t a_n) { const auto* b = static_cast<const std::uint8_t*>(a_p); buf.insert(buf.end(), b, b + a_n); };
		put(&n, 4);
		for (const auto& [k, c] : cells)
		{
			put(&k.ws, 4);
			put(&k.gx, 4);
			put(&k.gy, 4);
			put(&c.packing, 4);
			put(&c.lastDays, 4);
			put(&c.asOfDays, 4);
			const auto who = static_cast<std::uint8_t>(c.who);
			put(&who, 1);
			put(&c.passes, 2);
		}
		if (!a_intfc->WriteRecord('TRLS', 1, buf.data(), static_cast<std::uint32_t>(buf.size())))
		{
			logger::error("[co-save] could not write the trail record ({} squares, {} bytes)", n, buf.size());
			return;
		}
		logger::info("[co-save] saved {} trail square(s) ({} bytes, record TRLS v1)", n, buf.size());
	}

	void Load(const SKSE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length)
	{
		if (!a_intfc) { return; }
		if (a_version != 1)
		{
			logger::warn("[co-save] trail record version {} is not known to this build (expects 1); trails start empty", a_version);
			return;
		}
		std::vector<std::uint8_t> buf(a_length);
		if (a_length && a_intfc->ReadRecordData(buf.data(), a_length) != a_length)
		{
			logger::error("[co-save] the trail record is shorter than its header says ({} bytes); trails start empty", a_length);
			return;
		}
		if (a_length < 4) { logger::warn("[co-save] the trail record is empty"); return; }
		std::uint32_t n = 0;
		std::memcpy(&n, buf.data(), 4);
		if (4 + static_cast<std::uint64_t>(n) * kRecordSize != a_length)
		{
			logger::error("[co-save] the trail record says {} squares but holds {} bytes; trails start empty", n, a_length);
			return;
		}
		std::scoped_lock l(lock);
		cells.clear();
		lastSquare.clear();
		std::size_t off = 4, unresolved = 0;
		auto get = [&buf, &off](void* a_p, std::size_t a_n) { std::memcpy(a_p, buf.data() + off, a_n); off += a_n; };
		for (std::uint32_t i = 0; i < n; ++i)
		{
			Key k{};
			Cell c{};
			std::uint8_t who = 0;
			get(&k.ws, 4);
			get(&k.gx, 4);
			get(&k.gy, 4);
			get(&c.packing, 4);
			get(&c.lastDays, 4);
			get(&c.asOfDays, 4);
			get(&who, 1);
			get(&c.passes, 2);
			c.who = who <= 2 ? static_cast<Who>(who) : Who::kNPC;
			RE::FormID resolved = 0;
			if (!a_intfc->ResolveFormID(k.ws, resolved) || resolved == 0) { ++unresolved; continue; }  // its plugin is gone
			k.ws = resolved;
			if (!std::isfinite(c.packing) || !std::isfinite(c.lastDays) || !std::isfinite(c.asOfDays)) { continue; }
			c.packing = std::clamp(c.packing, 0.0F, 1.0F);
			cells[k] = c;
		}
		logger::info("[co-save] loaded {} trail square(s){}", cells.size(), unresolved ? std::format(" ({} dropped: worldspace no longer loaded)", unresolved) : std::string());
	}
}
