#include "PCH.h"

#include "Depth.h"

#include "DepthMath.h"
#include "Settings.h"
#include "SnowDeformationAPI.h"
#include "utils/Logger.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace depth
{
	namespace
	{
		std::atomic<float> dtdHeight{ -1.0F };  // < 0 = not read
		std::atomic<const SnowDeformationAPI_V1*> sdApi{ nullptr };  // set once, never cleared (a static table in CommunityShaders.dll)

		Source Setting()
		{
			const std::uint32_t v = settings::depth::source.load();
			return v == 1 ? Source::kModel : v == 2 ? Source::kSnowDeformation : Source::kAuto;
		}
	}

	void ResolveSnowDeformation(const char* a_when)
	{
		if (sdApi.load()) { return; }
		static bool loggedMissing = false;
		// Every call of the API is read-only and safe from any thread (its header and author); GetModuleHandle takes no
		// reference, and CommunityShaders.dll stays loaded for the life of the process like every SKSE plugin.
		HMODULE cs = GetModuleHandleW(L"CommunityShaders.dll");
		if (!cs)
		{
			if (!loggedMissing) { logger::info("[depth] Community Shaders is not loaded ({}); Snow Deformation is not available - Deep Snow's own model is used", a_when); }
			loggedMissing = true;
			return;
		}
		const auto getApi = reinterpret_cast<SnowDeformation_GetAPI_t>(GetProcAddress(cs, "SnowDeformation_GetAPI"));
		if (!getApi)
		{
			if (!loggedMissing) { logger::info("[depth] Community Shaders is loaded but exports no SnowDeformation_GetAPI ({}): it does not carry Snow Deformation; Deep Snow's own model is used", a_when); }
			loggedMissing = true;
			return;
		}
		const auto* api = static_cast<const SnowDeformationAPI_V1*>(getApi(1));
		if (!api || api->version < 1 || !api->IsActive || !api->GetSnowDepthAtRef || !api->GetSnowDepthAt || !api->GetSnowAccumulation)
		{
			if (!loggedMissing)
			{
				logger::warn("[depth] Snow Deformation's API did not hand back a usable version-1 table ({}; table {}, version {}); Deep Snow's own model is used", a_when,
					static_cast<const void*>(api), api ? api->version : 0U);
			}
			loggedMissing = true;
			return;
		}
		sdApi = api;
		logger::info("[depth] Snow Deformation API found ({}): table version {}, active now: {}, accumulation {:.2f}", a_when, api->version, api->IsActive(), api->GetSnowAccumulation());
	}

	bool SnowDeformationFound() { return sdApi.load() != nullptr; }

	bool SnowDeformationActive()
	{
		const auto* api = sdApi.load();
		return api && api->IsActive();
	}

	Source ActiveSource()
	{
		const Source want = Setting();
		const bool sd = want != Source::kModel && SnowDeformationActive();
		const Source got = sd ? Source::kSnowDeformation : Source::kModel;
		// Transitions only (rule 14): the source can change at run time when the feature is switched on or off.
		static std::atomic<int> last{ -1 };
		const int key = static_cast<int>(want) * 10 + static_cast<int>(got);
		if (last.exchange(key) != key)
		{
			if (want == Source::kSnowDeformation && got == Source::kModel)
			{
				logger::warn("[depth] uSource=2 asks for Snow Deformation, but it is {}; Deep Snow's own model is used until it is on",
					SnowDeformationFound() ? "switched off" : "not installed");
			}
			else
			{
				logger::info("[depth] depth source: {} (setting {})", SourceName(got), SourceName(want));
			}
		}
		return got;
	}

	const char* SourceName(Source a_source)
	{
		switch (a_source)
		{
		case Source::kModel: return "model";
		case Source::kSnowDeformation: return "SnowDeformation";
		default: return "auto";
		}
	}

	float PercentFor(RE::Actor* a_actor, const surface::Reading& a_r, float a_packing, float a_snowfall, float* a_rawUnits)
	{
		if (a_rawUnits) { *a_rawUnits = -1.0F; }
		const auto* api = sdApi.load();
		if (!a_actor || !api || ActiveSource() != Source::kSnowDeformation) { return Percent(a_r, a_packing, a_snowfall); }
		using K = surface::Kind;
		math::SDInput in;
		in.exempt = a_r.kind == K::kNone || a_r.kind == K::kIce || a_r.kind == K::kRoad || a_r.sheltered;
		in.landscape = a_r.kind == K::kSnow || a_r.kind == K::kTrail || a_r.kind == K::kOther;
		in.objectRow = settings::depth::object.load();
		in.fullUnits = settings::depth::sdFullDepthUnits.load();
		in.packing = a_packing;
		in.packedRemain = settings::trails::packedRemain.load();
		// Asked only where it counts: exempt and object ground never need Snow Deformation's reading.
		if (!in.exempt && in.landscape)
		{
			in.units = api->GetSnowDepthAtRef(static_cast<void*>(a_actor));
			if (a_rawUnits) { *a_rawUnits = in.units; }
		}
		return math::PercentFromSD(in);
	}

	float RowFraction(surface::Kind a_kind)
	{
		using K = surface::Kind;
		switch (a_kind)
		{
		case K::kSnow: return settings::depth::snow.load();
		case K::kTrail: return settings::depth::trail.load();
		case K::kObject: return settings::depth::object.load();
		case K::kOther: return settings::depth::other.load();
		default: return 0.0F;  // NONE, ROAD and ICE
		}
	}

	float Percent(const surface::Reading& a_r, float a_packing, float a_snowfall)
	{
		// ROAD is always exempt (the owner, 2026-10-24: "roads should be safe from this kind of debuff") - no depth, so no
		// band and no drain, whatever cover, packing or snowfall say. ICE is Ice is Slick's.
		if (a_r.kind == surface::Kind::kNone || a_r.kind == surface::Kind::kIce || a_r.kind == surface::Kind::kRoad || a_r.sheltered) { return 0.0F; }
		const float row = RowFraction(a_r.kind);
		const float cover = a_r.kind == surface::Kind::kSnow ? std::clamp(a_r.cover, 0.0F, 1.0F) : 1.0F;
		const float remain = std::clamp(settings::trails::packedRemain.load(), 0.0F, 1.0F);
		const float pack = 1.0F - std::clamp(a_packing, 0.0F, 1.0F) * (1.0F - remain);
		return std::max(0.0F, 100.0F * row * cover * pack * (1.0F + std::max(0.0F, a_snowfall)));
	}

	int BandFor(float a_percent)
	{
		// The deepest band whose threshold is reached; thresholds need not be in order on the page (DepthMath.h).
		static_assert(settings::kBandCount == math::kBands);
		std::array<float, math::kBands> t{};
		for (int i = 0; i < math::kBands; ++i) { t[i] = settings::bands[i].depth.load(); }
		return math::BandFor(a_percent, t);
	}

	float FullDepthUnits()
	{
		if (ActiveSource() == Source::kSnowDeformation) { return settings::depth::sdFullDepthUnits.load(); }
		const float d = dtdHeight.load();
		return settings::depth::useDTDHeight && d > 0.0F ? d : settings::depth::fullDepthUnits.load();
	}

	const char* FullDepthSource()
	{
		if (ActiveSource() == Source::kSnowDeformation) { return "SnowDeformation"; }
		return settings::depth::useDTDHeight && dtdHeight.load() > 0.0F ? "DTD" : "INI";
	}

	void ReadDTD()
	{
		// DTD (Dynamic Terrain Deformation) keeps SnowRaiseHeight in its own INI; we only READ the number (plain file
		// I/O, which MO2's VFS serves). Nothing of DTD's is linked or written.
		const auto path = std::filesystem::current_path() / "Data" / "SKSE" / "Plugins" / "NMN_DeformableTerrain.ini";
		std::ifstream in(path);
		if (!in)
		{
			logger::info("[depth] DTD's INI is not installed ({}); a full blanket is shown as fFullDepthUnits={:.1f} units", path.string(),
				settings::depth::fullDepthUnits.load());
			return;
		}
		std::string line;
		while (std::getline(in, line))
		{
			const auto semi = line.find(';');
			if (semi != std::string::npos) { line.resize(semi); }
			const auto eq = line.find('=');
			if (eq == std::string::npos) { continue; }
			std::string key = line.substr(0, eq);
			key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char c) { return std::isspace(c); }), key.end());
			if (key != "SnowRaiseHeight") { continue; }
			try
			{
				const float v = std::stof(line.substr(eq + 1));
				if (v > 0.0F && v < 1000.0F) { dtdHeight = v; }
				logger::info("[depth] DTD SnowRaiseHeight = {:.1f} units: a full blanket (100 %) is shown as that", v);
			}
			catch (...)
			{
				logger::warn("[depth] DTD's SnowRaiseHeight is not a number (\"{}\"); using fFullDepthUnits", line.substr(eq + 1));
			}
			return;
		}
		logger::info("[depth] DTD's INI has no SnowRaiseHeight; using fFullDepthUnits={:.1f}", settings::depth::fullDepthUnits.load());
	}
}
