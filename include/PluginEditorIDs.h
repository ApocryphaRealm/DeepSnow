#pragma once

// Deep Snow - reads editor IDs (EDID) straight out of a plugin file (.esm / .esp / .esl), standard C++ only, so the
// offline check (tools\editor_ids_check.cpp) runs the exact code the DLL runs.
//
// Why: the game drops the editor IDs of landscape textures (LTEX), texture sets (TXST) and statics (STAT) at load, and
// a wrapper on TESForm::SetFormEditorID (vtable slot 0x33) never sees them either - 1.1.0's wrapper, installed at
// plugin load, kept 0 names in six runs on 1.5.97. So Deep Snow reads the plugins itself once the data has loaded.
//
// What it reads: the TES4 header's master list (MAST), then only the top-level groups whose label is one of the wanted
// record types; every other group is skipped by its size without being read. For each wanted record it reads the
// EDID subrecord and hands (record type, file-relative FormID, editor ID) to the callback. A compressed record
// (flag 0x00040000) is counted and skipped - its subrecords are zlib data, and these three types are almost never
// compressed. A deleted record (flag 0x20) is skipped.

#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace plugin_edid
{
	constexpr std::uint32_t Tag(const char (&a)[5])
	{
		return static_cast<std::uint32_t>(static_cast<unsigned char>(a[0])) | static_cast<std::uint32_t>(static_cast<unsigned char>(a[1])) << 8 |
		       static_cast<std::uint32_t>(static_cast<unsigned char>(a[2])) << 16 | static_cast<std::uint32_t>(static_cast<unsigned char>(a[3])) << 24;
	}

	struct Scan
	{
		bool                     opened = false;      // the file could be opened and starts with a TES4 record
		std::vector<std::string> masters;             // MAST entries, in order (a FormID's top byte indexes this list)
		std::uint32_t            records = 0;         // wanted records seen
		std::uint32_t            compressed = 0;      // wanted records skipped because they are compressed
		std::string              error;               // why the read stopped early, if it did
	};

	// a_onRecord(type, fileFormID, editorID). Reads only the top-level groups labelled with one of a_types.
	inline Scan Read(const std::filesystem::path& a_file, const std::vector<std::uint32_t>& a_types,
		const std::function<void(std::uint32_t, std::uint32_t, std::string_view)>& a_onRecord)
	{
		Scan          out;
		std::ifstream in(a_file, std::ios::binary);
		if (!in) { return out; }

		auto readHeader = [&](unsigned char (&h)[24]) { return static_cast<bool>(in.read(reinterpret_cast<char*>(h), 24)); };
		auto u32 = [](const unsigned char* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; };
		auto u16 = [](const unsigned char* p) { std::uint16_t v; std::memcpy(&v, p, 2); return v; };
		constexpr std::uint32_t GRUP = Tag("GRUP");
		constexpr std::uint32_t TES4 = Tag("TES4");
		constexpr std::uint32_t EDID = Tag("EDID");
		constexpr std::uint32_t MAST = Tag("MAST");
		constexpr std::uint32_t XXXX = Tag("XXXX");
		constexpr std::uint32_t kCompressed = 0x00040000;
		constexpr std::uint32_t kDeleted = 0x00000020;

		std::vector<unsigned char> data;

		// Walks a record's subrecords; returns false to stop. XXXX gives the next subrecord's 32-bit size.
		auto eachSub = [&](const std::vector<unsigned char>& d, auto&& fn) {
			std::size_t   pos = 0;
			std::uint32_t bigSize = 0;
			while (pos + 6 <= d.size())
			{
				const std::uint32_t type = u32(&d[pos]);
				std::uint32_t       size = u16(&d[pos + 4]);
				pos += 6;
				if (type == XXXX && size == 4 && pos + 4 <= d.size())
				{
					bigSize = u32(&d[pos]);
					pos += 4;
					continue;
				}
				if (bigSize) { size = bigSize, bigSize = 0; }
				if (pos + size > d.size()) { return; }
				if (!fn(type, &d[pos], size)) { return; }
				pos += size;
			}
		};
		auto zstring = [](const unsigned char* p, std::uint32_t size) {
			std::size_t n = 0;
			while (n < size && p[n] != 0) { ++n; }
			return std::string_view(reinterpret_cast<const char*>(p), n);
		};

		// The TES4 header record.
		unsigned char h[24];
		if (!readHeader(h) || u32(h) != TES4) { return out; }
		out.opened = true;
		data.resize(u32(h + 4));
		if (!data.empty() && !in.read(reinterpret_cast<char*>(data.data()), data.size()))
		{
			out.error = "header record cut short";
			return out;
		}
		eachSub(data, [&](std::uint32_t type, const unsigned char* p, std::uint32_t size) {
			if (type == MAST) { out.masters.emplace_back(zstring(p, size)); }
			return true;
		});

		auto wanted = [&](std::uint32_t label) {
			for (const std::uint32_t t : a_types)
			{
				if (t == label) { return true; }
			}
			return false;
		};

		// Top-level groups: header 24 bytes, its size includes the header; label = the record type it holds.
		while (readHeader(h))
		{
			if (u32(h) != GRUP)
			{
				out.error = "a top-level entry is not a group";
				return out;
			}
			const std::uint32_t groupSize = u32(h + 4);
			const std::uint32_t label = u32(h + 8);
			if (groupSize < 24)
			{
				out.error = "a group size is smaller than its header";
				return out;
			}
			const std::streamoff groupEnd = static_cast<std::streamoff>(in.tellg()) + (groupSize - 24);
			if (!wanted(label) || u32(h + 12) != 0)
			{
				in.seekg(groupEnd);
				continue;
			}
			// The records of a wanted group (a nested group is skipped whole).
			while (static_cast<std::streamoff>(in.tellg()) < groupEnd && readHeader(h))
			{
				const std::uint32_t type = u32(h);
				const std::uint32_t size = u32(h + 4);
				if (type == GRUP)
				{
					if (size < 24) { break; }
					in.seekg(size - 24, std::ios::cur);
					continue;
				}
				const std::uint32_t flags = u32(h + 8);
				const std::uint32_t formID = u32(h + 12);
				if (!wanted(type) || (flags & kDeleted))
				{
					in.seekg(size, std::ios::cur);
					continue;
				}
				++out.records;
				if (flags & kCompressed)
				{
					++out.compressed;
					in.seekg(size, std::ios::cur);
					continue;
				}
				data.resize(size);
				if (size && !in.read(reinterpret_cast<char*>(data.data()), size))
				{
					out.error = "a record is cut short";
					return out;
				}
				eachSub(data, [&](std::uint32_t sub, const unsigned char* p, std::uint32_t subSize) {
					if (sub != EDID) { return true; }
					const std::string_view edid = zstring(p, subSize);
					if (!edid.empty()) { a_onRecord(type, formID, edid); }
					return false;
				});
			}
			in.clear();
			in.seekg(groupEnd);
		}
		return out;
	}
}
