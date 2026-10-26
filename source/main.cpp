// Deep Snow - own code, GPL-3.0-or-later (2026-10-24). Deep snow slows you and costs a little stamina; packed trails
// and roads stay fast. Movement changes are SpeedMult ability effects from DeepSnow.esp, added while an actor stands in
// a depth band and removed when it leaves - never an absolute SpeedMult write - so they stack with Inertia, Dynamic
// Sprint and Survival Mode. Depth comes from Snow Deformation (Community Shaders) when it is installed and on, else our
// own model (Depth.h); trails from every actor are kept in the co-save.
// The milestone-1 spike probe (Probe.cpp) stays in the build, off by default.
#include "PCH.h"

#include "Depth.h"
#include "DevBenchTool.h"
#include "EditorIDs.h"
#include "Effects.h"
#include "Movement.h"
#include "Probe.h"
#include "Serialization.h"
#include "Settings.h"
#include "UI.h"

#include "utils/AddressLibraryGuard.h"
#include "utils/Logger.h"
#include "utils/Strings.h"

namespace
{
	void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) { return; }
		switch (a_msg->type)
		{
		case SKSE::MessagingInterface::kPostLoad:
			DevBenchTool::Init(false);
			break;
		case SKSE::MessagingInterface::kPostPostLoad:
			// Snow Deformation's author: resolve its API after kPostPostLoad, when every plugin is loaded.
			depth::ResolveSnowDeformation("kPostPostLoad");
			UI::Register();
			break;
		case SKSE::MessagingInterface::kDataLoaded:
			strings::Configure("DeepSnow");
			editor_ids::Start();  // reads the plugins' road / path editor IDs in the background, logs the count when done
			DevBenchTool::Init(true);
			depth::ReadDTD();
			depth::ResolveSnowDeformation("kDataLoaded");  // rule 17: a second try, logged once
			effects::Init();
			movement::Start();
			probe::Start();
			break;
		case SKSE::MessagingInterface::kPostLoadGame:
			logger::info("save loaded - Deep Snow re-applies its effects by the ground on the next tick");
			movement::OnLoaded();
			break;
		case SKSE::MessagingInterface::kNewGame:
			movement::OnLoaded();
			break;
		default:
			break;
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::log::init("DeepSnow");
	// Address Library pre-check (the guard every mod of ours carries), BEFORE SKSE::Init, which opens the Address
	// Library itself: a missing file gets a message naming it, not CommonLib's bare failure, and the plugin loads inert.
	if (!AddressLibraryGuard::Guard("Deep Snow"))
	{
		return true;
	}
	SKSE::Init(a_skse);

	settings::Init("DeepSnow.ini");
	settings::ApplyLogLevel();
	SKSE::log::describe_level("DeepSnow.ini");

	logger::info("Deep Snow {} loading", SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."));

	serialization::Install();
	const SKSE::MessagingInterface* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(MessageHandler))
	{
		logger::error("could not register the SKSE message listener; Deep Snow will not start");
		return true;
	}
	return true;
}
