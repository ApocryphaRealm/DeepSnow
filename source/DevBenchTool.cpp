#include "PCH.h"

#include "DevBenchTool.h"

#include "DevBench/DevBenchAPI.h"
#include "Probe.h"
#include "Settings.h"
#include "utils/Logger.h"

#include <format>
#include <string>
#include <string_view>

namespace DevBenchTool
{
	namespace
	{
		// The value of a top-level JSON member as text: a quoted string or a bare word. Empty when absent.
		std::string Get(std::string_view a_json, const char* a_name)
		{
			const std::string key = std::format("\"{}\"", a_name);
			auto pos = a_json.find(key);
			if (pos == std::string_view::npos) { return {}; }
			pos = a_json.find(':', pos + key.size());
			if (pos == std::string_view::npos) { return {}; }
			++pos;
			while (pos < a_json.size() && (a_json[pos] == ' ' || a_json[pos] == '\t')) { ++pos; }
			std::string out;
			if (pos < a_json.size() && a_json[pos] == '"')
			{
				for (++pos; pos < a_json.size() && a_json[pos] != '"'; ++pos) { out += a_json[pos]; }
				return out;
			}
			while (pos < a_json.size() && a_json[pos] != ',' && a_json[pos] != '}' && a_json[pos] != ' ') { out += a_json[pos++]; }
			return out;
		}

		// Runs on DevBench's listener thread: it only reads the strings the game thread already built (Probe.cpp).
		void StateTool(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			const std::string_view args = a_argsJson ? a_argsJson : "";
			const std::string op = Get(args, "op");
			const std::string head = std::format("\"samples\":{},\"intervalMs\":{},\"enabled\":{}", probe::SampleCount(), settings::probe::intervalMs.load(),
				settings::probe::enabled ? "true" : "false");
			if (op == "history")
			{
				std::size_t n = 20;
				const std::string v = Get(args, "n");
				if (!v.empty()) { try { n = static_cast<std::size_t>(std::stoul(v)); } catch (...) {} }
				a_write(a_sink, std::format("{{\"ok\":true,\"op\":\"history\",{},\"history\":{}}}", head, probe::HistoryJson(n)).c_str());
				return;
			}
			a_write(a_sink, std::format("{{\"ok\":true,\"op\":\"state\",{},\"state\":{}}}", head, probe::LatestJson()).c_str());
		}
	}

	void Init(bool a_lastAttempt)
	{
		static bool registered = false;
		if (registered) { return; }

		DevBenchAPI::IDevBenchInterface001* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench)
		{
			if (a_lastAttempt) { logger::info("DevBench not detected; skipping the \"deepsnow.state\" tool"); }
			else { logger::debug("DevBench not detected yet; will retry at the next message"); }
			return;
		}

		constexpr const char* descriptor =
			"{"
			"\"description\":\"Deep Snow spike - read-only ground probe for the player, sampled on the game thread every uIntervalMs (500 ms). "
			"op=state (default): the newest sample - position, worldspace, cell (interior, exterior x/y), location, mounted, swimming, inWater, "
			"speedMult; landEngine = TES::GetLandTexture at the player (form, plugin, editor id, texture set, diffuse path, material); "
			"landQuad = the cell LAND's quadrant, nearest vertex, base texture and every layer with its alpha; navmesh = the triangle under "
			"the player (navmesh form, index, flags, preferred = flag 0x40, height gap, coordinate basis); roof = havok ray straight up "
			"(hit, distance, collision layer, hit reference and base); ground = the same ray straight down. op=history n=N: the newest N "
			"samples, oldest first (up to 240). Changes nothing in the game.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\"},\"n\":{\"type\":\"integer\"}}},"
			"\"readOnly\":true"
			"}";

		if (devBench->RegisterTool("deepsnow.state", descriptor, &StateTool, nullptr))
		{
			logger::info("Registered \"deepsnow.state\" with DevBench (build {})", devBench->GetBuildNumber());
		}
		else
		{
			logger::info("\"deepsnow.state\" replaced an existing DevBench tool of that name (build {})", devBench->GetBuildNumber());
		}
		registered = true;
	}
}
