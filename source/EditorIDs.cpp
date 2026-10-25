#include "PCH.h"

#include "EditorIDs.h"

#include "SurfaceRules.h"
#include "utils/Logger.h"

#include <mutex>
#include <unordered_map>

namespace editor_ids
{
	namespace
	{
		std::mutex lock;
		std::unordered_map<RE::FormID, std::string> names;  // guarded by lock

		void Keep(RE::TESForm* a_form, const char* a_str, bool a_onlyRoads)
		{
			if (!a_form || !a_str || *a_str == '\0') { return; }
			const RE::FormID id = a_form->GetFormID();
			if (id == 0 || id >= 0xFF000000) { return; }  // not loaded from a plugin
			// "path" names are kept too, for [Roads] bPathsAreRoads (a setting that can change while the game runs).
			if (a_onlyRoads && !surface::rules::HasRoad(a_str) && !surface::rules::HasPath(a_str)) { return; }
			std::lock_guard g(lock);
			names[id] = a_str;  // a later plugin's override renames it
		}

		// One wrapper per vtable, each with its own original (SetFormEditorID is TESForm virtual 0x33).
		template <class T, bool OnlyRoads>
		struct SetEditorID
		{
			static bool Thunk(RE::TESForm* a_this, const char* a_str)
			{
				Keep(a_this, a_str, OnlyRoads);
				return original(a_this, a_str);
			}
			static inline REL::Relocation<decltype(Thunk)> original;

			static void Install(const char* a_name)
			{
				REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[0] };
				original = vtbl.write_vfunc(0x33, Thunk);
				logger::info("[editorIDs] SetFormEditorID wrapped on the {} vtable at 0x{:X} (original 0x{:X})", a_name, vtbl.address(), original.address());
			}
		};
	}

	void Install()
	{
		SetEditorID<RE::TESLandTexture, false>::Install("TESLandTexture");
		SetEditorID<RE::BGSTextureSet, false>::Install("BGSTextureSet");
		SetEditorID<RE::TESObjectSTAT, true>::Install("TESObjectSTAT");
	}

	std::string Get(const RE::TESForm* a_form)
	{
		if (!a_form) { return {}; }
		{
			std::lock_guard g(lock);
			if (const auto it = names.find(a_form->GetFormID()); it != names.end()) { return it->second; }
		}
		const char* own = a_form->GetFormEditorID();
		return own ? std::string(own) : std::string();
	}

	std::size_t Count()
	{
		std::lock_guard g(lock);
		return names.size();
	}
}
