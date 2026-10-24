// Deep Snow - settings page on the Apocrypha Menu Framework (SKSE Menu Framework compatible). Own code,
// GPL-3.0-or-later (2026-10-24). Every change applies at once; Save writes the INI.
#include "PCH.h"

#include "UI.h"

#include "SKSEMenuFramework.h"
#include "PreciseSlider.h"

#include "Depth.h"
#include "Movement.h"
#include "Settings.h"
#include "Trails.h"

#include "utils/Logger.h"
#include "utils/Strings.h"
#include "utils/Toggle.h"

#include <algorithm>
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace UI
{
	namespace
	{
		using strings::TR;

		// Written by the page (render thread) and by tasks that finish on the game thread, so behind a lock.
		std::mutex msgLock;
		std::string statusMessage;  // guarded by msgLock
		void SetMsg(const char* a_text) { std::scoped_lock l(msgLock); statusMessage = a_text ? a_text : ""; }
		std::string GetMsg() { std::scoped_lock l(msgLock); return statusMessage; }

		constexpr const char* kLogLevelNames[] = { "Trace", "Debug", "Info", "Warning", "Error", "Critical", "Off" };
		constexpr const char* kLogLevelKeys[] = { "DS_LogTrace", "DS_LogDebug", "DS_LogInfo", "DS_LogWarning", "DS_LogError", "DS_LogCritical", "DS_LogOff" };

		void OnMainThread(std::function<void()> a_task)
		{
			if (auto* tasks = SKSE::GetTaskInterface()) { tasks->AddTask(std::move(a_task)); }
			else { logger::warn("[page] SKSE task interface is null; the request is dropped"); }
		}

		bool HasRequiredExports()
		{
			constexpr const char* required[] = {
				"AddSectionItem", "igTextV", "igTextDisabledV", "igTextWrappedV", "igSetTooltipV", "igSeparatorText", "igCombo_Str_arr", "igSliderFloat",
				"igIsKeyDown_Nil", "igIsMouseDown_Nil", "igIsItemHovered", "igButton", "igSameLine", "igSpacing", "igPushItemWidth", "igPopItemWidth",
				"igBeginTable", "igEndTable", "igTableSetupColumn", "igTableHeadersRow", "igTableNextRow", "igTableNextColumn", "igSetNextItemWidth",
				// utils/Toggle.h
				"igGetCursorScreenPos", "igGetWindowDrawList", "igGetFrameHeight", "igInvisibleButton", "igPushID_Str", "igPopID", "ImDrawList_AddRectFilled",
				"ImDrawList_AddCircleFilled"
			};
			for (const char* name : required)
			{
				if (!GetMenuFrameworkFunction<void*>(name))
				{
					logger::warn("[page] the menu framework does not export \"{}\"", name);
					return false;
				}
			}
			return true;
		}

		void Help(const char* a_text)
		{
			ImGuiMCP::SameLine();
			ImGuiMCP::TextDisabled("%s", TR("DS_HelpMark", "(?)"));
			if (ImGuiMCP::IsItemHovered()) { ImGuiMCP::SetTooltip("%s", a_text); }
		}

		bool Switch(const char* a_label, std::atomic<bool>& a_value, const char* a_help)
		{
			bool v = a_value.load();
			const bool changed = ImGuiMCP::Toggle(a_label, &v);
			if (changed)
			{
				a_value = v;
				settings::Touch();
				logger::debug("[page] {} -> {}", a_label, v);
			}
			Help(a_help);
			return changed;
		}

		// rule 68: precise::SliderFloat holds a keyboard / D-pad nudge to one unit of the digit the format shows.
		bool Slider(const char* a_label, std::atomic<float>& a_value, float a_min, float a_max, const char* a_format, const char* a_help)
		{
			float v = a_value.load();
			const bool changed = precise::SliderFloat(a_label, &v, a_min, a_max, a_format);
			if (changed)
			{
				a_value = std::clamp(v, a_min, a_max);
				settings::Touch();
			}
			if (a_help) { Help(a_help); }
			return changed;
		}

		const char* SurfaceLabel(surface::Kind a_kind)
		{
			using K = surface::Kind;
			switch (a_kind)
			{
			case K::kSnow: return TR("DS_SurfSnow", "Snow");
			case K::kTrail: return TR("DS_SurfTrail", "Trail");
			case K::kRoad: return TR("DS_SurfRoad", "Road");
			case K::kIce: return TR("DS_SurfIce", "Ice (left to Ice is Slick)");
			case K::kObject: return TR("DS_SurfObject", "Standing on an object");
			case K::kOther: return TR("DS_SurfOther", "Other ground");
			default: return TR("DS_SurfNone", "Not read (indoors, mounted, swimming or loading)");
			}
		}

		void RenderModel()
		{
			ImGuiMCP::SeparatorText(TR("DS_ModelTitle", "How deep is the snow?"));
			const std::string units = std::format("{:.0f}", depth::FullDepthUnits());
			ImGuiMCP::TextWrapped("%s", TR("DS_ModelText",
				"Depth here is Deep Snow's own model, not the snow you see - no mod can read Dynamic Terrain Deformation's real snow. "
				"100% is a full, untouched blanket of snow. Snowy paths count as a trench, roads and rocks as no snow, and the snow under a roof as none. "
				"Every footstep, yours or anyone's, packs the snow down, and packed snow fills back in over time."));
			ImGuiMCP::Text("%s %s", TR("DS_ModelUnits", "A full blanket, in game units:"), units.c_str());
		}

		void RenderStatus()
		{
			ImGuiMCP::SeparatorText(TR("DS_StatusTitle", "Under your feet"));
			const movement::Snapshot s = movement::Get();
			if (!s.havePlayer) { ImGuiMCP::TextWrapped("%s", TR("DS_StatusNoPlayer", "Load a game to see what is under your feet.")); return; }
			if (!s.effectsReady) { ImGuiMCP::TextWrapped("%s", TR("DS_StatusNoPlugin", "DeepSnow.esp is not active, so nothing can be applied. Enable it in your load order.")); return; }
			if (!s.enabled) { ImGuiMCP::TextWrapped("%s", TR("DS_StatusOff", "Deep Snow is switched off.")); return; }
			ImGuiMCP::Text("%s %s", TR("DS_StatusSurface", "Ground:"), SurfaceLabel(s.reading.kind));
			if (!s.reading.texture.empty()) { ImGuiMCP::SameLine(); ImGuiMCP::TextDisabled("(%s)", s.reading.texture.c_str()); }
			if (s.reading.sheltered) { ImGuiMCP::TextDisabled("%s", TR("DS_StatusRoof", "Under a roof - no snow here.")); }
			const std::string depthText = std::format("{:.0f}% ({:.1f})", s.depthPercent, s.depthPercent / 100.0F * depth::FullDepthUnits());
			ImGuiMCP::Text("%s %s", TR("DS_StatusDepth", "Snow depth:"), depthText.c_str());
			if (s.band > 0)
			{
				const auto& b = settings::bands[s.band - 1];
				const std::string bandText = std::format("{}  ({:.0f}%, {:.1f}/s)", s.band, b.speed.load(), settings::stamina::drain ? b.drain.load() : 0.0F);
				ImGuiMCP::Text("%s %s", TR("DS_StatusBand", "Depth band:"), bandText.c_str());
			}
			else
			{
				ImGuiMCP::Text("%s %s", TR("DS_StatusBand", "Depth band:"), TR("DS_StatusNoBand", "none"));
			}
			if (s.forcedBand >= 0) { ImGuiMCP::TextDisabled("%s", TR("DS_StatusForced", "A test tool is forcing this band.")); }
			if (s.road) { ImGuiMCP::Text("%s", TR("DS_StatusRoadBonus", "Road: faster travel is on.")); }
			const std::string speed = std::format("{:.0f}", s.speedMult);
			ImGuiMCP::Text("%s %s", TR("DS_StatusSpeed", "Movement speed (SpeedMult):"), speed.c_str());
		}

		void RenderGeneral()
		{
			using namespace settings;
			ImGuiMCP::SeparatorText(TR("DS_GeneralTitle", "General"));
			Switch(TR("DS_Enabled", "Deep snow slows you down"), general::enabled, TR("DS_HelpEnabled", "Off removes every Deep Snow effect at once."));
			Switch(TR("DS_AffectNPCs", "Also slow other people and creatures"), general::affectNPCs,
				TR("DS_HelpAffectNPCs", "Off: only you are slowed. Their footsteps still pack trails either way."));
			Switch(TR("DS_RoadBonus", "Faster travel on roads"), roads::bonus, TR("DS_HelpRoadBonus", "Optional. Off by default."));
			if (roads::bonus) { Slider(TR("DS_RoadSpeed", "Speed on roads"), roads::speed, 100.0F, 150.0F, "%.0f %%", TR("DS_HelpRoadSpeed", "100% is normal speed.")); }
		}

		void RenderBands()
		{
			using namespace settings;
			ImGuiMCP::SeparatorText(TR("DS_BandsTitle", "Depth bands"));
			ImGuiMCP::TextWrapped("%s", TR("DS_BandsText",
				"Each band starts at a snow depth and sets your speed there and a small stamina cost while you move. The deepest band you reach applies."));
			const ImGuiMCP::ImGuiTableFlags flags = ImGuiMCP::ImGuiTableFlags_SizingStretchProp;
			if (ImGuiMCP::BeginTable("##ds_bands", 4, flags))
			{
				ImGuiMCP::TableSetupColumn(TR("DS_ColBand", "Band"), 0, 0.4F);
				ImGuiMCP::TableSetupColumn(TR("DS_ColDepth", "Starts at depth"), 0, 1.0F);
				ImGuiMCP::TableSetupColumn(TR("DS_ColSpeed", "Speed"), 0, 1.0F);
				ImGuiMCP::TableSetupColumn(TR("DS_ColDrain", "Stamina per second"), 0, 1.0F);
				ImGuiMCP::TableHeadersRow();
				for (int i = 0; i < kBandCount; ++i)
				{
					ImGuiMCP::TableNextRow();
					ImGuiMCP::TableNextColumn();
					ImGuiMCP::Text("%d", i + 1);
					ImGuiMCP::TableNextColumn();
					ImGuiMCP::SetNextItemWidth(-1.0F);
					Slider(std::format("##d{}", i).c_str(), bands[i].depth, 0.0F, 150.0F, "%.0f %%", nullptr);
					ImGuiMCP::TableNextColumn();
					ImGuiMCP::SetNextItemWidth(-1.0F);
					Slider(std::format("##s{}", i).c_str(), bands[i].speed, 10.0F, 100.0F, "%.0f %%", nullptr);
					ImGuiMCP::TableNextColumn();
					ImGuiMCP::SetNextItemWidth(-1.0F);
					Slider(std::format("##r{}", i).c_str(), bands[i].drain, 0.0F, 20.0F, "%.1f", nullptr);
				}
				ImGuiMCP::EndTable();
			}
			Switch(TR("DS_Drain", "Deep snow costs stamina"), stamina::drain, TR("DS_HelpDrain", "A small cost each second while you move in a band - the band's stamina value."));
			Slider(TR("DS_Floor", "Never below"), stamina::floor, 0.0F, 100.0F, "%.0f %%", TR("DS_HelpFloor", "Deep snow never takes your stamina below this share of its maximum."));
		}

		void RenderTrails()
		{
			using namespace settings;
			ImGuiMCP::SeparatorText(TR("DS_TrailsTitle", "Packed trails"));
			Switch(TR("DS_Trails", "Footsteps pack the snow"), settings::trails::enabled, TR("DS_HelpTrails", "Anyone walking through snow packs it, so a path used often stays easy to walk."));
			Slider(TR("DS_PackPerPass", "Packing per pass"), settings::trails::packPerPass, 0.0F, 1.0F, "%.2f", TR("DS_HelpPackPerPass", "How much one person walking through packs the snow. 1 packs it fully at once."));
			Slider(TR("DS_PackedRemain", "Depth left on a packed trail"), settings::trails::packedRemain, 0.0F, 1.0F, "%.2f", TR("DS_HelpPackedRemain", "0 packs the snow down to nothing, 1 leaves it as deep as before."));
			Slider(TR("DS_LifeHours", "Hours to fill back in"), settings::trails::lifeHours, 0.1F, 240.0F, "%.1f", TR("DS_HelpLifeHours", "Game hours until fully packed snow is fresh again. Faster while it snows."));
			const std::string count = std::format("{}", ::trails::Count());
			ImGuiMCP::Text("%s %s", TR("DS_TrailCount", "Packed squares remembered:"), count.c_str());
			if (ImGuiMCP::Button(TR("DS_ClearTrails", "Clear trails")))
			{
				movement::RequestClearTrails();
				SetMsg(TR("DS_MsgTrailsCleared", "Trails cleared."));
			}
		}

		void RenderSnowfall()
		{
			using namespace settings;
			ImGuiMCP::SeparatorText(TR("DS_SnowfallTitle", "Snowfall"));
			Switch(TR("DS_Deepens", "Snowfall deepens the snow"), snowfall::deepens, TR("DS_HelpDeepens", "While it snows the snow grows deeper, and settles back after."));
			if (snowfall::deepens)
			{
				Slider(TR("DS_PerHour", "Deeper per hour of snowfall"), snowfall::perHour, 0.0F, 100.0F, "%.0f %%", nullptr);
				Slider(TR("DS_SnowMax", "At most"), snowfall::max, 0.0F, 200.0F, "%.0f %%", nullptr);
			}
		}

		void RenderSurfaces()
		{
			using namespace settings;
			ImGuiMCP::SeparatorText(TR("DS_SurfacesTitle", "Surfaces"));
			ImGuiMCP::TextWrapped("%s", TR("DS_SurfacesText",
				"Stand on a ground and mark what it is. Use this when a snow or path texture from another mod is not recognised. Ice is left to Ice is Slick: no slowdown and no stamina cost there."));
			if (ImGuiMCP::Button(TR("DS_MarkSnow", "This is snow"))) { movement::RequestMark(static_cast<int>(List::kSnow)); SetMsg(TR("DS_MsgMarked", "Marked and saved.")); }
			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button(TR("DS_MarkTrail", "This is a trail"))) { movement::RequestMark(static_cast<int>(List::kTrail)); SetMsg(TR("DS_MsgMarked", "Marked and saved.")); }
			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button(TR("DS_MarkRoad", "This is a road"))) { movement::RequestMark(static_cast<int>(List::kRoad)); SetMsg(TR("DS_MsgMarked", "Marked and saved.")); }
			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button(TR("DS_MarkIce", "This is ice"))) { movement::RequestMark(static_cast<int>(List::kIce)); SetMsg(TR("DS_MsgMarked", "Marked and saved.")); }

			const std::string lists[] = { ListText(List::kSnow), ListText(List::kTrail), ListText(List::kRoad), ListText(List::kIce) };
			ImGuiMCP::TextDisabled("%s %s", TR("DS_ListSnow", "Snow:"), lists[0].c_str());
			ImGuiMCP::TextDisabled("%s %s", TR("DS_ListTrail", "Trail:"), lists[1].c_str());
			ImGuiMCP::TextDisabled("%s %s", TR("DS_ListRoad", "Road:"), lists[2].c_str());
			ImGuiMCP::TextDisabled("%s %s", TR("DS_ListIce", "Ice:"), lists[3].c_str());

			Slider(TR("DS_TrailDepth", "Depth of a snowy path"), settings::depth::trail, 0.0F, 1.0F, "%.2f", TR("DS_HelpTrailDepth", "A path texture's snow, as a share of a full blanket."));
			Slider(TR("DS_OtherDepth", "Depth on other ground"), settings::depth::other, 0.0F, 1.0F, "%.2f",
				TR("DS_HelpOtherDepth", "Ground no list names. Set 1 if your load order covers all ground in snow."));
		}

		void RenderDebug()
		{
			using namespace settings;
			ImGuiMCP::SeparatorText(TR("DS_DebugTitle", "Debug"));
			int level = std::clamp(static_cast<int>(debug::logLevel.load()), 0, 6);
			std::vector<std::string> store;
			for (int i = 0; i < 7; ++i) { store.emplace_back(TR(kLogLevelKeys[i], kLogLevelNames[i])); }
			std::vector<const char*> labels;
			for (const auto& s : store) { labels.push_back(s.c_str()); }
			if (ImGuiMCP::Combo(TR("DS_LogLevel", "Log level"), &level, labels.data(), 7))
			{
				debug::logLevel = static_cast<std::uint32_t>(level);
				ApplyLogLevel();
			}
			Help(TR("DS_HelpLogLevel", "Applies at once. The log is Documents\\My Games\\Skyrim Special Edition\\SKSE\\DeepSnow.log."));
		}

		void RenderButtons()
		{
			ImGuiMCP::SeparatorText("");
			if (ImGuiMCP::Button(TR("DS_Save", "Save")))
			{
				SetMsg(TR("DS_MsgSaving", "Saving..."));
				OnMainThread([]() { SetMsg(settings::Save() ? TR("DS_MsgSaved", "Settings saved.") : TR("DS_MsgSaveFail", "Could not write the INI. See the log.")); });
			}
			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button(TR("DS_Reload", "Reload from INI")))
			{
				OnMainThread([]() { SetMsg(settings::Reload() ? TR("DS_MsgReloaded", "Settings reloaded.") : TR("DS_MsgReloadFail", "Could not read the INI. See the log.")); });
			}
			ImGuiMCP::SameLine();
			if (ImGuiMCP::Button(TR("DS_Restore", "Restore defaults")))
			{
				settings::RestoreDefaults();
				SetMsg(TR("DS_MsgRestored", "Defaults restored. Press Save to keep them."));
			}
			if (const std::string msg = GetMsg(); !msg.empty()) { ImGuiMCP::TextWrapped("%s", msg.c_str()); }
		}
	}

	void Register()
	{
		if (!SKSEMenuFramework::IsInstalled())
		{
			logger::info("[page] no menu framework is installed; settings come from the INI only");
			return;
		}
		if (!HasRequiredExports())
		{
			logger::warn("[page] the installed menu framework is older than this page needs; update Apocrypha Menu Framework to configure Deep Snow in game");
			return;
		}
		SKSEMenuFramework::SetSection("Deep Snow");
		SKSEMenuFramework::AddSectionItem("Settings", SettingsPanel::Render);
		logger::info("[page] registered the settings page with the menu framework");
	}

	void __stdcall SettingsPanel::Render()
	{
		strings::Tick();
		ImGuiMCP::TextWrapped("%s", TR("DS_Intro", "Changes apply as soon as you make them. Press Save to keep them for next time."));
		ImGuiMCP::PushItemWidth(300.0F);
		RenderModel();
		RenderStatus();
		RenderGeneral();
		RenderBands();
		RenderTrails();
		RenderSnowfall();
		RenderSurfaces();
		RenderDebug();
		ImGuiMCP::PopItemWidth();
		RenderButtons();
	}
}
