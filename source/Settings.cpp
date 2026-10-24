#include "PCH.h"

#include "Settings.h"

#include "utils/Logger.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>

namespace settings
{
	namespace
	{
		std::string iniPath;
		std::mutex fileLock;
		std::mutex listLock;
		std::array<std::vector<std::string>, static_cast<std::size_t>(List::kCount)> lists;  // guarded by listLock
		std::atomic<std::uint64_t> listGeneration{ 1 };
		std::atomic<std::uint64_t> generation{ 1 };

		std::string Lower(std::string a_s)
		{
			for (char& c : a_s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
			return a_s;
		}

		std::string Trim(const std::string& a_s)
		{
			const auto b = a_s.find_first_not_of(" \t\r\n");
			if (b == std::string::npos) { return {}; }
			const auto e = a_s.find_last_not_of(" \t\r\n");
			return a_s.substr(b, e - b + 1);
		}

		bool ParseBool(const std::string& a_text, bool& a_out)
		{
			const std::string v = Lower(Trim(a_text));
			if (v == "1" || v == "true" || v == "yes") { a_out = true; return true; }
			if (v == "0" || v == "false" || v == "no") { a_out = false; return true; }
			return false;
		}

		bool ParseUInt(const std::string& a_text, std::uint32_t& a_out)
		{
			try { a_out = static_cast<std::uint32_t>(std::stoull(Trim(a_text), nullptr, 0)); return true; } catch (...) { return false; }
		}

		bool ParseFloat(const std::string& a_text, float& a_out)
		{
			try { a_out = std::stof(Trim(a_text)); return true; } catch (...) { return false; }
		}

		std::vector<std::string> SplitList(const std::string& a_text)
		{
			std::vector<std::string> out;
			std::stringstream ss(a_text);
			std::string item;
			while (std::getline(ss, item, ','))
			{
				std::string t = Lower(Trim(item));
				if (t.size() >= 4 && t.ends_with(".dds")) { t.resize(t.size() - 4); }
				if (!t.empty() && std::find(out.begin(), out.end(), t) == out.end()) { out.push_back(std::move(t)); }
			}
			return out;
		}

		std::string Join(const std::vector<std::string>& a_items)
		{
			std::string out;
			for (const auto& s : a_items)
			{
				if (!out.empty()) { out += ","; }
				out += s;
			}
			return out;
		}

		std::string Fmt(float a_v) { return std::format("{:.2f}", a_v); }

		bool IsSectionHeader(const std::string& a_t) { return a_t.size() >= 2 && a_t.front() == '[' && a_t.back() == ']'; }

		// One description per key: the INI key, its section, how to read it and how to write it. Load, Save and the
		// DevBench "set" op all walk this one table, so a key cannot be read but not written (rule 16).
		struct Key
		{
			const char* section;
			std::string name;
			std::function<bool(const std::string&)> read;
			std::function<std::string()> write;
		};

		Key B(const char* a_sec, const char* a_name, std::atomic<bool>& a_v)
		{
			return { a_sec, a_name, [&a_v](const std::string& s) { bool b; if (!ParseBool(s, b)) { return false; } a_v = b; return true; },
				[&a_v]() { return std::string(a_v ? "1" : "0"); } };
		}
		Key F(const char* a_sec, std::string a_name, std::atomic<float>& a_v)
		{
			return { a_sec, std::move(a_name), [&a_v](const std::string& s) { float f; if (!ParseFloat(s, f)) { return false; } a_v = f; return true; },
				[&a_v]() { return Fmt(a_v.load()); } };
		}
		Key U(const char* a_sec, const char* a_name, std::atomic<std::uint32_t>& a_v)
		{
			return { a_sec, a_name, [&a_v](const std::string& s) { std::uint32_t u; if (!ParseUInt(s, u)) { return false; } a_v = u; return true; },
				[&a_v]() { return std::to_string(a_v.load()); } };
		}
		Key L(List a_list)
		{
			const auto i = static_cast<std::size_t>(a_list);
			return { "Surfaces", kListKeys[i], [a_list](const std::string& s) { SetList(a_list, SplitList(s)); return true; },
				[a_list]() { return ListText(a_list); } };
		}

		const std::vector<Key>& Keys()
		{
			static const std::vector<Key> keys = [] {
				std::vector<Key> k;
				k.push_back(B("General", "bEnabled", general::enabled));
				k.push_back(B("General", "bAffectNPCs", general::affectNPCs));
				k.push_back(U("General", "uTickMs", general::tickMs));
				k.push_back(U("General", "uActorsPerTick", general::actorsPerTick));
				k.push_back(F("General", "fScanRadius", general::scanRadius));
				k.push_back(B("General", "bRefreshSpeed", general::refreshSpeed));
				k.push_back(F("Depth", "fSnow", depth::snow));
				k.push_back(F("Depth", "fTrail", depth::trail));
				k.push_back(F("Depth", "fObject", depth::object));
				k.push_back(F("Depth", "fOther", depth::other));
				k.push_back(F("Depth", "fShelterHeight", depth::shelterHeight));
				k.push_back(F("Depth", "fFullDepthUnits", depth::fullDepthUnits));
				k.push_back(B("Depth", "bUseDTDHeight", depth::useDTDHeight));
				for (int i = 0; i < kBandCount; ++i)
				{
					k.push_back(F("Bands", std::format("fBand{}Depth", i + 1), bands[i].depth));
					k.push_back(F("Bands", std::format("fBand{}Speed", i + 1), bands[i].speed));
					k.push_back(F("Bands", std::format("fBand{}Drain", i + 1), bands[i].drain));
				}
				k.push_back(B("Stamina", "bDrain", stamina::drain));
				k.push_back(F("Stamina", "fFloor", stamina::floor));
				k.push_back(B("Roads", "bBonus", roads::bonus));
				k.push_back(F("Roads", "fSpeed", roads::speed));
				k.push_back(B("Roads", "bUseNavmesh", roads::useNavmesh));
				k.push_back(B("Trails", "bEnabled", trails::enabled));
				k.push_back(F("Trails", "fPackPerPass", trails::packPerPass));
				k.push_back(F("Trails", "fPackedRemain", trails::packedRemain));
				k.push_back(F("Trails", "fLifeHours", trails::lifeHours));
				k.push_back(F("Trails", "fSnowRefill", trails::snowRefill));
				k.push_back(U("Trails", "uMaxCells", trails::maxCells));
				k.push_back(F("Trails", "fCellSize", trails::cellSize));
				k.push_back(B("Snowfall", "bDeepens", snowfall::deepens));
				k.push_back(F("Snowfall", "fPerHour", snowfall::perHour));
				k.push_back(F("Snowfall", "fMax", snowfall::max));
				for (int i = 0; i < static_cast<int>(List::kCount); ++i) { k.push_back(L(static_cast<List>(i))); }
				k.push_back(B("Probe", "bEnabled", probe::enabled));
				k.push_back(B("Probe", "bLogSamples", probe::logSamples));
				k.push_back(U("Probe", "uIntervalMs", probe::intervalMs));
				k.push_back(F("Probe", "fRoofRayStart", probe::roofRayStart));
				k.push_back(F("Probe", "fRoofRayLength", probe::roofRayLength));
				k.push_back(F("Probe", "fGroundRayLength", probe::groundRayLength));
				k.push_back(F("Probe", "fNavmeshMaxDz", probe::navmeshMaxDz));
				k.push_back(U("Debug", "uLogLevel", debug::logLevel));
				return k;
			}();
			return keys;
		}

		bool LoadFileValues()
		{
			std::ifstream in(iniPath);
			if (!in)
			{
				// Safe: the compiled defaults are the shipped INI's values (rule 16), so a missing file behaves like a fresh install.
				logger::warn("settings: INI not found at {}; using the compiled defaults", iniPath);
				return false;
			}
			std::map<std::string, std::string> k;
			std::string line, section;
			while (std::getline(in, line))
			{
				const std::string t = Trim(line);
				if (t.empty() || t[0] == ';' || t[0] == '#') { continue; }
				if (IsSectionHeader(t)) { section = Lower(t.substr(1, t.size() - 2)); continue; }
				const auto eq = t.find('=');
				if (eq == std::string::npos) { continue; }
				k[Lower(Trim(t.substr(0, eq))) + ":" + section] = t.substr(eq + 1);
			}
			int read = 0, missing = 0, bad = 0;
			for (const Key& key : Keys())
			{
				const std::string id = Lower(key.name) + ":" + Lower(key.section);
				const auto it = k.find(id);
				if (it == k.end()) { ++missing; logger::debug("settings: {} missing; keeping the current value", id); continue; }
				if (!key.read(it->second)) { ++bad; logger::warn("settings: value \"{}\" for {} is not valid; keeping the current value", Trim(it->second), id); continue; }
				++read;
			}
			Clamp();
			logger::info("settings loaded from {}: {} read, {} missing, {} invalid", iniPath, read, missing, bad);
			return true;
		}

		bool WriteKey(std::vector<std::string>& a_lines, const char* a_section, const std::string& a_key, const std::string& a_value)
		{
			const std::string wantSection = Lower(a_section);
			const std::string wantKey = Lower(a_key);
			std::string section;
			for (auto& line : a_lines)
			{
				const std::string t = Trim(line);
				if (IsSectionHeader(t)) { section = Lower(t.substr(1, t.size() - 2)); continue; }
				const auto eq = t.find('=');
				if (eq == std::string::npos || section != wantSection || t[0] == ';') { continue; }
				if (Lower(Trim(t.substr(0, eq))) == wantKey)
				{
					line = a_key + "=" + a_value;
					return true;
				}
			}
			// A key an older INI does not have yet goes at the end of its section; a missing section at the end of the file.
			std::string current;
			std::size_t insertAt = std::string::npos;
			for (std::size_t i = 0; i < a_lines.size(); ++i)
			{
				const std::string t = Trim(a_lines[i]);
				if (IsSectionHeader(t))
				{
					current = Lower(t.substr(1, t.size() - 2));
					if (current == wantSection) { insertAt = i + 1; }
					continue;
				}
				if (current == wantSection && !t.empty()) { insertAt = i + 1; }
			}
			if (insertAt == std::string::npos)
			{
				if (!a_lines.empty() && !Trim(a_lines.back()).empty()) { a_lines.emplace_back(); }
				a_lines.push_back(std::string("[") + a_section + "]");
				a_lines.push_back(a_key + "=" + a_value);
				logger::info("Save: [{}] {} added", a_section, a_key);
				return true;
			}
			a_lines.insert(a_lines.begin() + static_cast<std::ptrdiff_t>(insertAt), a_key + "=" + a_value);
			logger::info("Save: {} added to [{}]", a_key, a_section);
			return true;
		}

		template <class T>
		void ClampAtomic(std::atomic<T>& a_v, T a_min, T a_max) { a_v = std::clamp(a_v.load(), a_min, a_max); }
	}

	std::vector<std::string> GetList(List a_list)
	{
		std::scoped_lock l(listLock);
		return lists[static_cast<std::size_t>(a_list)];
	}

	void SetList(List a_list, std::vector<std::string> a_items)
	{
		{
			std::scoped_lock l(listLock);
			lists[static_cast<std::size_t>(a_list)] = std::move(a_items);
		}
		++listGeneration;
	}

	std::string ListText(List a_list)
	{
		std::scoped_lock l(listLock);
		return Join(lists[static_cast<std::size_t>(a_list)]);
	}

	bool Mark(List a_list, const std::string& a_item)
	{
		std::vector<std::string> one = SplitList(a_item);
		if (one.empty()) { return false; }
		const std::string item = one.front();
		bool changed = false;
		{
			std::scoped_lock l(listLock);
			for (int i = 0; i < static_cast<int>(List::kCount); ++i)
			{
				const bool targetIsThis = i == static_cast<int>(a_list);
				// A texture is one surface: marking it takes it out of the other texture lists (never the mesh or material lists).
				const bool sameKind = IsTextureList(a_list) && IsTextureList(static_cast<List>(i));
				auto& v = lists[static_cast<std::size_t>(i)];
				if (targetIsThis)
				{
					if (std::find(v.begin(), v.end(), item) == v.end()) { v.push_back(item); changed = true; }
				}
				else if (sameKind)
				{
					const auto before = v.size();
					v.erase(std::remove(v.begin(), v.end(), item), v.end());
					changed |= v.size() != before;
				}
			}
		}
		if (changed) { ++listGeneration; }
		logger::info("settings: marked \"{}\" as {} ({})", item, kListKeys[static_cast<int>(a_list)], changed ? "changed" : "already there");
		return changed;
	}

	std::uint64_t ListsGeneration() { return listGeneration.load(); }
	std::uint64_t Generation() { return generation.load(); }
	void Touch() { ++generation; }

	const std::string& GetIniPath() { return iniPath; }

	namespace
	{
		const Key* FindKey(const std::string& a_key)
		{
			std::string want = Lower(Trim(a_key));
			std::string wantSection;
			if (const auto colon = want.find(':'); colon != std::string::npos)
			{
				wantSection = want.substr(0, colon);
				want = want.substr(colon + 1);
			}
			for (const Key& k : Keys())
			{
				if (Lower(k.name) == want && (wantSection.empty() || Lower(k.section) == wantSection)) { return &k; }
			}
			return nullptr;
		}
	}

	bool SetValue(const std::string& a_key, const std::string& a_value)
	{
		const Key* k = FindKey(a_key);
		if (!k) { logger::warn("settings: set: no key \"{}\"", a_key); return false; }
		if (!k->read(a_value)) { logger::warn("settings: set: \"{}\" is not valid for {}", a_value, a_key); return false; }
		Clamp();
		++generation;
		logger::info("settings: set {}:{} = {}", k->section, k->name, k->write());
		return true;
	}

	std::string GetValue(const std::string& a_key)
	{
		const Key* k = FindKey(a_key);
		return k ? k->write() : std::string();
	}

	std::string AllJson()
	{
		std::string out = "{";
		bool first = true;
		for (const Key& k : Keys())
		{
			if (!first) { out += ","; }
			first = false;
			std::string esc;
			for (const char c : k.write())
			{
				if (c == '\\' || c == '"') { esc += '\\'; }
				esc += c;
			}
			out += std::format("\"{}:{}\":\"{}\"", k.section, k.name, esc);
		}
		return out + "}";
	}

	void Clamp()
	{
		ClampAtomic(general::tickMs, 50u, 2000u);
		ClampAtomic(general::actorsPerTick, 0u, 64u);
		ClampAtomic(general::scanRadius, 512.0F, 20000.0F);
		for (auto* v : { &depth::snow, &depth::trail, &depth::object, &depth::other }) { ClampAtomic(*v, 0.0F, 1.0F); }
		ClampAtomic(depth::shelterHeight, 0.0F, 4096.0F);
		ClampAtomic(depth::fullDepthUnits, 1.0F, 200.0F);
		for (auto& b : bands)
		{
			ClampAtomic(b.depth, 0.0F, 150.0F);
			ClampAtomic(b.speed, 10.0F, 100.0F);
			ClampAtomic(b.drain, 0.0F, 20.0F);
		}
		ClampAtomic(stamina::floor, 0.0F, 100.0F);
		ClampAtomic(roads::speed, 100.0F, 150.0F);
		ClampAtomic(trails::packPerPass, 0.0F, 1.0F);
		ClampAtomic(trails::packedRemain, 0.0F, 1.0F);
		ClampAtomic(trails::lifeHours, 0.1F, 720.0F);
		ClampAtomic(trails::snowRefill, 1.0F, 20.0F);
		ClampAtomic(trails::maxCells, 100u, 200000u);
		ClampAtomic(trails::cellSize, 32.0F, 1024.0F);
		ClampAtomic(snowfall::perHour, 0.0F, 100.0F);
		ClampAtomic(snowfall::max, 0.0F, 200.0F);
		ClampAtomic(probe::intervalMs, 100u, 10000u);
		ClampAtomic(probe::roofRayStart, 0.0F, 400.0F);
		ClampAtomic(probe::roofRayLength, 64.0F, 16384.0F);
		ClampAtomic(probe::groundRayLength, 16.0F, 4096.0F);
		ClampAtomic(probe::navmeshMaxDz, 8.0F, 2048.0F);
		ClampAtomic(debug::logLevel, 0u, 6u);
	}

	void Init(const std::string& a_iniFileName)
	{
		iniPath = (std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / a_iniFileName).string();
		for (int i = 0; i < static_cast<int>(List::kCount); ++i) { SetList(static_cast<List>(i), SplitList(kListDefaults[i])); }
		LoadFileValues();
		++generation;
	}

	bool Reload()
	{
		const bool ok = LoadFileValues();
		ApplyLogLevel();
		++generation;
		return ok;
	}

	bool Save()
	{
		std::scoped_lock l(fileLock);
		std::vector<std::string> lines;
		{
			std::ifstream in(iniPath);
			if (!in) { logger::warn("Save: could not open {} for reading; writing a fresh file", iniPath); }
			std::string line;
			while (std::getline(in, line)) { lines.push_back(line); }
		}
		for (const Key& key : Keys()) { WriteKey(lines, key.section, key.name, key.write()); }
		std::ofstream out(iniPath, std::ios::trunc);
		if (!out) { logger::error("Save: could not open {} for writing", iniPath); return false; }
		for (const auto& line : lines) { out << line << '\n'; }
		logger::info("settings saved to {} ({} keys)", iniPath, Keys().size());
		return static_cast<bool>(out);
	}

	void RestoreDefaults()
	{
		// The compiled defaults, which the shipped INI repeats (rule 16).
		general::enabled = true;
		general::affectNPCs = false;
		general::tickMs = 250;
		general::actorsPerTick = 6;
		general::scanRadius = 6000.0F;
		general::refreshSpeed = true;
		depth::snow = 1.0F;
		depth::trail = 0.35F;
		depth::object = 0.0F;
		depth::other = 0.0F;
		depth::shelterHeight = 1024.0F;
		depth::fullDepthUnits = 35.0F;
		depth::useDTDHeight = true;
		for (int i = 0; i < kBandCount; ++i)
		{
			bands[i].depth = kBandDefaults[i].depth;
			bands[i].speed = kBandDefaults[i].speed;
			bands[i].drain = kBandDefaults[i].drain;
		}
		stamina::drain = true;
		stamina::floor = 10.0F;
		roads::bonus = false;
		roads::speed = 110.0F;
		roads::useNavmesh = true;
		trails::enabled = true;
		trails::packPerPass = 0.2F;
		trails::packedRemain = 0.25F;
		trails::lifeHours = 24.0F;
		trails::snowRefill = 3.0F;
		trails::maxCells = 20000;
		trails::cellSize = 128.0F;
		snowfall::deepens = false;
		snowfall::perHour = 10.0F;
		snowfall::max = 50.0F;
		for (int i = 0; i < static_cast<int>(List::kCount); ++i) { SetList(static_cast<List>(i), SplitList(kListDefaults[i])); }
		probe::enabled = false;
		probe::logSamples = true;
		probe::intervalMs = 500;
		probe::roofRayStart = 100.0F;
		probe::roofRayLength = 2048.0F;
		probe::groundRayLength = 256.0F;
		probe::navmeshMaxDz = 160.0F;
		debug::logLevel = 2;
		ApplyLogLevel();
		++generation;
		logger::info("settings: defaults restored (not saved)");
	}

	void ApplyLogLevel()
	{
		using Lv = spdlog::level::level_enum;
		const std::uint32_t v = debug::logLevel.load();
		const Lv lvl = v >= 6 ? Lv::off : static_cast<Lv>(v);
		logger::set_level(lvl, lvl);
	}
}
