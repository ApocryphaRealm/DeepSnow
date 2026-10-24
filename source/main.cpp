// Deep Snow - own code, GPL-3.0-or-later (2026-10-24). Milestone 1, the spike: a read-only ground probe that logs, at
// 2 Hz, what the player stands on (landscape texture, navmesh preferred-path flag), whether a roof is overhead, and
// where the player is - so the deep-snow / packed-trail classifier can be checked against what the eye sees before
// anything changes movement. Nothing in the game is changed: no hook, no form edit, no effect.
#include "PCH.h"

#include "DevBenchTool.h"
#include "Probe.h"
#include "Settings.h"

#include "utils/AddressLibraryGuard.h"
#include "utils/Logger.h"

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
		case SKSE::MessagingInterface::kDataLoaded:
			DevBenchTool::Init(true);
			probe::Start();
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

	logger::info("Deep Snow {} loading (spike: read-only ground probe, nothing in the game is changed)",
		SKSE::PluginDeclaration::GetSingleton()->GetVersion().string("."));

	const SKSE::MessagingInterface* messaging = SKSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(MessageHandler))
	{
		logger::error("could not register the SKSE message listener; the probe will not start");
		return true;
	}
	return true;
}
