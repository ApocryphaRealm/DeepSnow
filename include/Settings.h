#pragma once

// Deep Snow - settings (spike). Read once at load with plain file I/O (redirector-proof, rule 16); the spike never
// writes the INI. Every compiled default below equals the value in dist\SKSE\Plugins\DeepSnow.ini.

#include <atomic>
#include <cstdint>
#include <string>

namespace settings
{
	namespace probe
	{
		inline std::atomic<bool> enabled = true;            // bEnabled:Probe - sample the ground under the player
		inline std::atomic<bool> logSamples = true;         // bLogSamples:Probe - one log line per sample
		inline std::atomic<std::uint32_t> intervalMs = 500;  // uIntervalMs:Probe - time between samples (500 = 2 Hz)
		inline std::atomic<float> roofRayStart = 100.0F;    // fRoofRayStart:Probe - ray origin above the feet, game units
		inline std::atomic<float> roofRayLength = 2048.0F;  // fRoofRayLength:Probe - how far up the roof ray looks
		inline std::atomic<float> groundRayLength = 256.0F; // fGroundRayLength:Probe - how far down the ground ray looks
		inline std::atomic<float> navmeshMaxDz = 160.0F;    // fNavmeshMaxDz:Probe - largest height gap to a navmesh triangle
	}

	namespace debug
	{
		inline std::uint32_t logLevel = 2;  // uLogLevel:Debug - info, the shipped default (rule 14)
	}

	void Init(const std::string& a_iniFileName);
	void ApplyLogLevel();
	const std::string& GetIniPath();
}
