#include "PCH.h"

#include "Serialization.h"

#include "Effects.h"
#include "Movement.h"
#include "Trails.h"
#include "utils/Logger.h"

namespace serialization
{
	namespace
	{
		std::string TypeText(std::uint32_t a_type)
		{
			std::string s;
			for (int i = 3; i >= 0; --i)
			{
				const char c = static_cast<char>((a_type >> (i * 8)) & 0xFF);
				s += (c >= 0x20 && c < 0x7F) ? c : '?';
			}
			return s;
		}

		void OnSave(SKSE::SerializationInterface* a_intfc)
		{
			logger::debug("[co-save] saving");
			trails::Save(a_intfc);
			effects::Save(a_intfc);
			movement::Save(a_intfc);
		}

		void OnLoad(SKSE::SerializationInterface* a_intfc)
		{
			if (!a_intfc) { return; }
			std::uint32_t type = 0, version = 0, length = 0;
			int records = 0;
			while (a_intfc->GetNextRecordInfo(type, version, length))
			{
				++records;
				switch (type)
				{
				case 'TRLS': trails::Load(a_intfc, version, length); break;
				case 'APLD': effects::Load(a_intfc, version, length); break;
				case 'SNFL': movement::Load(a_intfc, version, length); break;
				default:
					logger::warn("[co-save] unknown record '{}' v{} ({} bytes) skipped - written by a newer Deep Snow?", TypeText(type), version, length);
					break;
				}
			}
			logger::info("[co-save] load done: {} record(s)", records);
		}

		void OnRevert(SKSE::SerializationInterface*)
		{
			trails::Clear();
			effects::ForgetAll();
			movement::Revert();
			logger::info("[co-save] revert: trails, remembered actors and the snowfall boost cleared");
		}
	}

	void Install()
	{
		const SKSE::SerializationInterface* s = SKSE::GetSerializationInterface();
		if (!s)
		{
			logger::error("[co-save] SKSE serialization interface is null: trails will not survive a save");
			return;
		}
		s->SetUniqueID('DPSN');
		s->SetSaveCallback(OnSave);
		s->SetLoadCallback(OnLoad);
		s->SetRevertCallback(OnRevert);
		logger::info("[co-save] callbacks registered (id DPSN; records TRLS v1, APLD v1, SNFL v1)");
	}
}
