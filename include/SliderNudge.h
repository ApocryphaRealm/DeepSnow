#pragma once

// ============================================================================================================
// The highlighted slider takes Left / Right (Deep Snow 1.1.0; the owner, via Main Agent, 2026-10-25: one consistent way
// everywhere - a highlighted slider steps one unit of its shown digit on keyboard Left/Right and on the D-pad, keeps the
// highlight, and needs no activation).
//
// Why it is needed: Dear ImGui (1.90.8, the framework's) changes a slider from the keyboard or pad ONLY while the slider
// is the active item - after A / Space / Enter activates it (SliderScalar: make_active = clicked || NavActivateId == id).
// A slider that merely holds the highlight hands Left / Right to ImGui's navigation, which moves the highlight to
// whatever lies that way. On a page with one slider per row there is often nothing to the right, so nothing happened; in
// Deep Snow's depth-band table the next column's slider IS to the right, so Right walked the highlight across the row
// (combined run 2026-10-25, request 04f6 step 8; AMF's tools\nav_left_check case 6 shows the same).
//
// What it does, right after the slider is drawn (call it with the same value and range as the slider):
//   - only when the slider holds the highlight, is NOT active (an active slider is ImGui's own, and precise::SliderFloat
//     already holds that nudge to one step), and the left mouse button is up (a drag is the mouse's);
//   - a Left / Right press (keyboard arrow or D-pad, with ImGui's key repeat when held) moves the value one unit of the
//     digit the format shows (precise::Step: "%.0f" -> 1, "%.1f" -> 0.1), onto that grid, clamped to the range;
//   - on EVERY frame one of those keys is down it cancels ImGui's navigation move (NavMoveRequestCancel), so the highlight
//     stays: ImGui repeats a held move on its own faster schedule than the key repeat (Dragon's Eye Minimap test d844), and
//     a move left uncancelled on an in-between frame would walk the highlight away. The cancel is also how the framework
//     tells "the page took this press" (AMF's navside::PageTookTheMove), so Left never leaves for the mod list here.
// Up / Down are left to navigation, so the highlight still moves between rows. The left stick is left to navigation too.
// Same shape as Dragon's Eye Minimap's and Local Map Upgrade's NudgeableSlider (IsKeyPressed + NavMoveRequestCancel),
// keyed on the highlight instead of on a slider clicked earlier.
// Checked offline against the framework's own ImGui build: tools\slider_nudge_check.bat.
// ============================================================================================================

#include "SKSEMenuFramework.h"
#include "PreciseSlider.h"

#include <algorithm>
#include <cmath>

namespace precise
{
	// The value one step from a_before in a_dir (+1 / -1): onto the grid the format shows, clamped to the range.
	inline float StepFrom(float a_before, int a_dir, float a_min, float a_max, const char* a_format)
	{
		const double step = Step(a_format);
		double v = static_cast<double>(a_before) + (a_dir > 0 ? step : -step);
		v = std::round(v / step) * step;
		const double lo = (std::min)(a_min, a_max);
		const double hi = (std::max)(a_min, a_max);
		return static_cast<float>(std::clamp(v, lo, hi));
	}

	// For the slider item just drawn. True when the value changed.
	inline bool HighlightNudge(float* a_v, float a_min, float a_max, const char* a_format)
	{
		using namespace ImGuiMCP;
		if (!a_v || !IsItemFocused() || IsItemActive() || IsMouseDown(ImGuiMouseButton_Left)) {
			return false;
		}
		const bool left = IsKeyPressed(ImGuiKey_LeftArrow, true) || IsKeyPressed(ImGuiKey_GamepadDpadLeft, true);
		const bool right = IsKeyPressed(ImGuiKey_RightArrow, true) || IsKeyPressed(ImGuiKey_GamepadDpadRight, true);
		const bool held = IsKeyDown(ImGuiKey_LeftArrow) || IsKeyDown(ImGuiKey_RightArrow) || IsKeyDown(ImGuiKey_GamepadDpadLeft) ||
		                  IsKeyDown(ImGuiKey_GamepadDpadRight);
		if (left || right || held) {
			NavMoveRequestCancel();   // the press is this slider's: the highlight stays
		}
		if (left == right) {          // neither, or both at once
			return false;
		}
		const float before = *a_v;
		*a_v = StepFrom(before, right ? 1 : -1, a_min, a_max, a_format);
		return *a_v != before;
	}
}
