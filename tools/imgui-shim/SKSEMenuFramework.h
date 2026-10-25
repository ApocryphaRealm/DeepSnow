#pragma once

// Offline-check stand-in for include/SKSEMenuFramework.h (tools\slider_nudge_check only - never compiled into the plugin).
// The plugin reaches the framework's Dear ImGui through ImGuiMCP, whose functions look up the framework's exports at run
// time. The check links the framework's own ImGui build instead, so here ImGuiMCP simply names those same ImGui functions
// (each export is a plain forward to them) - and include/PreciseSlider.h and include/SliderNudge.h compile unchanged.
#include <imgui.h>
#include <imgui_internal.h>

namespace ImGuiMCP
{
	using ::ImGuiKey;
	using ::ImGuiKey_LeftArrow;
	using ::ImGuiKey_RightArrow;
	using ::ImGuiKey_GamepadDpadLeft;
	using ::ImGuiKey_GamepadDpadRight;
	using ::ImGuiKey_GamepadLStickLeft;
	using ::ImGuiKey_GamepadLStickRight;
	using ::ImGuiMouseButton_Left;
	using ::ImGuiSliderFlags_AlwaysClamp;

	using ImGui::IsItemActive;
	using ImGui::IsItemFocused;
	using ImGui::IsKeyDown;
	using ImGui::IsKeyPressed;
	using ImGui::IsMouseDown;
	using ImGui::NavMoveRequestCancel;
	using ImGui::SliderFloat;
	using ImGui::SliderInt;
}
