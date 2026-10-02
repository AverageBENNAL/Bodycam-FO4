#pragma once
// Named presets: plain copies of the MCM settings file, kept as Data\F4SE\Bodycam Presets\<name>.ini.
// Anything dropped in that folder can be stepped to and applied.
// Subtle, Default and Intense ship there too: the stock settings at each strength ([Main] iPreset).
//
// MCM side, all under [Presets] in MCM/Settings/Bodycam.ini:
//   sName      the Preset Name box: the preset that Apply loads and Save writes
//   iSaveTick  bumped by the Save button (Scripts\BodycamPresets.pex)
//   iLoadTick  bumped by the Apply button when the script cannot push the values into MCM itself
// The buttons count up instead of setting a flag because MCM keeps its own copy of every setting
// and only writes the file when a value differs from that copy. A flag cleared here would still
// read as set to MCM, so the second press would write nothing.
//
// There is no dropdown of presets. MCM fixes a dropdown's entries when the pause menu opens, so a
// preset saved a moment ago could not be in it, and changing the open menu from a script through
// F4SE's UI.Get deadlocked the game (AE, pause menu open). Previous / Next step through the folder
// instead and put the name in the box.
#include "Config.h"
#include "Logger.h"
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace Presets
{
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
		if (out.empty())
			return "My Preset";
		// CON, NUL, COM1 and the like are devices, not files, whatever the extension
		std::string stem = out.substr(0, out.find('.'));
		for (char& ch : stem)
			ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
		bool numbered = stem.size() == 4 && stem[3] >= '1' && stem[3] <= '9' && (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0);
		if (numbered || stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL")
			out.insert(0, "_");
		return out;
	}

	// The name in the Preset Name box, as a file name.
	inline std::string Current()
	{
		char typed[128]{};
		GetPrivateProfileStringA("Presets", "sName", "", typed, sizeof(typed), Config::UserPath().c_str());
		return Clean(typed);
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

	// Every preset in the folder, read fresh: the three shipped ones first, in strength order, then
	// the rest by name.
	inline std::vector<std::string> List()
	{
		std::vector<std::string> names;
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
				// Non-ASCII names come back in the ANSI codepage and MCM wants UTF-8: skipped.
				bool ascii = std::all_of(name.begin(), name.end(), [](unsigned char ch) { return ch >= 32 && ch < 127; });
				if (ascii && !name.empty())
					names.push_back(name);
			} while (FindNextFileA(h, &fd));
			FindClose(h);
		}
		auto rank = [](const std::string& n) { return n == "Subtle" ? 0 : n == "Default" ? 1 : n == "Intense" ? 2 : 3; };
		std::sort(names.begin(), names.end(), [&](const std::string& a, const std::string& b) {
			return rank(a) != rank(b) ? rank(a) < rank(b) : _stricmp(a.c_str(), b.c_str()) < 0;
		});
		return names;
	}

	// The preset before or after the one in the box, wrapping. A name that is not a preset yet
	// (typed, not saved) steps to the first or the last.
	inline std::string Step(int dir)
	{
		const std::vector<std::string> names = List();
		if (names.empty())
			return std::string();
		const std::string current = Current();
		int n = static_cast<int>(names.size()), at = -1;
		for (int i = 0; i < n; ++i)
			if (_stricmp(names[i].c_str(), current.c_str()) == 0)
				at = i;
		if (at < 0)
			return dir > 0 ? names.front() : names.back();
		return names[(at + dir + n) % n];
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
			CP_LOG("preset: no such preset %s", src.c_str());
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

	// The value queue is filled and drained on the script thread, the poll runs on mine.
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

	// Script side of Apply, see PapyrusBridge.h. One value per f/i/b key of the shipped
	// settings.ini, in file order: the preset's if it has the key, otherwise what is in effect
	// now, so a preset that only holds a few keys leaves the rest alone. The generated script
	// walks the same keys in the same order and checks the count before using any of it.
	inline float BridgeBegin()
	{
		std::lock_guard<std::mutex> guard(Lock());
		std::vector<float>& values = BridgeValues();
		// A second press of Apply while the first script is still taking values would have both
		// pulling from this one queue and landing them on each other's settings. Refuse it. The
		// time limit is for a script that died half way, so it cannot block Apply for good.
		static ULONGLONG begun = 0;
		if (BridgeCursor() < values.size() && GetTickCount64() - begun < 10000)
		{
			CP_LOG("preset: still applying the last one, ignored");
			return -1.0f;
		}
		begun = GetTickCount64();
		values.clear();
		BridgeCursor() = 0;
		const std::string defaults = Config::DefaultsPath(), user = Config::UserPath();
		const std::string src = PathOf(Current());
		if (GetFileAttributesA(src.c_str()) == INVALID_FILE_ATTRIBUTES)
		{
			CP_LOG("preset: no such preset %s", src.c_str());
			return 0.0f;
		}
		for (const std::string& sec : SectionNames(defaults))
			for (const std::string& line : SectionLines(defaults, sec))
			{
				size_t eq = line.find('=');
				if (eq == std::string::npos)
					continue;
				std::string key = line.substr(0, eq), val = line.substr(eq + 1);
				if (key.empty() || Skipped(sec, key) || !std::strchr("fib", key[0]))
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

	inline float BridgeValue()
	{
		std::lock_guard<std::mutex> guard(Lock());
		const std::vector<float>& values = BridgeValues();
		size_t& cursor = BridgeCursor();
		return cursor < values.size() ? values[cursor++] : 0.0f;
	}

	inline std::string BridgeNextName() { return Step(1); }
	inline std::string BridgePrevName() { return Step(-1); }

	// Called every settings poll. Returns true when it changed the player's file.
	inline bool Poll(bool settingsChanged)
	{
		std::lock_guard<std::mutex> guard(Lock());
		static bool started = false;
		static int lastSave = 0, lastLoad = 0;
		const std::string user = Config::UserPath();
		auto setting = [&](const char* key) { return static_cast<int>(GetPrivateProfileIntA("Presets", key, 0, user.c_str())); };

		if (!started)
		{
			started = true;
			lastSave = setting("iSaveTick");
			lastLoad = setting("iLoadTick");
			return false;
		}
		if (!settingsChanged)
			return false;

		int save = setting("iSaveTick"), load = setting("iLoadTick");
		if (save != lastSave)
		{
			lastSave = save;
			Save(Current());
		}
		if (load == lastLoad)
			return false;
		lastLoad = load;
		return Load(Current());
	}
}
