#include "F4SEPluginApiMinimal.h"

#include "Bodycam.h"
#include "Config.h"
#include "Logger.h"

#include <windows.h>
#include <thread>
#include <atomic>

// Stamped into the load line so a log always names the build it came from.
#ifndef BODYCAM_VERSION
#define BODYCAM_VERSION "dev"
#endif

namespace
{
	HMODULE g_selfModule = nullptr;
	std::atomic<bool> g_running{ false };

	void MaintenanceThread()
	{
		while (g_running)
		{
			Bodycam::MaintainHook();
			Sleep(300);
		}
	}
}

extern "C"
{
#if defined(BODYCAM_OG)
	// Old-gen F4SE (0.6.x) asks every plugin whether it supports this runtime before loading it.
	// Each build hardcodes one exe's addresses, so refuse anything but 1.10.163 - the wrong build
	// then simply doesn't load instead of patching random memory. (F4SE 0.7.x never loads this
	// DLL: it requires F4SEPlugin_Version, which only the AE and NG builds export.)
	__declspec(dllexport) bool F4SEPlugin_Query(const F4SEInterface* f4se, PluginInfo* info)
	{
		info->infoVersion = PluginInfo::kInfoVersion;
		info->name = "Bodycam";
		info->version = 1;
		if (f4se->isEditor || f4se->runtimeVersion != kRuntimeVersion_1_10_163)
		{
			Logger::Get().Init(g_selfModule);
			CP_LOG("Bodycam (Old-Gen build) needs Fallout 4 1.10.163 - this game reports runtime 0x%08X. "
				"Reinstall Bodycam and pick the matching game version in the installer.", f4se->runtimeVersion);
			return false;
		}
		return true;
	}
#else
	__declspec(dllexport) F4SEPluginVersionData F4SEPlugin_Version =
	{
		F4SEPluginVersionData::kVersion,

		1,
		"Bodycam",
		"AverageBENNAL",

		0, // addressIndependence: we use hardcoded (RE-derived) addresses, not runtime signature scanning
#if defined(BODYCAM_NG)
		F4SEPluginVersionData::kStructureIndependence_1_10_980Layout,
		{ kRuntimeVersion_1_10_984, 0 },
#else
		F4SEPluginVersionData::kStructureIndependence_1_11_137Layout,
		{ kRuntimeVersion_1_11_240, 0 },
#endif

		0,
	};
#endif

	__declspec(dllexport) bool F4SEPlugin_Load(const F4SEInterface* f4se)
	{
		// g_selfModule was set in DllMain(DLL_PROCESS_ATTACH), which always runs before
		// F4SE calls this function.
		Logger::Get().Init(g_selfModule);
#if defined(BODYCAM_OG)
		CP_LOG("Bodycam %s loading (Old-Gen build, 1.10.163), compiled %s %s. F4SE runtime version = 0x%08X",
			BODYCAM_VERSION, __DATE__, __TIME__, f4se->runtimeVersion);
#elif defined(BODYCAM_NG)
		CP_LOG("Bodycam %s loading (NG build, 1.10.984), compiled %s %s. F4SE runtime version = 0x%08X",
			BODYCAM_VERSION, __DATE__, __TIME__, f4se->runtimeVersion);
#else
		CP_LOG("Bodycam %s loading (AE build, 1.11.240), compiled %s %s. F4SE runtime version = 0x%08X",
			BODYCAM_VERSION, __DATE__, __TIME__, f4se->runtimeVersion);
#endif

		// Which file is this, really? Every "I tested the new build" question is settled here: the
		// path names the mod folder the game loaded from, the timestamp names the build.
		{
			char self[MAX_PATH] = "";
			GetModuleFileNameA(g_selfModule, self, MAX_PATH);
			FILETIME ft{};
			SYSTEMTIME st{};
			HANDLE h = CreateFileA(self, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
			if (h != INVALID_HANDLE_VALUE)
			{
				GetFileTime(h, nullptr, nullptr, &ft);
				CloseHandle(h);
				FILETIME lft{};
				FileTimeToLocalFileTime(&ft, &lft);
				FileTimeToSystemTime(&lft, &st);
			}
			CP_LOG("Bodycam DLL: %s (file written %04u-%02u-%02u %02u:%02u:%02u local)",
				self, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
		}

		// Before the first read: if this is the first launch after an install whose choices differ
		// from what is saved, the shipped defaults replace the saved settings (see ApplyInstallStamp).
		{
			std::string shipped, saved;
			int applied = Config::ApplyInstallStamp(shipped, saved);
			if (applied != 0)
				CP_LOG("Install stamp %s (was '%s', now '%s'): saved MCM settings %s",
					applied > 0 ? "changed" : "changed but could not be applied",
					saved.c_str(), shipped.c_str(),
					applied > 0 ? "replaced with the installed defaults" : "left alone");
		}

		g_config = Config::LoadAll();
		CP_LOG("Config: defaults=%s (%s) user=%s (%s) enabled=%d preset=%d restCant=%.1f sprintBob=%.1f",
			Config::DefaultsPath().c_str(), GetFileAttributesA(Config::DefaultsPath().c_str()) != INVALID_FILE_ATTRIBUTES ? "found" : "missing",
			Config::UserPath().c_str(), GetFileAttributesA(Config::UserPath().c_str()) != INVALID_FILE_ATTRIBUTES ? "found" : "none yet",
			g_config.enabled, g_config.preset, g_config.gunRestCantDeg, g_config.sprint.vertical);

		// The aim test lives or dies on these four, so put them in the log rather than trusting
		// whatever the MCM ini happened to hold.
		CP_LOG("Config: freeAim=%d weaponHold=%d crosshairFollow=%d crosshairScale=%.2f debugLog=%d",
			g_config.freeAim, g_config.weaponHold, g_config.crosshairFollow, g_config.crosshairScale, g_config.debugLog);

		uintptr_t moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
		Bodycam::Init(moduleBase);

		if (auto* msg = static_cast<F4SEMessagingInterface*>(f4se->QueryInterface(F4SEMessagingInterface::kInterface_Messaging)))
		{
			bool ok = msg->RegisterListener(f4se->GetPluginHandle(), "F4SE", [](F4SEMessagingInterface::Message* m) {
				if (m && m->type == F4SEMessagingInterface::kMessage_PreSaveGame)
					Bodycam::OnPreSave();
			});
			CP_LOG("F4SE messaging listener: %s", ok ? "registered" : "FAILED");
		}

		g_running = true;
		std::thread(MaintenanceThread).detach();

		CP_LOG("Bodycam loaded successfully.");
		return true;
	}
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
	if (reason == DLL_PROCESS_ATTACH)
	{
		g_selfModule = hModule;
	}
	else if (reason == DLL_PROCESS_DETACH)
	{
		g_running = false;
	}
	return TRUE;
}
