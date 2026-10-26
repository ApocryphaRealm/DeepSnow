// Offline check of include\PluginEditorIDs.h - the plugin reader the DLL runs at kDataLoaded for the road rule.
//
// For each plugin given on the command line it prints the master list, how many LTEX / TXST / STAT records were read
// (and how many were compressed and skipped), the road / path names it would keep, and how long the read took.
// With --expect NAME it fails (exit 1) unless some plugin yields that editor ID (e.g. COTN_LRoadStone01 from
// Northern Roads.esp). Read-only: the plugins are opened for reading and nothing is written.

#include "PluginEditorIDs.h"
#include "SurfaceRules.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
	std::vector<std::string> files;
	std::string              expect;
	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		if (a == "--expect" && i + 1 < argc) { expect = argv[++i]; }
		else { files.push_back(a); }
	}
	if (files.empty())
	{
		std::printf("usage: editor_ids_check <plugin> [<plugin>...] [--expect EDID]\n");
		return 2;
	}
	const std::vector<std::uint32_t> types{ plugin_edid::Tag("LTEX"), plugin_edid::Tag("TXST"), plugin_edid::Tag("STAT") };
	bool                             sawExpected = false;
	int                              failures = 0;
	for (const std::string& f : files)
	{
		std::size_t kept = 0;
		const auto  t0 = std::chrono::steady_clock::now();
		std::vector<std::string> lines;
		const plugin_edid::Scan s = plugin_edid::Read(f, types, [&](std::uint32_t type, std::uint32_t id, std::string_view edid) {
			if (!surface::rules::HasRoad(edid) && !surface::rules::HasPath(edid)) { return; }
			++kept;
			char t[5] = { char(type), char(type >> 8), char(type >> 16), char(type >> 24), 0 };
			char buf[512];
			std::snprintf(buf, sizeof buf, "    %s %08X %.*s", t, id, int(edid.size()), edid.data());
			lines.emplace_back(buf);
			if (!expect.empty() && edid == expect) { sawExpected = true; }
		});
		const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		std::printf("%s\n  opened=%d masters=%zu records=%u compressed=%u kept=%zu time=%.1f ms%s%s\n", f.c_str(), int(s.opened), s.masters.size(),
			s.records, s.compressed, kept, ms, s.error.empty() ? "" : " error=", s.error.c_str());
		for (std::size_t m = 0; m < s.masters.size(); ++m) { std::printf("    master %02zX %s\n", m, s.masters[m].c_str()); }
		for (std::size_t i = 0; i < lines.size() && i < 40; ++i) { std::printf("%s\n", lines[i].c_str()); }
		if (lines.size() > 40) { std::printf("    ... %zu more\n", lines.size() - 40); }
		if (!s.opened || !s.error.empty()) { ++failures; }
	}
	if (!expect.empty())
	{
		std::printf("expected %s: %s\n", expect.c_str(), sawExpected ? "found" : "NOT FOUND");
		if (!sawExpected) { return 1; }
	}
	return failures ? 1 : 0;
}
