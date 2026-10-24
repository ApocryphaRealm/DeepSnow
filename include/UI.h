#pragma once

namespace UI
{
	// Adds Deep Snow's page to the menu framework (Apocrypha Menu Framework, or SKSE Menu Framework). Safe when neither
	// is installed or the framework is too old: it logs why and the mod runs from the INI alone. Call once, kPostPostLoad.
	void Register();

	namespace SettingsPanel
	{
		void __stdcall Render();
	}
}
