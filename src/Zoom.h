#pragma once
// Zoom and gimbal.
//
// A key narrows the world FOV, spyglass style; arms and gun keep their size. The mouse slows with
// it, and while zoomed the look input goes through a spring, so the frame glides after the mouse
// like a camera on a gimbal instead of jumping with every nudge. First and third person.
//
// 1.11.240 only so far: with the RVAs in Offsets.h at 0, Install says so in the log and nothing
// else runs.
//
// Where the engine takes the world FOV from (read out of the exe, 2026-10-22):
//   third person   PlayerCamera+0x168
//   first person   the fDefault1stPersonFOV *setting*; the state change at 0x10274F0 hands it to
//                  the frustum when the new state is the first-person one. The first build wrote
//                  +0x168 alone and zoomed third person only.
//   arms and gun   PlayerCamera+0x16C, the weapon FOV Weapon Hold drives. Never touched here.
// The console's "fov a b" (0x5F0470) is a -> +0x168, +0x16C and fDefaultWorldFOV, b ->
// fDefault1stPersonFOV, so its first number is the weapon FOV and "fov 20" alone blows the arms up.
// It also marks the settings dirty; this code does not, so a zoomed value never reaches
// Fallout4Prefs.ini.
//
// The gimbal sits on PlayerControls::PerformInputProcessing, which runs the look handler once a
// frame and leaves this frame's scaled mouse input in PlayerControlsData::lookInputVec for the
// camera states to read later in the frame. After the original returns, the vector is replaced
// with the spring's output for this frame, so both camera states see the eased motion and nothing
// else changes.
#include "F4SEPluginApiMinimal.h"
#include "Bodycam.h"
#include "Config.h"
#include "Logger.h"
#include "Offsets.h"

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <string>

namespace Zoom
{
	inline uintptr_t g_base = 0;
	inline bool      g_available = false;
	inline LARGE_INTEGER g_freq{};

	inline float Seconds(const LARGE_INTEGER& a, const LARGE_INTEGER& b)
	{
		return static_cast<float>(b.QuadPart - a.QuadPart) / static_cast<float>(g_freq.QuadPart);
	}

	inline uint8_t* g_lookData = nullptr; // PlayerControlsData, found by MaintainLookHook
	inline float* LookVec() { return g_lookData ? reinterpret_cast<float*>(g_lookData + Offsets::kOff_ControlsData_lookInput) : nullptr; }

	// ---- key capture, behind the MCM button -----------------------------------------------------
	// GetAsyncKeyState from the frame task, so it works with the menu open and sees the mouse
	// buttons MCM's own picker does not. Armed only once everything is up, or the click on the
	// button would be the answer. Virtual-key codes, like the other keys in [Keys].
	inline std::atomic<int> g_capture{ 0 }; // -1 waiting, 0 idle or cancelled, >0 the key
	inline bool g_captureArmed = false;
	inline int  g_captured = 0;

	inline std::string KeyName(int vk)
	{
		switch (vk)
		{
		case 0:           return "None";
		case VK_LBUTTON:  return "Left Mouse";
		case VK_RBUTTON:  return "Right Mouse";
		case VK_MBUTTON:  return "Middle Mouse";
		case VK_XBUTTON1: return "Mouse 4";
		case VK_XBUTTON2: return "Mouse 5";
		}
		UINT sc = MapVirtualKeyA(static_cast<UINT>(vk), MAPVK_VK_TO_VSC);
		bool ext = false;
		switch (vk)
		{
		case VK_RCONTROL: case VK_RMENU: case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
		case VK_PRIOR: case VK_NEXT: case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN: case VK_DIVIDE:
			ext = true;
		}
		char buf[64]{};
		if (sc && GetKeyNameTextA(static_cast<LONG>((sc << 16) | (ext ? (1 << 24) : 0)), buf, sizeof(buf)) > 0)
			return buf;
		return "Key " + std::to_string(vk);
	}

	inline void PollCapture()
	{
		if (g_capture != -1)
			return;
		bool anyDown = false, escape = false;
		int found = 0;
		for (int vk = 1; vk < 255; ++vk)
		{
			if (!(GetAsyncKeyState(vk) & 0x8000))
				continue;
			anyDown = true;
			if (vk == VK_ESCAPE)
				escape = true;
			// left mouse fires the gun and the generic modifiers have left/right codes of their own
			else if (!found && vk != VK_LBUTTON && vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU)
				found = vk;
		}
		if (!g_captureArmed)
		{
			if (!anyDown)
				g_captureArmed = true;
			return;
		}
		if (escape)
		{
			g_capture = 0;
			CP_LOG("zoom key: capture cancelled");
		}
		else if (found)
		{
			g_captured = found;
			g_capture = found;
			CP_LOG("zoom key: captured %d (%s)", found, KeyName(found).c_str());
		}
	}

	// The three natives on BodycamPresets (no arguments, see PapyrusBridge.h).
	inline float BridgeKeyBegin()
	{
		g_captureArmed = false;
		g_capture = -1;
		return 0.0f;
	}
	inline float BridgeKeyPoll()
	{
		int v = g_capture;
		if (v > 0)
			g_capture = 0; // read once
		return static_cast<float>(v);
	}
	inline std::string BridgeKeyName() { return KeyName(g_captured ? g_captured : g_config.zoomKey); }

	// ---- the zoom -----------------------------------------------------------------------------------
	inline std::atomic<bool> g_active{ false }; // a zoom is on or on its way out
	inline bool  g_moving = false, g_leaving = false;
	inline bool  g_wasDown = false, g_toggled = false;
	inline float g_restWorld = 0.0f, g_restSetting = 0.0f, g_rest1st = 0.0f, g_restView = 0.0f;
	inline float g_restMouseX = 0.0f, g_restMouseY = 0.0f;
	inline float g_cur = 0.0f, g_from = 0.0f, g_to = 0.0f, g_dur = 0.0f;
	inline LARGE_INTEGER g_t0{};

	inline uint8_t* Camera()     { return *reinterpret_cast<uint8_t**>(g_base + Offsets::kRVA_g_playerCamera); }
	inline uint8_t* Scene()      { return *reinterpret_cast<uint8_t**>(g_base + Offsets::kRVA_g_sceneCamera); }
	inline float&   WorldField(uint8_t* cam) { return *reinterpret_cast<float*>(cam + Offsets::kOff_PlayerCamera_fovWorld); }
	inline float&   SettingWorld() { return *reinterpret_cast<float*>(g_base + Offsets::kRVA_fDefaultWorldFOV + 8); }
	inline float&   Setting1st()   { return *reinterpret_cast<float*>(g_base + Offsets::kRVA_fDefault1stFOV + 8); }
	inline float&   MouseX()       { return *reinterpret_cast<float*>(g_base + Offsets::kRVA_fMouseHeadingX + 8); }
	inline float&   MouseY()       { return *reinterpret_cast<float*>(g_base + Offsets::kRVA_fMouseHeadingY + 8); }

	inline bool InFirstPerson(uint8_t* cam)
	{
		void* state = *reinterpret_cast<void**>(cam + Offsets::kOff_PlayerCamera_state);
		return state && state == *reinterpret_cast<void**>(cam + Offsets::kOff_PlayerCamera_firstState);
	}

	// The FOV the view is really at when not zoomed: the two settings can differ (70 / 80 by default).
	inline float RestFor(uint8_t* cam) { return InFirstPerson(cam) ? g_rest1st : g_restWorld; }

	inline void RebuildFrustum(float fov)
	{
		if (auto* scene = Scene())
			reinterpret_cast<void (*)(void*, float, bool, void*, bool)>(g_base + Offsets::kRVA_SetSceneFov)(scene, fov, true, nullptr, false);
		reinterpret_cast<void (*)(float)>(g_base + Offsets::kRVA_SetFovScale)(fov);
	}

	inline void Apply(uint8_t* cam, float fov)
	{
		RebuildFrustum(fov);
		WorldField(cam) = fov;
		SettingWorld() = fov;
		Setting1st() = fov;
	}

	// Mouse speed in step with the zoom: the FOV ratio, times the Zoomed Mouse Speed slider. The
	// engine's own FOV scale alone left the view turning at full speed at 20 over 70.
	inline void ApplyMouse(float fov, float mult)
	{
		float ratio = g_restView > 1.0f ? std::clamp(fov / g_restView, 0.02f, 1.0f) : 1.0f;
		float factor = std::clamp(ratio * (mult > 0.0f ? mult : 1.0f), 0.02f, 4.0f);
		MouseX() = g_restMouseX * factor;
		MouseY() = g_restMouseY * factor;
	}

	// Something other than this code built the frustum or put a rest value back (a camera state
	// change, the Pip-Boy closing).
	inline bool Drifted(uint8_t* cam, float want)
	{
		if (std::fabs(WorldField(cam) - want) > 0.01f || std::fabs(Setting1st() - want) > 0.01f)
			return true;
		if (auto* scene = Scene())
			return std::fabs(*reinterpret_cast<float*>(scene + Offsets::kOff_Scene_fov) - want) > 0.01f;
		return false;
	}

	inline void Move(float from, float to, float seconds)
	{
		g_from = from;
		g_to = to;
		g_dur = std::max(0.0f, seconds);
		g_moving = true;
		QueryPerformanceCounter(&g_t0);
	}

	inline void Begin(uint8_t* cam, const Config& c)
	{
		if (!g_active)
		{
			g_restWorld = WorldField(cam);
			g_restSetting = SettingWorld();
			g_rest1st = Setting1st();
			g_restMouseX = MouseX();
			g_restMouseY = MouseY();
			g_restView = RestFor(cam);
			g_cur = g_restView;
			g_active = true;
			CP_LOG("zoom: in, %.1f -> %.1f over %.2fs (settings world %.1f / 1st %.1f, mouse %.4f / %.4f x%.2f, %s person)",
				g_restView, c.zoomFov, c.zoomTime, g_restSetting, g_rest1st, g_restMouseX, g_restMouseY, c.zoomMouse,
				InFirstPerson(cam) ? "first" : "third");
		}
		g_leaving = false;
		Move(g_cur, c.zoomFov, c.zoomTime);
	}

	inline void End(uint8_t* cam, float seconds)
	{
		if (!g_active || g_leaving)
			return;
		g_leaving = true;
		Move(g_cur, RestFor(cam), seconds);
	}

	// Back exactly where it was: field, both settings, mouse, and the frustum rebuilt from whichever
	// setting the current state uses.
	inline void Restore(uint8_t* cam)
	{
		WorldField(cam) = g_restWorld;
		SettingWorld() = g_restSetting;
		Setting1st() = g_rest1st;
		MouseX() = g_restMouseX;
		MouseY() = g_restMouseY;
		RebuildFrustum(RestFor(cam));
		reinterpret_cast<void (*)(float)>(g_base + Offsets::kRVA_SetFovScale)(g_restWorld);
		g_active = false;
		g_leaving = false;
		g_moving = false;
		CP_LOG("zoom: out, rest world %.1f / 1st %.1f restored", g_restWorld, g_rest1st);
	}

	// A save loaded while zoomed would keep the override with nothing holding the key.
	inline void OnLoadGame()
	{
		g_toggled = false;
		if (g_active)
			if (uint8_t* cam = Camera())
				Restore(cam);
	}

	inline void Tick()
	{
		PollCapture();
		if (!g_available)
			return;
		// The standalone Quick Zoom mod this came from writes the same FOV and mouse values every
		// frame. Both at once and whichever reads its rest second takes the other's 20 as normal,
		// and puts 20 back on release: stuck zoomed. It loads after this DLL (Q after B), so the
		// check waits for the first frame.
		static bool checked = false;
		if (!checked)
		{
			checked = true;
			if (GetModuleHandleA("QuickZoom.dll"))
			{
				g_available = false;
				CP_LOG("zoom: QuickZoom.dll is loaded as well; Bodycam's zoom is off. Remove the standalone Quick Zoom mod, Bodycam carries it now");
				return;
			}
		}
		const Config& c = g_config;
		uint8_t* cam = Camera();
		if (!cam)
			return;

		bool menu = Bodycam::ViewMenuOpen();
		bool down = c.zoomKey > 0 && (GetAsyncKeyState(c.zoomKey) & 0x8000) != 0;
		bool pressed = down && !g_wasDown;
		g_wasDown = down;
		bool want;
		if (c.zoomHold)
			want = down && !menu;
		else
		{
			if (menu)
				g_toggled = false; // pause and Pip-Boy are where saving and the settings menu happen
			else if (pressed)
				g_toggled = !g_toggled;
			want = g_toggled;
		}

		if (want && (!g_active || g_leaving))
			Begin(cam, c);
		else if (!want && g_active)
			End(cam, c.zoomTime);
		// the slider moved while zoomed
		if (g_active && !g_leaving && !g_moving && std::fabs(g_to - c.zoomFov) > 0.01f)
			Move(g_cur, c.zoomFov, 0.0f);

		if (g_moving)
		{
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			float t = g_dur > 0.0f ? Seconds(g_t0, now) / g_dur : 1.0f;
			if (t >= 1.0f)
				t = 1.0f;
			float s = t * t * (3.0f - 2.0f * t);
			g_cur = g_from + (g_to - g_from) * s;
			Apply(cam, g_cur);
			ApplyMouse(g_cur, c.zoomMouse);
			if (t >= 1.0f)
			{
				g_moving = false;
				if (g_leaving)
					Restore(cam);
			}
		}
		else if (g_active)
		{
			if (Drifted(cam, g_cur))
				Apply(cam, g_cur);
			ApplyMouse(g_cur, c.zoomMouse); // the settings menu can rewrite these any time
		}
	}

	class FrameTask final : public ITaskDelegate
	{
	public:
		void Run() override
		{
			__try { Tick(); }
			__except (EXCEPTION_EXECUTE_HANDLER) { CP_LOG("zoom: tick faulted"); }
		}
	};

	// ---- gimbal: the look input through a spring --------------------------------------------------
	// Where the vector is written and read, counted on the bench 2026-10-26: PerformInputProcessing
	// zeroes lookInputVec, the look handler puts raw counts in it, and before returning it scales
	// them by fMouseHeadingSensitivity * fMouseHeadingXScale / frame time (0xDDB5CA), so what is
	// left is a RATE; the camera states multiply by the frame time again later in the frame. The
	// F4SE frame task runs after them, twice per frame, so a write from there never reaches the
	// camera; a write from this hook does. The spring works in displacement (rate * dt in, delta /
	// dt out): written as plain deltas the turn came out 25% long on one run and right on the next,
	// with the frame time. Critically damped by default: omega = 3 / fGimbalTime.
	using ProcessFn = void (*)(void*, void*);
	struct LookShadow
	{
		ProcessFn original;
		void*     col;
		void*     vtbl[16];
	};
	inline LookShadow* g_lookShadow = nullptr;
	inline bool     g_filterLive = false;
	inline float    g_tx = 0.0f, g_ty = 0.0f, g_px = 0.0f, g_py = 0.0f, g_vx = 0.0f, g_vy = 0.0f;
	inline unsigned g_springSteps = 0;
	inline bool     g_lookWarned = false;

	inline void Spring()
	{
		float* look = LookVec();
		if (!look)
			return;
		const Config& c = g_config;
		float dt = *reinterpret_cast<float*>(g_base + Offsets::kRVA_g_frameTime);
		if (!(dt > 0.0f))
			return; // paused: the engine left the vector at zero
		dt = std::min(dt, 0.1f);

		bool active = g_active && c.gimbal;
		if (!active && !g_filterLive)
			return;
		if (active && !g_filterLive)
		{
			g_filterLive = true;
			g_tx = g_ty = g_px = g_py = g_vx = g_vy = 0.0f;
			g_springSteps = 0;
			CP_LOG("gimbal: on (%.2fs, settle %.2f)", c.gimbalTime, c.gimbalDamping);
		}

		g_tx += look[0] * dt;
		g_ty += look[1] * dt;
		// Coming out of the zoom the spring finishes fast, then lets go.
		float w = 3.0f / std::max(0.05f, active ? c.gimbalTime : c.gimbalTime * 0.3f);
		float zeta = std::clamp(active ? c.gimbalDamping : 1.0f, 0.3f, 1.0f);
		float px = g_px, py = g_py;
		int steps = std::max(1, static_cast<int>(std::ceil(dt * w * 4.0f)));
		float h = dt / static_cast<float>(steps);
		for (int i = 0; i < steps; ++i)
		{
			g_vx += (w * w * (g_tx - g_px) - 2.0f * zeta * w * g_vx) * h;
			g_vy += (w * w * (g_ty - g_py) - 2.0f * zeta * w * g_vy) * h;
			g_px += g_vx * h;
			g_py += g_vy * h;
		}
		look[0] = (g_px - px) / dt;
		look[1] = (g_py - py) / dt;
		++g_springSteps;

		if (!active && std::fabs(g_tx - g_px) < 0.01f && std::fabs(g_ty - g_py) < 0.01f
			&& std::fabs(g_vx) < 0.1f && std::fabs(g_vy) < 0.1f)
		{
			g_filterLive = false;
			CP_LOG("gimbal: off after %u frames", g_springSteps);
		}
	}

	inline void Hook_PerformInput(void* self, void* queue)
	{
		g_lookShadow->original(self, queue);
		__try { Spring(); }
		__except (EXCEPTION_EXECUTE_HANDLER) { CP_LOG("gimbal: spring faulted"); }
	}

	// From MaintainHook, every few hundred ms until it holds. PlayerControls lives for the whole
	// run, so this installs once.
	inline void MaintainLookHook()
	{
		if (!g_available || g_lookShadow)
			return;
		auto* controls = *reinterpret_cast<uint8_t**>(g_base + Offsets::kRVA_g_playerControls);
		if (!controls)
			return;
		void** vtbl = *reinterpret_cast<void***>(controls);
		if (reinterpret_cast<uintptr_t>(vtbl) != g_base + Offsets::kRVA_PlayerControls_vtbl)
		{
			if (!g_lookWarned)
				CP_LOG("gimbal: PlayerControls has an unexpected vtable (RVA 0x%llX), another plugin may be hooking it; gimbal off",
					static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(vtbl) - g_base));
			g_lookWarned = true;
			return;
		}
		auto* look = *reinterpret_cast<uint8_t**>(controls + Offsets::kOff_PlayerControls_look);
		if (!look || reinterpret_cast<uintptr_t>(*reinterpret_cast<void**>(look)) != g_base + Offsets::kRVA_LookHandler_vtbl)
		{
			if (!g_lookWarned)
				CP_LOG("gimbal: no LookHandler at PlayerControls+0x%llX; gimbal off", static_cast<unsigned long long>(Offsets::kOff_PlayerControls_look));
			g_lookWarned = true;
			return;
		}
		g_lookData = *reinterpret_cast<uint8_t**>(look + Offsets::kOff_InputHandler_data);
		if (!g_lookData)
			return;

		auto* block = new LookShadow{};
		block->original = reinterpret_cast<ProcessFn>(vtbl[Offsets::kVtblIndex_PerformInput]);
		block->col = vtbl[-1];
		std::memcpy(block->vtbl, vtbl, sizeof(block->vtbl));
		block->vtbl[Offsets::kVtblIndex_PerformInput] = reinterpret_cast<void*>(&Hook_PerformInput);
		g_lookShadow = block;
		*reinterpret_cast<void***>(controls) = block->vtbl;
		CP_LOG("gimbal: hooked PlayerControls::PerformInputProcessing on 0x%p (look data 0x%p)", controls, g_lookData);
	}

	inline void Install(const F4SEInterface* f4se, uintptr_t moduleBase)
	{
		g_base = moduleBase;
		QueryPerformanceFrequency(&g_freq);
		g_available = Offsets::kRVA_g_sceneCamera != 0 && Offsets::kRVA_g_playerControls != 0;
		if (!g_available)
		{
			CP_LOG("zoom: not on this game version yet (no addresses for this build)");
			return;
		}
		auto* task = static_cast<F4SETaskInterface*>(f4se->QueryInterface(F4SETaskInterface::kInterface_Task));
		if (task)
			task->AddTaskPermanent(new FrameTask());
		CP_LOG("zoom: %s", task ? "frame task installed" : "no task interface, zoom off");
		g_available = task != nullptr;
	}
}
