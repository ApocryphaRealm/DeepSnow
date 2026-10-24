#include "PCH.h"

#include "Depth.h"

#include "Settings.h"
#include "utils/Logger.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace depth
{
	namespace
	{
		std::atomic<float> dtdHeight{ -1.0F };  // < 0 = not read
	}

	float RowFraction(surface::Kind a_kind)
	{
		using K = surface::Kind;
		switch (a_kind)
		{
		case K::kSnow: return settings::depth::snow.load();
		case K::kTrail: return settings::depth::trail.load();
		case K::kRoad: return settings::depth::road.load();
		case K::kObject: return settings::depth::object.load();
		case K::kOther: return settings::depth::other.load();
		default: return 0.0F;  // NONE and ICE
		}
	}

	float Percent(const surface::Reading& a_r, float a_packing, float a_snowfall)
	{
		if (a_r.kind == surface::Kind::kNone || a_r.kind == surface::Kind::kIce || a_r.sheltered) { return 0.0F; }
		const float row = RowFraction(a_r.kind);
		const float cover = a_r.kind == surface::Kind::kSnow ? std::clamp(a_r.cover, 0.0F, 1.0F) : 1.0F;
		const float remain = std::clamp(settings::trails::packedRemain.load(), 0.0F, 1.0F);
		const float pack = 1.0F - std::clamp(a_packing, 0.0F, 1.0F) * (1.0F - remain);
		return std::max(0.0F, 100.0F * row * cover * pack * (1.0F + std::max(0.0F, a_snowfall)));
	}

	int BandFor(float a_percent)
	{
		// The deepest band whose threshold is reached; thresholds need not be in order on the page.
		int best = 0;
		float bestDepth = -1.0F;
		for (int i = 0; i < settings::kBandCount; ++i)
		{
			const float d = settings::bands[i].depth.load();
			if (a_percent >= d && d > bestDepth && a_percent > 0.0F)
			{
				best = i + 1;
				bestDepth = d;
			}
		}
		return best;
	}

	float FullDepthUnits()
	{
		const float d = dtdHeight.load();
		return settings::depth::useDTDHeight && d > 0.0F ? d : settings::depth::fullDepthUnits.load();
	}

	const char* FullDepthSource() { return settings::depth::useDTDHeight && dtdHeight.load() > 0.0F ? "DTD" : "INI"; }

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
