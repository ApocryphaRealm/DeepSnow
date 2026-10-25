#include "PCH.h"

#include "DevBenchTool.h"

#include "DevBench/DevBenchAPI.h"
#include "Effects.h"
#include "Movement.h"
#include "Probe.h"
#include "Settings.h"
#include "Trails.h"
#include "utils/Logger.h"
#include "utils/Strings.h"

#include <algorithm>
#include <atomic>
#include <functional>
#include <format>
#include <string>
#include <string_view>

// Two tools (rules 31 and 64):
//   deepsnow.state   (read-only) what the tick decided for the player, the effects, the settings, trails, the spike probe
//   deepsnow.control (drives)    force a band on the player, mark the ground, set / save / reload / restore settings,
//                                add or clear trail packing - so the page and the mechanism are tested without a keypress
// Both handlers run on DevBench's listener thread: they read finished snapshots and queue anything that touches the
// game to the game thread (SKSE task interface).

namespace DevBenchTool
{
	namespace
	{
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

		int ToInt(const std::string& a_s, int a_default)
		{
			try { return a_s.empty() ? a_default : std::stoi(a_s); } catch (...) { return a_default; }
		}

		void Queue(std::function<void()> a_task)
		{
			if (auto* tasks = SKSE::GetTaskInterface()) { tasks->AddTask(std::move(a_task)); }
			else { logger::warn("[devbench] SKSE task interface is null; the request is dropped"); }
		}

		void StateTool(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			const std::string_view args = a_argsJson ? a_argsJson : "";
			const std::string op = Get(args, "op");
			if (op == "probe") { a_write(a_sink, std::format("{{\"ok\":true,\"op\":\"probe\",\"samples\":{},\"state\":{}}}", probe::SampleCount(), probe::LatestJson()).c_str()); return; }
			if (op == "history")
			{
				const std::size_t n = static_cast<std::size_t>(std::max(1, ToInt(Get(args, "n"), 20)));
				a_write(a_sink, std::format("{{\"ok\":true,\"op\":\"history\",\"history\":{}}}", probe::HistoryJson(n)).c_str());
				return;
			}
			if (op == "trails")
			{
				const std::size_t n = static_cast<std::size_t>(std::max(1, ToInt(Get(args, "n"), 50)));
				a_write(a_sink, std::format("{{\"ok\":true,\"op\":\"trails\",\"trails\":{}}}", trails::Json(n)).c_str());
				return;
			}
			if (op == "settings") { a_write(a_sink, std::format("{{\"ok\":true,\"op\":\"settings\",\"ini\":\"{}\",\"settings\":{}}}", "DeepSnow.ini", settings::AllJson()).c_str()); return; }
			a_write(a_sink, std::format("{{\"ok\":true,\"op\":\"state\",\"tick\":{},\"effects\":{},\"strings\":{}}}", movement::Json(), effects::Json(), strings::StatusJson()).c_str());
		}

		void ControlTool(void*, const char* a_argsJson, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			const std::string_view args = a_argsJson ? a_argsJson : "";
			const std::string op = Get(args, "op");
			auto reply = [&](bool a_ok, const std::string& a_what) {
				a_write(a_sink, std::format("{{\"ok\":{},\"op\":\"{}\",\"result\":\"{}\",\"note\":\"read deepsnow.state after the next tick (uTickMs)\"}}", a_ok ? "true" : "false", op, a_what).c_str());
			};
			logger::info("[devbench] deepsnow.control {}", a_argsJson ? a_argsJson : "{}");
			if (op == "forceband")
			{
				const int band = ToInt(Get(args, "band"), -1);
				movement::ForceBand(band);
				reply(true, band < 0 ? "forcing off" : std::format("band {} forced on the player", std::min(band, settings::kBandCount)));
				return;
			}
			if (op == "mark")
			{
				const std::string list = Get(args, "list");
				int idx = -1;
				if (list == "snow") { idx = static_cast<int>(settings::List::kSnow); }
				else if (list == "trail") { idx = static_cast<int>(settings::List::kTrail); }
				else if (list == "road") { idx = static_cast<int>(settings::List::kRoad); }
				else if (list == "ice") { idx = static_cast<int>(settings::List::kIce); }
				if (idx < 0) { reply(false, "list must be snow, trail, road or ice"); return; }
				movement::RequestMark(idx);
				reply(true, "queued: the ground under the player is marked " + list + " and the INI saved");
				return;
			}
			if (op == "set")
			{
				const std::string key = Get(args, "key");
				const std::string value = Get(args, "value");
				const bool ok = settings::SetValue(key, value);
				reply(ok, ok ? key + " = " + settings::GetValue(key) : "no such key or bad value: " + key);
				return;
			}
			if (op == "save") { Queue([]() { settings::Save(); }); reply(true, "queued: settings saved"); return; }
			if (op == "reload") { Queue([]() { settings::Reload(); }); reply(true, "queued: settings reloaded"); return; }
			if (op == "restore") { settings::RestoreDefaults(); reply(true, "defaults restored (not saved)"); return; }
			if (op == "cleartrails") { movement::RequestClearTrails(); reply(true, "queued: trails cleared"); return; }
			if (op == "stamp")
			{
				// Adds N passes of packing at the player's square (credited straight to the square) - the
				// co-save round trip test needs trail data without walking.
				const int passes = std::clamp(ToInt(Get(args, "passes"), 1), 1, 50);
				Queue([passes]() {
					auto* player = RE::PlayerCharacter::GetSingleton();
					RE::TESWorldSpace* ws = player ? player->GetWorldspace() : nullptr;
					const RE::Calendar* cal = RE::Calendar::GetSingleton();
					if (!player || !ws || !cal) { logger::warn("[devbench] stamp: no player in a worldspace"); return; }
					const RE::NiPoint3 p = player->GetPosition();
					trails::AddPasses(ws->GetFormID(), p.x, p.y, trails::Who::kNPC, cal->GetDaysPassed(), passes);
					logger::info("[devbench] stamp: {} pass(es) at ({:.0f},{:.0f}) ws {:08X}", passes, p.x, p.y, ws->GetFormID());
				});
				reply(true, std::format("queued: {} pass(es) at the player", passes));
				return;
			}
			reply(false, "unknown op; use forceband, mark, set, save, reload, restore, cleartrails or stamp");
		}
	}

	void Init(bool a_lastAttempt)
	{
		static bool registered = false;
		if (registered) { return; }

		DevBenchAPI::IDevBenchInterface001* devBench = DevBenchAPI::GetDevBenchInterface001();
		if (!devBench)
		{
			if (a_lastAttempt) { logger::info("DevBench not detected; skipping the deepsnow tools"); }
			else { logger::debug("DevBench not detected yet; will retry at the next message"); }
			return;
		}

		constexpr const char* stateDescriptor =
			"{"
			"\"description\":\"Deep Snow - read-only state. op=state (default): tick = what the last tick decided for the player (surface snow/trail/road/ice/object/other/none, "
			"why, texture stem, mesh model, support collision material (decimal; 873356572/2431524493 = ice, Ice is Slick's), snow cover 0..1, navmesh preferred, sheltered + roofDistance, trail packing 0..1, snowfall boost, depthPercent of a full blanket, "
			"depthSource (model or SnowDeformation), depthSourceSetting (uSource 0 auto/1 model/2 SnowDeformation), sdFound, sdActive, sdUnits (Snow Deformation raw game units under the player, -1 = not asked), sdFullDepthUnits, fullDepthUnits and its source DTD/INI/SnowDeformation, groundBand (ground alone) and band (applied), forcedBand, road bonus, moving, speedMult, stamina, drainedLastSecond, tick "
			"timing in us, NPCs near/classified/affected, trailSquares, the band table; ROAD and ICE always read depthPercent 0, groundBand 0, no drain); effects = DeepSnow.esp forms found, each band ability's SpeedMult magnitude, actors "
			"holding one; strings = the language loaded. op=settings: every INI key and value. op=trails n=N: newest N trail squares. op=probe / op=history n=N: the "
			"milestone-1 spike probe (only when [Probe] bEnabled=1).\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\"},\"n\":{\"type\":\"integer\"}}},"
			"\"readOnly\":true"
			"}";
		constexpr const char* controlDescriptor =
			"{"
			"\"description\":\"Deep Snow - drive the mechanism and the page's settings. op=forceband band=N: put band N (1-5) on the player whatever the ground says, 0 = "
			"no band, -1 = forcing off (not saved). op=mark list=snow|trail|road|ice: the page's 'mark the ground under me' buttons - adds the texture (or, on a mesh, the "
			"mesh for ice) under the player to that list and saves the INI. op=set key=<INI key, e.g. fBand1Speed or Bands:fBand1Speed> value=<v>: as moving that "
			"control on the page (applies at once, not saved). op=save / op=reload / op=restore: the page's buttons. op=cleartrails. op=stamp passes=N: N passes of "
			"trail packing at the player's square (co-save tests). Game-side work is queued to the game thread; read deepsnow.state after the next tick.\","
			"\"inputSchema\":{\"type\":\"object\",\"properties\":{\"op\":{\"type\":\"string\"},\"band\":{\"type\":\"integer\"},\"list\":{\"type\":\"string\"},"
			"\"key\":{\"type\":\"string\"},\"value\":{\"type\":\"string\"},\"passes\":{\"type\":\"integer\"}}}"
			"}";

		const bool a = devBench->RegisterTool("deepsnow.state", stateDescriptor, &StateTool, nullptr);
		const bool b = devBench->RegisterTool("deepsnow.control", controlDescriptor, &ControlTool, nullptr);
		logger::info("Registered \"deepsnow.state\"{} and \"deepsnow.control\"{} with DevBench (build {})", a ? "" : " (replaced)", b ? "" : " (replaced)", devBench->GetBuildNumber());
		registered = true;
	}
}
