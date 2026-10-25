// Offline check of Deep Snow's band sliders taking Left / Right while they only hold the highlight (1.1.0; combined run
// 2026-10-25, request 04f6 step 8: keyboard Right on a band slider moved the highlight to the next table column, and keys
// changed the value only after the slider was activated). Runs the framework's own Dear ImGui build (1.90.8 with AMF's
// overlay patches) headless with keyboard / gamepad navigation on, and compiles the plugin's own include/PreciseSlider.h
// and include/SliderNudge.h through tools\imgui-shim\SKSEMenuFramework.h (ImGuiMCP -> the same ImGui functions).
//   tools\slider_nudge_check.bat   -> one line per check and "slider_nudge_check: N of N passed"; exits 1 on any failure.
// What it proves, on a four-column table shaped like the page's (Band | Starts at depth | Speed | Stamina per second):
//   1. the cause, on a control table drawn the old way (precise::SliderFloat only): with the highlight on a slider that is
//      not active, Right moves the highlight to the next column and the value does not change;
//   2. keyboard: highlighted, not activated, Right Right Left steps "Starts at depth" 10 -> 11 -> 12 -> 11, the highlight
//      never leaves the slider; a held Right steps once per key repeat and still stays; the top of the range clamps; the
//      "%.1f" stamina column steps 0.1; Down still moves to the next row;
//   3. D-pad (gamepad navigation, keyboard navigation off - how the framework runs with a pad): the same 10 -> 11 -> 12 ->
//      11 and the highlight stays; D-pad down still moves to the next row;
//   4. activation still works: Space (keyboard) and A (pad) activate the slider, Right / Left then step it one unit per
//      press (no double step from the two paths), the highlight stays, and the same button lets it go.
// What it cannot: the framework's own per-frame decisions (Renderer.cpp - Left leaving for the mod list is AMF's
// navside, proven by AMF's tools\nav_left_check) and the game's input path (Input.cpp); the in-game steps cover those.
#include "PreciseSlider.h"
#include "SliderNudge.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
	int g_pass = 0, g_fail = 0;
	void Check(bool a_ok, const std::string& a_what)
	{
		(a_ok ? g_pass : g_fail)++;
		std::printf("%s %s\n", a_ok ? "PASS" : "FAIL", a_what.c_str());
	}

	constexpr int kRows = 2;
	struct Band { float depth, speed, drain; };
	Band g_bands[kRows] = { { 10.0F, 85.0F, 2.0F }, { 30.0F, 70.0F, 4.0F } };   // the page's table (fBand1..2)
	Band g_control[kRows] = { { 10.0F, 85.0F, 2.0F }, { 30.0F, 70.0F, 4.0F } }; // the same table drawn the old way

	ImGuiID g_depth[kRows] = {}, g_speed[kRows] = {}, g_drain[kRows] = {};
	ImGuiID g_cDepth[kRows] = {}, g_cSpeed[kRows] = {};
	ImGuiID* g_focusWanted = nullptr;   // focus this item the next frame it is drawn
	int g_pressedRight = 0;             // frames ImGui reported RightArrow pressed (with repeat) - one step each

	// UI.cpp's Slider(): precise::SliderFloat, then precise::HighlightNudge on the same value
	bool PageSlider(const char* a_label, float* a_v, float a_min, float a_max, const char* a_format)
	{
		bool changed = precise::SliderFloat(a_label, a_v, a_min, a_max, a_format);
		if (precise::HighlightNudge(a_v, a_min, a_max, a_format)) { changed = true; }
		return changed;
	}

	void Mark(ImGuiID& a_slot)
	{
		a_slot = ImGui::GetItemID();
		if (g_focusWanted == &a_slot)
		{
			ImGui::SetFocusID(a_slot, ImGui::GetCurrentWindow());
			GImGui->NavDisableHighlight = false;
			g_focusWanted = nullptr;
		}
	}

	void Table(const char* a_id, Band* a_rows, bool a_new)
	{
		if (!ImGui::BeginTable(a_id, 4, ImGuiTableFlags_SizingStretchProp)) { return; }
		ImGui::TableSetupColumn("Band", 0, 0.4F);
		ImGui::TableSetupColumn("Starts at depth", 0, 1.0F);
		ImGui::TableSetupColumn("Speed", 0, 1.0F);
		ImGui::TableSetupColumn("Stamina per second", 0, 1.0F);
		ImGui::TableHeadersRow();
		for (int i = 0; i < kRows; ++i)
		{
			const std::string d = "##d" + std::to_string(i), s = "##s" + std::to_string(i), r = "##r" + std::to_string(i);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::Text("%d", i + 1);
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0F);
			if (a_new) { PageSlider(d.c_str(), &a_rows[i].depth, 0.0F, 150.0F, "%.0f %%"); Mark(g_depth[i]); }
			else { precise::SliderFloat(d.c_str(), &a_rows[i].depth, 0.0F, 150.0F, "%.0f %%"); Mark(g_cDepth[i]); }
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0F);
			if (a_new) { PageSlider(s.c_str(), &a_rows[i].speed, 10.0F, 100.0F, "%.0f %%"); Mark(g_speed[i]); }
			else { precise::SliderFloat(s.c_str(), &a_rows[i].speed, 10.0F, 100.0F, "%.0f %%"); Mark(g_cSpeed[i]); }
			ImGui::TableNextColumn();
			ImGui::SetNextItemWidth(-1.0F);
			if (a_new) { PageSlider(r.c_str(), &a_rows[i].drain, 0.0F, 20.0F, "%.1f"); Mark(g_drain[i]); }
			else { precise::SliderFloat(r.c_str(), &a_rows[i].drain, 0.0F, 20.0F, "%.1f"); }
		}
		ImGui::EndTable();
	}

	struct Keys { bool left = false, right = false, down = false, space = false, padLeft = false, padRight = false, padDown = false, padA = false; };

	void Frame(const Keys& a_k = {})
	{
		ImGuiIO& io = ImGui::GetIO();
		io.AddKeyEvent(ImGuiKey_LeftArrow, a_k.left);
		io.AddKeyEvent(ImGuiKey_RightArrow, a_k.right);
		io.AddKeyEvent(ImGuiKey_DownArrow, a_k.down);
		io.AddKeyEvent(ImGuiKey_Space, a_k.space);
		io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, a_k.padLeft);
		io.AddKeyEvent(ImGuiKey_GamepadDpadRight, a_k.padRight);
		io.AddKeyEvent(ImGuiKey_GamepadDpadDown, a_k.padDown);
		io.AddKeyEvent(ImGuiKey_GamepadFaceDown, a_k.padA);
		io.DeltaTime = 1.0F / 60.0F;
		ImGui::NewFrame();
		if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) { ++g_pressedRight; }
		ImGui::SetNextWindowPos(ImVec2(100, 100));
		ImGui::SetNextWindowSize(ImVec2(1200, 700));
		ImGui::Begin("###ds-page", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
		Table("##ds_bands", g_bands, true);
		ImGui::Separator();
		Table("##ds_bands_old", g_control, false);
		ImGui::End();
		ImGui::Render();
	}

	// one press: a frame with the key down, then frames with it up (the move, if any, lands by then)
	void Press(Keys a_down)
	{
		Frame(a_down);
		Frame();
		Frame();
	}

	void FocusOn(ImGuiID& a_slot)
	{
		g_focusWanted = &a_slot;
		Frame();
		Frame();
	}

	bool Near(float a, float b) { return std::fabs(a - b) < 0.0005F; }

	std::string Val(float a_v)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.2f", a_v);
		return buf;
	}

	void KeyboardMode()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags = (io.ConfigFlags | ImGuiConfigFlags_NavEnableKeyboard) & ~ImGuiConfigFlags_NavEnableGamepad;
	}

	void PadMode()
	{
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags = (io.ConfigFlags | ImGuiConfigFlags_NavEnableGamepad) & ~ImGuiConfigFlags_NavEnableKeyboard;
	}
}

int main()
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.DisplaySize = ImVec2(1920, 1080);
	io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
	{
		unsigned char* pixels = nullptr;
		int w = 0, h = 0;
		io.Fonts->AddFontDefault();
		io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
		io.Fonts->SetTexID(reinterpret_cast<ImTextureID>(static_cast<intptr_t>(1)));
	}
	KeyboardMode();
	Frame();
	Frame();
	const ImGuiContext& g = *GImGui;

	// 1. the cause: the old way, Right on a highlighted (not activated) slider is ImGui's navigation
	FocusOn(g_cDepth[0]);
	Check(g.NavId == g_cDepth[0] && g.ActiveId == 0, "control: the highlight is on the old-way depth slider, not activated");
	Press({ .right = true });
	Check(g.NavId == g_cSpeed[0], "control (the reported fault): Right moved the highlight to the next column (Speed)");
	Check(Near(g_control[0].depth, 10.0F), "control: and the value did not change (" + Val(g_control[0].depth) + ")");

	// 2. keyboard, highlighted, no activation
	FocusOn(g_depth[0]);
	Check(g.NavId == g_depth[0] && g.ActiveId == 0, "keyboard: the highlight is on band 1 'Starts at depth' (10), not activated");
	Press({ .right = true });
	Check(Near(g_bands[0].depth, 11.0F) && g.NavId == g_depth[0], "keyboard Right: 10 -> " + Val(g_bands[0].depth) + ", highlight stays");
	Press({ .right = true });
	Check(Near(g_bands[0].depth, 12.0F) && g.NavId == g_depth[0], "keyboard Right: -> " + Val(g_bands[0].depth) + ", highlight stays");
	Press({ .left = true });
	Check(Near(g_bands[0].depth, 11.0F) && g.NavId == g_depth[0], "keyboard Left: -> " + Val(g_bands[0].depth) + ", highlight stays");
	Check(g.ActiveId == 0, "keyboard: the slider was never activated");
	Check(Near(g_bands[0].speed, 85.0F), "keyboard: the Speed column beside it is untouched (" + Val(g_bands[0].speed) + ")");

	{   // held Right for one second: one step per key repeat, the highlight stays every frame
		g_pressedRight = 0;
		const float start = g_bands[0].depth;
		bool stayed = true;
		for (int f = 0; f < 60; ++f)
		{
			Frame({ .right = true });
			stayed = stayed && g.NavId == g_depth[0];
		}
		Frame();
		Frame();
		const float want = start + static_cast<float>(g_pressedRight);
		Check(stayed && g.NavId == g_depth[0], "keyboard held Right (1 s): the highlight stayed on the slider every frame");
		Check(g_pressedRight > 1 && Near(g_bands[0].depth, want),
			  "keyboard held Right: " + std::to_string(g_pressedRight) + " presses and repeats -> " + Val(g_bands[0].depth) + " (want " + Val(want) + ")");
	}
	g_bands[0].depth = 150.0F;
	Press({ .right = true });
	Check(Near(g_bands[0].depth, 150.0F) && g.NavId == g_depth[0], "keyboard Right at the top (150): stays 150, highlight stays");
	g_bands[0].depth = 10.0F;

	FocusOn(g_drain[0]);
	Press({ .right = true });
	Check(Near(g_bands[0].drain, 2.1F) && g.NavId == g_drain[0], "keyboard Right on 'Stamina per second' (%.1f): 2.0 -> " + Val(g_bands[0].drain));
	Press({ .left = true });
	Check(Near(g_bands[0].drain, 2.0F) && g.NavId == g_drain[0], "keyboard Left on 'Stamina per second': -> " + Val(g_bands[0].drain));

	FocusOn(g_depth[0]);
	Press({ .down = true });
	Check(g.NavId == g_depth[1], "keyboard Down: still moves to band 2's 'Starts at depth'");
	Check(Near(g_bands[1].depth, 30.0F), "keyboard Down: and changes nothing (" + Val(g_bands[1].depth) + ")");

	// 3. D-pad, gamepad navigation (keyboard navigation off)
	PadMode();
	g_bands[0].depth = 10.0F;
	FocusOn(g_depth[0]);
	Check(g.NavId == g_depth[0] && g.ActiveId == 0, "pad: the highlight is on band 1 'Starts at depth' (10), not activated");
	Press({ .padRight = true });
	Check(Near(g_bands[0].depth, 11.0F) && g.NavId == g_depth[0], "D-pad right: 10 -> " + Val(g_bands[0].depth) + ", highlight stays");
	Press({ .padRight = true });
	Check(Near(g_bands[0].depth, 12.0F) && g.NavId == g_depth[0], "D-pad right: -> " + Val(g_bands[0].depth) + ", highlight stays");
	Press({ .padLeft = true });
	Check(Near(g_bands[0].depth, 11.0F) && g.NavId == g_depth[0], "D-pad left: -> " + Val(g_bands[0].depth) + ", highlight stays");
	Check(g.ActiveId == 0, "pad: the slider was never activated");
	Press({ .padDown = true });
	Check(g.NavId == g_depth[1], "D-pad down: still moves to band 2's 'Starts at depth'");

	// 4. activation still works: A on the pad ...
	FocusOn(g_depth[0]);
	Press({ .padA = true });
	Check(g.ActiveId == g_depth[0], "pad A: activates the slider");
	Press({ .padRight = true });
	Check(Near(g_bands[0].depth, 12.0F) && g.NavId == g_depth[0], "activated, D-pad right: 11 -> " + Val(g_bands[0].depth) + " (one step)");
	Press({ .padLeft = true });
	Check(Near(g_bands[0].depth, 11.0F) && g.NavId == g_depth[0], "activated, D-pad left: -> " + Val(g_bands[0].depth) + " (one step)");
	Press({ .padA = true });
	Check(g.ActiveId == 0 && g.NavId == g_depth[0], "pad A again: lets the slider go, the highlight stays on it");

	// ... and Space on the keyboard
	KeyboardMode();
	g_bands[0].depth = 10.0F;
	FocusOn(g_depth[0]);
	Press({ .space = true });
	Check(g.ActiveId == g_depth[0], "keyboard Space: activates the slider");
	Press({ .right = true });
	Check(Near(g_bands[0].depth, 11.0F) && g.NavId == g_depth[0], "activated, keyboard Right: 10 -> " + Val(g_bands[0].depth) + " (one step)");
	Press({ .right = true });
	Check(Near(g_bands[0].depth, 12.0F), "activated, keyboard Right: -> " + Val(g_bands[0].depth) + " (one step)");
	Press({ .left = true });
	Check(Near(g_bands[0].depth, 11.0F) && g.NavId == g_depth[0], "activated, keyboard Left: -> " + Val(g_bands[0].depth) + " (one step)");
	Press({ .space = true });
	Check(g.ActiveId == 0 && g.NavId == g_depth[0], "keyboard Space again: lets the slider go, the highlight stays on it");
	Press({ .right = true });
	Check(Near(g_bands[0].depth, 12.0F) && g.NavId == g_depth[0], "after letting go, Right on the highlight: -> " + Val(g_bands[0].depth));

	ImGui::DestroyContext();
	std::printf("slider_nudge_check: %d of %d passed\n", g_pass, g_pass + g_fail);
	return g_fail == 0 ? 0 : 1;
}
