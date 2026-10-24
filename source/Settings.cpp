#include "PCH.h"

#include "Settings.h"

#include "utils/Logger.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>

namespace settings
{
	namespace
	{
		std::string iniPath;

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
	}

	const std::string& GetIniPath() { return iniPath; }

	void Init(const std::string& a_iniFileName)
	{
		iniPath = (std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / a_iniFileName).string();
		std::ifstream in(iniPath);
		if (!in)
		{
			// Safe: the compiled defaults are the shipped INI's values (rule 16), so a missing file behaves like a fresh install.
			logger::warn("settings: INI not found at {}; using the compiled defaults", iniPath);
			return;
		}
		std::map<std::string, std::string> k;
		std::string line, section;
		while (std::getline(in, line))
		{
			const std::string t = Trim(line);
			if (t.empty() || t[0] == ';' || t[0] == '#') { continue; }
			if (t.size() >= 2 && t.front() == '[' && t.back() == ']') { section = Lower(t.substr(1, t.size() - 2)); continue; }
			const auto eq = t.find('=');
			if (eq == std::string::npos) { continue; }
			k[Lower(Trim(t.substr(0, eq))) + ":" + section] = t.substr(eq + 1);
		}

		auto get = [&](const char* a_key, auto a_apply) {
			const auto it = k.find(a_key);
			if (it == k.end()) { logger::debug("settings: {} missing; keeping the compiled default", a_key); return; }
			if (!a_apply(it->second)) { logger::warn("settings: value \"{}\" for {} is not valid; keeping the compiled default", Trim(it->second), a_key); }
		};
		auto getBool = [&](const char* a_key, std::atomic<bool>& a_out) {
			get(a_key, [&](const std::string& v) { bool b; if (!ParseBool(v, b)) { return false; } a_out = b; return true; });
		};
		auto getFloat = [&](const char* a_key, std::atomic<float>& a_out, float a_min, float a_max) {
			get(a_key, [&](const std::string& v) { float f; if (!ParseFloat(v, f)) { return false; } a_out = std::clamp(f, a_min, a_max); return true; });
		};

		get("uloglevel:debug", [](const std::string& v) { return ParseUInt(v, debug::logLevel); });
		getBool("benabled:probe", probe::enabled);
		getBool("blogsamples:probe", probe::logSamples);
		get("uintervalms:probe", [](const std::string& v) {
			std::uint32_t u;
			if (!ParseUInt(v, u)) { return false; }
			probe::intervalMs = std::clamp<std::uint32_t>(u, 100u, 10000u);
			return true;
		});
		getFloat("froofraystart:probe", probe::roofRayStart, 0.0F, 400.0F);
		getFloat("froofraylength:probe", probe::roofRayLength, 64.0F, 16384.0F);
		getFloat("fgroundraylength:probe", probe::groundRayLength, 16.0F, 4096.0F);
		getFloat("fnavmeshmaxdz:probe", probe::navmeshMaxDz, 8.0F, 2048.0F);

		logger::info("settings loaded from {}: enabled={} logSamples={} intervalMs={} roofRayStart={} roofRayLength={} groundRayLength={} navmeshMaxDz={} logLevel={}",
			iniPath, probe::enabled.load(), probe::logSamples.load(), probe::intervalMs.load(), probe::roofRayStart.load(), probe::roofRayLength.load(),
			probe::groundRayLength.load(), probe::navmeshMaxDz.load(), debug::logLevel);
	}

	void ApplyLogLevel()
	{
		using L = spdlog::level::level_enum;
		const L lvl = debug::logLevel >= 6 ? L::off : static_cast<L>(debug::logLevel);
		logger::set_level(lvl, lvl);
	}
}
