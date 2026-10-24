#pragma once

// Deep Snow - settings. Read with plain file I/O and written back in place (comments and unknown keys survive) - never
// the Win32 profile API, which MO2's VFS does not redirect (rule 16). Every compiled default below equals the value in
// dist\SKSE\Plugins\DeepSnow.ini, and RestoreDefaults() puts exactly these values back.
//
// Scalars are atomics: the page draws on the render thread, the tick runs on the game thread, the DevBench handler on
// its own listener thread. The surface lists are strings behind a mutex (Lists()).

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace settings
{
	inline constexpr int kBandCount = 5;

	namespace general
	{
		inline std::atomic<bool> enabled = true;             // bEnabled:General - the whole mechanism
		inline std::atomic<bool> affectNPCs = false;         // bAffectNPCs:General - NPCs and creatures are slowed and drained too
		inline std::atomic<std::uint32_t> tickMs = 250;      // uTickMs:General - time between ticks (250 = 4 a second)
		inline std::atomic<std::uint32_t> actorsPerTick = 6; // uActorsPerTick:General - NPCs classified per tick (round robin)
		inline std::atomic<float> scanRadius = 6000.0F;      // fScanRadius:General - NPCs farther from the player are ignored
		inline std::atomic<bool> refreshSpeed = true;        // bRefreshSpeed:General - nudge CarryWeight so a SpeedMult change takes hold
	}

	// The depth model (PLAN.md, "The depth model"): depth % = 100 x the surface row's depth x the snow cover under the
	// feet x (1 - trail packing x (1 - packed remain)) x (1 + snowfall boost); 0 under a roof.
	namespace depth
	{
		inline std::atomic<float> snow = 1.0F;           // fSnow:Depth - SNOW row: full blanket
		inline std::atomic<float> trail = 0.35F;         // fTrail:Depth - TRAIL row (snowy paths): a trench
		inline std::atomic<float> road = 0.0F;           // fRoad:Depth - ROAD row: no snow to wade
		inline std::atomic<float> object = 0.0F;         // fObject:Depth - standing on a mesh (rock, bridge, dock)
		inline std::atomic<float> other = 0.0F;          // fOther:Depth - landscape no list names (1.0 for an all-snow list)
		inline std::atomic<float> shelterHeight = 1024.0F;  // fShelterHeight:Depth - a roof within this many units overhead = no snow
		inline std::atomic<float> fullDepthUnits = 35.0F;   // fFullDepthUnits:Depth - shown units of a full blanket when DTD's INI is absent
		inline std::atomic<bool> useDTDHeight = true;       // bUseDTDHeight:Depth - read SnowRaiseHeight from DTD's INI for the shown units
	}

	struct Band
	{
		std::atomic<float> depth;  // fBandNDepth:Bands - the band starts at this depth % of a full blanket
		std::atomic<float> speed;  // fBandNSpeed:Bands - movement speed in the band, % of normal (SpeedMult points off = 100 - speed)
		std::atomic<float> drain;  // fBandNDrain:Bands - stamina points a second while moving in the band
	};
	inline std::array<Band, kBandCount> bands{ {
		{ 10.0F, 95.0F, 0.5F },
		{ 30.0F, 88.0F, 1.0F },
		{ 50.0F, 80.0F, 1.5F },
		{ 70.0F, 72.0F, 2.0F },
		{ 90.0F, 65.0F, 2.5F },
	} };
	struct BandDefault { float depth, speed, drain; };
	inline constexpr std::array<BandDefault, kBandCount> kBandDefaults{ { { 10.0F, 95.0F, 0.5F }, { 30.0F, 88.0F, 1.0F }, { 50.0F, 80.0F, 1.5F },
		{ 70.0F, 72.0F, 2.0F }, { 90.0F, 65.0F, 2.5F } } };

	namespace stamina
	{
		inline std::atomic<bool> drain = true;      // bDrain:Stamina - the per-band drain
		inline std::atomic<float> floor = 10.0F;    // fFloor:Stamina - never drain below this % of max stamina
	}

	namespace roads
	{
		inline std::atomic<bool> bonus = false;     // bBonus:Roads - faster on roads (optional, off by default)
		inline std::atomic<float> speed = 110.0F;   // fSpeed:Roads - speed on a road with the bonus on, % of normal
		inline std::atomic<bool> useNavmesh = true; // bUseNavmesh:Roads - the navmesh "preferred path" flag marks a road (player only)
	}

	namespace trails
	{
		inline std::atomic<bool> enabled = true;            // bEnabled:Trails - actors pack the snow they walk through
		inline std::atomic<float> packPerPass = 0.2F;       // fPackPerPass:Trails - packing one actor's pass adds (0..1)
		inline std::atomic<float> packedRemain = 0.25F;     // fPackedRemain:Trails - depth left on a fully packed trail (0..1)
		inline std::atomic<float> lifeHours = 24.0F;        // fLifeHours:Trails - game hours a full packing takes to fill back in
		inline std::atomic<float> snowRefill = 3.0F;        // fSnowRefill:Trails - fill-in runs this many times faster while it snows
		inline std::atomic<std::uint32_t> maxCells = 20000; // uMaxCells:Trails - most trail squares kept (oldest dropped first)
		inline std::atomic<float> cellSize = 128.0F;        // fCellSize:Trails - one trail square's side, game units
	}

	namespace snowfall
	{
		inline std::atomic<bool> deepens = false;    // bDeepens:Snowfall - snowing weather deepens the snow
		inline std::atomic<float> perHour = 10.0F;   // fPerHour:Snowfall - % deeper per game hour of snowfall (and back)
		inline std::atomic<float> max = 50.0F;       // fMax:Snowfall - most % deeper
	}

	namespace probe
	{
		inline std::atomic<bool> enabled = false;           // bEnabled:Probe - the milestone-1 spike probe (78b3 is done; off)
		inline std::atomic<bool> logSamples = true;         // bLogSamples:Probe - one log line per sample
		inline std::atomic<std::uint32_t> intervalMs = 500;  // uIntervalMs:Probe - time between samples (500 = 2 Hz)
		inline std::atomic<float> roofRayStart = 100.0F;    // fRoofRayStart:Probe - ray origin above the feet, game units
		inline std::atomic<float> roofRayLength = 2048.0F;  // fRoofRayLength:Probe - how far up the roof ray looks
		inline std::atomic<float> groundRayLength = 256.0F; // fGroundRayLength:Probe - how far down the ground ray looks
		inline std::atomic<float> navmeshMaxDz = 160.0F;    // fNavmeshMaxDz:Probe - largest height gap to a navmesh triangle
	}

	namespace debug
	{
		inline std::atomic<std::uint32_t> logLevel = 2;  // uLogLevel:Debug - info, the shipped default (rule 14)
	}

	// The surface lists: landscape texture file stems (no folder, no .dds, lower case; '*' matches anything) and, for
	// ICE, mesh path fragments. Comma-separated in the INI's [Surfaces] section.
	enum class List : int { kSnow = 0, kTrail, kRoad, kIce, kIceMesh, kCount };
	inline constexpr const char* kListKeys[] = { "sSnow", "sTrail", "sRoad", "sIce", "sIceMeshes" };
	inline constexpr const char* kListDefaults[] = { "snow01,snow02,snowrocks01", "dirtsnowpath01,*_trail*", "snowcobble01",
		"frozenmarshice01,frozenmarshice02", "icefloe,iceberg,frozenlake" };

	std::vector<std::string> GetList(List a_list);
	void SetList(List a_list, std::vector<std::string> a_items);
	std::string ListText(List a_list);  // comma-joined
	// Adds a_item to a_list and takes it out of every other texture list (a texture is one surface). True if it changed.
	bool Mark(List a_list, const std::string& a_item);
	std::uint64_t ListsGeneration();  // bumps on every list change, so the classifier knows to re-read

	void Init(const std::string& a_iniFileName);
	bool Reload();
	bool Save();
	void RestoreDefaults();
	void ApplyLogLevel();
	void Clamp();  // keeps every value inside its slider's range (after an INI read or a DevBench set)
	const std::string& GetIniPath();
	std::uint64_t Generation();  // bumps on every Reload / RestoreDefaults / page change, so magnitudes are re-applied
	void Touch();                // a page or DevBench change: bump Generation()
	// DevBench "set": one INI key by name ("fBand1Speed", or "Bands:fBand1Speed"), parsed and clamped as at load.
	bool SetValue(const std::string& a_key, const std::string& a_value);
	std::string GetValue(const std::string& a_key);  // empty when no such key
	std::string AllJson();                           // every key and its current value
}
