#pragma once
// Named presets: plain copies of the MCM settings file, kept as Data\F4SE\Bodycam Presets\<name>.ini.
// Anything dropped in that folder shows up in the MCM list.
// Subtle, Default and Intense ship there too: the stock settings at each strength ([Main] iPreset).
//
// MCM side, all under [Presets] in MCM/Settings/Bodycam.ini:
//   sName      the name typed for saving
//   iPick      index into the Saved Presets dropdown
//   iSaveTick  bumped by the Save button (Scripts\BodycamPresets.pex)
//   iLoadTick  bumped by the Load button when the script cannot push the values into MCM itself
// The buttons count up instead of setting a flag because MCM keeps its own copy of every setting
// and only writes the file when a value differs from that copy. A flag cleared here would still
// read as set to MCM, so the second press would write nothing.
//
// MCM dropdowns are fixed lists in config.json, so the list is kept current by rewriting the
// options of iPick in the installed config.json.
#include "Config.h"
#include "Logger.h"
#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace Presets
{
	inline const char* kNone = "(none saved yet)";

	// Feel only. Keybinds, controller mode, logging and the install stamp belong to the player,
	// not to whoever made the preset.
	inline bool Skipped(const std::string& sec, const std::string& key)
	{
		if (sec == "Keys" || sec == "Debug" || sec == "Install" || sec == "Presets")
			return true;
		return sec == "Main" && (key == "bEnabled" || key == "bControllerMode");
	}

	inline std::string Dir() { return Config::DataPath("F4SE\\Bodycam Presets"); }
	inline std::string PathOf(const std::string& name) { return Dir() + "\\" + name + ".ini"; }
	inline std::string MenuPath() { return Config::DataPath("MCM\\Config\\Bodycam\\config.json"); }

	// Dropdown order. Sorted once at startup, then only appended to: MCM holds iPick as an index,
	// so reordering under an open menu would point it at a different preset.
	inline std::vector<std::string>& Names()
	{
		static std::vector<std::string> names;
		return names;
	}

	// A typed name as a file name.
	inline std::string Clean(std::string s)
	{
		std::string out;
		for (unsigned char ch : s)
			if (ch >= 32 && ch < 127 && !std::strchr("<>:\"/\\|?*", ch))
				out += static_cast<char>(ch);
		while (!out.empty() && (out.back() == ' ' || out.back() == '.'))
			out.pop_back();
		size_t lead = out.find_first_not_of(' ');
		out = lead == std::string::npos ? std::string() : out.substr(lead);
		if (out.size() > 60)
			out.resize(60);
		return out.empty() ? std::string("My Preset") : out;
	}

	// "key=value" lines of one section, in file order.
	inline std::vector<std::string> SectionLines(const std::string& path, const std::string& sec)
	{
		std::vector<std::string> out;
		std::vector<char> buf(32768);
		DWORD n = GetPrivateProfileSectionA(sec.c_str(), buf.data(), static_cast<DWORD>(buf.size()), path.c_str());
		for (const char* p = buf.data(); p < buf.data() + n && *p; p += std::strlen(p) + 1)
			out.emplace_back(p);
		return out;
	}

	inline std::vector<std::string> SectionNames(const std::string& path)
	{
		std::vector<std::string> out;
		std::vector<char> buf(8192);
		DWORD n = GetPrivateProfileSectionNamesA(buf.data(), static_cast<DWORD>(buf.size()), path.c_str());
		for (const char* p = buf.data(); p < buf.data() + n && *p; p += std::strlen(p) + 1)
			out.emplace_back(p);
		return out;
	}

	inline bool ReadAll(const std::string& path, std::string& out)
	{
		HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
		if (h == INVALID_HANDLE_VALUE)
			return false;
		DWORD size = GetFileSize(h, nullptr), got = 0;
		out.resize(size);
		bool ok = size == 0 || (ReadFile(h, out.data(), size, &got, nullptr) && got == size);
		CloseHandle(h);
		return ok;
	}

	// Rewrites the options of the iPick dropdown in the installed config.json. Truncates the
	// existing file rather than replacing it, so under MO2 it stays in the mod's own folder.
	inline void WriteList()
	{
		const std::string path = MenuPath();
		std::string text;
		if (!ReadAll(path, text))
			return;
		size_t id = text.find("\"iPick:Presets\"");
		size_t open = id == std::string::npos ? id : text.find("\"options\": [", id);
		if (open == std::string::npos)
		{
			CP_LOG("preset: no iPick dropdown in %s", path.c_str());
			return;
		}
		open += std::strlen("\"options\": [");
		size_t close = open;
		for (bool quoted = false; close < text.size(); ++close)
		{
			char ch = text[close];
			if (quoted && ch == '\\')
				++close;
			else if (ch == '"')
				quoted = !quoted;
			else if (ch == ']' && !quoted)
				break;
		}
		if (close >= text.size())
			return;

		std::string list = "\n";
		const std::vector<std::string>& names = Names();
		size_t count = names.empty() ? 1 : names.size();
		for (size_t i = 0; i < count; ++i)
			list += "              \"" + (names.empty() ? std::string(kNone) : names[i]) + (i + 1 < count ? "\",\n" : "\"\n");
		list += "            ";
		if (text.compare(open, close - open, list) == 0)
			return;
		text.replace(open, close - open, list);

		HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, 0, nullptr);
		if (h == INVALID_HANDLE_VALUE)
		{
			CP_LOG("preset: cannot write %s (error %lu)", path.c_str(), GetLastError());
			return;
		}
		DWORD put = 0;
		WriteFile(h, text.data(), static_cast<DWORD>(text.size()), &put, nullptr);
		CloseHandle(h);
		CP_LOG("preset: menu list now has %d presets", static_cast<int>(names.size()));
	}

	// Picks up files added to the folder. Returns true when the list grew.
	inline bool Scan(bool sort)
	{
		std::vector<std::string>& names = Names();
		size_t before = names.size();
		WIN32_FIND_DATAA fd{};
		HANDLE h = FindFirstFileA((Dir() + "\\*.ini").c_str(), &fd);
		if (h != INVALID_HANDLE_VALUE)
		{
			do
			{
				if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
					continue;
				std::string name(fd.cFileName);
				name.resize(name.size() - 4);
				// No quotes or backslashes can be in a file name, so it goes into the json as is.
				// Non-ASCII names come back in the ANSI codepage, which is not valid json: skipped.
				bool ascii = std::all_of(name.begin(), name.end(), [](unsigned char ch) { return ch >= 32 && ch < 127; });
				if (ascii && !name.empty() && std::find(names.begin(), names.end(), name) == names.end())
					names.push_back(name);
			} while (FindNextFileA(h, &fd));
			FindClose(h);
		}
		if (sort)
		{
			// the three shipped ones first, in strength order, then the rest by name
			auto rank = [](const std::string& n) { return n == "Subtle" ? 0 : n == "Default" ? 1 : n == "Intense" ? 2 : 3; };
			std::sort(names.begin(), names.end(), [&](const std::string& a, const std::string& b) {
				return rank(a) != rank(b) ? rank(a) < rank(b) : a < b;
			});
		}
		return names.size() != before;
	}

	// Every key the defaults file knows, at the value in effect now (the player's file wins).
	inline bool Save(const std::string& name)
	{
		const std::string defaults = Config::DefaultsPath(), user = Config::UserPath(), dst = PathOf(name);
		CreateDirectoryA(Dir().c_str(), nullptr);
		DeleteFileA(dst.c_str());
		int written = 0;
		for (const std::string& sec : SectionNames(defaults))
			for (const std::string& line : SectionLines(defaults, sec))
			{
				size_t eq = line.find('=');
				if (eq == std::string::npos)
					continue;
				std::string key = line.substr(0, eq), val = line.substr(eq + 1);
				if (Skipped(sec, key))
					continue;
				char buf[256]{};
				if (GetPrivateProfileStringA(sec.c_str(), key.c_str(), "", buf, sizeof(buf), user.c_str()) && buf[0])
					val = buf;
				WritePrivateProfileStringA(sec.c_str(), key.c_str(), val.c_str(), dst.c_str());
				++written;
			}
		CP_LOG("preset: saved %d settings to %s", written, dst.c_str());
		return written > 0;
	}

	inline bool Load(const std::string& name)
	{
		const std::string src = PathOf(name), user = Config::UserPath();
		if (GetFileAttributesA(src.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			CP_LOG("preset: %s is gone", src.c_str());
			return false;
		}
		int written = 0;
		for (const std::string& sec : SectionNames(src))
			for (const std::string& line : SectionLines(src, sec))
			{
				size_t eq = line.find('=');
				if (eq == std::string::npos)
					continue;
				std::string key = line.substr(0, eq);
				if (Skipped(sec, key))
					continue;
				WritePrivateProfileStringA(sec.c_str(), key.c_str(), line.substr(eq + 1).c_str(), user.c_str());
				++written;
			}
		CP_LOG("preset: loaded %d settings from %s", written, src.c_str());
		return written > 0;
	}

	// The list is read by the script thread (BridgeBegin) and changed by the settings poll.
	inline std::mutex& Lock()
	{
		static std::mutex m;
		return m;
	}

	inline std::vector<float>& BridgeValues()
	{
		static std::vector<float> values;
		return values;
	}
	inline size_t& BridgeCursor()
	{
		static size_t cursor = 0;
		return cursor;
	}

	// Script side of a load, see PapyrusBridge.h. One value per f/i/b key of the shipped
	// settings.ini, in file order: the preset's if it has the key, otherwise what is in effect
	// now, so a preset that only holds a few keys leaves the rest alone. The generated script
	// walks the same keys in the same order and checks the count before using any of it.
	inline float BridgeBegin()
	{
		std::lock_guard<std::mutex> guard(Lock());
		std::vector<float>& values = BridgeValues();
		values.clear();
		BridgeCursor() = 0;
		const std::string defaults = Config::DefaultsPath(), user = Config::UserPath();
		int pick = static_cast<int>(GetPrivateProfileIntA("Presets", "iPick", 0, user.c_str()));
		const std::vector<std::string>& names = Names();
		if (pick < 0 || pick >= static_cast<int>(names.size()))
			return 0.0f;
		const std::string src = PathOf(names[pick]);
		if (GetFileAttributesA(src.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			CP_LOG("preset: %s is gone", src.c_str());
			return 0.0f;
		}
		for (const std::string& sec : SectionNames(defaults))
			for (const std::string& line : SectionLines(defaults, sec))
			{
				size_t eq = line.find('=');
				if (eq == std::string::npos)
					continue;
				std::string key = line.substr(0, eq), val = line.substr(eq + 1);
				if (Skipped(sec, key) || !std::strchr("fib", key[0]))
					continue;
				char buf[64]{};
				if ((GetPrivateProfileStringA(sec.c_str(), key.c_str(), "", buf, sizeof(buf), src.c_str()) && buf[0])
					|| (GetPrivateProfileStringA(sec.c_str(), key.c_str(), "", buf, sizeof(buf), user.c_str()) && buf[0]))
					val = buf;
				values.push_back(static_cast<float>(std::atof(val.c_str())));
			}
		CP_LOG("preset: handing %d values from %s to MCM", static_cast<int>(values.size()), src.c_str());
		return static_cast<float>(values.size());
	}

	inline float BridgeNext()
	{
		std::lock_guard<std::mutex> guard(Lock());
		const std::vector<float>& values = BridgeValues();
		size_t& cursor = BridgeCursor();
		return cursor < values.size() ? values[cursor++] : 0.0f;
	}

	// Called every settings poll. Returns true when it changed the player's file.
	inline bool Poll(bool settingsChanged)
	{
		std::lock_guard<std::mutex> guard(Lock());
		static bool started = false;
		static int lastSave = 0, lastLoad = 0;
		static ULONGLONG lastScan = 0;
		const std::string user = Config::UserPath();
		auto setting = [&](const char* key) { return static_cast<int>(GetPrivateProfileIntA("Presets", key, 0, user.c_str())); };

		if (!started)
		{
			started = true;
			lastSave = setting("iSaveTick");
			lastLoad = setting("iLoadTick");
			lastScan = GetTickCount64();
			Scan(true);
			WriteList();
			return false;
		}
		if (GetTickCount64() - lastScan > 2000)
		{
			lastScan = GetTickCount64();
			if (Scan(false))
				WriteList();
		}
		if (!settingsChanged)
			return false;

		int save = setting("iSaveTick"), load = setting("iLoadTick");
		if (save != lastSave)
		{
			lastSave = save;
			char typed[128]{};
			GetPrivateProfileStringA("Presets", "sName", "", typed, sizeof(typed), user.c_str());
			if (Save(Clean(typed)) && Scan(false))
				WriteList();
		}
		if (load == lastLoad)
			return false;
		lastLoad = load;
		int pick = setting("iPick");
		const std::vector<std::string>& names = Names();
		if (pick < 0 || pick >= static_cast<int>(names.size()))
		{
			CP_LOG("preset: nothing to load (pick %d of %d)", pick, static_cast<int>(names.size()));
			return false;
		}
		return Load(names[pick]);
	}
}
