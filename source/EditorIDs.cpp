#include "PCH.h"

#include "EditorIDs.h"

#include "PluginEditorIDs.h"
#include "SurfaceRules.h"
#include "utils/Logger.h"

#include <chrono>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace editor_ids
{
	namespace
	{
		std::mutex                                  lock;
		std::unordered_map<RE::FormID, std::string> names;  // guarded by lock

		// The game folder's Data directory (the exe's folder + Data). Under Mod Organizer 2 the virtual file system
		// answers opens there with the mod folders' files.
		std::filesystem::path DataFolder()
		{
			std::wstring buf(MAX_PATH, L'\0');
			for (;;)
			{
				const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
				if (n == 0) { return std::filesystem::path(L"Data"); }
				if (n < buf.size())
				{
					buf.resize(n);
					break;
				}
				buf.resize(buf.size() * 2);
			}
			return std::filesystem::path(buf).parent_path() / L"Data";
		}

		// The runtime FormID of a record: its top byte picks the owning file (a master, or the plugin itself when the
		// index is past the master list); a light owner takes the FE xxx index and keeps the low 12 bits.
		RE::FormID RuntimeID(RE::TESDataHandler* a_dh, const RE::TESFile* a_self, const std::vector<std::string>& a_masters, std::uint32_t a_fileID)
		{
			const std::uint32_t index = a_fileID >> 24;
			const RE::TESFile*  owner = index < a_masters.size() ? a_dh->LookupLoadedModByName(a_masters[index]) : a_self;
			if (!owner) { return 0; }
			if (owner->IsLight()) { return 0xFE000000u | (static_cast<std::uint32_t>(owner->GetSmallFileCompileIndex()) << 12) | (a_fileID & 0xFFF); }
			if (owner->GetCompileIndex() == 0xFF) { return 0; }
			return (static_cast<std::uint32_t>(owner->GetCompileIndex()) << 24) | (a_fileID & 0xFFFFFF);
		}

		void ReadAll()
		{
			const auto start = std::chrono::steady_clock::now();
			auto*      dh = RE::TESDataHandler::GetSingleton();
			if (!dh)
			{
				logger::warn("[editorIDs] no data handler - no editor IDs read; the road rule uses texture paths and the game's own names only");
				return;
			}
			const std::filesystem::path data = DataFolder();
			const std::vector<std::uint32_t> types{ plugin_edid::Tag("LTEX"), plugin_edid::Tag("TXST"), plugin_edid::Tag("STAT") };

			std::size_t plugins = 0, unopened = 0, records = 0, compressed = 0, unresolved = 0, stopped = 0;
			std::size_t kept[3] = { 0, 0, 0 };  // LTEX, TXST, STAT
			struct Found
			{
				std::uint32_t type;
				std::uint32_t fileID;
				std::string   edid;
			};
			std::vector<Found> found;
			for (const RE::TESFile* file : dh->files)  // load order; a later plugin's override renames a form
			{
				if (!file || file->GetCompileIndex() == 0xFF) { continue; }  // not active
				++plugins;
				found.clear();
				const std::string fileName(file->GetFilename());
				const plugin_edid::Scan scan = plugin_edid::Read(data / std::filesystem::path(fileName), types,
					[&](std::uint32_t a_type, std::uint32_t a_fileID, std::string_view a_edid) {
						// Only names the road rule can use: "road", or "path" for [Roads] bPathsAreRoads (which can
						// change while the game runs).
						if (surface::rules::HasRoad(a_edid) || surface::rules::HasPath(a_edid)) { found.push_back({ a_type, a_fileID, std::string(a_edid) }); }
					});
				if (!scan.opened)
				{
					if (++unopened <= 5) { logger::warn("[editorIDs] could not open or read {} - its editor IDs are not kept", fileName); }
					continue;
				}
				if (!scan.error.empty() && ++stopped <= 5) { logger::warn("[editorIDs] {}: {} - kept what was read before that", fileName, scan.error); }
				records += scan.records;
				compressed += scan.compressed;
				for (Found& f : found)
				{
					const RE::FormID id = RuntimeID(dh, file, scan.masters, f.fileID);
					if (id == 0)
					{
						++unresolved;
						continue;
					}
					++kept[f.type == types[0] ? 0 : f.type == types[1] ? 1 : 2];
					std::lock_guard g(lock);
					names[id] = std::move(f.edid);
				}
			}
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
			logger::info("[editorIDs] {} editor IDs kept for the road rule (road/path names: {} landscape textures, {} texture sets, {} statics) "
						 "from {} plugins in {} ms; {} LTEX/TXST/STAT records read, {} compressed skipped, {} unresolved, {} plugins unreadable",
				Count(), kept[0], kept[1], kept[2], plugins, ms, records, compressed, unresolved, unopened);
		}
	}

	void Start()
	{
		std::thread(ReadAll).detach();
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
