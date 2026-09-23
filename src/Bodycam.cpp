#include "Bodycam.h"
#include "Offsets.h"
#include "GameTypes.h"
#include "RayCast.h"
#include "Config.h"
#include "Logger.h"
#include "BodycamRecoilAPI.h"
#include "RecoilModel.h"

#include <windows.h>
#include <shlobj.h>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <cstddef>
#include <intrin.h>

// Bodycam-style first person.
//
// The game's camera orientation this frame is your REAL aim. The view is rendered trailing
// behind it on a spring while the arms and gun stay ON it, so the gun swings ahead and the
// view catches up - and since the aim itself is never delayed, shots still follow the barrel.
//
// Applied inside NiAVObject::UpdateWorldData, after the game computes a node's world
// transform and before its children update: camera root gets the view motion, first-person
// skeleton root gets the gun motion.
//
// Rotation math here is COLUMN form - convert with ToColumnForm() / ToGameForm() (GameTypes.h).
namespace Bodycam
{
	constexpr float kPi = 3.14159265f;
	constexpr float kDegToRad = kPi / 180.0f;
	constexpr float kTwoPi = 2.0f * kPi;
	constexpr int   kLogThrottle = 30;           // motion log every 30 simulated frames
	constexpr int   kDiagEvery = 4;              // extra diagnostics every 4th motion log
	constexpr float kRepeatCallWindow = 0.0015f; // a second camera update this soon is the same frame
	const NiPoint3  kWorldUp{ 0.0f, 0.0f, 1.0f };

	using UpdateWorldDataFn = void(*)(void* thisPtr, void* updateData);

	// Per-instance shadow vtable. The object's vptr is set to &vtbl[0], so
	//   vtbl[-1] = original RTTI CompleteObjectLocator (keeps dynamic_cast/typeid working)
	//   vtbl[-2] = original UpdateWorldData, recovered inside the hook
	struct ShadowBlock
	{
		UpdateWorldDataFn original;
		void*             col;
		void*             vtbl[Offsets::kShadowVtableSize];
	};

	static uintptr_t g_base = 0;
	static std::atomic<bool> g_installing{ false };
	static void* g_cameraShadow = nullptr;
	static void* g_rigShadow = nullptr;
	static std::atomic<NiAVObjectView*> g_rigNode{ nullptr };

	// ---- simulation state (main thread only) --------------------------------------------
	static LARGE_INTEGER g_qpcFreq{};
	static LARGE_INTEGER g_simTick{};   // last camera simulation
	static LARGE_INTEGER g_rigTick{};   // last rig update
	static float  g_frameDt = 1.0f / 60.0f;
	static double g_time = 0.0;
	static int    g_logCounter = 0;

	static bool     g_havePrev = false;
	static float    g_prevYaw = 0.0f, g_prevPitch = 0.0f;
	static float    g_prevBodyYaw = 0.0f;
	static NiPoint3 g_prevEye{};
	static NiPoint3 g_prevActorPos{};

	// What the rig hook applied to the gun this frame. The game builds the camera from the
	// first-person rig (measured: camera roll == -gun lean, every sample), so this has to be
	// removed from the camera before we apply the camera's own motion.
	static NiMatrix43    g_rigAppliedRot = Identity();
	static NiPoint3      g_rigAppliedShift{};
	static NiPoint3      g_rigHoldShift{}; // Weapon Hold part of the shift (kept out of the follow detector)
	static LARGE_INTEGER g_rigAppliedTick{};
	static float         g_rawRollDeg = 0.0f;
	// Does the camera also inherit the gun's position shift? Measured by regression.
	static float    g_shiftNum = 0.0f, g_shiftDen = 0.0f, g_shiftFollow = 0.0f;
	static bool     g_shiftDecided = false, g_haveShiftPrev = false;
	static NiPoint3 g_prevEyeRel{}, g_prevRigShift{};


	// ---- Weapon-FOV compensation --------------------------------------------------------------
	// The weapon renders at fDefault1stPersonFOV (+0x16C), the world at the camera frustum, so one
	// direction lands on two different pixels. Measured 2026-09-20 at weapon 120 / world 80: sight
	// 93 px off centre, bullet 188 px - ratio 2.02 vs predicted k = tan(60)/tan(40) = 2.064. They
	// agree dead centre, which is why only Free Aim exposes it. Fix: rotate the DRAWN gun to
	// tan(a') = k*tan(a). Bullets and crosshair keep the true aim (g_trueRot), never this.
	static float g_fovK = 1.0f;


	// ---- The 1.0.6 gate -----------------------------------------------------------------------
	// THE rule for 1.0.6: when the weapon FOV equals the world FOV, every 1.0.6 behaviour is
	// skipped and the code takes literally the 1.0.5 branch. Not an equivalent-looking branch -
	// the same one. Everything that went wrong on 2026-09-20 was a change made globally to fix
	// the raised-FOV case which silently moved the standard case as well: the crosshair
	// projection (10% in ADS, because the ADS zoom narrows the frustum while +0x16C does not),
	// the muzzle-delta correction (bob and cant move the muzzle too), and Weapon Scale.
	// If you add a 1.0.6 behaviour, put it behind g_fovRaised.
	static bool g_fovRaised = false;
	static NiMatrix43 g_fovCompRot = Identity();

	// How far this frame's gun pose moved the muzzle; AdjustLaunch subtracts it. The game's spread is
	// recovered against a ray from d->origin - the muzzle node we just moved - so any displacement is
	// absorbed into "spread" and re-applied on top of the aim. Measured 2026-09-20: with the FOV
	// compensation on, extracted spread stopped straddling zero and its pitch scaled with aim offset.
	// Covers every gun-pose term at once, so a new one cannot quietly reintroduce it.
	static NiPoint3 g_muzzleDelta{};
	static float g_muzzleFix = 0.0f; // log only: |g_muzzleDelta|

	static float g_blend = 0.0f;  // 0..1 fade-in after entering first person / resets
	static float g_lagYaw = 0.0f, g_lagYawVel = 0.0f;
	static float g_lagPitch = 0.0f, g_lagPitchVel = 0.0f;
	static float g_yawRate = 0.0f;
	static bool  g_freeAim = false; // free-aim box active this frame
	// Screen float: the view chases the free-aim offset on its own under-damped spring, so it
	// overshoots a touch and settles when the gun changes direction instead of tracking it rigidly.
	// Weapon inertia from MOUSE LOOK: the gun swings against the direction you turn and settles.
	// Distinct from the strafe-driven "Gun Drag" below, which only responds to sideways movement.
	static float g_swingYaw = 0.0f, g_swingYawVel = 0.0f;
	static float g_swingPitch = 0.0f, g_swingPitchVel = 0.0f;
	static float g_cantVel = 0.0f;  // gun cant is second-order so left<->right reversals are smooth
	static float g_floatYaw = 0.0f, g_floatYawVel = 0.0f;
	static float g_floatPitch = 0.0f, g_floatPitchVel = 0.0f;
	static double g_stageW = 1280.0, g_stageH = 720.0; // HUD movie stage, read from the movie itself
	static float g_crossDu = 0.0f, g_crossDv = 0.0f, g_crossR = 0.839f, g_crossT = 0.472f;
	static float g_crossRAlt = 0.839f, g_crossTAlt = 0.472f; // the projection we are NOT using
	static const char* NodeName(const void* obj); // defined below
	static void DumpNodes(NiAVObjectView* obj, int depth, int& count); // defined below
	// First node whose name contains `part` (case-insensitive). Sight nodes are named per weapon
	// ('P-Scope', 'MachineGunSight:0', ...), so we match loosely rather than by exact name.
	static NiAVObjectView* FindNodeContaining(NiAVObjectView* obj, const char* part, int depth)
	{
		if (!obj || depth > 24)
			return nullptr;
		const char* n = NodeName(obj);
		for (const char* p = n; p && *p; ++p)
		{
			const char* a = p; const char* b = part;
			while (*a && *b && std::tolower(static_cast<unsigned char>(*a)) == std::tolower(static_cast<unsigned char>(*b))) { ++a; ++b; }
			if (!*b)
				return obj;
		}
		using AsNodeFn = void* (*)(void*);
		void** vtbl = *reinterpret_cast<void***>(obj);
		if (!reinterpret_cast<AsNodeFn>(vtbl[3])(obj))
			return nullptr;
		auto addr = reinterpret_cast<uintptr_t>(obj);
		auto** kids = *reinterpret_cast<NiAVObjectView***>(addr + 0x120 + 0x08);
		uint16_t n2 = *reinterpret_cast<uint16_t*>(addr + 0x120 + 0x12);
		for (uint16_t i = 0; kids && i < n2; ++i)
			if (NiAVObjectView* hit = FindNodeContaining(kids[i], part, depth + 1))
				return hit;
		return nullptr;
	}

	static NiAVObjectView* FindNode(NiAVObjectView* obj, const char* name, int depth); // defined below
	static float* LiveFov();                      // defined below
	static float  WorldFov();                     // defined below (log only)
	static float g_turnRate = 0.0f; // yaw rate after deadzone + sensitivity; drives both leans
	static float g_roll = 0.0f;       // turn + strafe lean: drives the gun lean (never gated by the camera switches)
	static float g_rollScreen = 0.0f; // same, minus strafe when "Camera Tilt When Strafing" is off: screen only
	static float g_restTilt = 0.0f;   // resting camera tilt, eased in/out so the switch never pops the view
	static float g_tiltSign = 1.0f;   // which way the camera is currently hanging
	static float g_tiltNext = 0.0f;   // seconds until it shifts to the other side
	static float g_phase = 0.0f;
	static float g_bobSpeed = 0.0f; // smoothed speed driving the gait blend
	static float g_cant = 0.0f;
	static NiPoint3 g_inertia{};
	static float g_strafeNorm = 0.0f;

	// Stance: is the body actually standing on something, and is it wearing a power frame?
	static float g_grounded = 1.0f;   // 1 = feet on the floor, 0 = airborne (smoothed)
	static float g_fallSpeed = 0.0f;  // signed vertical body speed, for the log
	static int   g_inPowerArmor = -1; // -1 unknown, else last seen state
	static int   g_prevCamState = -99; // last logged camera-state index
	static int   g_prevMenuOpen = 0;   // last logged Pip-Boy/pause menu state
	static int   g_camStateIdx = -1;   // this frame's camera state (0 = plain first person)
	static float g_paScale = 1.0f;    // smoothed power-armor amplitude scale
	static float g_amplitude = 1.0f;  // preset x power armor: every amplitude in both hooks

	static float g_retract = 0.0f;
	// Weapon Hold (1.0.4)
	static LARGE_INTEGER g_lastShotTick{}; // last player weapon shot (Projectile::Launch hook)
	static int   g_holdType = -2;          // Config hold type 0..4, -1 none; -2 = not yet read
	static float g_holdAds = 0.0f, g_holdForward = 0.0f, g_holdDrop = 0.0f, g_lowReady = 0.0f;
	static float g_fovBase = 0.0f, g_fovApplied = 0.0f, g_fovWritten = 0.0f;
	static bool  g_fovOwned = false;
	static float g_gunPeek = 0.0f; // -1..1
	static float g_camPeek = 0.0f; // -1..1
	static float g_gunPeekVel = 0.0f, g_camPeekVel = 0.0f; // sprung, so the lean cannot snap

	// Per-frame results shared with the rig hook (column form).
	static bool       g_active = false;
	static NiMatrix43 g_trueRot{};       // real aim (what the game computed)
	static NiPoint3   g_trueEye{};
	static NiMatrix43 g_viewRot{};       // what we rendered
	static NiPoint3   g_viewEye{};
	static float      g_stepAge = 1.0f, g_stepAmp = 0.0f; // footstep impact kick
	// Jump: the launch dip and the landing impact are the same damped-sine kick as a footstep, run
	// from their own state so a landing cannot cut a footstep short (or the reverse).
	static bool       g_wasAirborne = false;  // raw airborne state last frame, for the two edges
	static float      g_airTime     = 0.0f;   // seconds since leaving the ground
	static float      g_peakFall    = 0.0f;   // fastest DOWNWARD speed seen this flight
	static float      g_jumpAge     = 1.0f, g_jumpAmp = 0.0f;   // launch dip / landing kick (units)
	static float      g_landShakeAge = 1.0f, g_landShakeAmp = 0.0f; // landing shake (degrees)
	// Landing absorb: a settle-and-return, not a ring. Its envelope is (e^-ft - e^-rt), which starts
	// at zero (continuous, so frame generation is safe), rises fast and eases back.
	// Attack rate. 25 was a snap; a knee takes ~0.15 s to compress, and the softer onset is most of
	// what stopped the landing reading as jittery.
	static constexpr float kCrouchRise = 12.0f;
	static float      g_crouchAge = 1.0f, g_crouchAmp = 0.0f, g_crouchNorm = 1.0f;
	static float      g_launchAge = 1.0f, g_launchAmp = 0.0f, g_launchNorm = 1.0f;
	static bool       g_liftedOff = false; // in flight, from take-off until the landing fires
	static float      g_landGunDip = 0.0f; // written by the camera hook, consumed by the rig hook

	// Peak of (e^-ft - e^-rt), so a slider can mean "how deep in units" instead of an opaque scale.
	static float EnvNorm(float f, float r)
	{
		f = std::max(0.1f, f);
		if (std::fabs(r - f) < 0.001f)
			return 1.0f;
		float tp = std::log(r / f) / (r - f);
		float pk = std::exp(-f * tp) - std::exp(-r * tp);
		return pk > 0.001f ? 1.0f / pk : 1.0f;
	}

	// Compression envelope: starts at zero (continuous, so frame generation stays happy), rises on
	// r, eases back on f. One compression and one recovery - it never crosses zero, so it cannot
	// ring the way a damped sine does.
	static float Env(float f, float r, float t, float norm)
	{
		return (std::exp(-std::max(0.1f, f) * t) - std::exp(-r * t)) * norm;
	}
	static float      g_breathPhase = 0.0f;               // integrated: the rate varies by gait
	static NiPoint3   g_bobOffset{};     // chest bob applied to the view, in world space
	static float      g_viewRollDeg = 0.0f; // total screen lean applied this frame (signed, blended)
	static float      g_gunRollDeg = 0.0f;  // total gun lean applied (for the log)
	static NiPoint3   g_peekEyeShift{};     // sideways move of the view from the corner peek, this frame
	static float      g_peekEyeSide = 0.0f; // same, as a distance along the view's right
	static float      g_peekEyeDrop = 0.0f; // and how far it lowered
	static int        g_rigLocalSpace = -1; // 1 = rig positioned relative to the camera (origin)
	static NiMatrix43 g_viewDeltaRot{};  // view = delta * trueRot, reapplied on repeat calls
	static NiPoint3   g_viewDeltaPos{};

	// The rig updates BEFORE the camera each frame (measured), so it can't use this frame's eye.
	// Instead we remember where the eye sits in the rig's own space and rebuild it from the
	// rig's current (this frame) transform.
	static NiPoint3   g_rigSeenPos{};
	static NiMatrix43 g_rigSeenRot{};
	static LARGE_INTEGER g_rigSeenTick{};
	static NiPoint3   g_eyeInRig{};
	static bool       g_haveEyeInRig = false;

	// Rig parent auto-detect: which camera does the game build the arms from?
	static float g_rigScoreTrue = 0.0f, g_rigScoreView = 0.0f;
	static float g_prevRelTrue = 0.0f, g_prevRelView = 0.0f;
	static bool  g_haveRelPrev = false;
	static int   g_rigSamples = 0;
	static bool  g_rigFollowsView = false;
	static bool  g_rigDecided = false;

	// Diagnostics.
	static int   g_prevFirstPerson = -1;
	static int   g_camCalls = 0, g_camRepeats = 0, g_rigCalls = 0, g_rigStale = 0;
	static float g_rigAfterCamMs = 0.0f;

	// What we last left in the rig's world transform, and how far the engine's own idea of the
	// previous frame drifted from it (0 = the engine already carries our motion).
	static NiTransform   g_rigWritten{};
	// Eye bone hold-back state (see the low ready block in Hook_Rig), shared with the pivot so it
	// can recover the animation's own eye position.
	static NiAVObjectView* g_eyeBoneHeld = nullptr;
	static NiPoint3        g_eyeWritten{}, g_eyeApplied{};
	static LARGE_INTEGER g_rigWrittenTick{};
	static float         g_mvDelta = 0.0f;
	static BodycamRecoilHostV1 g_recoilHost{};
	static BodycamRecoilPoseV1 g_recoil{};

	static UpdateWorldDataFn OriginalFor(void* thisPtr)
	{
		void** vptr = *reinterpret_cast<void***>(thisPtr);
		return reinterpret_cast<UpdateWorldDataFn>(vptr[-2]);
	}

	static bool  KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }
	static float Dot(const NiPoint3& a, const NiPoint3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	static NiPoint3 Cross(const NiPoint3& a, const NiPoint3& b)
	{
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}

	// Minimal rotation taking unit vector `a` onto unit vector `b`.
	static NiMatrix43 RotationBetween(const NiPoint3& a, const NiPoint3& b)
	{
		NiPoint3 axis = Cross(a, b);
		float s = Length(axis);
		float c = Dot(a, b);
		if (s < 1e-6f)
			return Identity();
		return AxisAngle(axis * (1.0f / s), std::atan2(s, c));
	}
	static float Wrap(float a)
	{
		while (a > kPi) a -= kTwoPi;
		while (a < -kPi) a += kTwoPi;
		return a;
	}
	static float Approach(float current, float target, float rate, float dt)
	{
		return current + (target - current) * (1.0f - std::exp(-rate * dt));
	}
	static float YawOf(const NiPoint3& fwd) { return std::atan2(fwd.x, fwd.y); }
	static float Seconds(const LARGE_INTEGER& from, const LARGE_INTEGER& to)
	{
		return g_qpcFreq.QuadPart ? static_cast<float>(to.QuadPart - from.QuadPart) / static_cast<float>(g_qpcFreq.QuadPart) : 0.0f;
	}

	// Damped spring pulling `x` back to 0.
	// 0..1, xorshift - only used for the resting-tilt dwell, so quality is irrelevant.
	static float RandUnit()
	{
		static uint32_t r = 0x9E3779B9u;
		r ^= r << 13; r ^= r >> 17; r ^= r << 5;
		return static_cast<float>(r & 0xffff) / 65535.0f;
	}

	static void Spring(float& x, float& v, float freq, float damping, float dt)
	{
		float a = -freq * freq * x - 2.0f * damping * freq * v;
		v += a * dt;
		x += v * dt;
	}

	// Same spring, but chasing a target that is itself moving. Used for the screen float: the
	// applied view offset trails the free-aim offset and overshoots it slightly on a direction
	// change, which is what reads as the view being carried rather than bolted on.
	static void SpringTo(float& x, float& v, float target, float freq, float damping, float dt)
	{
		float d = x - target;
		Spring(d, v, freq, damping, dt);
		x = target + d;
	}

	// Clamp at the limit AND drop any velocity still pushing outward, so the view eases back
	// instead of hanging at the limit and snapping.
	static void ClampLag(float& x, float& v, float limit)
	{
		if (x > limit) { x = limit; if (v > 0.0f) v = 0.0f; }
		else if (x < -limit) { x = -limit; if (v < 0.0f) v = 0.0f; }
	}

	// Eases into a limit instead of hitting it. Matches a straight line of slope 1 for small x, so
	// ordinary turning leans exactly as much as it did before, but the response flattens as it
	// approaches the limit: a fast flick can no longer slam the lean from one rail to the other.
	static float SoftLimit(float x, float limit)
	{
		if (limit <= 1e-4f)
			return 0.0f;
		return limit * std::tanh(x / limit);
	}

	// Subtractive deadzone: continuous through zero (a step here would be frame-gen hostile).
	static float Deadzone(float x, float dz)
	{
		if (dz <= 0.0f)
			return x;
		float over = std::fabs(x) - dz;
		return over <= 0.0f ? 0.0f : (x < 0.0f ? -over : over);
	}

	static void ResetMotion()
	{
		g_blend = 0.0f;
		g_lagYaw = g_lagYawVel = g_lagPitch = g_lagPitchVel = 0.0f;
		g_floatYaw = g_floatYawVel = g_floatPitch = g_floatPitchVel = 0.0f;
		g_cantVel = 0.0f;
		g_swingYaw = g_swingYawVel = g_swingPitch = g_swingPitchVel = 0.0f;
		g_tiltSign = 1.0f; g_tiltNext = 0.0f;
		g_yawRate = g_turnRate = g_roll = g_rollScreen = g_cant = g_strafeNorm = 0.0f;
		g_inertia = {};
		g_retract = g_gunPeek = g_camPeek = 0.0f;
		g_gunPeekVel = g_camPeekVel = 0.0f;
		g_viewRollDeg = 0.0f;
		g_peekEyeShift = {};
		g_peekEyeSide = 0.0f;
		g_peekEyeDrop = 0.0f;
		g_stepAmp = 0.0f;
		g_stepAge = 1.0f;
		// A teleport or a cell load must not read as a landing: clear the flight state too, or the
		// first frame back on the floor fires a full-force impact. (Same class of miss as the
		// footstep kick surviving a reset in 1.0.5.)
		g_wasAirborne = false;
		g_airTime = g_peakFall = 0.0f;
		g_jumpAmp = 0.0f;       g_jumpAge = 1.0f;
		g_landShakeAmp = 0.0f;  g_landShakeAge = 1.0f;
		g_crouchAmp = 0.0f;     g_crouchAge = 1.0f;
		g_launchAmp = 0.0f;     g_launchAge = 1.0f;
		g_liftedOff = false;
		g_landGunDip = 0.0f;
		g_breathPhase = 0.0f;
		RecoilModel::Reset();
		g_havePrev = false;
	}

	// Piloting power armor is recorded as an ExtraPowerArmor entry on the player reference.
	// The presence bitfield answers "is this type present?" without walking the list or taking
	// its lock - one byte read, safe to do every frame.
	static bool PlayerInPowerArmor(void* player)
	{
		if (!player)
			return false;
		auto* list = *reinterpret_cast<uint8_t**>(reinterpret_cast<uintptr_t>(player) + Offsets::kOff_REFR_extraDataList);
		if (!list)
			return false;
		auto* presence = *reinterpret_cast<uint8_t**>(list + Offsets::kOff_ExtraDataList_presence);
		if (!presence)
			return false;
		constexpr uint32_t t = Offsets::kExtraData_PowerArmor;
		return (presence[t >> 3] & (1u << (t & 7))) != 0;
	}

	// Menu open? Uses the game's own UI::IsMenuOpen. The name strings are built once, the first
	// time the UI exists, and kept for the life of the process (never released).
	// Interned game string (BSFixedString): equal names share one entry pointer.
	struct FixedStringRef { void* data = nullptr; };
	static void MakeFixedString(FixedStringRef& out, const char* name)
	{
		using CtorFn = FixedStringRef* (*)(FixedStringRef*, const char*);
		reinterpret_cast<CtorFn>(g_base + Offsets::kRVA_BSFixedString_ctor)(&out, name);
	}

	enum MenuId { kMenu_Pipboy, kMenu_Pause, kMenu_Scope, kMenu_Workshop, kMenu_Count };
	static bool MenuOpen(MenuId id)
	{
		using IsOpenFn = bool (*)(void*, const FixedStringRef&);
		static FixedStringRef s_names[kMenu_Count];
		static bool s_init = false;

		void* ui = *reinterpret_cast<void**>(g_base + Offsets::kRVA_g_ui);
		if (!ui)
			return false;
		if (!s_init)
		{
			MakeFixedString(s_names[kMenu_Pipboy], "PipboyMenu");
			MakeFixedString(s_names[kMenu_Pause], "PauseMenu");
			MakeFixedString(s_names[kMenu_Scope], "ScopeMenu");
			MakeFixedString(s_names[kMenu_Workshop], "WorkshopMenu"); // settlement build mode
			s_init = true;
		}
		auto isOpen = reinterpret_cast<IsOpenFn>(g_base + Offsets::kRVA_UI_IsMenuOpen);
		return isOpen(ui, s_names[id]);
	}

	static bool SuspendingMenuOpen()
	{
		return MenuOpen(kMenu_Pipboy) || MenuOpen(kMenu_Pause) || MenuOpen(kMenu_Workshop);
	}

	// ==== crosshair follows the gun ===========================================================
	// With Free Aim the gun leaves the screen centre, so the vanilla crosshair marks the wrong spot.
	// Project the gun's aim through the render camera's frustum and move the HUD clip there; put it
	// back exactly where it was when Free Aim is not in effect.
	//
	// Scaleform: UI+0x190 menuStack tArray<IMenu*> {entries@0, count@0x10}; IMenu+0x40 GFxMovieView*,
	// IMenu+0x50 menuName; GFxMovieView+0x18 root; root vtable 0x31 SetVariable, 0x32 GetVariable;
	// GFxValue 0x20 bytes {iface@0, type@8, data@0x10}, type 5 = Number. Clip name from HUDMenu.swf
	// (vanilla and HUDFramework both use HUDCrosshair_mc).
	struct GFxValueView
	{
		void*    iface = nullptr;
		uint32_t type = 0;
		uint32_t pad = 0;
		double   number = 0.0;
		void*    unk18 = nullptr;
	};
	static_assert(sizeof(GFxValueView) == 0x20, "GFxValue size");
	constexpr uint32_t kGFxNumber = 5;

	using GFxSetVarFn = bool (*)(void*, const char*, const GFxValueView*, uint32_t);
	using GFxGetVarFn = bool (*)(void*, GFxValueView*, const char*);

	static const char* const kCrosshairPaths[] = { "root.CenterGroup_mc.HUDCrosshair_mc", "root.HUDCrosshair_mc" };
	static int    g_crossPath = -1;        // which of kCrosshairPaths exists (-1 not found yet)
	static bool   g_crossHaveHome = false; // original position captured
	static double g_crossHomeX = 0.0, g_crossHomeY = 0.0;
	static bool   g_crossMoved = false;    // we have it off home right now
	static void*  g_crossRoot = nullptr;   // movie root the home position belongs to
	static int    g_crossLogCount = 0;

	// ---- Hit indicator ------------------------------------------------------------------------
	// The red marker that flashes when you damage something. It is NOT inside the crosshair clip
	// we move, so with Free Aim it stays at screen centre while the shot lands wherever the gun
	// was pointing. Its path is probed rather than assumed: Scaleform here is path-get/set only,
	// there is no way to enumerate the movie, so every candidate below is tried once and each one
	// that exists is logged. If none match on someone's HUD the feature just does nothing.
	static const char* const kHitPaths[] = {
		"root.CenterGroup_mc.HitIndicator_mc",
		"root.HitIndicator_mc",
		"root.CenterGroup_mc.CrosshairHitIndicator_mc",
		"root.CenterGroup_mc.HUDCrosshair_mc.HitIndicator_mc",
		"root.HUDCrosshair_mc.HitIndicator_mc",
		"root.CenterGroup_mc.HitFlash_mc",
		"root.HitFlash_mc",
		"root.CenterGroup_mc.CombatHitIndicator_mc",
	};
	static int    g_hitPath = -1;          // index into kHitPaths (-1 not probed, 99 none found)
	static double g_hitHomeX = 0.0, g_hitHomeY = 0.0;
	static bool   g_hitMoved = false;

	static void* HudMovieRoot()
	{
		void* ui = *reinterpret_cast<void**>(g_base + Offsets::kRVA_g_ui);
		if (!ui)
			return nullptr;
		static FixedStringRef s_hud;
		if (!s_hud.data)
			MakeFixedString(s_hud, "HUDMenu");
		auto base = reinterpret_cast<uintptr_t>(ui);
		auto** entries = *reinterpret_cast<uint8_t***>(base + 0x190);
		uint32_t count = *reinterpret_cast<uint32_t*>(base + 0x190 + 0x10);
		for (uint32_t i = 0; entries && i < count && i < 64; ++i)
		{
			uint8_t* menu = entries[i];
			if (!menu || *reinterpret_cast<void**>(menu + 0x50) != s_hud.data)
				continue;
			auto* view = *reinterpret_cast<uint8_t**>(menu + 0x40);
			return view ? *reinterpret_cast<void**>(view + 0x18) : nullptr;
		}
		return nullptr;
	}

	static bool GetNumber(void* root, const char* path, double& out)
	{
		GFxValueView v;
		auto get = reinterpret_cast<GFxGetVarFn>((*reinterpret_cast<void***>(root))[0x32]);
		if (!get(root, &v, path))
			return false;
		if (v.type == kGFxNumber) { out = v.number; return true; }
		if (v.type == 3) { out = static_cast<double>(*reinterpret_cast<int32_t*>(&v.number)); return true; }
		if (v.type == 4) { out = static_cast<double>(*reinterpret_cast<uint32_t*>(&v.number)); return true; }
		return false;
	}

	static void SetNumber(void* root, const char* path, double value)
	{
		GFxValueView v;
		v.type = kGFxNumber;
		v.number = value;
		auto set = reinterpret_cast<GFxSetVarFn>((*reinterpret_cast<void***>(root))[0x31]);
		set(root, path, &v, 0);
	}

	// Moves the hit marker by the same offset as the crosshair, or hides it. offX/offY are in HUD
	// coordinates; pass 0,0 to put it back home.
	static void ApplyHitIndicator(void* root, double offX, double offY, const Config& c)
	{
		if (c.hitIndicator == 0)
			return;
		if (g_hitPath < 0)
		{
			g_hitPath = 99;
			for (int i = 0; i < static_cast<int>(sizeof(kHitPaths) / sizeof(kHitPaths[0])); ++i)
			{
				char hx[128], hy[128];
				double x = 0.0, y = 0.0;
				std::snprintf(hx, sizeof(hx), "%s.x", kHitPaths[i]);
				std::snprintf(hy, sizeof(hy), "%s.y", kHitPaths[i]);
				if (GetNumber(root, hx, x) && GetNumber(root, hy, y))
				{
					CP_LOG("hit indicator: found %s at (%.1f, %.1f)%s", kHitPaths[i], x, y,
						g_hitPath == 99 ? " <- using this one" : " (also present, not used)");
					if (g_hitPath == 99)
					{
						g_hitPath = i;
						g_hitHomeX = x;
						g_hitHomeY = y;
					}
				}
			}
			if (g_hitPath == 99)
				CP_LOG("hit indicator: none of the %d candidate paths exist on this HUD - leaving it alone",
					static_cast<int>(sizeof(kHitPaths) / sizeof(kHitPaths[0])));
		}
		if (g_hitPath > 90)
			return;

		char path[160];
		if (c.hitIndicator == 2)
		{
			const char* props[] = { "_visible", "visible", "_alpha", "alpha" };
			const double vals[] = { 0.0, 0.0, 0.0, 0.0 };
			for (int i = 0; i < 4; ++i)
			{
				std::snprintf(path, sizeof(path), "%s.%s", kHitPaths[g_hitPath], props[i]);
				SetNumber(root, path, vals[i]);
			}
			return;
		}

		if (offX == 0.0 && offY == 0.0 && !g_hitMoved)
			return;
		std::snprintf(path, sizeof(path), "%s.x", kHitPaths[g_hitPath]);
		SetNumber(root, path, g_hitHomeX + offX);
		std::snprintf(path, sizeof(path), "%s.y", kHitPaths[g_hitPath]);
		SetNumber(root, path, g_hitHomeY + offY);
		g_hitMoved = !(offX == 0.0 && offY == 0.0);
	}

	// The direction the gun's barrel actually points, with the weapon-FOV compensation taken back
	// out (that rotation only exists so the gun is DRAWN in the right place - see Hook_Rig).
	// Falls back to `aim` when there is no muzzle node or it is nowhere near the aim, which is the
	// same rule the launch hook has always used. One definition, so the crosshair marks the line
	// the bullet leaves on rather than a second guess at it.
	static NiPoint3 BarrelDir(const NiPoint3& aim)
	{
		NiAVObjectView* rig = g_rigNode.load();
		NiAVObjectView* muzzle = rig ? FindNode(rig, "ProjectileNode", 0) : nullptr;
		if (!muzzle)
			return aim;
		NiMatrix43 m = ToColumnForm(muzzle->worldTransform.rot);
		NiPoint3 cand[6] = { m.Right(), m.Forward(), m.Up(), m.Right() * -1.0f, m.Forward() * -1.0f, m.Up() * -1.0f };
		int axis = -1;
		float best = -2.0f;
		for (int i = 0; i < 6; ++i)
		{
			float dp = Dot(Normalized(cand[i]), aim);
			if (dp > best) { best = dp; axis = i; }
		}
		if (best <= 0.9f) // no axis within ~25 deg of the aim: not a barrel we can trust
			return aim;
		return Normalized(Transposed(g_fovCompRot).Mul(Normalized(cand[axis])));
	}

	static void UpdateCrosshair()
	{
		const Config& c = g_config;
		void* root = HudMovieRoot();
		if (!root)
			return;
		if (root != g_crossRoot) // HUD reloaded (new game / load): forget the old movie
		{
			g_crossRoot = root;
			g_crossPath = -1;
			g_hitPath = -1;
			g_hitMoved = false;
			g_crossHaveHome = false;
			g_crossMoved = false;
		}

		char px[96], py[96];
		if (g_crossPath < 0)
		{
			// The pixel mapping depends on the movie's stage size. Vanilla HUDMenu.swf is 1280x720,
			// but HUD mods can change it, and a wrong stage size scales every crosshair offset.
			double sw = 0.0, sh = 0.0;
			GetNumber(root, "root.stage.width", sw);
			GetNumber(root, "root.stage.height", sh);
			if (sw > 200.0 && sh > 200.0)
			{
				g_stageW = sw;
				g_stageH = sh;
			}
			CP_LOG("crosshair: stage %.0fx%.0f (using %.0fx%.0f for the pixel mapping)", sw, sh, g_stageW, g_stageH);

			for (int i = 0; i < 2; ++i)
			{
				double x = 0.0, y = 0.0;
				std::snprintf(px, sizeof(px), "%s.x", kCrosshairPaths[i]);
				std::snprintf(py, sizeof(py), "%s.y", kCrosshairPaths[i]);
				if (GetNumber(root, px, x) && GetNumber(root, py, y))
				{
					g_crossPath = i;
					g_crossHomeX = x;
					g_crossHomeY = y;
					g_crossHaveHome = true;
					CP_LOG("crosshair: found %s at (%.1f, %.1f)", kCrosshairPaths[i], x, y);
					break;
				}
			}
			if (g_crossPath < 0)
			{
				g_crossPath = 99; // not found: give up on this movie
				CP_LOG("crosshair: HUDCrosshair_mc not found - crosshair stays vanilla");
			}
		}
		if (g_crossPath > 1 || !g_crossHaveHome)
			return;
		std::snprintf(px, sizeof(px), "%s.x", kCrosshairPaths[g_crossPath]);
		std::snprintf(py, sizeof(py), "%s.y", kCrosshairPaths[g_crossPath]);


		// Where the gun aims, on screen. g_trueRot is the real aim (the gun and, with Free Aim, the
		// bullets); g_viewRot is the camera we drew. The render camera's frustum gives the mapping.
		// Free Aim moves the whole aim off centre; barrel mode leaves the aim alone but fires along
		// the gun, so the crosshair has to mark the barrel instead. Either way the crosshair shows
		// where the round goes.
		bool barrelMode = !c.freeAim && c.hipAim == 1;
		bool follow = ((c.freeAim && g_freeAim) || barrelMode) && c.crosshairFollow && c.enabled && g_active
			&& g_camStateIdx == Offsets::kCameraState_FirstPerson;
		float du = 0.0f, dv = 0.0f, aspect = 16.0f / 9.0f;
		if (follow)
		{
			auto* cam = *reinterpret_cast<PlayerCameraView**>(g_base + Offsets::kRVA_g_playerCamera);
			auto* cn = cam ? cam->cameraNode : nullptr;
			NiAVObjectView* niCam = nullptr;
			if (cn)
			{
				auto addr = reinterpret_cast<uintptr_t>(cn);
				auto** kids = *reinterpret_cast<NiAVObjectView***>(addr + 0x120 + 0x08);
				uint16_t n = *reinterpret_cast<uint16_t*>(addr + 0x120 + 0x12);
				if (kids && n > 0)
					niCam = kids[0];
			}
			// Which camera is which: list the camera node's children once per session with their
			// frustums, so a wrong pick (e.g. the first-person weapon camera) is visible in the log.
			static bool s_camDiag = false;
			if (cn && c.debugLog && !s_camDiag)
			{
				s_camDiag = true;
				auto addr = reinterpret_cast<uintptr_t>(cn);
				auto** kids = *reinterpret_cast<NiAVObjectView***>(addr + 0x120 + 0x08);
				uint16_t n = *reinterpret_cast<uint16_t*>(addr + 0x120 + 0x12);
				auto* pc = *reinterpret_cast<uint8_t**>(g_base + Offsets::kRVA_g_playerCamera);
				CP_LOG("cameras: node has %u child(ren); PlayerCamera world FOV=%.1f weapon FOV=%.1f",
					n, pc ? *reinterpret_cast<float*>(pc + Offsets::kOff_PlayerCamera_fovWorld) : -1.0f,
					pc ? *reinterpret_cast<float*>(pc + Offsets::kOff_PlayerCamera_fov1st) : -1.0f);
				for (uint16_t i = 0; kids && i < n; ++i)
				{
					if (!kids[i])
						continue;
					auto* f = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(kids[i]) + 0x160);
					CP_LOG("cameras:   [%u] '%s' frustum L=%.4f R=%.4f T=%.4f B=%.4f -> hFOV=%.1f vFOV=%.1f",
						i, NodeName(kids[i]), f[0], f[1], f[2], f[3],
						2.0f * std::atan(std::fabs(f[1])) / kDegToRad, 2.0f * std::atan(std::fabs(f[2])) / kDegToRad);
				}
			}
			// NiCamera::viewFrustum @ 0x160 {left, right, top, bottom, near, far} - the projection the
			// WORLD is drawn with, which is what the crosshair must mark. Do NOT rebuild this from the
			// live FOV: measured 2026-09-20 at weapon FOV 120 / world 80, bullet holes sat on the
			// frustum's 80 deg mapping within a pixel, while the 120 mapping put them 85 px off, on the
			// front sight post. fDefault1stPersonFOV is viewmodel-only.
			float L = -1.0f, R = 1.0f, T = 0.5625f, B = -0.5625f;
			if (niCam)
			{
				auto* f = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(niCam) + 0x160);
				if (f[0] < 0.0f && f[1] > 0.0f && f[2] > 0.0f && f[3] < 0.0f && f[1] < 10.0f && f[2] < 10.0f)
				{
					L = f[0]; R = f[1]; T = f[2]; B = f[3];
				}
				// STANDARD CONFIG: 1.0.5's projection, byte for byte. It overrode the frustum
				// with tan(fov1st/2) and was verified in iron sights at the box edge. In ADS the
				// frustum reads narrower than that (0.7603 vs 0.8391 - the vanilla ADS zoom),
				// and swapping to it moved the standard case by 10%. Not to be "improved"
				// without a measurement that distinguishes the two; g_crossRAlt below logs the
				// other hypothesis every shot so one screenshot can settle it.
				if (!g_fovRaised)
				{
					if (float* live = LiveFov())
					{
						if (*live > 20.0f && *live < 160.0f)
						{
							float ar = (R - L) / (T - B);
							R = std::tan(*live * 0.5f * kDegToRad);
							L = -R;
							T = R / ar;
							B = -T;
						}
					}
				}
			}
			aspect = (R - L) / (T - B);
			NiPoint3 aim = Normalized(g_trueRot.Forward());
			if (barrelMode)
				aim = BarrelDir(aim); // mark the barrel, which is what the bullet follows here
			float vx = Dot(aim, Normalized(g_viewRot.Right()));
			float vy = Dot(aim, Normalized(g_viewRot.Forward()));
			float vz = Dot(aim, Normalized(g_viewRot.Up()));
			if (vy > 0.1f)
			{
				du = ((vx / vy) - L) / (R - L) - 0.5f; // + = right
				dv = (T - (vz / vy)) / (T - B) - 0.5f; // + = down
			}
			g_crossDu = du; g_crossDv = dv; g_crossR = R; g_crossT = T;
			// The OTHER hypothesis for the world projection, so a shot can print both and one
			// screenshot with a visible bullet hole settles which is right - permanently.
			// Whichever of these the code is using, the alternative is the raw frustum vs
			// tan(fov1st/2).
			{
				float altR = R, altT = T;
				if (niCam)
				{
					auto* f = reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(niCam) + 0x160);
					bool ok = f[0] < 0.0f && f[1] > 0.0f && f[2] > 0.0f && f[3] < 0.0f && f[1] < 10.0f && f[2] < 10.0f;
					if (g_fovRaised)
					{
						// using the frustum -> the alternative is tan(fov1st/2)
						if (float* live = LiveFov())
							if (*live > 20.0f && *live < 160.0f && ok)
							{
								altR = std::tan(*live * 0.5f * kDegToRad);
								altT = altR / ((f[1] - f[0]) / (f[2] - f[3]));
							}
					}
					else if (ok)
					{
						// using tan(fov1st/2) -> the alternative is the raw frustum
						altR = f[1]; altT = f[2];
					}
				}
				g_crossRAlt = altR; g_crossTAlt = altT;
			}
			if (c.debugLog && g_crossLogCount < 20 && (std::fabs(du) > 0.02f || std::fabs(dv) > 0.02f))
			{
				++g_crossLogCount;
				CP_LOG("crosshair: frustum L=%.3f R=%.3f T=%.3f B=%.3f aspect=%.3f | aim in view (%.3f, %.3f, %.3f) -> du=%.3f dv=%.3f",
					L, R, T, B, aspect, vx, vy, vz, du, dv);
			}
		}

		if (!follow || (std::fabs(du) < 1e-4f && std::fabs(dv) < 1e-4f))
		{
			if (g_crossMoved)
			{
				SetNumber(root, px, g_crossHomeX);
				SetNumber(root, py, g_crossHomeY);
				g_crossMoved = false;
			}
			ApplyHitIndicator(root, 0.0, 0.0, c);
			return;
		}

		// HUD coordinates are in the movie's authored 1280x720 space; on other aspects the visible
		// stage grows along the long axis. root.stage.width/height report the VISIBLE stage after
		// scaling (1159x673 here), which is not the coordinate space - using it put the marker 6%
		// short, measured as ~12 px at 2560 wide with the gun ~200 px off centre.
		double visW = 1280.0, visH = 720.0;
		if (aspect > 16.0f / 9.0f) visW = 720.0 * aspect;
		else if (aspect < 16.0f / 9.0f) visH = 1280.0 / aspect;
		double scale = c.crosshairScale;
		SetNumber(root, px, g_crossHomeX + du * visW * scale);
		SetNumber(root, py, g_crossHomeY + dv * visH * scale);
		g_crossMoved = true;
		ApplyHitIndicator(root, du * visW * scale, dv * visH * scale, c);

		// Debug aim marker: the game hides the crosshair while you aim, so there is nothing to check
		// the sight picture against. With this on, the crosshair is forced visible in iron sights and
		// sits exactly where the shot will land - if it is not on the front sight post, that gap is
		// the error, measured on screen instead of guessed from bullet holes.
		if (c.aimCrosshair && g_holdAds > 0.5f)
		{
			char path[160];
			const char* props[] = { "_visible", "visible", "_alpha", "alpha" };
			const double vals[] = { 1.0, 1.0, 100.0, 100.0 };
			// Parents matter too: hiding CenterGroup_mc or CrosshairBase_mc hides the crosshair no
			// matter what we set on the clip itself.
			// The container is visible and positioned (readback confirms it) but the game switches the
			// crosshair's inner state clip to "None" while aiming, so nothing is drawn inside it.
			// Force the state clips on as well.
			const char* base = kCrosshairPaths[g_crossPath];
			char clipBuf[6][192];
			std::snprintf(clipBuf[0], sizeof(clipBuf[0]), "%s", base);
			std::snprintf(clipBuf[1], sizeof(clipBuf[1]), "%s.CrosshairBase_mc", base);
			std::snprintf(clipBuf[2], sizeof(clipBuf[2]), "%s.CrosshairBase_mc.CrosshairClips_mc", base);
			std::snprintf(clipBuf[3], sizeof(clipBuf[3]), "%s.CrosshairBase_mc.CrosshairClips_mc.Standard_Standard", base);
			std::snprintf(clipBuf[4], sizeof(clipBuf[4]), "%s.CrosshairBase_mc.CrosshairTicks_mc", base);
			std::snprintf(clipBuf[5], sizeof(clipBuf[5]), "root.CenterGroup_mc");
			const char* clips[] = { clipBuf[0], clipBuf[1], clipBuf[2], clipBuf[3], clipBuf[4], clipBuf[5] };
			for (int cIdx = 0; cIdx < 6; ++cIdx)
				for (int i = 0; i < 4; ++i)
				{
					std::snprintf(path, sizeof(path), "%s.%s", clips[cIdx], props[i]);
					SetNumber(root, path, vals[i]);
				}
			// Read back once a second: if these come back 0 (or unreadable) the HUD is overriding us.
			static LARGE_INTEGER s_lastVis{};
			LARGE_INTEGER nowVis;
			QueryPerformanceCounter(&nowVis);
			if (c.debugLog && (s_lastVis.QuadPart == 0 || Seconds(s_lastVis, nowVis) > 1.0f))
			{
				s_lastVis = nowVis;
				double vis = -1.0, alpha = -1.0, cx = -1.0, cy = -1.0;
				std::snprintf(path, sizeof(path), "%s._visible", kCrosshairPaths[g_crossPath]);
				bool okV = GetNumber(root, path, vis);
				std::snprintf(path, sizeof(path), "%s._alpha", kCrosshairPaths[g_crossPath]);
				bool okA = GetNumber(root, path, alpha);
				bool okX = GetNumber(root, px, cx);
				bool okY = GetNumber(root, py, cy);
				double sv = -1.0, tv = -1.0;
				std::snprintf(path, sizeof(path), "%s.CrosshairBase_mc.CrosshairClips_mc.Standard_Standard._visible", base);
				bool okS = GetNumber(root, path, sv);
				std::snprintf(path, sizeof(path), "%s.CrosshairBase_mc.CrosshairTicks_mc._visible", base);
				bool okT = GetNumber(root, path, tv);
				CP_LOG("aim marker: container _visible=%.0f(ok=%d) _alpha=%.0f(ok=%d) at (%.1f,%.1f) | Standard=%.0f(ok=%d) Ticks=%.0f(ok=%d) | target (%.1f,%.1f)",
					vis, okV ? 1 : 0, alpha, okA ? 1 : 0, cx, cy, sv, okS ? 1 : 0, tv, okT ? 1 : 0,
					g_crossHomeX + du * visW * scale, g_crossHomeY + dv * visH * scale);
				(void)okX; (void)okY;
			}
		}
	}

	static int ActiveCameraStateIndex(PlayerCameraView* cam)
	{
		if (!cam || !cam->cameraState)
			return -1;
		for (int i = 0; i < 13; ++i)
			if (cam->cameraStates[i] == cam->cameraState)
				return i;
		return -2;
	}

	static int   g_dbgPeekOpen = 0, g_dbgPeekCancel = 0;
	static int   g_peekHeldSide = 0; // the corner being peeked: +1 open to the right, -1 left
	static float g_dbgPeekClose = 0.0f;

	// ---- walls: retract (aiming only) + lead around corners (aimed, or a quarter from the hip) ----
	static void UpdateWalls(const Config& c, float dt, const NiPoint3& eye, const NiPoint3& fwdFlat,
		const NiPoint3& rightFlat, bool aiming, void*& world, char* logBuf, size_t logLen)
	{
		float strafe = 0.0f;
		if (KeyDown(c.leftKey) != KeyDown(c.rightKey))
			strafe = KeyDown(c.leftKey) ? -1.0f : 1.0f;

		float wallDist = c.probeForward, leftDist = c.sideProbe, rightDist = c.sideProbe;
		bool wallAhead = false, leftBlocked = false, rightBlocked = false;
		float retractTarget = 0.0f, peekTarget = 0.0f;

		// Runs from the hip too, at a quarter of the aimed lean. Retract stays aim-only.
		{
			world = RayCast::GetPlayerWorld(g_base);
			wallAhead = RayCast::Cast(g_base, world, eye, eye + fwdFlat * c.probeForward, wallDist);

			NiPoint3 ahead = eye + fwdFlat * std::min(wallDist + 20.0f, c.peekDetect);
			leftBlocked  = RayCast::Cast(g_base, world, eye, ahead - rightFlat * c.sideProbe, leftDist);
			rightBlocked = RayCast::Cast(g_base, world, eye, ahead + rightFlat * c.sideProbe, rightDist);

			if (wallAhead && aiming)
				retractTarget = std::clamp((c.retractStart - wallDist) / std::max(1.0f, c.retractStart - c.retractFull), 0.0f, 1.0f);

			bool nearCover = wallAhead && wallDist < c.peekDetect;
			// How committed the lean is, by how close the corner actually is: full against it,
			// tapering to nothing at Cover Detect Distance. Approach() smoothing alone is a TIME
			// ease, so a corner across the room leaned exactly as hard as one at arm's length.
			float closeness = 1.0f;
			if (wallAhead && c.peekDetect > c.peekFull)
				closeness = std::clamp((c.peekDetect - wallDist) / (c.peekDetect - c.peekFull), 0.0f, 1.0f);
			closeness = std::max(closeness, std::clamp(c.peekMinScale, 0.0f, 1.0f));

			// Automatic peek: in cover with exactly one side open, lean out to that side without
			// any key. A tap toward the wall side cancels it until the corner changes (other side
			// opens, or cover is lost), so you can step back in without it leaning straight out again.
			static int s_autoSide = 0, s_cancelSide = 0;
			// Find the edge itself with a fan of flat rays across the view. Fixed side probes only
			// worked square-on, and requiring cover near the middle of the fan missed a wall right
			// beside you that ends just ahead. Any adjacent pair of rays where one hits and the next
			// runs clear (or jumps well past it) is a candidate; bisecting between them finds where
			// the surface really stops. A real corner stops close; a long wall seen at a grazing
			// angle only "stops" at the edge of ray range, so it never counts.
			int openSide = 0;
			{
				constexpr int kRays = 25;          // -72..+72 deg, 6 deg apart: turned well into the wall, the edge sits far off to the side
				constexpr float kStepDeg = 6.0f;
				constexpr float kJump = 50.0f;
				const float range = c.peekDetect * 1.6f;
				const int mid = kRays / 2;
				auto dirAt = [&](float deg) {
					float r = deg * kDegToRad;
					return fwdFlat * std::cos(r) + rightFlat * std::sin(r);
				};
				auto cast = [&](float deg) {
					float d = range;
					if (!RayCast::Cast(g_base, world, eye, eye + dirAt(deg) * range, d))
						d = range;
					return d;
				};
				float dist[kRays];
				for (int i = 0; i < kRays; ++i)
					dist[i] = cast((i - mid) * kStepDeg);

				// Best edge on each side, by how far past it you are looking.
				float bestDist[2] = { range, range };
				float bestLook[2] = { 0.0f, 0.0f };
				float bestScore[2] = { 0.0f, 0.0f };
				for (int i = 0; i + 1 < kRays; ++i)
				{
					for (int dirSide = 0; dirSide < 2; ++dirSide)
					{
						// dirSide 0: wall at i, open at i+1 (open to the right); 1: the mirror.
						int hit = dirSide == 0 ? i : i + 1, open = dirSide == 0 ? i + 1 : i;
						if (dist[hit] >= range - 1.0f || !(dist[open] >= range - 1.0f || dist[open] - dist[hit] > kJump))
							continue;
						float aHit = (hit - mid) * kStepDeg, aOpen = (open - mid) * kStepDeg, dHit = dist[hit];
						for (int k = 0; k < 4; ++k)
						{
							float am = 0.5f * (aHit + aOpen), dm = cast(am);
							if (dm >= range - 1.0f || dm - dHit > kJump) aOpen = am;
							else { aHit = am; dHit = dm; }
						}
						// Which way the wall runs, from the edge point and a point further back along it.
						NiPoint3 pEdge = eye + dirAt(aHit) * dHit;
						int back = dirSide == 0 ? hit - 1 : hit + 1;
						NiPoint3 pBack = eye + dirAt((hit - mid) * kStepDeg) * dist[hit];
						if (back >= 0 && back < kRays && dist[back] < range - 1.0f && std::fabs(dist[back] - dist[hit]) < kJump)
							pBack = eye + dirAt((back - mid) * kStepDeg) * dist[back];
						NiPoint3 along = pEdge - pBack;
						along.z = 0.0f;
						// The edge only has to be in ray range: hugging a wall that ends a couple of metres
						// ahead is cover, even though the edge itself is further than Cover Detect Distance.
						// A long wall seen at a grazing angle "ends" only where the rays run out.
						if (dHit >= range * 0.9f)
							continue;
						// How close the cover is: the nearest ray on the wall's side of the edge.
						float wallNear = range;
						for (int j = hit; j >= 0 && j < kRays; j += (dirSide == 0 ? -1 : 1))
							wallNear = std::min(wallNear, dist[j]);
						if (wallNear >= c.peekDetect)
							continue;
						dHit = wallNear;
						// Turning into the wall is the control: looking along it (parallel) = no lean, turned
						// Peek Start Angle into it the lean begins, Full Lean Angle and beyond = full. A
						// doorway you walk past is a wall running parallel to you, so it never leans.
						float alen = Length(along);
						if (alen < 1.0f)
							continue;
						// Signed against the direction toward the edge: 0 = looking along the wall at the
						// edge, 90 = square to it, past 90 = turned on into the wall, away from the edge.
						// Unsigned, going past square read as turning back out and dropped the peek.
						float cosA = std::clamp(Dot(along * (1.0f / alen), fwdFlat), -1.0f, 1.0f);
						float angDeg = std::acos(cosA) / kDegToRad;
						float look = std::clamp((angDeg - c.peekStartDeg) / std::max(1.0f, c.peekFullDeg - c.peekStartDeg), 0.0f, 1.0f);
						float over = angDeg - (90.0f + c.peekOverDeg);
						if (over > 0.0f)
							look *= std::clamp(1.0f - over / 15.0f, 0.0f, 1.0f);
						// Rank by angle AND nearness: with a long Cover Detect Distance a far wall across the
						// room could out-rank the cover you are standing at.
						float prox = c.peekDetect > c.peekFull
							? std::clamp((c.peekDetect - dHit) / (c.peekDetect - c.peekFull), 0.0f, 1.0f) : 1.0f;
						float score = look * (0.25f + 0.75f * prox);
						if (score > bestScore[dirSide])
						{
							bestScore[dirSide] = score;
							bestLook[dirSide] = look;
							bestDist[dirSide] = dHit;
						}
					}
				}
				// Stay with the corner you are already peeking while it still counts. A wall usually
				// has an edge at each end; square to it, both read as full lean, and turning on into
				// it flipped the lean to the far end (logged: corner=+1/-1 alternating every second).
				int side = bestScore[0] >= bestScore[1] ? 0 : 1;
				int heldIdx = g_peekHeldSide == 1 ? 0 : (g_peekHeldSide == -1 ? 1 : -1);
				if (heldIdx >= 0 && bestScore[heldIdx] > 0.0f)
					side = heldIdx;
				if (bestLook[side] > 0.0f)
				{
					openSide = side == 0 ? 1 : -1;
					float cl = 1.0f;
					if (c.peekDetect > c.peekFull)
						cl = std::max(std::clamp((c.peekDetect - bestDist[side]) / (c.peekDetect - c.peekFull), 0.0f, 1.0f),
							std::clamp(c.peekMinScale, 0.0f, 1.0f));
					closeness = cl * bestLook[side];
				}
			}
			// Hold and smooth. One ray slipping past a sill or a post for a frame used to drop the
			// corner and snap the lean back: now a lost corner is held for a moment, a new one has
			// to be seen for a moment before it switches sides, and the strength eases.
			{
				int& s_heldSide = g_peekHeldSide;
				static float s_heldClose = 0.0f, s_lostFor = 0.0f, s_otherFor = 0.0f;
				constexpr float kHold = 0.35f, kSwitch = 0.15f, kEase = 8.0f;
				if (openSide == 0)
				{
					s_lostFor += dt;
					s_otherFor = 0.0f;
					if (s_lostFor < kHold)
						openSide = s_heldSide;
					else
						s_heldSide = 0;
				}
				else if (s_heldSide != 0 && openSide != s_heldSide)
				{
					s_otherFor += dt;
					s_lostFor = 0.0f;
					if (s_otherFor < kSwitch)
						openSide = s_heldSide;
					else
						s_heldSide = openSide, s_otherFor = 0.0f;
				}
				else
				{
					s_heldSide = openSide;
					s_lostFor = s_otherFor = 0.0f;
				}
				float want = openSide != 0 && openSide == s_heldSide && s_lostFor == 0.0f ? closeness : (openSide != 0 ? s_heldClose : 0.0f);
				s_heldClose += (want - s_heldClose) * std::clamp(kEase * dt, 0.0f, 1.0f);
				if (openSide != 0)
					closeness = s_heldClose;
			}
			if (openSide != s_autoSide)
			{
				s_autoSide = openSide;
				s_cancelSide = 0;
			}
			// A tap toward the wall steps back into cover for a moment, not for good. Held forever it
			// stuck once the hold filter above stopped the corner flickering - one sideways step along
			// the wall and the peek never came back.
			static float s_cancelLeft = 0.0f;
			if (openSide != 0 && strafe != 0.0f && (strafe > 0.0f ? 1 : -1) == -openSide)
			{
				s_cancelSide = openSide;
				s_cancelLeft = 1.0f;
			}
			else if (s_cancelSide != 0 && (s_cancelLeft -= dt) <= 0.0f)
				s_cancelSide = 0;
			g_dbgPeekOpen = openSide;
			g_dbgPeekClose = closeness;
			g_dbgPeekCancel = s_cancelSide;

			float lean = 0.0f;
			if (strafe > 0.0f && !rightBlocked && (nearCover || leftBlocked))
				lean = closeness;
			else if (strafe < 0.0f && !leftBlocked && (nearCover || rightBlocked))
				lean = -closeness;
			else if (openSide != 0 && s_cancelSide != openSide)
				lean = openSide * closeness;
			peekTarget = c.tCornerPeek ? lean * (aiming ? 1.0f : c.peekHipScale) : 0.0f;
		}

		// Springs, NOT Approach(). The peek target STEPS - it goes from 0 to full the instant the
		// strafe key and the two side rays agree - and Approach() is first-order, so at that step
		// its velocity jumps from 0 to its maximum in one frame. Measured off a capture: the
		// gun moved ~112 px at 1440p in each of two consecutive frames and then stopped dead.
		// A spring starts from zero velocity and builds, so the lean swings out instead of
		// snapping. This is the same fix, and the same reason, as the gun-cant snap in 1.0.6.
		SpringTo(g_gunPeek, g_gunPeekVel, peekTarget, c.gunRate, c.gunPeekDamping, dt);
		SpringTo(g_camPeek, g_camPeekVel, peekTarget, c.camRate, c.camPeekDamping, dt);
		g_retract = Approach(g_retract, retractTarget * (1.0f - std::fabs(g_gunPeek)), c.retractRate, dt);

		snprintf(logBuf, logLen, "wall=%d(%.0f) L=%d(%.0f) R=%d(%.0f) strafeKey=%.0f retract=%.2f peek=%.2f corner=%d close=%.2f cancel=%d",
			wallAhead, wallDist, leftBlocked, leftDist, rightBlocked, rightDist, strafe, g_retract, g_gunPeek,
			g_dbgPeekOpen, g_dbgPeekClose, g_dbgPeekCancel);
	}

	// Checks for assumptions that still need verifying in game. Logged every ~2 s.
	static void LogDiagnostics(PlayerCameraView* cam, NiAVObjectView* rig)
	{
		// Axis convention: camera forward should now match the player's heading.
		float playerHeading = 0.0f;
		void* player = *reinterpret_cast<void**>(g_base + Offsets::kRVA_g_player);
		if (player)
			playerHeading = reinterpret_cast<NiPoint3*>(reinterpret_cast<uintptr_t>(player) + Offsets::kOff_REFR_rot)->z;
		float headingDeg = Wrap(playerHeading) / kDegToRad;
		float camYawDeg = YawOf(g_trueRot.Forward()) / kDegToRad;

		// Feedback check: the game's own (pre-mod) camera should have ~0 roll in first person.
		NiPoint3 r = g_trueRot.Right(), u = g_trueRot.Up();
		float trueRollDeg = std::atan2(r.z, u.z) / kDegToRad;

		// Ray sanity: straight down from the eye must hit the floor.
		void* world = RayCast::GetPlayerWorld(g_base);
		float downDist = 0.0f;
		bool downHit = RayCast::Cast(g_base, world, g_trueEye, g_trueEye - kWorldUp * 400.0f, downDist);

		// Rig space: where the first-person rig actually sits, and its heading vs the camera's.
		// If its position stays near a constant while you walk, it's in its own local space; if
		// its heading stays fixed while you turn, that space is camera-local (not world-oriented).
		float rigYawDeg = YawOf(g_rigSeenRot.Forward()) / kDegToRad;
		NiPoint3 rigUp = g_rigSeenRot.Up();

		float playerPitchDeg = 0.0f;
		if (player)
			playerPitchDeg = reinterpret_cast<NiPoint3*>(reinterpret_cast<uintptr_t>(player) + Offsets::kOff_REFR_rot)->x / kDegToRad;
		float camPitchDeg = std::asin(std::clamp(g_trueRot.Forward().z, -1.0f, 1.0f)) / kDegToRad;

		CP_LOG("diag pitch: player=%.1f cam=%.1f", playerPitchDeg, camPitchDeg);
		CP_LOG("diag axes: playerHeading=%.1f camYaw=%.1f | trueRoll=%.2f (raw %.2f, shiftFollow=%.0f%s) | downRay hit=%d dist=%.1f | "
			"camState=%d rigFlags=0x%llX | calls cam=%d (repeat=%d) rig=%d (stale=%d) rigAfterCam=%.2fms | "
			"rigPos=(%.1f,%.1f,%.1f) rigYaw=%.1f rigUp=(%.2f,%.2f,%.2f) eye=(%.0f,%.0f,%.0f) mvDelta=%.3f",
			headingDeg, camYawDeg, trueRollDeg, g_rawRollDeg, g_shiftFollow, g_shiftDecided ? "" : "?", downHit, downDist,
			ActiveCameraStateIndex(cam), rig ? static_cast<unsigned long long>(rig->flags) : 0ull,
			g_camCalls, g_camRepeats, g_rigCalls, g_rigStale, g_rigAfterCamMs,
			g_rigSeenPos.x, g_rigSeenPos.y, g_rigSeenPos.z, rigYawDeg, rigUp.x, rigUp.y, rigUp.z,
			g_trueEye.x, g_trueEye.y, g_trueEye.z, g_mvDelta);

		g_camCalls = g_camRepeats = g_rigCalls = g_rigStale = 0;
	}

	// ---- camera root: the rendered view --------------------------------------------------
	static void WriteCameraNode(NiAVObjectView* node, const NiMatrix43& rot, const NiPoint3& pos);

	static void Hook_Camera(void* thisPtr, void* updateData)
	{
		OriginalFor(thisPtr)(thisPtr, updateData);

		__try { UpdateCrosshair(); }
		__except (EXCEPTION_EXECUTE_HANDLER) { CP_LOG("crosshair: caught access violation"); }

		__try
		{
			const Config& c = g_config;
			auto* node = reinterpret_cast<NiAVObjectView*>(thisPtr);
			auto* cam = *reinterpret_cast<PlayerCameraView**>(g_base + Offsets::kRVA_g_playerCamera);

			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			++g_camCalls;

			NiAVObjectView* rig = g_rigNode.load();
			void* player = *reinterpret_cast<void**>(g_base + Offsets::kRVA_g_player);

			// Climbing into or out of power armor replaces the first-person skeleton. The node we
			// hooked is then no longer the one the game renders, its transform is stale, and the
			// camera compensation below would be undoing a rotation that never reached the camera.
			// Sit out until MaintainHook picks up the new node (within ~300 ms).
			if (rig && player)
			{
				auto* liveRig = *reinterpret_cast<NiAVObjectView**>(
					reinterpret_cast<uintptr_t>(player) + Offsets::kOff_Player_firstPersonSkeleton);
				if (liveRig && liveRig != rig)
				{
					CP_LOG("first-person skeleton swapped (0x%p -> 0x%p) - motion paused until re-hooked", rig, liveRig);
					g_rigNode.store(nullptr);
					ResetMotion();
					g_active = false;
					g_haveEyeInRig = false;
					return;
				}
			}

			bool firstPerson = rig && (rig->flags & 1) == 0;

			// Which camera is actually driving? Only the plain first-person state is ours. Dialogue,
			// VATS, furniture, bleedout and the transitions between them are animated by the engine:
			// it pans the camera on its own, and reading that pan as though the player had turned
			// makes our lean and view lag fight the scripted move, with the eye pushed off the body.
			// The Museum of Freedom's top floor is one long conversation, which is exactly this.
			int camStateIdx = ActiveCameraStateIndex(cam);
			g_camStateIdx = camStateIdx;
			bool ownCamera = !c.firstPersonOnly || camStateIdx == Offsets::kCameraState_FirstPerson;
			bool menuOpen = c.suspendInMenus && SuspendingMenuOpen();
			if ((menuOpen ? 1 : 0) != g_prevMenuOpen)
			{
				CP_LOG("Pip-Boy/pause/workshop menu -> %s", menuOpen ? "open, fading out" : "closed");
				g_prevMenuOpen = menuOpen ? 1 : 0;
			}
			ownCamera = ownCamera && !menuOpen;
			if (camStateIdx != g_prevCamState)
			{
				CP_LOG("camera state -> %d (%s)", camStateIdx, ownCamera ? "ours" : "engine-driven, fading out");
				g_prevCamState = camStateIdx;
			}

			if ((firstPerson ? 1 : 0) != g_prevFirstPerson)
			{
				CP_LOG("first-person state -> %d (rig=0x%p rigFlags=0x%llX camState=%d)",
					firstPerson, rig, rig ? static_cast<unsigned long long>(rig->flags) : 0ull, ActiveCameraStateIndex(cam));
				g_prevFirstPerson = firstPerson ? 1 : 0;
			}

			NiMatrix43 rawRot = ToColumnForm(node->worldTransform.rot);
			NiPoint3 rawEye = node->worldTransform.pos;
			{
				NiPoint3 rr = rawRot.Right(), ru = rawRot.Up();
				g_rawRollDeg = std::atan2(rr.z, ru.z) / kDegToRad;
			}

			// Player body position: movement speed comes from here, NOT the camera, so neither
			// vanilla head sway nor our own gun motion (which leaks into the camera) reads as strafing.
			NiPoint3 actorPos = g_trueEye;
			if (player)
				actorPos = *reinterpret_cast<NiPoint3*>(reinterpret_cast<uintptr_t>(player) + Offsets::kOff_REFR_pos);

			// Undo what the gun rig pushed into the camera this frame (the rig updates first).
			bool rigThisFrame = g_rigAppliedTick.QuadPart > g_simTick.QuadPart;
			NiMatrix43 rigR = rigThisFrame ? g_rigAppliedRot : Identity();
			NiPoint3 rigS = rigThisFrame ? g_rigAppliedShift : NiPoint3{};
			NiPoint3 rigHold = rigThisFrame ? g_rigHoldShift : NiPoint3{};
			g_trueRot = Mul(Transposed(rigR), rawRot);

			if (c.cameraFollowsRigShift >= 0)
			{
				g_shiftFollow = static_cast<float>(c.cameraFollowsRigShift);
				g_shiftDecided = true;
			}
			else if (!g_shiftDecided && firstPerson)
			{
				NiPoint3 rel = rawEye - actorPos;
				if (g_haveShiftPrev)
				{
					NiPoint3 dRel = rel - g_prevEyeRel, dS = (rigS - rigHold) - g_prevRigShift;
					float dd = Dot(dS, dS);
					if (dd > 1e-4f && Length(dRel) < 20.0f)
					{
						g_shiftNum += Dot(dRel, dS);
						g_shiftDen += dd;
					}
				}
				g_prevEyeRel = rel;
				g_prevRigShift = rigS - rigHold;
				g_haveShiftPrev = true;
				if (g_shiftDen > 150.0f)
				{
					float k = g_shiftNum / g_shiftDen;
					g_shiftFollow = k > 0.5f ? 1.0f : 0.0f;
					g_shiftDecided = true;
					CP_LOG("Camera follows gun position shift: k=%.2f -> %s", k, g_shiftFollow > 0.5f ? "YES, compensating" : "no");
				}
			}
			g_trueEye = rawEye - rigS * g_shiftFollow;

			if (!firstPerson)
			{
				if (g_active)
					ResetMotion();
				g_active = false;
				g_haveEyeInRig = false;
				return;
			}

			// Second update of the camera within the same frame: reapply this frame's result
			// instead of simulating again with a near-zero timestep.
			float sinceSim = Seconds(g_simTick, now);
			if (g_active && g_simTick.QuadPart != 0 && sinceSim < kRepeatCallWindow)
			{
				++g_camRepeats;
				NiPoint3 recoilRight = Normalized(g_trueRot.Right());
				g_trueRot = Mul(AxisAngle(kWorldUp, g_recoil.aimYawDeg * kDegToRad),
					Mul(AxisAngle(recoilRight, g_recoil.aimPitchUpDeg * kDegToRad), g_trueRot));
				g_viewRot = Mul(g_viewDeltaRot, g_trueRot);
				g_viewEye = g_trueEye + g_viewDeltaPos;
				WriteCameraNode(node, g_viewRot, g_viewEye);
				return;
			}

			float dt = g_simTick.QuadPart != 0 ? sinceSim : 1.0f / 60.0f;
			g_simTick = now;
			bool hitch = dt > 0.1f;
			dt = std::clamp(dt, 1e-4f, 0.1f);
			g_frameDt = dt;
			g_time += dt;
			g_recoil = {};
			// An add-on registered through Bodycam_RegisterRecoilV1 replaces the built-in model
			// entirely, so the two can never both drive the pose.
			if (g_recoilHost.Update)
				g_recoilHost.Update(dt, KeyDown(c.aimKey), c.enabled && ownCamera, &g_recoil);
			else
				RecoilModel::Update(dt, KeyDown(c.aimKey), c.enabled && ownCamera, &g_recoil);
			// Diagnostic: is recoil switched on, are impulses arriving, and is anything coming out?
			// Prints while the springs are moving, plus once a second when they are not, so an
			// empty result is still evidence rather than silence.
			if (c.debugLog)
			{
				static LARGE_INTEGER s_lastRecoilLog{};
				static int s_loggedShots = -1;
				const float mag = std::fabs(g_recoil.aimPitchUpDeg) + std::fabs(g_recoil.gunPitchUpDeg)
					+ std::fabs(g_recoil.cameraPitchUpDeg) + std::fabs(g_recoil.gunBack);
				const bool moving = mag > 0.001f;
				const float since = s_lastRecoilLog.QuadPart ? Seconds(s_lastRecoilLog, now) : 999.0f;
				if ((moving && since > 0.05f) || since > 1.0f || RecoilModel::g_shots != s_loggedShots)
				{
					s_lastRecoilLog = now;
					s_loggedShots = RecoilModel::g_shots;
					CP_LOG("recoil: on=%d addon=%d shots=%d lastV0=%.0f | aim=%.2f gun=%.2f cam=%.2f back=%.2f"
						" | springs aim=%.2f wpn=%.2f cam=%.2f | active=%d blend=%.2f dt=%.4f",
						c.recoil ? 1 : 0, g_recoilHost.Update ? 1 : 0, RecoilModel::g_shots, RecoilModel::g_lastV0,
						g_recoil.aimPitchUpDeg, g_recoil.gunPitchUpDeg, g_recoil.cameraPitchUpDeg, g_recoil.gunBack,
						RecoilModel::g_aimPitch.x, RecoilModel::g_pitch.x, RecoilModel::g_camPitch.x,
						(c.enabled && ownCamera) ? 1 : 0, g_blend, dt);
				}
			}
			if (c.enabled && ownCamera)
			{
				NiPoint3 recoilRight = Normalized(g_trueRot.Right());
				g_trueRot = Mul(AxisAngle(kWorldUp, g_recoil.aimYawDeg * kDegToRad),
					Mul(AxisAngle(recoilRight, g_recoil.aimPitchUpDeg * kDegToRad), g_trueRot));
			}

			// Where the eye sits in the rig's own space, measured from the rig update that ran
			// earlier in this same frame. The rig hook uses it next frame.
			if (g_rigSeenTick.QuadPart != 0 && Seconds(g_rigSeenTick, now) < dt * 0.6f)
			{
				g_eyeInRig = Transposed(g_rigSeenRot).Mul(g_trueEye - g_rigSeenPos);
				g_haveEyeInRig = true;
			}

			NiPoint3 fwd   = Normalized(g_trueRot.Forward());
			NiPoint3 right = Normalized(g_trueRot.Right());
			NiPoint3 fwdFlat   = Normalized({ fwd.x, fwd.y, 0.0f });
			NiPoint3 rightFlat = Normalized({ right.x, right.y, 0.0f });
			float yaw = YawOf(fwd);
			float pitch = std::asin(std::clamp(fwd.z, -1.0f, 1.0f));

			// Turn rate comes from the BODY's heading, not the camera's. Weapon recoil, explosion
			// shake and getting hit all rotate the camera without the player having turned; fed into
			// the lean that reads as the horizon rocking on its own in a firefight. The two track
			// each other to ~0.1 deg in normal play (diag log), so the lean itself is unchanged.
			float bodyYaw = player
				? Wrap(reinterpret_cast<NiPoint3*>(reinterpret_cast<uintptr_t>(player) + Offsets::kOff_REFR_rot)->z)
				: yaw;
			float dBodyYaw = g_havePrev ? Wrap(bodyYaw - g_prevBodyYaw) : 0.0f;
			g_prevBodyYaw = bodyYaw;

			float dYaw = g_havePrev ? Wrap(yaw - g_prevYaw) : 0.0f;
			float dPitch = g_havePrev ? pitch - g_prevPitch : 0.0f;
			NiPoint3 moved = g_havePrev ? actorPos - g_prevActorPos : NiPoint3{};

			// Teleports, loads, menus closing after a long pause, the zoom into first person:
			// don't turn them into motion, and fade the effects back in afterwards.
			// A long fall covers a lot of ground vertically at a perfectly ordinary rate, so the
			// horizontal and vertical budgets are separate: sharing one made every drop re-trigger
			// the reset each frame, which is what turns a jump into a shuddering camera.
			float movedFlat = std::sqrt(moved.x * moved.x + moved.y * moved.y);
			if (hitch || std::fabs(dYaw) > 0.8f || movedFlat > 100.0f + 1500.0f * dt
				|| std::fabs(moved.z) > 100.0f + 6000.0f * dt)
			{
				static LARGE_INTEGER lastResetLog{};
				if (c.debugLog && Seconds(lastResetLog, now) > 0.25f)
				{
					CP_LOG("reset: hitch=%d dt=%.3f dYaw=%.2f movedFlat=%.1f movedZ=%.1f eye=(%.0f,%.0f,%.0f) prevEye=(%.0f,%.0f,%.0f)",
						hitch, dt, dYaw, movedFlat, moved.z, g_trueEye.x, g_trueEye.y, g_trueEye.z,
						g_prevEye.x, g_prevEye.y, g_prevEye.z);
					lastResetLog = now;
				}
				ResetMotion();
				dYaw = dPitch = dBodyYaw = 0.0f;
				moved = {};
			}
			g_prevYaw = yaw;
			g_prevPitch = pitch;
			g_prevEye = g_trueEye;
			g_prevActorPos = actorPos;
			g_havePrev = true;
			// Master switch: fade all motion out/in smoothly instead of snapping (frame-gen-safe).
			g_blend = Approach(g_blend, (c.enabled && ownCamera) ? 1.0f : 0.0f, c.blendInRate, dt);

			// Faded out on a scripted camera: hand it back bit-exact and stop the gun hook too.
			if (!ownCamera)
			{
				dYaw = dPitch = 0.0f;
				moved = {};
				if (g_blend < 0.005f)
				{
					ResetMotion();
					g_active = false;
					g_haveEyeInRig = false;
					return;
				}
			}

			NiPoint3 vel = moved * (1.0f / dt);
			g_fallSpeed = vel.z;
			vel.z = 0.0f;
			float speed = Length(vel);

			// Grounded test. Bob is a footstep effect: played while the body is in the air it reads
			// as floating rather than walking. The down-ray is the same one the diagnostics proved
			// works (~120 to the floor on foot, more in power armor); the vertical-speed term covers
			// the first frames off a ledge, before the ray has anything left to miss.
			void* world = RayCast::GetPlayerWorld(g_base);
			float groundDist = 0.0f;
			bool groundHit = RayCast::Cast(g_base, world, g_trueEye, g_trueEye - kWorldUp * c.groundedProbe, groundDist);
			bool airborne = !groundHit || std::fabs(g_fallSpeed) > c.airborneFallSpeed;
			g_grounded = Approach(g_grounded, airborne ? 0.0f : 1.0f, c.groundedRate, dt);

			// Leaving and hitting the ground. Both edges come off the RAW airborne test, not the
			// smoothed g_grounded, so the kick lands on the frame it happens instead of trailing the
			// blend. The landing's size is how fast you were actually falling, tracked as a peak
			// across the flight because by the frame the ray finds the floor the speed has gone.
			// TAKE-OFF is detected from upward speed, not from the ground ray. The ray probes 220
			// units and reads ~120 standing, so it only misses once you have risen ~100 units -
			// most of a vanilla jump - which is why the launch dip looked like it did nothing.
			bool rising = g_fallSpeed > c.jumpTakeoffSpeed;
			if (!g_liftedOff && (rising || airborne))
			{
				g_liftedOff = true;
				g_airTime = 0.0f;
				g_peakFall = 0.0f;
				if (c.jumpLaunchDip > 0.001f)
				{
					g_launchAge = 0.0f;
					g_launchAmp = c.jumpLaunchDip; // load through the legs, then extend
					g_launchNorm = EnvNorm(c.jumpLaunchRate, kCrouchRise);
				}
				CP_LOG("jump: take-off (rising=%d vz=%.0f airborne=%d)", rising ? 1 : 0, g_fallSpeed, airborne ? 1 : 0);
			}
			else if (g_liftedOff)
			{
				g_airTime += dt;
				g_peakFall = std::max(g_peakFall, -g_fallSpeed); // fallSpeed is negative going down
			}
			// LANDING needs to have been FALLING first. At take-off the ray still finds the floor
			// for the first ~100 units of rise, so "on the ground again" alone would fire a landing
			// on the very frame we left it.
			if (g_liftedOff && !airborne && g_peakFall > 50.0f)
			{
				// Hops and single stairs should not shake the camera, so a landing needs both a
				// minimum airtime and some downward speed to have built up.
				float force = std::clamp(g_peakFall / std::max(1.0f, c.jumpLandRefSpeed), 0.0f, 1.0f);
				if (g_airTime >= c.jumpMinAirTime && force > 0.01f)
				{
					if (c.jumpLandImpact > 0.001f)
					{
						g_jumpAge = 0.0f;
						g_jumpAmp = c.jumpLandImpact * force;
					}
					if (c.jumpLandShake > 0.001f)
					{
						g_landShakeAge = 0.0f;
						g_landShakeAmp = c.jumpLandShake * force;
					}
					if (c.jumpLandCrouch > 0.001f)
					{
						g_crouchAge = 0.0f;
						g_crouchAmp = c.jumpLandCrouch * force;
						g_crouchNorm = EnvNorm(c.jumpLandCrouchRate, kCrouchRise);
					}
					CP_LOG("jump: landed after %.2fs, peakFall=%.0f force=%.2f -> crouch=%.2f shake=%.2f",
						g_airTime, g_peakFall, force, g_crouchAmp, g_landShakeAmp);
				}
				g_liftedOff = false;
				g_airTime = 0.0f;
				g_peakFall = 0.0f;
			}
			g_wasAirborne = airborne;

			// Power armor drives its own weighty camera motion and sits the eye much higher;
			// full-strength bodycam on top of that is what makes it feel unstable.
			bool inPowerArmor = PlayerInPowerArmor(player);
			if (g_inPowerArmor != (inPowerArmor ? 1 : 0))
			{
				CP_LOG("power armor -> %d (motion scale %.2f)", inPowerArmor, inPowerArmor ? c.powerArmorScale : 1.0f);
				g_inPowerArmor = inPowerArmor ? 1 : 0;
			}
			g_paScale = Approach(g_paScale, inPowerArmor ? c.powerArmorScale : 1.0f, c.powerArmorRate, dt);
			g_amplitude = c.PresetScale() * g_paScale;
			float strafeSpeed = Dot(vel, rightFlat);

			g_yawRate = Approach(g_yawRate, dBodyYaw / dt, 20.0f, dt);
			g_strafeNorm = Approach(g_strafeNorm, std::clamp(strafeSpeed / c.strafeRefSpeed, -1.5f, 1.5f), 10.0f, dt);

			// View lag: the input moves the real aim now; the view gets there late.
			g_lagYaw -= dYaw;
			g_lagPitch -= dPitch;
			// Free-aim box (1.0.3): the gun (= the real aim, where shots go) roams freely inside a
			// box and the view only turns once it reaches the edge - no spring pulling it back.
			// Scopes draw a centred overlay, so the box collapses while one is up.
			bool scoped = MenuOpen(kMenu_Scope);
			g_freeAim = c.freeAim && !scoped;
			if (g_freeAim)
			{
				// Gun Speed: only this share of the mouse movement moves the gun across the box; the
				// rest turns the view straight away (lag above took the full amount, give some back).
				// Floor 0.3: lower, the view takes almost all the movement and Free Aim feels switched
				// off (2026-09-18 at 0.1: view moved at once, crosshair stayed centred).
				// The preset then scales it (Subtle 0.25 / Default 0.5 / Intense 0.75 at the default 0.5).
				float gunShare = std::clamp(std::clamp(c.freeAimSpeed, 0.3f, 1.0f) * c.FreeAimSpeedPresetScale(), 0.15f, 1.0f);
				g_lagYaw += dYaw * (1.0f - gunShare);
				g_lagPitch += dPitch * (1.0f - gunShare);
				float decay = std::exp(-c.freeAimRecenter * dt); // 0 = stays where you left it
				g_lagYaw *= decay;
				g_lagPitch *= decay;
				g_lagYawVel = g_lagPitchVel = 0.0f;
				ClampLag(g_lagYaw, g_lagYawVel, c.freeAimYawDeg * kDegToRad);
				ClampLag(g_lagPitch, g_lagPitchVel, c.freeAimPitchDeg * kDegToRad);
				// Screen float. Free Aim used to zero the velocity and apply the offset straight to
				// the view, so Catch-up Speed / Catch-up Smoothness did nothing here and the view
				// tracked the gun rigidly. Now the APPLIED offset chases the box offset on the same
				// under-damped spring the non-free-aim path uses, so the screen lags a fraction and
				// overshoots slightly when the gun stops - the float.
				if (c.viewFloat > 0.001f)
				{
					SpringTo(g_floatYaw, g_floatYawVel, g_lagYaw, c.lagFreq, c.lagDamping, dt);
					SpringTo(g_floatPitch, g_floatPitchVel, g_lagPitch, c.lagFreq, c.lagDamping, dt);
					// Max View Lag caps how far the view is allowed to trail the gun here too.
					// Before this it was read, but the free-aim branch never touched it, so the
					// slider did nothing at all whenever Free Aim was on - which is the default.
					const float lagMax = c.lagMaxDeg * kDegToRad;
					g_floatYaw   = g_lagYaw   + std::clamp(g_floatYaw   - g_lagYaw,   -lagMax, lagMax);
					g_floatPitch = g_lagPitch + std::clamp(g_floatPitch - g_lagPitch, -lagMax, lagMax);
				}
				else
				{
					g_floatYaw = g_lagYaw; g_floatPitch = g_lagPitch;
					g_floatYawVel = g_floatPitchVel = 0.0f;
				}
			}
			else
			{
				g_floatYaw = g_lagYaw; g_floatPitch = g_lagPitch; // float is a Free Aim effect only
				g_floatYawVel = g_floatPitchVel = 0.0f;
				float lagMax = c.lagMaxDeg * kDegToRad;
				if (scoped) // snap back onto the scope quickly but smoothly
				{
					float decay = std::exp(-20.0f * dt);
					g_lagYaw *= decay;
					g_lagPitch *= decay;
				}
				Spring(g_lagYaw, g_lagYawVel, c.lagFreq, c.lagDamping, dt);
				Spring(g_lagPitch, g_lagPitchVel, c.lagFreq, c.lagDamping, dt);
				ClampLag(g_lagYaw, g_lagYawVel, lagMax);
				ClampLag(g_lagPitch, g_lagPitchVel, lagMax);
			}

			// Weapon inertia: several kilos of metal does not change direction the instant the mouse
			// does. The gun swings AGAINST the turn by an amount proportional to how fast you are
			// turning, then springs back once you stop - high displacement, quick recovery. Damping
			// below 1 lets it settle past centre, which is what reads as organic rather than sprung.
			if (c.lookInertia > 0.001f && dt > 1e-4f)
			{
				float yawRateNow   = dYaw / dt;   // rad/s of mouse look this frame
				float pitchRateNow = dPitch / dt;
				float lim = c.lookInertiaMax;
				float tYaw   = std::clamp(-yawRateNow   * c.lookInertia, -lim, lim);
				float tPitch = std::clamp(-pitchRateNow * c.lookInertia, -lim, lim);
				SpringTo(g_swingYaw,   g_swingYawVel,   tYaw,   c.lookInertiaRate, c.lookInertiaDamping, dt);
				SpringTo(g_swingPitch, g_swingPitchVel, tPitch, c.lookInertiaRate, c.lookInertiaDamping, dt);
				g_swingYaw   = std::clamp(g_swingYaw,   -lim, lim);
				g_swingPitch = std::clamp(g_swingPitch, -lim, lim);
			}
			else
			{
				float decay = std::exp(-10.0f * dt);
				g_swingYaw *= decay; g_swingPitch *= decay;
				g_swingYawVel = g_swingPitchVel = 0.0f;
			}

			// Body roll into turns and strafes.
			g_turnRate = Deadzone(g_yawRate, c.turnDeadzone) * c.turnSensitivity;
			// The gun always leans on strafes; bStrafeTilt only takes the strafe part off the screen.
			float turnRoll = SoftLimit(g_turnRate * c.rollTurn, c.rollMaxDeg);
			float strafeRoll = g_strafeNorm * c.rollStrafe;
			float rollTarget = std::clamp(turnRoll + strafeRoll, -c.rollMaxDeg, c.rollMaxDeg);
			float rollScreenTarget = std::clamp(turnRoll + (c.strafeTilt ? strafeRoll : 0.0f), -c.rollMaxDeg, c.rollMaxDeg);
			g_roll = Approach(g_roll, rollTarget, c.rollRate, dt);
			g_rollScreen = Approach(g_rollScreen, rollScreenTarget, c.rollRate, dt);

			// Chest-mounted bob, per gait. Speed is smoothed so walk -> jog -> sprint blends instead
			// of popping (keeps frame generation happy), then amplitude/sway/rhythm are interpolated
			// between the walk, jog and sprint anchors. Below walk speed it fades out to standing.
			g_bobSpeed = Approach(g_bobSpeed, speed, 6.0f, dt);
			float gv, gl, ghz, gsy, gsr, gstep, gbAmp, gbHz, standFade;
			{
				const Config::Gait& w = c.walk, & j = c.jog, & r = c.sprint;
				float sp = g_bobSpeed;
				auto lerp = [](float a, float b, float t) { return a + (b - a) * t; };
				if (sp <= w.speed)
				{
					standFade = std::clamp(sp / std::max(1.0f, w.speed), 0.0f, 1.0f);
					gv = w.vertical; gl = w.lateral; ghz = w.cycleHz; gsy = w.swayYawDeg; gsr = w.swayRollDeg;
					gstep = w.stepImpact;
					// Breathing is the one thing that does NOT stop when you do, so below walking
					// pace it blends down to the standing pair instead of fading out.
					gbAmp = lerp(c.breathAmp, w.breathAmp, standFade);
					gbHz  = lerp(c.breathHz,  w.breathHz,  standFade);
				}
				else if (sp <= j.speed)
				{
					// Clamped: nothing stops a player setting Walk Speed above Jog Speed in the MCM,
					// and an unclamped t would then extrapolate the gait values instead of blending.
					float t = std::clamp((sp - w.speed) / std::max(1.0f, j.speed - w.speed), 0.0f, 1.0f);
					standFade = 1.0f;
					gv = lerp(w.vertical, j.vertical, t); gl = lerp(w.lateral, j.lateral, t); ghz = lerp(w.cycleHz, j.cycleHz, t);
					gsy = lerp(w.swayYawDeg, j.swayYawDeg, t); gsr = lerp(w.swayRollDeg, j.swayRollDeg, t);
					gstep = lerp(w.stepImpact, j.stepImpact, t);
					gbAmp = lerp(w.breathAmp, j.breathAmp, t); gbHz = lerp(w.breathHz, j.breathHz, t);
				}
				else
				{
					float t = std::clamp((sp - j.speed) / std::max(1.0f, r.speed - j.speed), 0.0f, 1.0f);
					standFade = 1.0f;
					gv = lerp(j.vertical, r.vertical, t); gl = lerp(j.lateral, r.lateral, t); ghz = lerp(j.cycleHz, r.cycleHz, t);
					gsy = lerp(j.swayYawDeg, r.swayYawDeg, t); gsr = lerp(j.swayRollDeg, r.swayRollDeg, t);
					gstep = lerp(j.stepImpact, r.stepImpact, t);
					gbAmp = lerp(j.breathAmp, r.breathAmp, t); gbHz = lerp(j.breathHz, r.breathHz, t);
				}
			}
			// Motion sickness: horizontal rocking is the part that triggers it, so drop the lateral
			// shift and the stride swing/tilt and keep the vertical bounce, footstep impact and
			// breathing exactly as they were.
			if (c.noSideMotion)
				gl = gsy = gsr = 0.0f;

			// Master scale over everything the movement produces, so "turn the bob off" is one
			// slider at 0 rather than five sliders across three paces.
			{
				const float mm = std::max(0.0f, c.moveMotion);
				gv *= mm; gl *= mm; gsy *= mm; gsr *= mm; gstep *= mm;
			}

			// Everything driven by standFade is footstep-shaped, so the whole lot fades with contact.
			standFade *= c.airborneBobScale + (1.0f - c.airborneBobScale) * g_grounded;
			float prevPhase = g_phase;
			g_phase = std::fmod(g_phase + ghz * dt * kTwoPi, kTwoPi);
			// Footfall impact: |sin| bottoms out at 0 and pi, i.e. once per foot. Crossing either one
			// starts a short damped kick that begins and ends at zero, so it stays frame-gen safe.
			if (gstep > 0.001f && standFade > 0.05f)
			{
				bool landed = (prevPhase < kPi && g_phase >= kPi) || g_phase < prevPhase; // pi, then wrap past 2pi
				if (landed)
				{
					g_stepAge = 0.0f;
					g_stepAmp = gstep * standFade; // size is the gait's own, fixed at the moment of landing
				}
			}
			g_stepAge += dt;
			float stepKick = 0.0f;
			if (g_stepAmp > 0.001f)
			{
				stepKick = -g_stepAmp * std::exp(-c.stepImpactDecay * g_stepAge)
					* std::sin(kTwoPi * c.stepImpactHz * g_stepAge); // first swing is downward
				if (g_stepAge > 1.0f)
					g_stepAmp = 0.0f;
			}
			// Jump launch dip / landing impact. Same damped-sine shape as the footstep kick and the
			// same sign convention (first swing downward), so a launch reads as the body loading and
			// a landing as the floor arriving. Deliberately NOT faded by standFade or g_grounded -
			// a jump happens precisely when neither of those is 1.
			g_jumpAge += dt;
			float jumpKick = 0.0f;
			if (g_jumpAmp > 0.001f)
			{
				jumpKick = -g_jumpAmp * std::exp(-c.jumpDecay * g_jumpAge)
					* std::sin(kTwoPi * c.jumpHz * g_jumpAge);
				if (g_jumpAge > 1.5f)
					g_jumpAmp = 0.0f;
			}
			// Landing absorb (view sinks and returns) and the extra weapon-only drop. Both ride the
			// same envelope so the hands and the body take the hit together; the gun just travels
			// further, which is what makes the movement readable at all - moving gun and view by the
			// same amount produces no relative motion on screen.
			// Push-off: the same single compression as the landing, not a ring - legs load and
			// extend, they do not bounce. Recovers on its own (faster) rate because you extend into
			// the jump rather than settling out of it.
			g_launchAge += dt;
			float launchKick = 0.0f;
			if (g_launchAmp > 0.001f)
			{
				float env = Env(c.jumpLaunchRate, kCrouchRise, g_launchAge, g_launchNorm);
				if (env > 0.001f)
					launchKick = -g_launchAmp * env;
				else if (g_launchAge > 0.2f)
					g_launchAmp = 0.0f;
			}
			g_crouchAge += dt;
			float crouchKick = 0.0f;
			g_landGunDip = 0.0f;
			if (g_crouchAmp > 0.001f)
			{
				float env = Env(c.jumpLandCrouchRate, kCrouchRise, g_crouchAge, g_crouchNorm);
				if (env > 0.001f)
				{
					crouchKick = -g_crouchAmp * env;
					// Same envelope, its own size, and scaled by the landing force already baked
					// into g_crouchAmp via c.jumpLandCrouch.
					if (c.jumpLandCrouch > 0.001f)
						g_landGunDip = c.jumpLandGunDip * (g_crouchAmp / c.jumpLandCrouch) * env;
				}
				else if (g_crouchAge > 0.2f)
				{
					g_crouchAmp = 0.0f;
				}
			}
			// Footfall-shaped bounce: |sin| dips sharply at each foot strike and rounds over mid-stride
			// (two dips per full left+right cycle), mean-centred so the view doesn't sink overall.
			float footfall = (std::fabs(std::sin(g_phase)) - 0.6366f) * 2.0f;
			g_breathPhase = std::fmod(g_breathPhase + kTwoPi * gbHz * dt, kTwoPi);
			float bobV = footfall * gv * standFade + stepKick + jumpKick + crouchKick + launchKick
				+ std::sin(g_breathPhase) * gbAmp;
			float bobL = std::sin(g_phase) * gl * standFade;
			float bobRoll = std::sin(g_phase) * c.bobRollDeg * standFade;
			// Stride sway (view only): swing and tilt toward the planted foot, once per left+right cycle.
			float swayYawDeg  = std::sin(g_phase) * gsy * standFade;
			float swayRollDeg = std::sin(g_phase) * gsr * standFade;

			char wallLog[180]{};
			bool aiming = c.enabled && KeyDown(c.aimKey);
			UpdateWalls(c, dt, g_trueEye, fwdFlat, rightFlat, aiming, world, wallLog, sizeof(wallLog));

			// Corner lean shift for the view, collision-clamped so it never enters geometry.
			float camShift = g_camPeek * c.camPeekDist;
			if (std::fabs(camShift) > 0.01f)
			{
				if (!world)
					world = RayCast::GetPlayerWorld(g_base);
				// The view is a near plane, not a point: it sits in front of the eye and spreads sideways.
				// One ray straight out from the eye missed a jamb just ahead of it, and the near plane
				// went through the wall. Cast from the eye and from where the near plane sits.
				float side = camShift > 0.0f ? 1.0f : -1.0f;
				float reach = std::fabs(camShift) + c.camMargin;
				float room = std::fabs(camShift);
				for (float ahead : { 0.0f, c.camMargin })
				{
					NiPoint3 from = g_trueEye + fwdFlat * ahead;
					float hitDist;
					if (RayCast::Cast(g_base, world, from, from + rightFlat * (side * reach), hitDist))
						room = std::min(room, std::max(0.0f, hitDist - c.camMargin));
				}
				camShift = side * room;
			}

			// Compose the rendered view: lag (yaw about world up, pitch about camera right), then
			// roll about the lagged forward. Everything scales with the fade-in.
			// Preset (MCM): Subtle x0.6 / Default x1.0 / Intense x1.5 on all motion amplitudes.
			float wv = c.viewMotion ? g_blend : 0.0f;
			float k = g_amplitude;
			// The box is a hard geometric limit, so the intensity preset must not scale it: at x1.5 the
			// view would swing the wrong way while the gun moves inside the box.
			float lagK = g_freeAim ? 1.0f : k;
			// Blend between tracking the gun rigidly and the sprung, floating version.
			float fl = g_freeAim ? std::clamp(c.viewFloat, 0.0f, 1.0f) : 0.0f;
			float lagYawUsed   = g_lagYaw   + (g_floatYaw   - g_lagYaw)   * fl;
			float lagPitchUsed = g_lagPitch + (g_floatPitch - g_lagPitch) * fl;
			float yawOffset = (lagYawUsed * lagK + swayYawDeg * kDegToRad * k) * wv;
			NiMatrix43 view = Mul(AxisAngle(kWorldUp, -yawOffset), Mul(AxisAngle(right, lagPitchUsed * lagK * wv), g_trueRot));
			// g_viewRollDeg drives the gun lean, so stride sway roll is deliberately kept out of it.
			g_viewRollDeg = (g_roll + bobRoll) * k * c.rollSign * wv + g_camPeek * c.camRollDeg * c.rollSign * wv;
			// Screen roll. g_viewRollDeg above drives the gun and is untouched by either camera switch.
			// The two switches are independent: bStrafeTilt removes only strafe lean from the screen
			// (g_rollScreen), bCameraTilt removes only the constant resting tilt. Turn lean, footstep
			// roll and stride sway roll always reach the screen. Both switches ease in/out (FG-safe).
			// Resting tilt: positive rolls the view right (right side down), negative left.
			// A body cam does not hang at one fixed angle all night - it works its way to one side,
			// sits there a while, then settles the other way. The side flips on a random dwell and
			// the value always EASES across, so it stays continuous and frame-generation safe.
			if (c.tiltAlternate > 0.01f)
			{
				g_tiltNext -= dt;
				if (g_tiltNext <= 0.0f)
				{
					// 0.5x to 1.5x the average dwell, so the flips never fall into a rhythm
					g_tiltNext = c.tiltAlternate * (0.5f + std::fabs(RandUnit()));
					g_tiltSign = -g_tiltSign;
				}
			}
			else
			{
				g_tiltSign = 1.0f;
			}
			g_restTilt = Approach(g_restTilt, c.cameraTilt ? c.cameraTiltDeg * g_tiltSign : 0.0f,
				c.tiltAlternate > 0.01f ? c.tiltDriftRate : c.rollRate, dt);
			float screenRollDeg = (g_rollScreen + bobRoll) * k * c.rollSign * wv + g_camPeek * c.camRollDeg * c.rollSign * wv;
			float rollTotal = (screenRollDeg + swayRollDeg * k * c.rollSign * wv + g_restTilt * wv) * kDegToRad;
			view = Mul(AxisAngle(Normalized(view.Forward()), rollTotal), view);
			if (c.enabled && ownCamera)
			{
				NiPoint3 recoilRight = Normalized(view.Right());
				view = Mul(AxisAngle(kWorldUp, g_recoil.cameraYawDeg * kDegToRad),
					Mul(AxisAngle(recoilRight, g_recoil.cameraPitchUpDeg * kDegToRad), view));
				view = Mul(AxisAngle(Normalized(view.Forward()), g_recoil.cameraRollDeg * kDegToRad), view);
			}

			// Landing shake. VIEW ONLY, applied here with the recoil rotations and deliberately not
			// folded into the rig, so it can never move the muzzle or the point of impact - the same
			// rule Camera Rattle follows. Three axes at different rates, because matched axes read as
			// one straight jolt rather than a shake.
			g_landShakeAge += dt;
			if (g_landShakeAmp > 0.001f && c.enabled && ownCamera)
			{
				// Fixed rate on purpose. This used to be derived from Jump Sharpness, so raising
				// that slider (to shape the ring) silently drove the shake to a 36 Hz buzz - which
				// is the opposite of what someone reaching for that slider wants.
				constexpr float kShakeHz = 9.0f, kShakeDecay = 12.0f;
				float env = g_landShakeAmp * std::exp(-kShakeDecay * g_landShakeAge);
				if (env > 0.001f)
				{
					float w = kTwoPi * kShakeHz * g_landShakeAge;
					float sp = env * std::sin(w) * wv;
					float sy = env * 0.79f * std::sin(w * 1.37f + 1.1f) * wv;
					float sr = env * 1.37f * std::sin(w * 0.79f + 2.3f) * wv;
					NiPoint3 shakeRight = Normalized(view.Right());
					view = Mul(AxisAngle(kWorldUp, sy * kDegToRad),
						Mul(AxisAngle(shakeRight, sp * kDegToRad), view));
					view = Mul(AxisAngle(Normalized(view.Forward()), sr * kDegToRad), view);
				}
				else
				{
					g_landShakeAmp = 0.0f;
				}
			}

			g_bobOffset = (rightFlat * bobL + kWorldUp * bobV) * (k * wv);
			g_viewRot = view;
			// Recoil punch: the shot shoves the eye back along the view and a touch up. Only the
			// built-in model supplies this - an external add-on drives the pose struct, which has no
			// positional field in v1, so it stays zero there.
			NiPoint3 punch{};
			if (!g_recoilHost.Update)
			{
				NiPoint3 vf = Normalized(g_trueRot.Forward());
				punch = (vf * -RecoilModel::g_punch.x) + kWorldUp * (RecoilModel::g_punch.x * 0.35f);
			}
			// A real lean pivots at the hips, so the eye comes down as it goes out. Moving straight
			// sideways read as rising up over the cover.
			g_peekEyeSide = camShift * wv;
			g_peekEyeDrop = std::fabs(g_camPeek) * c.camPeekDrop * wv;
			g_peekEyeShift = rightFlat * g_peekEyeSide - kWorldUp * g_peekEyeDrop;
			g_viewEye = g_trueEye + g_bobOffset + punch * wv + g_peekEyeShift;
			g_viewDeltaRot = Mul(view, Transposed(g_trueRot));
			g_viewDeltaPos = g_viewEye - g_trueEye;

			WriteCameraNode(node, g_viewRot, g_viewEye);
			g_active = true;

			static uint32_t s_peekLog = 0;
			if (c.debugLog && std::fabs(g_camPeek) > 0.05f && (s_peekLog++ % 30) == 0)
				CP_LOG("peek height: peek=%.2f ads=%.2f trueEye.z=%.1f viewEye.z=%.1f drop=%.1f side=%.1f | rigS=(%.1f,%.1f,%.1f) follow=%.2f gunRoll=%.1f eyeAboveFeet=%.1f",
					g_camPeek, g_holdAds, g_trueEye.z, g_viewEye.z, g_peekEyeDrop, g_peekEyeSide,
					g_rigAppliedShift.x, g_rigAppliedShift.y, g_rigAppliedShift.z, g_shiftFollow, g_gunRollDeg,
					player ? g_trueEye.z - reinterpret_cast<NiPoint3*>(reinterpret_cast<uintptr_t>(player) + Offsets::kOff_REFR_pos)->z : -1.0f);

			if (c.debugLog && (g_logCounter++ % kLogThrottle) == 0)
			{
				CP_LOG("fp blend=%.2f ground=%.2f(fall=%.0f d=%.0f) pa=%d(x%.2f) turn=%.2f lag=(%.1f,%.1f)deg yawRate=%.2f roll=%.1f viewLean=%.1f gunLean=%.1f speed=%.0f strafe=%.2f cant=%.1f "
					"rigFollowsView=%d(%s, %.2f/%.2f) aim=%d %s",
					g_blend, g_grounded, g_fallSpeed, groundHit ? groundDist : -1.0f, g_inPowerArmor, g_paScale,
					g_turnRate,
					g_lagYaw / kDegToRad, g_lagPitch / kDegToRad, g_yawRate, g_roll, g_viewRollDeg, g_gunRollDeg, speed, g_strafeNorm, g_cant,
					g_rigFollowsView, g_rigDecided ? "decided" : "sampling",
					g_rigScoreTrue, g_rigScoreView, aiming, wallLog);

				if ((g_logCounter / kLogThrottle) % kDiagEvery == 0)
					LogDiagnostics(cam, rig);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			CP_LOG("Hook_Camera: caught access violation, skipping this frame");
		}
	}

	// ---- first-person skeleton root: arms + gun -----------------------------------------
	// Writes the camera node's world transform AND the matching local transform. The game derives
	// the camera node's LOCAL rotation from the first-person rig (the resting gun cant showed up
	// there as a constant -3 deg), and something later in the frame rebuilds the rendered view from
	// that local value. Correcting only the world transform left the two disagreeing: the log read
	// world 0.00 / local -3.00 while the player saw the horizon tilted in hip fire.
	static void WriteCameraNode(NiAVObjectView* node, const NiMatrix43& rot, const NiPoint3& pos)
	{
		node->worldTransform.rot = ToGameForm(rot);
		node->worldTransform.pos = pos;
		auto* parent = *reinterpret_cast<NiAVObjectView**>(reinterpret_cast<uintptr_t>(node) + 0x28);
		if (!parent)
		{
			node->localTransform.rot = node->worldTransform.rot;
			node->localTransform.pos = pos;
			return;
		}
		NiMatrix43 pInv = Transposed(ToColumnForm(parent->worldTransform.rot));
		float ps = parent->worldTransform.scale;
		float inv = std::fabs(ps) > 1e-6f ? 1.0f / ps : 1.0f;
		node->localTransform.rot = ToGameForm(Mul(pInv, rot));
		node->localTransform.pos = pInv.Mul(pos - parent->worldTransform.pos) * inv;
	}

	// ==== 1.0.4: Weapon Hold =====================================================================
	// Per gun type: arms + gun pushed out along the line of sight (hip and sights separately), a hip
	// low ready that comes up when you shoot, and a viewmodel FOV change. Everything eases (FG-safe).
	// PAUSED 2026-09-18: Free Aim broke right after this shipped; Weapon Hold is held
	// fully inert (no shift, no low ready, FOV left alone) and hidden from the MCM until that is fixed.
	// 1.0.6: back on, with the weapon-FOV compensation fixing the aim.
	static constexpr bool kWeaponHoldPaused = false;
	static const char* const kHoldTypeNames[Config::kHoldTypes] = { "Pistol", "Rifle/SMG", "Shotgun", "Heavy", "Melee" };

	static bool KeywordFormHas(const uint8_t* kwForm, uint32_t formId)
	{
		if (!kwForm)
			return false;
		const uint8_t* const* kws = *reinterpret_cast<const uint8_t* const* const*>(kwForm + Offsets::kOff_KeywordForm_keywords);
		uint32_t n = *reinterpret_cast<const uint32_t*>(kwForm + Offsets::kOff_KeywordForm_count);
		for (uint32_t i = 0; kws && i < n && i < 256; ++i)
			if (kws[i] && *reinterpret_cast<const uint32_t*>(kws[i] + Offsets::kOff_Form_formID) == formId)
				return true;
		return false;
	}

	// The player's equipped hand weapon -> hold type (-1 = none / thrown only). Instance keywords come
	// first because weapon mods change them (a pipe gun's grip decides pistol vs rifle).
	static int DetectHoldType(uintptr_t player, bool* hasScope = nullptr)
	{
		auto* proc = *reinterpret_cast<uint8_t**>(player + Offsets::kOff_Actor_middleProcess);
		auto* data = proc ? *reinterpret_cast<uint8_t**>(proc + Offsets::kOff_Process_data08) : nullptr;
		if (!data)
			return -1;
		auto* entries = *reinterpret_cast<uint8_t**>(data + Offsets::kOff_Data08_equipData);
		uint32_t count = *reinterpret_cast<uint32_t*>(data + Offsets::kOff_Data08_equipData + 0x10);
		for (uint32_t i = 0; entries && i < count && i < 8; ++i)
		{
			uint8_t* e = entries + i * Offsets::kEquipDataSize;
			auto* item = *reinterpret_cast<uint8_t**>(e);
			if (!item || item[Offsets::kOff_Form_formType] != Offsets::kFormType_WEAP)
				continue;
			auto* inst = *reinterpret_cast<uint8_t**>(e + 0x08);
			const uint8_t* ik = inst ? *reinterpret_cast<uint8_t**>(inst + Offsets::kOff_WeapInstance_keywords) : nullptr;
			const uint8_t* bk = item + Offsets::kOff_WEAP_keywordForm;
			auto has = [&](uint32_t id) { return KeywordFormHas(ik, id) || KeywordFormHas(bk, id); };
			if (has(Offsets::kKW_WeaponTypeThrown) || has(Offsets::kKW_WeaponTypeGrenade) || has(Offsets::kKW_WeaponTypeMine))
				continue;
			// The assault rifle's iron sights add HasScope too (seen in game and in Fallout4.esm), so
			// iron sights are ruled out by their own keyword.
			if (hasScope)
				*hasScope = has(Offsets::kKW_HasScope) && !has(Offsets::kKW_HasIronSights);
			if (has(Offsets::kKW_WeaponTypeMelee1H) || has(Offsets::kKW_WeaponTypeMelee2H)
				|| has(Offsets::kKW_WeaponTypeUnarmed) || has(Offsets::kKW_WeaponTypeHandToHand))
				return 4;
			if (has(Offsets::kKW_WeaponTypeHeavyGun)) return 3;
			if (has(Offsets::kKW_WeaponTypeShotgun))  return 2;
			if (has(Offsets::kKW_WeaponTypePistol))   return 0;
			return 1; // rifles, SMGs and any unlabelled gun
		}
		return -1;
	}

	static int SafeDetectHoldType(uintptr_t player, bool* hasScope = nullptr)
	{
		__try { return player ? DetectHoldType(player, hasScope) : -1; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
	}

	// Is the player's equipped weapon a thrown one (grenade / mine / thrown)? DetectHoldType skips
	// those and returns -1, which is the same answer it gives for "nothing equipped" - the throw fix
	// must not fire in that second case, so it gets its own check rather than reusing -1.
	static bool ThrownEquipped(uintptr_t player)
	{
		auto* proc = *reinterpret_cast<uint8_t**>(player + Offsets::kOff_Actor_middleProcess);
		auto* data = proc ? *reinterpret_cast<uint8_t**>(proc + Offsets::kOff_Process_data08) : nullptr;
		if (!data)
			return false;
		auto* entries = *reinterpret_cast<uint8_t**>(data + Offsets::kOff_Data08_equipData);
		uint32_t count = *reinterpret_cast<uint32_t*>(data + Offsets::kOff_Data08_equipData + 0x10);
		for (uint32_t i = 0; entries && i < count && i < 8; ++i)
		{
			uint8_t* e = entries + i * Offsets::kEquipDataSize;
			auto* item = *reinterpret_cast<uint8_t**>(e);
			if (!item || item[Offsets::kOff_Form_formType] != Offsets::kFormType_WEAP)
				continue;
			auto* inst = *reinterpret_cast<uint8_t**>(e + 0x08);
			const uint8_t* ik = inst ? *reinterpret_cast<uint8_t**>(inst + Offsets::kOff_WeapInstance_keywords) : nullptr;
			const uint8_t* bk = item + Offsets::kOff_WEAP_keywordForm;
			auto has = [&](uint32_t id) { return KeywordFormHas(ik, id) || KeywordFormHas(bk, id); };
			if (has(Offsets::kKW_WeaponTypeThrown) || has(Offsets::kKW_WeaponTypeGrenade) || has(Offsets::kKW_WeaponTypeMine))
				return true;
		}
		return false;
	}

	static bool SafeThrownEquipped(uintptr_t player)
	{
		__try { return player ? ThrownEquipped(player) : false; }
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}

	// Viewmodel FOV = the player's normal weapon FOV (read from the game INIs, never learned from
	// the live value) + our eased offset. 1.0.4 adopted any outside change as the new base, and the
	// game re-applying our own value ratcheted it up to the clamp - which then got saved into the
	// save game and survived uninstalling. The base now comes only from the INIs, and it is written
	// back right before every save (OnPreSave) and whenever the Pip-Boy / pause menu is open.
	static float g_fovIniBase = -1.0f;

	static float ReadIniFov(const char* key = "fDefault1stPersonFOV")
	{
		char docs[MAX_PATH] = {};
		float v = 80.0f; // vanilla default
		if (SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, 0, docs) != S_OK)
			return v;
		// Later files override earlier ones, same order as the game.
		for (const char* name : { "Fallout4.ini", "Fallout4Prefs.ini", "Fallout4Custom.ini" })
		{
			char path[MAX_PATH * 2], buf[64];
			snprintf(path, sizeof(path), "%s\\My Games\\Fallout4\\%s", docs, name);
			if (GetPrivateProfileStringA("Display", key, "", buf, sizeof(buf), path) > 0)
			{
				float f = static_cast<float>(atof(buf));
				if (f >= 30.0f && f <= 150.0f)
					v = f;
			}
		}
		return v;
	}

	static float* LiveFov()
	{
		auto* cam = *reinterpret_cast<uint8_t**>(g_base + Offsets::kRVA_g_playerCamera);
		return cam ? reinterpret_cast<float*>(cam + Offsets::kOff_PlayerCamera_fov1st) : nullptr;
	}

	// World FOV, for the log only: it is what third person and the world camera use, never the
	// first-person projection (measured 2026-09-19).
	static float WorldFov()
	{
		auto* cam = *reinterpret_cast<uint8_t**>(g_base + Offsets::kRVA_g_playerCamera);
		return cam ? *reinterpret_cast<float*>(cam + Offsets::kOff_PlayerCamera_fovWorld) : -1.0f;
	}

	static void UpdateViewmodelFov(float offset, float dt, const Config& c)
	{
		float* fov = LiveFov();
		if (!fov)
			return;
		if (g_fovIniBase < 0.0f)
		{
			g_fovIniBase = ReadIniFov();
			CP_LOG("weapon hold: normal weapon FOV from INI = %.1f (live value %.1f)", g_fovIniBase, *fov);
		}
		g_fovBase = g_fovIniBase;
		if (g_fovOwned && std::fabs(*fov - g_fovWritten) > 0.01f)
			CP_LOG("weapon hold: weapon FOV changed outside Bodycam (%.1f -> %.1f); keeping INI base %.1f", g_fovWritten, *fov, g_fovBase);

		// Pip-Boy / pause: the Pip-Boy view reads this same FOV when it opens, so hand the normal
		// value back instantly (the pause menu is also where saving and quitting happen).
		bool release = SuspendingMenuOpen();
		if (!release)
			g_fovApplied = Approach(g_fovApplied, offset, c.holdBlendRate, dt);
		if (release || (std::fabs(offset) < 0.001f && std::fabs(g_fovApplied) < 0.02f))
		{
			if (g_fovOwned)
				*fov = g_fovBase;
			g_fovOwned = false;
			if (release)
				g_fovApplied = 0.0f;
			return;
		}
		float v = std::clamp(g_fovBase + g_fovApplied, 40.0f, 150.0f);
		*fov = v;
		g_fovWritten = v;
		g_fovOwned = true;
	}

	struct HoldPose { float forward, drop, pitchDeg, side, yawDeg = 0.0f, lrDrop = 0.0f, lrSide = 0.0f,
		hipForward = 0.0f, hipHeight = 0.0f, hipSide = 0.0f, level = 0.0f, armShare = 1.0f; };

	// 1.0.5 probe live tuning (numpad): forward / side / drop in game units, FOV offset in degrees.
	static float g_probeFwd = 15.0f, g_probeSide = 0.0f, g_probeDrop = 6.0f, g_probeFov = 0.0f;
	static float g_probeEF = 0.0f, g_probeES = 0.0f, g_probeED = 0.0f, g_holdSide = 0.0f;

	static void ProbeKeys()
	{
		struct K { int vk; float* v; float step; };
		static const K keys[] = {
			{ VK_NUMPAD8, &g_probeFwd, 2.0f },  { VK_NUMPAD2, &g_probeFwd, -2.0f },
			{ VK_NUMPAD6, &g_probeSide, 1.0f }, { VK_NUMPAD4, &g_probeSide, -1.0f },
			{ VK_NUMPAD3, &g_probeDrop, 1.0f }, { VK_NUMPAD9, &g_probeDrop, -1.0f },
			{ VK_ADD, &g_probeFov, 5.0f },      { VK_SUBTRACT, &g_probeFov, -5.0f },
		};
		static bool was[sizeof(keys) / sizeof(keys[0])] = {};
		for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
		{
			bool d = KeyDown(keys[i].vk);
			if (d && !was[i])
			{
				*keys[i].v += keys[i].step;
				CP_LOG("probe tune: forward=%.0f side=%.0f drop=%.0f fov=%+.0f", g_probeFwd, g_probeSide, g_probeDrop, g_probeFov);
			}
			was[i] = d;
		}
	}

	static HoldPose UpdateWeaponHold(float dt, float wg, const Config& c)
	{
		uintptr_t player = reinterpret_cast<uintptr_t>(*reinterpret_cast<void**>(g_base + Offsets::kRVA_g_player));
		bool hasScope = false;
		int type = ((!kWeaponHoldPaused && c.weaponHold) || c.holdProbe) ? SafeDetectHoldType(player, &hasScope) : -1;
		static bool s_hadScope = false;
		if (type != g_holdType || hasScope != s_hadScope)
		{
			CP_LOG("weapon hold: %s%s", type >= 0 ? kHoldTypeNames[type] : (c.weaponHold ? "vanilla (no hand weapon)" : "off"),
				hasScope ? " (scoped)" : "");
			g_holdType = type;
			s_hadScope = hasScope;
		}
		// Applies before the scope overlay opens (raising the gun) and all the time with scopes that
		// never open it; once the overlay is up, h is null anyway.
		const float scopeK = hasScope ? std::clamp(c.holdScoped, 0.0f, 1.0f) : 1.0f;
		bool scoped = MenuOpen(kMenu_Scope);
		const Config::Hold* h = (type >= 0 && !scoped) ? &c.hold[type] : nullptr;

		// 1.0.5 probe: exaggerated values that switch on/off every 4 s, so one launch shows whether the
		// forward push and the viewmodel FOV reach the screen at all. Eased like everything else (FG-safe).
		static Config::Hold kProbeOn{};
		static const Config::Hold kProbeOff{};
		if (c.holdProbe)
		{
			ProbeKeys();
			kProbeOn.sightsForward = g_probeFwd;
			kProbeOn.fovOffset = g_probeFov;
		}
		bool probePhase = false;
		if (c.holdProbe && h)
		{
			// F10 toggles (starts ON). Manual so either state can be held as long as needed.
			static bool s_on = true, s_wasDown = false;
			bool down = KeyDown(VK_F10);
			if (down && !s_wasDown)
			{
				s_on = !s_on;
				CP_LOG("probe: F10 -> %s", s_on ? "ON" : "off");
			}
			s_wasDown = down;
			probePhase = s_on;
			h = probePhase ? &kProbeOn : &kProbeOff;
		}

		bool aiming = KeyDown(c.aimKey);
		g_holdAds = Approach(g_holdAds, aiming ? 1.0f : 0.0f, c.holdBlendRate, dt);

		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);

		// Low ready: its own switch, any hand weapon, whether or not Weapon Hold is on. Down and to
		// the left while idle or moving; up the moment you shoot, swing or aim, down again after the
		// delay. Only turns and drops the gun - no forward push - so the arm-mesh stump stays hidden.
		int lrType = c.lowReady ? (type >= 0 ? type : SafeDetectHoldType(player)) : -1;
		bool recentShot = g_lastShotTick.QuadPart != 0 && Seconds(g_lastShotTick, now) < c.lowReadyDelay;
		bool raised = aiming || recentShot || KeyDown(VK_LBUTTON) || scoped;
		float lowTarget = (lrType >= 0 && !raised && !c.holdProbe) ? 1.0f : 0.0f;
		g_lowReady = Approach(g_lowReady, lowTarget, lowTarget > g_lowReady ? c.lowReadyLowerRate : c.lowReadyRaiseRate, dt);

		// Sights distance while aiming, the per-type hip pose otherwise. Guns ship with hip at 0.
		float hipW = 1.0f - g_holdAds;
		float fwdTarget = h ? h->sightsForward * scopeK * g_holdAds : 0.0f;

		g_holdForward = Approach(g_holdForward, fwdTarget, c.holdBlendRate, dt);
		g_holdDrop = Approach(g_holdDrop, 0.0f, c.holdBlendRate, dt);
		g_holdSide = Approach(g_holdSide, 0.0f, c.holdBlendRate, dt);
		// Hip pose goes out as a hands-only shift (see the eye bone hold-back in Hook_Rig). Through
		// hold.forward/drop/side it moved the eye with the rig and never showed on screen.
		static float s_hipFwd = 0.0f, s_hipHeight = 0.0f, s_hipSide = 0.0f;
		s_hipFwd    = Approach(s_hipFwd,    h ? h->hipForward * hipW : 0.0f, c.holdBlendRate, dt);
		s_hipHeight = Approach(s_hipHeight, h ? h->hipHeight * hipW : 0.0f,  c.holdBlendRate, dt);
		// Hip Raise: comes up at the low ready raise speed while firing, settles at its lower speed.
		static float s_hipRaise = 0.0f;
		bool firing = recentShot || KeyDown(VK_LBUTTON);
		float raiseTarget = (h && firing) ? h->hipRaise * hipW : 0.0f;
		s_hipRaise = Approach(s_hipRaise, raiseTarget, raiseTarget > s_hipRaise ? c.lowReadyRaiseRate : c.lowReadyLowerRate, dt);
		// Same timing for levelling the muzzle. Weapon Hold or not, since the
		// resting pitch applies either way.
		static float s_level = 0.0f;
		float levelTarget = firing ? std::clamp(c.hipFireLevel, 0.0f, 1.0f) : 0.0f;
		s_level = Approach(s_level, levelTarget, levelTarget > s_level ? c.lowReadyRaiseRate : c.lowReadyLowerRate, dt);
		s_hipSide   = Approach(s_hipSide,   h ? h->hipSide * hipW : 0.0f,    c.holdBlendRate, dt);
		static float s_hipPitch = 0.0f;
		s_hipPitch = Approach(s_hipPitch, h ? h->hipPitchDeg * hipW : 0.0f, c.holdBlendRate, dt);

		// Low ready pose follows the weapon type, eased so a swap never snaps it.
		static float s_lrPitch = 0.0f, s_lrYaw = 0.0f, s_lrDrop = 0.0f, s_lrSide = 0.0f, s_lrArm = 1.0f;
		if (lrType >= 0)
		{
			const Config::Hold& lr = c.hold[lrType];
			s_lrPitch = Approach(s_lrPitch, lr.lrPitchDeg, c.holdBlendRate, dt);
			s_lrYaw   = Approach(s_lrYaw,   lr.lrYawDeg,   c.holdBlendRate, dt);
			s_lrDrop  = Approach(s_lrDrop,  lr.lrDrop,     c.holdBlendRate, dt);
			s_lrSide  = Approach(s_lrSide,  lr.lrSide,     c.holdBlendRate, dt);
			s_lrArm   = Approach(s_lrArm,   lr.lrArmShare, c.holdBlendRate, dt);
		}

		// One-time: a float window around PlayerCamera, looking for a first-person render FOV that
		// is NOT the one at +0x16C. Anything near the world FOV, the weapon FOV or a plausible
		// vertical FOV is worth chasing.
		static bool s_camDump = false;
		if (c.debugLog && !s_camDump)
		{
			if (auto* cd = *reinterpret_cast<uint8_t**>(g_base + Offsets::kRVA_g_playerCamera))
			{
				s_camDump = true;
				for (uintptr_t off = 0x140; off < 0x1E0; off += 0x10)
					CP_LOG("cam floats: +0x%03llX  %10.3f %10.3f %10.3f %10.3f",
						static_cast<unsigned long long>(off),
						*reinterpret_cast<float*>(cd + off + 0x0), *reinterpret_cast<float*>(cd + off + 0x4),
						*reinterpret_cast<float*>(cd + off + 0x8), *reinterpret_cast<float*>(cd + off + 0xC));
			}
		}

		// Sights value while aiming, Hip FOV otherwise. This value is the whole first-person view's
		// FOV, so the crosshair projection reads it live (see UpdateCrosshair) instead of trusting
		// the cached frustum.
		__try { UpdateViewmodelFov(h ? (h->fovOffset * scopeK * g_holdAds + h->hipFov * (1.0f - g_holdAds)) * wg : 0.0f, dt, c); }
		__except (EXCEPTION_EXECUTE_HANDLER) {}

		static LARGE_INTEGER s_lastLog{};
		if (c.debugLog && (s_lastLog.QuadPart == 0 || Seconds(s_lastLog, now) > (c.holdProbe ? 0.5f : 2.0f)))
		{
			s_lastLog = now;
			auto* cam = *reinterpret_cast<uint8_t**>(g_base + Offsets::kRVA_g_playerCamera);
			CP_LOG("hold: type=%s ads=%.2f lowReady=%.2f fwd=%.1f drop=%.1f side=%.1f dip=%.1f | fov now=%.1f base=%.1f +%.1f owned=%d",
				type >= 0 ? kHoldTypeNames[type] : "none", g_holdAds, g_lowReady, g_holdForward, g_holdDrop, g_holdSide,
				s_lrPitch * g_lowReady,
				cam ? *reinterpret_cast<float*>(cam + Offsets::kOff_PlayerCamera_fov1st) : -1.0f,
				g_fovBase, g_fovApplied, g_fovOwned ? 1 : 0);
			if (c.holdProbe)
				CP_LOG("probe: phase=%s shiftFollow=%.2f", probePhase ? "ON " : "off", g_shiftFollow);
		}
		// forward/drop are final shifts (already faded by wg); pitchDeg is faded by the caller with the rest of the pitch.
		if (c.holdProbe)
		{
			bool on = h && probePhase;
			g_probeEF = Approach(g_probeEF, on ? g_probeFwd : 0.0f, c.holdBlendRate, dt);
			g_probeES = Approach(g_probeES, on ? g_probeSide : 0.0f, c.holdBlendRate, dt);
			g_probeED = Approach(g_probeED, on ? g_probeDrop : 0.0f, c.holdBlendRate, dt);
			return { g_probeEF * wg, g_probeED * wg, 0.0f, g_probeES * wg };
		}
		return { g_holdForward * wg, g_holdDrop * wg, s_lrPitch * g_lowReady - s_hipPitch * (1.0f - s_level),
			g_holdSide * wg, s_lrYaw * g_lowReady * wg, s_lrDrop, s_lrSide,
			s_hipFwd * wg, (s_hipHeight + s_hipRaise) * wg, s_hipSide * wg, s_level, s_lrArm };
	}

	// COM probe. The rig hierarchy - note 'Camera' (the eye) is a SIBLING of COM, not under it:
	//   Root -+- COM - SPINE1/2 - Chest -+- LArm... - PipboyBone
	//         |                          `- RArm... - RArm_Hand - Weapon - ProjectileNode
	//         +- Camera          <- the eye, z=120.4
	//         `- Camera Control
	// Gun motion goes on their shared parent, the root, which is why the camera inherits it. Writing
	// COM instead would leave the eye alone - if a mid-tree write survives the engine's recompute,
	// which is what this tests. Read the log's `com probe:` weaponYaw across modes, not the screen.
	// Answered since: mid-tree writes are discarded, see the note above Hook_Rig.
	static void ComProbe(NiAVObjectView* node, int mode, const Config& c)
	{
		NiAVObjectView* com = FindNode(node, "COM", 0);
		NiAVObjectView* eye = FindNode(node, "Camera", 0);
		NiAVObjectView* gun = FindNode(node, "Weapon", 0);
		if (!eye)
			return;
		const NiPoint3 eyePos = eye->worldTransform.pos;

		static LARGE_INTEGER s_last{};
		LARGE_INTEGER now;
		QueryPerformanceCounter(&now);
		if (c.debugLog && (s_last.QuadPart == 0 || Seconds(s_last, now) > 1.0f))
		{
			s_last = now;
			float weaponYaw = 0.0f, weaponDist = 0.0f;
			if (gun)
			{
				NiPoint3 d = gun->worldTransform.pos - eyePos;
				weaponYaw = std::atan2(d.x, d.y) / kDegToRad;
				weaponDist = Length(d);
			}
			CP_LOG("com probe: mode=%d (%s) | COM=(%.2f,%.2f,%.2f) Camera=(%.2f,%.2f,%.2f) Weapon=(%.2f,%.2f,%.2f) | weaponYaw=%.2f dist=%.2f",
				mode, mode == 1 ? "rotate COM" : (mode == 2 ? "rotate ROOT (control)" : "off"),
				com ? com->worldTransform.pos.x : 0.0f, com ? com->worldTransform.pos.y : 0.0f, com ? com->worldTransform.pos.z : 0.0f,
				eyePos.x, eyePos.y, eyePos.z,
				gun ? gun->worldTransform.pos.x : 0.0f, gun ? gun->worldTransform.pos.y : 0.0f, gun ? gun->worldTransform.pos.z : 0.0f,
				weaponYaw, weaponDist);
		}

		NiAVObjectView* target = (mode == 2) ? node : com;
		if (!target)
			return;
		// A fixed, unmistakable yaw about the eye. Rig space is world-oriented, so world up is the
		// yaw axis here exactly as it is for the real gun pose.
		NiMatrix43 r = AxisAngle(kWorldUp, 15.0f * kDegToRad);
		NiTransform& t = target->worldTransform;
		t.pos = eyePos + r.Mul(t.pos - eyePos);
		t.rot = ToGameForm(Mul(r, ToColumnForm(t.rot)));
	}

	// F8 cycles the COM probe: off -> rotate COM -> rotate ROOT (control) -> off.
	static int g_comProbeMode = -1; // -1 = follow the ini
	static void ComProbeKey(const Config& c)
	{
		static bool was = false;
		bool down = KeyDown(VK_F8);
		if (down && !was)
		{
			int cur = g_comProbeMode < 0 ? c.comProbe : g_comProbeMode;
			g_comProbeMode = (cur + 1) % 3;
			CP_LOG("com probe: F8 -> mode %d (%s)", g_comProbeMode,
				g_comProbeMode == 1 ? "rotate COM" : (g_comProbeMode == 2 ? "rotate ROOT (control)" : "off"));
		}
		was = down;
	}

	// MEASURED 2026-09-20 - ONLY THE RIG ROOT'S TRANSFORM SURVIVES. The engine recomputes every node
	// from the root, so mid-tree writes are discarded. Proof: scaling LArm_UpperArm 1.10 left
	// shoulder-to-hand at 24.8 units (would be 27.3) and never compounded across frames.
	// So the arms cannot be lengthened to hide the first-person cut-off, and gun motion cannot move
	// to COM to decouple the eye. Neither is worth re-attempting.
	static void Hook_Rig(void* thisPtr, void* updateData)
	{
		OriginalFor(thisPtr)(thisPtr, updateData);

		__try
		{
			if (!g_active)
				return;

			// Only the skeleton the game currently renders may drive the gun. A swapped-out node
			// (power armor entry/exit) keeps our shadow vtable and can still be updated; letting it
			// write g_rigApplied* would make the camera hook undo a rotation the camera never got.
			if (thisPtr != g_rigNode.load())
				return;

			const Config& c = g_config;
			auto* node = reinterpret_cast<NiAVObjectView*>(thisPtr);

			// One-time: dump the weapon/arms node tree with each node's position (rig space, camera at
			// the origin). Iron sights are pure geometry - firing along the line the sights themselves
			// define lands the shot where they cover, whatever the FOV does - so we need to know which
			// nodes exist to build that line from.
			static bool s_nodeDump = false;
			if (c.debugLog && !s_nodeDump && FindNode(node, "ProjectileNode", 0)) // wait for a drawn gun
			{
				s_nodeDump = true;
				int count = 0;
				DumpNodes(node, 0, count);
				CP_LOG("nodes: %d listed", count);
			}
			NiTransform& w = node->worldTransform;
			NiMatrix43 rigRot = ToColumnForm(w.rot);

			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			++g_rigCalls;

			// Motion vectors for the gun come from m_previousWorld vs m_worldTransform. We move this
			// node after the engine has finished with it, so whether the pair stays consistent depends
			// on where the engine snapshots m_previousWorld. If it snapshots its own freshly computed
			// transform rather than the one we left behind last frame, every motion vector on the
			// weapon is short by exactly our own per-frame motion, and the reprojection smears the
			// thin high-contrast parts first - the sights. Restoring last frame's actual rendered
			// transform makes the pair consistent either way; when the engine already had it right
			// this is a no-op, and g_mvDelta says which case we are in.
			if (c.fixMotionVectors && g_rigWrittenTick.QuadPart != 0)
			{
				// Only when we wrote this same node on the immediately preceding frame - a stale
				// snapshot would be worse than the problem it fixes.
				float age = Seconds(g_rigWrittenTick, now);
				if (age > 1e-5f && age < g_frameDt * 1.5f)
				{
					g_mvDelta = Length(node->previousWorld.pos - g_rigWritten.pos);
					node->previousWorld = g_rigWritten;
				}
			}

			// The engine's own transform for this frame, kept so the sanity net at the end has
			// something known-good to fall back to.
			NiTransform engineTransform = w;
			// The muzzle exactly as the engine placed it, before any of our writes. The same
			// affine we apply to the root is applied to this below to get the displacement.
			NiAVObjectView* muzzleNode = FindNode(node, "ProjectileNode", 0);
			NiPoint3 muzzlePre = muzzleNode ? muzzleNode->worldTransform.pos : NiPoint3{};

			// Record the rig as the game placed it this frame, for the camera hook's eye measurement.
			g_rigSeenPos = w.pos;
			g_rigSeenRot = rigRot;
			g_rigSeenTick = now;

			float dt = g_rigTick.QuadPart != 0 ? Seconds(g_rigTick, now) : g_frameDt;
			g_rigTick = now;
			dt = std::clamp(dt, 1e-4f, 0.1f);

			// Did the camera already simulate this frame, or are we ahead of it?
			g_rigAfterCamMs = Seconds(g_simTick, now) * 1000.0f;
			bool stale = g_rigAfterCamMs > g_frameDt * 1000.0f * 0.6f;
			if (stale)
				++g_rigStale;

			// This frame's eye: directly if the camera already ran, otherwise rebuilt from where
			// the eye sits in the rig's own space.
			NiPoint3 eye = g_trueEye;
			if (stale && g_haveEyeInRig)
				eye = w.pos + rigRot.Mul(g_eyeInRig);

			// The rig is usually positioned RELATIVE TO THE CAMERA - camera at the origin - so pivot
			// there, not about the world eye 110k units away (that flung the rig and broke frame gen).
			// Whichever point the root sits nearer IS its space's origin; the test is scale-free on
			// purpose. NEVER gate it on absolute world coordinates: the old "eye more than 5000 units
			// from origin" test failed in Vault 111 (~700 units out), which picked world space, threw
			// the root ~38 units on resting cant alone, and sank the view into the floor.
			float dOrigin = Length(w.pos);
			float dEye    = Length(w.pos - eye);
			bool localSpace;
			if (c.rigSpace >= 0)
				localSpace = c.rigSpace == 1;
			else if (dOrigin < dEye * 0.5f)
				localSpace = true;
			else if (dEye < dOrigin * 0.5f)
				localSpace = false;
			else
				localSpace = g_rigLocalSpace != 0; // too close to call: keep the last answer
			if ((localSpace ? 1 : 0) != g_rigLocalSpace)
			{
				CP_LOG("rig space: %s (rigPos=(%.1f,%.1f,%.1f) toOrigin=%.0f toEye=%.0f)",
					localSpace ? "camera-relative (pivot = origin)" : "world (pivot = eye)",
					w.pos.x, w.pos.y, w.pos.z, dOrigin, dEye);
				g_rigLocalSpace = localSpace ? 1 : 0;
			}
			// Pivot for every gun rotation. In rig space the origin is the player's FEET, not the eye
			// (the node dump puts the rig's own 'Camera' bone at z~120), and turning the gun about a
			// point ~120 units below the eye swings it sideways by ~17 units at 8 degrees. Hip fire
			// barely shows it, but it lifts the gun's sight line off the eye, so the sight picture no
			// longer marks where the shot goes. Rotating about the EYE keeps the sight line through
			// the eye at any angle - which is what makes Free Aim usable in iron sights.
			NiPoint3 pivot = localSpace ? NiPoint3{ 0.0f, 0.0f, 0.0f } : eye;
			if (localSpace)
			{
				// Built from THIS frame's engine transform and the bone chain's local offsets. Reading
				// the bone's world position back instead fed last frame's written rig into this frame's
				// pivot; at large low ready angles the error compounded every frame and walked the rig
				// (and the view) hundreds of units off.
				NiAVObjectView* eyeBone = FindNode(node, "Camera", 0);
				NiPoint3 eyeLocal{};
				bool found = false;
				for (NiAVObjectView* n = eyeBone; n; n = *reinterpret_cast<NiAVObjectView**>(reinterpret_cast<uintptr_t>(n) + 0x28))
				{
					if (n == node) { found = true; break; }
					NiPoint3 lp = n->localTransform.pos;
					if (n == g_eyeBoneHeld && Length(lp - g_eyeWritten) < 1e-3f)
						lp = lp + g_eyeApplied; // our own hold-back, not the animation's
					eyeLocal = ToColumnForm(n->localTransform.rot).Mul(eyeLocal * n->localTransform.scale) + lp;
				}
				if (found)
					pivot = w.pos + rigRot.Mul(eyeLocal * w.scale);
			}
			static bool s_pivotLogged = false;
			if (c.debugLog && !s_pivotLogged)
			{
				s_pivotLogged = true;
				CP_LOG("rig pivot: (%.1f, %.1f, %.1f) %s", pivot.x, pivot.y, pivot.z,
					localSpace ? "(rig 'Camera' bone = the eye)" : "(world eye)");
			}

			// Which camera is the rig built from? Compare how steady the rig's heading is
			// relative to the real aim vs the rendered view while they differ.
			if (c.rigFollowsView >= 0)
			{
				g_rigFollowsView = c.rigFollowsView == 1;
				g_rigDecided = true;
			}
			else if (!g_rigDecided)
			{
				float rigYaw = YawOf(rigRot.Forward());
				float relTrue = Wrap(rigYaw - YawOf(g_trueRot.Forward()));
				float relView = Wrap(rigYaw - YawOf(g_viewRot.Forward()));
				if (g_haveRelPrev && std::fabs(g_lagYaw) > 1.0f * kDegToRad)
				{
					g_rigScoreTrue += std::fabs(Wrap(relTrue - g_prevRelTrue));
					g_rigScoreView += std::fabs(Wrap(relView - g_prevRelView));
					if (++g_rigSamples >= 240)
					{
						g_rigFollowsView = g_rigScoreView < g_rigScoreTrue * 0.5f;
						g_rigDecided = true;
						CP_LOG("Rig parent detected: follows %s (steadiness true=%.3f view=%.3f)",
							g_rigFollowsView ? "RENDERED VIEW" : "REAL AIM", g_rigScoreTrue, g_rigScoreView);
					}
				}
				g_prevRelTrue = relTrue;
				g_prevRelView = relView;
				g_haveRelPrev = true;
			}

			// COM probe: replaces the normal gun pose entirely so the measurement is clean, and
			// leaves g_rigApplied* at identity because in mode 1 the root is never touched.
			ComProbeKey(c);
			int comMode = g_comProbeMode < 0 ? c.comProbe : g_comProbeMode;
			if (comMode != 0)
			{
				ComProbe(node, comMode, c);
				g_rigAppliedRot = Identity();
				g_rigAppliedShift = NiPoint3{};
				g_rigHoldShift = NiPoint3{};
				g_muzzleDelta = NiPoint3{};
				g_rigAppliedTick = now;
				return;
			}

			if (!c.gunMotion)
				return;

			// If the rig was built from our rendered view, move it back onto the real aim so
			// the gun leads instead of lagging along with the view.
			if (g_rigFollowsView && !localSpace)
			{
				NiMatrix43 q = Mul(g_trueRot, Transposed(g_viewRot));
				w.pos = g_trueEye + q.Mul(w.pos - g_viewEye);
				rigRot = Mul(q, rigRot);
			}

			const NiMatrix43& axes = localSpace ? rigRot : g_trueRot;
			NiPoint3 right = Normalized(axes.Right());
			NiPoint3 fwd   = Normalized(axes.Forward());
			NiPoint3 up    = Normalized(axes.Up());
			NiPoint3 fwdFlat = Normalized({ fwd.x, fwd.y, 0.0f });

			// Gun cant into turns/strafes. The corner lean is applied further down.
			float cantTarget = std::clamp(SoftLimit(g_turnRate * c.cantTurn, c.cantMaxDeg)
				+ g_strafeNorm * c.cantStrafe, -c.cantMaxDeg, c.cantMaxDeg);
			// Approach() is first-order: it eases toward the target but its VELOCITY jumps the moment
			// the target flips sign, so swinging left-to-right put a kink in the roll that reads as a
			// snap. A critically damped spring has continuous velocity, so the gun rolls through
			// centre and out the other side in one motion. Damping 1 = no overshoot.
			SpringTo(g_cant, g_cantVel, cantTarget, c.cantRate, c.cantDamping, dt);

			// Gun drag: trails a little behind body movement.
			NiPoint3 dragTarget = Normalized({ g_strafeNorm * right.x, g_strafeNorm * right.y, 0.0f })
				* -std::min(std::fabs(g_strafeNorm) * c.strafeRefSpeed * c.inertia, c.inertiaMax) * g_amplitude;
			g_inertia.x = Approach(g_inertia.x, dragTarget.x, c.inertiaRate, dt);
			g_inertia.y = Approach(g_inertia.y, dragTarget.y, c.inertiaRate, dt);
			g_inertia.z = Approach(g_inertia.z, dragTarget.z, c.inertiaRate, dt);

			float wg = g_blend;
			// Weapon inertia. Turning the gun back against the turn cancels Free Aim's own lead, so
			// under Free Aim it only cants and slides; without it, it does all three.
			// Scaled by the intensity preset like every other amplitude.
			float swingYaw = g_freeAim ? 0.0f : g_swingYaw * g_amplitude;
			float swingPitch = g_freeAim ? 0.0f : g_swingPitch * g_amplitude;
			// Negated: the swing runs against the turn, but Gun Lean Into Turns leans with it, and
			// the two cancelled. Tilting the same way as the turn lean lets them stack.
			float swingCant = -g_swingYaw * c.lookInertiaCant * g_amplitude;
			NiPoint3 swingSlide = right * (g_swingYaw * c.lookInertiaSlide * g_amplitude) + up * (g_swingPitch * c.lookInertiaSlide * g_amplitude);
			// Corner: Gun Turn is hip-only. Aimed, it turned the gun off the sight line.
			float yaw   = (-g_gunPeek * c.gunYawDeg * c.yawSign * (1.0f - g_holdAds) + g_recoil.gunYawDeg + swingYaw) * kDegToRad * wg;
			float yawHold = 0.0f; // low ready swing, added once the pose is known below
			// Gun lean = the screen lean (so the weapon tilts with the view) + extra cant on top.
			g_gunRollDeg = std::clamp(g_viewRollDeg * c.gunFollowRoll + g_cant * c.cantSign * g_amplitude * wg,
				-c.gunRollMaxDeg, c.gunRollMaxDeg)
				+ (c.gunRestCantDeg + (c.gunRestCantAdsDeg - c.gunRestCantDeg) * g_holdAds) * c.cantSign * wg; // resting cant, hip -> ADS on the aim blend
			// Corner peek takes the gun's lean over completely. Mixed in, the resting tilt added to one
			// side's peek and subtracted from the other's, and Max Total Gun Lean clipped it - a negative
			// resting tilt peeked well left and barely right. Same camera share (Gun Follows Camera Lean)
			// plus Corner: Gun Tilt, identical both ways.
			float peekW = std::clamp(std::fabs(g_gunPeek), 0.0f, 1.0f);
			float peekRoll = (g_camPeek * c.camRollDeg * c.rollSign * c.gunFollowRoll + g_gunPeek * c.gunCantDeg * c.cantSign) * wg;
			g_gunRollDeg += (peekRoll - g_gunRollDeg) * peekW;
			float cant  = (g_gunRollDeg + g_recoil.gunRollDeg + swingCant * c.cantSign * wg * (1.0f - peekW)) * kDegToRad;
			// Weapon Hold: out along the line of sight (keeps iron sights lined up), low-ready drop along the
			// view's down axis, and the muzzle dipping - same sign convention as Retract Muzzle Down.
			HoldPose hold = UpdateWeaponHold(dt, wg, c);
			float pitch = (-g_retract * c.retractPitchDeg - hold.pitchDeg + g_recoil.gunPitchUpDeg + swingPitch
				+ c.gunRestPitchDeg * (1.0f - g_holdAds) * (1.0f - hold.level)) * kDegToRad * c.pitchSign * wg;
			const float armShare = std::clamp(hold.armShare, 0.0f, 1.0f);
			yawHold = hold.yawDeg * armShare * kDegToRad * c.yawSign;
			const float gunOnlyYaw = hold.yawDeg * (1.0f - armShare) * kDegToRad * c.yawSign;
			NiMatrix43 r = Mul(AxisAngle(up, yaw + yawHold), Mul(AxisAngle(fwd, cant), AxisAngle(right, pitch)));


			// Weapon-FOV compensation. The gun is drawn with the weapon projection and the world
			// with the camera's, so swing the gun out to where it has to be DRAWN for its sight
			// line to cover the impact point: tan(a') = k * tan(a) about the view axis.
			//
			// Built in the world frame from directions we trust (g_trueRot is what the bullet
			// follows and what the crosshair marks, both verified against bullet holes), then
			// carried into the rig's own frame when the rig is camera-relative - no sign guessing.
			// It folds into the same rotation as everything else, so the camera hook removes it
			// and g_trueRot stays the true aim rather than the drawn one.
			NiMatrix43 fovComp = Identity();
			{
				float f1st = 0.0f, fWorld = 0.0f;
				if (auto* cam = *reinterpret_cast<uint8_t**>(g_base + Offsets::kRVA_g_playerCamera))
				{
					f1st   = *reinterpret_cast<float*>(cam + Offsets::kOff_PlayerCamera_fov1st);
					fWorld = *reinterpret_cast<float*>(cam + Offsets::kOff_PlayerCamera_fovWorld);
				}
				g_fovK = (f1st > 20.0f && f1st < 160.0f && fWorld > 20.0f && fWorld < 160.0f)
					? std::tan(f1st * 0.5f * kDegToRad) / std::tan(fWorld * 0.5f * kDegToRad) : 1.0f;
				g_fovRaised = c.weaponHold && std::fabs(f1st - fWorld) > 0.5f;
				// k == 1 whenever the two FOVs match, so with no Weapon FOV Change raised this
				// whole block is identity and cannot affect the shipped default behaviour.
				if (g_fovRaised && std::fabs(g_fovK - 1.0f) > 0.01f)
				{
					NiPoint3 vR = Normalized(g_viewRot.Right());
					NiPoint3 vF = Normalized(g_viewRot.Forward());
					NiPoint3 vU = Normalized(g_viewRot.Up());
					NiPoint3 aimW = Normalized(g_trueRot.Forward());
					float vy = Dot(aimW, vF);
					if (vy > 0.2f)
					{
						float tx = Dot(aimW, vR) / vy, tz = Dot(aimW, vU) / vy;
						// Where the gun has to be DRAWN for its sights to cover the impact.
						NiPoint3 want = Normalized(vF + vR * (g_fovK * tx) + vU * (g_fovK * tz));
						// Clamp before building the rotation. Near the edge of the view cone
						// tan() runs away, and this rotation feeds a loop (see below) - an
						// unbounded term there is what took the rig to 1e15 and then NaN.
						const float kMaxComp = 30.0f * kDegToRad;
						float ang = std::acos(std::clamp(Dot(aimW, want), -1.0f, 1.0f));
						if (ang > kMaxComp)
							want = Normalized(aimW + (want - aimW) * (kMaxComp / ang));
						// NO basis change. Rig space is camera-TRANSLATED but world-ORIENTED
						// (measured: the log's rigYaw equals camYaw exactly), so a world-frame
						// rotation applies as-is in both spaces. Conjugating it by rigRot here
						// injected a large spurious rotation which then fed back through
						// g_rigAppliedRot -> g_trueRot at gain k per frame, and the rig diverged
						// to NaN inside a second. Do not reintroduce a conversion.
						fovComp = RotationBetween(aimW, want);
					}
				}
			}
			g_fovCompRot = fovComp;
			r = Mul(fovComp, r);

			// Low ready slide and drop, along the aim's own right and world up. The rig's own axes
			// ("right" above) did not move it sideways at all in game.
			NiPoint3 aimRightFlat = Normalized({ g_trueRot.Right().x, g_trueRot.Right().y, 0.0f });
			NiPoint3 lowReadyShift = (aimRightFlat * hold.lrSide + kWorldUp * hold.lrDrop) * (g_lowReady * wg)
				+ g_trueRot.Forward() * hold.hipForward + kWorldUp * hold.hipHeight + aimRightFlat * hold.hipSide;
			static uint32_t s_lrLog = 0;
			if (c.debugLog && g_lowReady > 0.05f && (s_lrLog++ % 120) == 0)
				CP_LOG("low ready: amount=%.2f shift=(%.1f,%.1f,%.1f) side=%.1f height=%.1f yaw=%.1f rigSpace=%s",
					g_lowReady, lowReadyShift.x, lowReadyShift.y, lowReadyShift.z, hold.lrSide, hold.lrDrop,
					hold.yawDeg, localSpace ? "camera-relative" : "world");
			NiPoint3 shift = lowReadyShift + (g_bobOffset * c.gunBobScale
				+ g_inertia + swingSlide
				+ right * (g_gunPeek * c.gunPeekDist * (1.0f - g_holdAds))
				- fwdFlat * (g_retract * c.retractBack + g_recoil.gunBack)
				- kWorldUp * (g_retract * c.retractDrop + g_landGunDip)) * wg
				+ (right * g_peekEyeSide - kWorldUp * g_peekEyeDrop) * g_holdAds // aimed: the gun moves exactly with the view, so the sights stay on
				+ fwd * hold.forward - up * hold.drop + right * hold.side
				+ up * (c.sightsUpDown * g_holdAds * wg); // sights calibration (aiming only)

			w.pos = pivot + r.Mul(w.pos - pivot) + shift;

			w.rot = ToGameForm(Mul(r, rigRot));

			// Same affine, applied to the muzzle, to get the displacement the shot maths must undo.
			// Order matches the writes above: rotate about the pivot, add the shift, then scale
			// about the pivot.
			if (muzzleNode)
			{
				g_muzzleDelta = (pivot + r.Mul(muzzlePre - pivot) + shift) - muzzlePre;
			}
			else
			{
				g_muzzleDelta = NiPoint3{};
			}

			g_rigAppliedRot = r;
			g_rigAppliedShift = shift - lowReadyShift; // the eye bone is held back, so the camera does not follow this part
			g_rigHoldShift = lowReadyShift + fwd * hold.forward - up * hold.drop + right * hold.side
				+ up * (c.sightsUpDown * g_holdAds * wg);
			// Safety net. Everything above feeds next frame's input, so one bad frame can
			// compound instead of washing out. If what we are about to leave on the node is not
			// finite, or has thrown the rig an implausible distance from the eye, put the
			// engine's own transform back and sit out - a vanilla frame is always recoverable,
			// a NaN in the skeleton is not.
			{
				const float* f = &w.rot.data[0][0];
				bool sane = std::isfinite(w.pos.x) && std::isfinite(w.pos.y) && std::isfinite(w.pos.z)
					&& std::isfinite(w.scale) && Length(w.pos - pivot) < 4000.0f
					&& std::isfinite(lowReadyShift.x) && std::isfinite(lowReadyShift.y) && std::isfinite(lowReadyShift.z);
				for (int i = 0; sane && i < 12; ++i)
					sane = std::isfinite(f[i]);
				if (!sane)
				{
					w = engineTransform;
					g_rigAppliedRot = Identity();
					g_rigAppliedShift = NiPoint3{};
					g_rigHoldShift = NiPoint3{};
					g_muzzleDelta = NiPoint3{};
					static int s_insaneLogged = 0;
					if (s_insaneLogged < 10)
					{
						++s_insaneLogged;
						CP_LOG("rig sanity: rejected a bad transform (scale=%.3f k=%.3f) - engine transform restored", w.scale, g_fovK);
						CP_LOG("rig sanity: shift=(%.1f,%.1f,%.1f) lowReady=(%.1f,%.1f,%.1f) pivot=(%.1f,%.1f,%.1f) pitch=%.2f yaw=%.2f cant=%.2f",
							shift.x, shift.y, shift.z, lowReadyShift.x, lowReadyShift.y, lowReadyShift.z,
							pivot.x, pivot.y, pivot.z, pitch, yaw + yawHold, cant);
						CP_LOG("rig sanity: bob=(%.1f,%.1f,%.1f) inertia=(%.1f,%.1f,%.1f) landDip=%.2f recoilBack=%.2f fovComp00=%.3f wg=%.2f",
							g_bobOffset.x, g_bobOffset.y, g_bobOffset.z, g_inertia.x, g_inertia.y, g_inertia.z,
							g_landGunDip, g_recoil.gunBack, g_fovCompRot.data[0][0], wg);
					}
					ResetMotion();
					return;
				}
			}

			// Low ready moves the HANDS, not the view. The game builds the first-person camera from the
			// rig's own 'Camera' bone, so sliding the rig slid the eye with it and the gun never moved
			// on screen - only rotations showed. Hold the eye bone back by the same amount so the
			// slide stays in the arms. The bone may or may not be re-animated every frame, so the
			// value we wrote is remembered and the animation's own value recovered from it.
			{
				NiAVObjectView*& s_eyeBone = g_eyeBoneHeld;
				NiPoint3& s_written = g_eyeWritten;
				NiPoint3& s_applied = g_eyeApplied;
				static bool s_logged = false;
				NiAVObjectView* eyeBone = FindNode(node, "Camera", 0);
				if (eyeBone)
				{
					NiPoint3& lp = eyeBone->localTransform.pos;
					bool ours = eyeBone == s_eyeBone && Length(lp - s_written) < 1e-3f;
					NiPoint3 base = ours ? lp + s_applied : lp;
					NiMatrix43 rootCol = ToColumnForm(w.rot);
					float sc = std::fabs(w.scale) > 1e-6f ? w.scale : 1.0f;
					NiPoint3 back = Transposed(rootCol).Mul(lowReadyShift) * (1.0f / sc);
					lp = base - back;
					s_eyeBone = eyeBone;
					s_written = lp;
					s_applied = back;
					if (!s_logged && c.debugLog)
					{
						s_logged = true;
						auto* parent = *reinterpret_cast<NiAVObjectView**>(reinterpret_cast<uintptr_t>(eyeBone) + 0x28);
						CP_LOG("low ready: eye bone parent '%s' (root '%s')", parent ? NodeName(parent) : "?", NodeName(node));
					}
				}
			}

			// The rest of the low ready swing turns the gun alone, about its own origin in the right
			// hand. UpdateWorldData here runs before the children, so a local write shows this frame.
			// The parent's world rotation is last frame's; the axis error from that is negligible.
			{
				static NiAVObjectView* s_gun = nullptr;
				static NiMatrix43 s_written{}, s_applied{}; // s_applied only read once s_gun is set
				NiAVObjectView* gun = FindNode(node, "Weapon", 0);
				auto* gunParent = gun ? *reinterpret_cast<NiAVObjectView**>(reinterpret_cast<uintptr_t>(gun) + 0x28) : nullptr;
				if (gun && gunParent)
				{
					NiMatrix43 local = ToColumnForm(gun->localTransform.rot);
					bool ours = gun == s_gun && std::memcmp(&gun->localTransform.rot, &s_written, sizeof(s_written)) == 0;
					NiMatrix43 base = ours ? Mul(Transposed(s_applied), local) : local;
					NiPoint3 axis = Normalized(Transposed(ToColumnForm(gunParent->worldTransform.rot)).Mul(up));
					NiMatrix43 turn = std::fabs(gunOnlyYaw) > 1e-5f ? AxisAngle(axis, gunOnlyYaw) : Identity();
					gun->localTransform.rot = ToGameForm(Mul(turn, base));
					s_gun = gun;
					s_written = gun->localTransform.rot;
					s_applied = turn;
				}
			}

			g_rigAppliedTick = now;
			g_rigWritten = w;
			g_rigWrittenTick = now;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			CP_LOG("Hook_Rig: caught access violation, skipping this frame");
		}
	}

	// Installs a per-instance shadow vtable whose UpdateWorldData slot is `hook`.
	static void* InstallOn(void* instance, UpdateWorldDataFn hook, void* currentShadow, const char* what)
	{
		void** originalVtable = *reinterpret_cast<void***>(instance);
		if (currentShadow && originalVtable == currentShadow)
			return currentShadow;

		// Allocated per install and never freed: an old instance may still hold the previous
		// table, and it must keep pointing at functions valid for its own class.
		auto* block = new ShadowBlock{};
		block->original = reinterpret_cast<UpdateWorldDataFn>(originalVtable[Offsets::kVtblIndex_UpdateWorldData]);
		block->col = originalVtable[-1];
		std::memcpy(block->vtbl, originalVtable, sizeof(block->vtbl));
		block->vtbl[Offsets::kVtblIndex_UpdateWorldData] = reinterpret_cast<void*>(hook);

		*reinterpret_cast<void***>(instance) = block->vtbl;

		CP_LOG("Installed UpdateWorldData hook on %s 0x%p (original vtable RVA 0x%llX)", what, instance,
			static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(originalVtable) - g_base));
		return block->vtbl;
	}

	// ==== 1.0.3: shots follow the gun =========================================================
	// The game aims the player's shots through the CAMERA - it takes the point under the screen
	// centre and fires from the muzzle toward it. With the free-aim box the gun leaves the screen
	// centre, so shots stayed on the crosshair while the barrel pointed elsewhere.
	// Hooks Projectile::Launch and, for the player's own shots in first person (hip fire and iron
	// sights; scopes and VATS stay vanilla), re-aims the launch along the muzzle node's barrel axis.
	// Spread survives: the game's angles minus the direction toward the view-ray point it was
	// converging on is the weapon's spread for this pellet, and that offset is kept on top.

	// ProjectileLaunchData (0xA0), only the fields we touch. Launch reads the projectile form
	// from +0x18 in this exe, matching this layout.
	struct LaunchDataView
	{
		NiPoint3 origin;          // 0x00
		NiPoint3 contactNormal;   // 0x0C
		void*    projectileBase;  // 0x18
		void*    shooter;         // 0x20
		void*    combatCtrl;      // 0x28
		void*    weapon[2];       // 0x30
		void*    ammo;            // 0x40
		uint32_t equipIndex;      // 0x48
		float    zAngle;          // 0x4C heading, atan2(x, y)
		float    xAngle;          // 0x50 pitch, positive = down (measured 2026-09-18)
	};
	static_assert(offsetof(LaunchDataView, shooter) == 0x20, "ProjectileLaunchData.shooter");
	static_assert(offsetof(LaunchDataView, zAngle) == 0x4C, "ProjectileLaunchData.zAngle");
	static_assert(offsetof(LaunchDataView, xAngle) == 0x50, "ProjectileLaunchData.xAngle");

	using LaunchFn = uint64_t (*)(void*, void*, void*, void*);
	static LaunchFn g_launchOrig = nullptr;
	// Offsets::kLaunchPrologue: the exact bytes stolen into the trampoline (15 on 1.11.240, 20 on 1.10.163).
	static constexpr size_t kLaunchPatchLen = sizeof(Offsets::kLaunchPrologue);
	static int g_shotLogCount = 0;
	// Multi-projectile detection for iPinpoint (see AdjustLaunch): pellets of one shot arrive as
	// separate Launch calls inside the same frame.
	static LARGE_INTEGER g_lastLaunchTick{};
	static uint32_t g_pelletWeapId = 0;
	static int g_pelletsThisFrame = 0, g_pelletMax = 0;

	static const char* NodeName(const void* obj)
	{
		auto* entry = *reinterpret_cast<const uint8_t* const*>(reinterpret_cast<uintptr_t>(obj) + 0x10);
		return entry ? reinterpret_cast<const char*>(entry + 0x18) : "";
	}

	// Depth-first search under a node (NiObject::GetAsNiNode is vtable slot 3).
	// Walks the weapon/arms tree once and logs each node with its position (rig space, camera at the
	// origin) so sight-related nodes can be identified by name and by where they sit.
	static void DumpNodes(NiAVObjectView* obj, int depth, int& count)
	{
		if (!obj || depth > 24 || count > 600)
			return;
		const NiPoint3& p = obj->worldTransform.pos;
		CP_LOG("nodes: %*s'%s' at (%.1f, %.1f, %.1f)", depth * 2, "", NodeName(obj), p.x, p.y, p.z);
		++count;
		using AsNodeFn = void* (*)(void*);
		void** vtbl = *reinterpret_cast<void***>(obj);
		if (!reinterpret_cast<AsNodeFn>(vtbl[3])(obj))
			return;
		auto addr = reinterpret_cast<uintptr_t>(obj);
		auto** kids = *reinterpret_cast<NiAVObjectView***>(addr + 0x120 + 0x08);
		uint16_t n = *reinterpret_cast<uint16_t*>(addr + 0x120 + 0x12);
		for (uint16_t i = 0; kids && i < n; ++i)
			DumpNodes(kids[i], depth + 1, count);
	}

	static NiAVObjectView* FindNode(NiAVObjectView* obj, const char* name, int depth)
	{
		if (!obj || depth > 48)
			return nullptr;
		if (std::strcmp(NodeName(obj), name) == 0)
			return obj;
		using AsNodeFn = void* (*)(void*);
		void** vtbl = *reinterpret_cast<void***>(obj);
		if (!reinterpret_cast<AsNodeFn>(vtbl[3])(obj))
			return nullptr;
		auto addr = reinterpret_cast<uintptr_t>(obj);
		auto** kids = *reinterpret_cast<NiAVObjectView***>(addr + 0x120 + 0x08);
		uint16_t n = *reinterpret_cast<uint16_t*>(addr + 0x120 + 0x12);
		for (uint16_t i = 0; kids && i < n; ++i)
			if (NiAVObjectView* hit = FindNode(kids[i], name, depth + 1))
				return hit;
		return nullptr;
	}

	static float HeadingOf(const NiPoint3& d) { return std::atan2(d.x, d.y); }
	static float PitchDownOf(const NiPoint3& d) { return -std::asin(std::clamp(d.z, -1.0f, 1.0f)); }

	static void AdjustLaunch(LaunchDataView* d, uintptr_t retRva)
	{
		const Config& c = g_config;
		void* player = *reinterpret_cast<void**>(g_base + Offsets::kRVA_g_player);
		if (!d || !player || d->shooter != player)
			return;
		// Only the player's weapon fire (measured caller). Thrown grenades, mines and projectiles
		// spawned by other projectiles come from elsewhere and keep the game's direction.
		if (retRva != Offsets::kRVA_PlayerWeaponFireReturn)
		{
			// ...except that "keeps the game's direction" was not true for a THROW. The game builds
			// the throw off the hand, and we rotate the whole first-person rig, so free aim, cant,
			// bob and the hold all leaked into it: reported (and reproduced 2026-09-21) as grenades
			// landing low, following the barrel instead of the crosshair.
			//
			// We do not re-aim the throw, we only take OUR OWN rotation back out of it: the same
			// affine the rig hook applied, inverted. The game's aim and its lob arc are whatever
			// vanilla would have produced, so nothing here has to know how a throw is aimed - the
			// same "immune to the gun pose by construction" rule as g_muzzleDelta.
			if (!SafeThrownEquipped(reinterpret_cast<uintptr_t>(player)))
				return;
			NiMatrix43 applied = g_rigAppliedRot;
			NiPoint3 dir{ std::sin(d->zAngle) * std::cos(d->xAngle),
						  std::cos(d->zAngle) * std::cos(d->xAngle),
						  -std::sin(d->xAngle) };
			NiPoint3 fixed = Normalized(Transposed(applied).Mul(dir));
			float newZ = HeadingOf(fixed), newX = PitchDownOf(fixed);
			if (std::isfinite(newZ) && std::isfinite(newX))
			{
				CP_LOG("throw: un-rigged z=%.2f x=%.2f -> z=%.2f x=%.2f (dz=%.2f dx=%.2f)",
					d->zAngle / kDegToRad, d->xAngle / kDegToRad, newZ / kDegToRad, newX / kDegToRad,
					Wrap(newZ - d->zAngle) / kDegToRad, (newX - d->xAngle) / kDegToRad);
				d->zAngle = newZ;
				d->xAngle = newX;
			}
			return;
		}
		QueryPerformanceCounter(&g_lastShotTick); // Weapon Hold: raise out of the low ready

		bool aiming = KeyDown(c.aimKey);
		uint32_t formID = 0;
		if (d->weapon[0])
			formID = *reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(d->weapon[0]) + Offsets::kOff_Form_formID);
		int holdType = SafeDetectHoldType(reinterpret_cast<uintptr_t>(player));
		if (g_recoilHost.OnShot)
			g_recoilHost.OnShot(formID, holdType, aiming);
		else
			RecoilModel::OnShot(formID, holdType, aiming);

		// Multi-projectile? A shotgun's PATTERN IS ITS SPREAD - every pellet is its own Launch - so
		// pinpointing them stacks the whole load on one point. Pinpoint removes AIM error, never a
		// designed pattern. Detected by COUNTING launches in one frame rather than by keyword, which
		// is right for modded weapons and for a slug barrel on a shotgun receiver. The keyword check
		// only covers the first shot with a never-fired weapon.
		bool neverFiredThisWeapon = (formID != g_pelletWeapId) || g_pelletMax == 0;
		{
			LARGE_INTEGER lt;
			QueryPerformanceCounter(&lt);
			bool sameFrame = g_lastLaunchTick.QuadPart != 0 && Seconds(g_lastLaunchTick, lt) < 0.008f;
			g_lastLaunchTick = lt;
			if (formID != g_pelletWeapId)
			{
				g_pelletWeapId = formID;
				g_pelletMax = 0;
				g_pelletsThisFrame = 0;
			}
			g_pelletsThisFrame = sameFrame ? g_pelletsThisFrame + 1 : 1;
			if (g_pelletsThisFrame > g_pelletMax)
			{
				g_pelletMax = g_pelletsThisFrame;
				if (g_pelletMax == 2 && c.debugLog)
					CP_LOG("multi-projectile weapon learned (formID %08X) - keeping its spread", formID);
			}
		}
		// Learned answer once we have one; on the very first pellet of a never-seen weapon there
		// is nothing to count yet, so fall back to the weapon type for that single shot.
		bool multiProjectile = (g_pelletMax > 1) || (neverFiredThisWeapon && holdType == 2);
		bool scoped = MenuOpen(kMenu_Scope);
		// Tied to Free Aim so the gun and the bullets can never disagree: whenever the box lets the
		// gun leave the screen centre, shots follow the gun; with it off, shots stay vanilla.
		// With Free Aim off the gun is locked to the real aim, so the player chooses: vanilla (the
		// shot goes to the crosshair whatever the gun is doing) or the barrel, which is visible
		// once Weapon Hold, cant, lean or bob move the muzzle off the screen centre.
		bool wanted = (c.freeAim || c.hipAim == 1) && c.enabled && g_active
			&& g_camStateIdx == Offsets::kCameraState_FirstPerson && !scoped;
		if (!wanted && !c.debugLog)
			return;

		// Where the game was converging: the point under the screen centre of the view we drew.
		NiPoint3 viewFwd = Normalized(g_viewRot.Forward());
		const float kFar = 60000.0f;
		NiPoint3 target = g_viewEye + viewFwd * kFar;
		float hitDist = 0.0f;
		void* world = RayCast::GetPlayerWorld(g_base);
		bool hit = RayCast::Cast(g_base, world, g_viewEye, target, hitDist);
		if (hit)
			target = g_viewEye + viewFwd * hitDist;
		// The muzzle where the GAME believes it is - before the FOV compensation swung the rig - since
		// d->zAngle/xAngle know nothing about that rotation and the spread must be measured against
		// the same muzzle the game used. Measured 2026-09-20: without this the extracted spread stops
		// straddling zero (yaw +0.79..+1.22, pitch -1.56..-2.45 over four shots) and its pitch grows
		// with aim offset, i.e. a displacement re-applied as spread.
		// Raised-FOV only: 1.0.5 measures correct without it, so leave that baseline alone.
		NiPoint3 originTrue = g_fovRaised ? (d->origin - g_muzzleDelta) : d->origin;
		g_muzzleFix = g_fovRaised ? Length(g_muzzleDelta) : 0.0f;
		NiPoint3 conv = Normalized(target - originTrue);
		float spreadZ = Wrap(d->zAngle - HeadingOf(conv));
		float spreadX = d->xAngle - PitchDownOf(conv);
		const float kMaxSpread = 12.0f * kDegToRad;
		bool spreadOk = std::fabs(spreadZ) < kMaxSpread && std::fabs(spreadX) < kMaxSpread;

		// Where the gun is aiming, out in the world - the same ray the crosshair marks. Shots are
		// launched from the muzzle TOWARD that point, which is how the game itself keeps the muzzle
		// sitting below the eye from throwing shots low: the bullet starts about half a metre below
		// and in front of the camera, so firing parallel to the aim lands low by that much at short
		// range (measured: ~66 px at 1440p on a nearby wall).
		NiPoint3 aimRay = Normalized(g_trueRot.Forward());
		// From the eye the player is actually looking out of: a corner peek moves the view up to
		// 40 units sideways, and converging on the unmoved eye's point missed the sights by that much.
		NiPoint3 aimEye = g_trueEye + g_peekEyeShift;
		NiPoint3 aimTarget = aimEye + aimRay * kFar;
		float aimDist = 0.0f;
		if (RayCast::Cast(g_base, world, aimEye, aimTarget, aimDist))
			aimTarget = aimEye + aimRay * aimDist;
		NiPoint3 aimFromMuzzle = Normalized(aimTarget - d->origin);

		// Barrel: whichever axis of the weapon's ProjectileNode lines up with the gun (the real aim).
		NiPoint3 aim = Normalized(g_trueRot.Forward());
		NiAVObjectView* rig = g_rigNode.load();
		NiAVObjectView* muzzle = rig ? FindNode(rig, "ProjectileNode", 0) : nullptr;


		// Same barrel the crosshair marks - BarrelDir also takes the weapon-FOV compensation back
		// out, without which the shot inherits the gun's drawn-out swing and lands at k times the
		// crosshair's offset from screen centre (measured: bullet du -0.1995 vs crosshair -0.0969
		// at k=2.064, 1.0.6).
		NiPoint3 barrel = BarrelDir(aim);
		int axis = -1;
		float best = -2.0f;
		if (muzzle)
		{
			NiMatrix43 m = ToColumnForm(muzzle->worldTransform.rot);
			NiPoint3 cand[6] = { m.Right(), m.Forward(), m.Up(), m.Right() * -1.0f, m.Forward() * -1.0f, m.Up() * -1.0f };
			for (int i = 0; i < 6; ++i)
			{
				float dp = Dot(Normalized(cand[i]), aim);
				if (dp > best) { best = dp; axis = i; }
			}
			if (best <= 0.9f)
				axis = -1; // logged as 'missing': BarrelDir fell back to the aim
		}
		// No muzzle node (some weapons): the gun is locked to the real aim, so that is the barrel.

		// What the sights are actually pointing at, measured from the rig itself: the line from the
		// first-person 'Camera' node through the weapon's sight node. This is pure geometry in rig
		// space - no projection, no FOV - so it says exactly where the player sees the sights aiming.
		// Logged as yaw/pitch away from the direction we are about to fire.
		if (c.debugLog && rig)
		{
			NiAVObjectView* sightNode = FindNodeContaining(rig, "Sight", 0);
			if (!sightNode)
				sightNode = FindNodeContaining(rig, "Scope", 0);
			if (sightNode)
			{
				// The sight line is the sight node's OWN axis (whichever of its six axes points along
				// the gun), not the short eye-to-sight vector - over ~15 units that vector is all
				// position and almost no aim, which is why it read 13 degrees off.
				NiMatrix43 m = ToColumnForm(sightNode->worldTransform.rot);
				NiPoint3 cand[6] = { m.Right(), m.Forward(), m.Up(), m.Right() * -1.0f, m.Forward() * -1.0f, m.Up() * -1.0f };
				int bestAxis = -1; float bestDot = -2.0f;
				for (int i = 0; i < 6; ++i)
				{
					float dp = Dot(Normalized(cand[i]), barrel);
					if (dp > bestDot) { bestDot = dp; bestAxis = i; }
				}
				if (bestDot > 0.9f)
				{
					NiPoint3 sightDir = Normalized(cand[bestAxis]);
					float dYaw = Wrap(HeadingOf(sightDir) - HeadingOf(barrel)) / kDegToRad;
					float dPitch = (PitchDownOf(sightDir) - PitchDownOf(barrel)) / kDegToRad;
					CP_LOG("sights: '%s' axis %d (dot %.3f) points yaw=%+.2f pitch=%+.2f deg from the shot "
						"(+yaw = sights right of it, +pitch = sights below it) | ads=%.2f",
						NodeName(sightNode), bestAxis, bestDot, dYaw, dPitch, g_holdAds);
				}
				else
					CP_LOG("sights: '%s' has no axis along the gun (best dot %.3f) - cannot measure", NodeName(sightNode), bestDot);
			}
			else
				CP_LOG("sights: no sight node on this weapon");
		}

		// [Look] iPinpoint: drop the game's spread so the shot goes exactly where the sights
		// point. With vanilla spread a 10mm throws about +/-1.5 degrees, which is ~45 px at 1440p
		// on a target 4 m away - that is the weapon's accuracy cone, applied after the aim, so no
		// aim correction can remove it. 1 = iron sights only, which keeps hip fire honest: the
		// sight picture is a precision tool, shooting from the hip is not.
		bool pinpointNow = (c.pinpoint == 2 || (c.pinpoint == 1 && aiming)) && !multiProjectile;
		float useSpreadZ = (spreadOk && !pinpointNow) ? spreadZ : 0.0f;
		float useSpreadX = (spreadOk && !pinpointNow) ? spreadX : 0.0f;
		// Shots leave along the BARREL, not along a line from your eye to whatever the aim ray hits.
		// 1.0.5 converged on that hit point, which made the crosshair exact at every range but also
		// made shots ignore the barrel's pose - recoil and sway stopped moving the point of impact,
		// and it read as magnetic (1.0.5 vs 1.0.4). Firing along the barrel puts
		// recoil and sway back into where rounds land, and keeps iron sights honest because the
		// sights sit on the barrel. The cost is parallax: the muzzle is about half a metre below and
		// in front of the eye, so at very short range a hip-fire shot lands below the crosshair by
		// roughly that much, shrinking to nothing with distance - the same as a real gun.
		NiPoint3 shotDir = barrel;
		float newZ = Wrap(HeadingOf(shotDir) + useSpreadZ);
		float newX = PitchDownOf(shotDir) + useSpreadX;
		float oldZ = d->zAngle, oldX = d->xAngle;
		if (wanted)
		{
			d->zAngle = newZ;
			d->xAngle = newX;
		}

		if (c.debugLog && g_shotLogCount < 400)
		{
			++g_shotLogCount;
			CP_LOG("shot: caller=+0x%llX %s aiming=%d scoped=%d | game z=%.2f x=%.2f | view z=%.2f x=%.2f conv z=%.2f x=%.2f (hit=%d %.0f) spread=%.2f,%.2f%s "
				"| gun z=%.2f x=%.2f (muzzle=%s axis=%d dot=%.3f) | -> z=%.2f x=%.2f",
				static_cast<unsigned long long>(retRva), wanted ? "APPLIED" : "vanilla", aiming, scoped,
				oldZ / kDegToRad, oldX / kDegToRad,
				HeadingOf(viewFwd) / kDegToRad, PitchDownOf(viewFwd) / kDegToRad,
				HeadingOf(conv) / kDegToRad, PitchDownOf(conv) / kDegToRad, hit, hitDist,
				spreadZ / kDegToRad, spreadX / kDegToRad,
				pinpointNow ? " (PINPOINT: dropped)" : (multiProjectile ? " (multi-projectile: kept)" : (spreadOk ? "" : " (too big, dropped)")),
				HeadingOf(barrel) / kDegToRad, PitchDownOf(barrel) / kDegToRad,
				muzzle ? "found" : "missing", axis, best,
				d->zAngle / kDegToRad, d->xAngle / kDegToRad);
			// Screen check: where this shot's direction lands with the frustum the crosshair uses,
			// against where the crosshair actually sits. Equal numbers mean the maths agrees with
			// itself, so a gap the player sees is the frustum / stage mapping being wrong.
			NiPoint3 sd{ std::sin(d->zAngle) * std::cos(d->xAngle), std::cos(d->zAngle) * std::cos(d->xAngle), -std::sin(d->xAngle) };
			float sx = Dot(sd, Normalized(g_viewRot.Right()));
			float sy = Dot(sd, Normalized(g_viewRot.Forward()));
			float sz = Dot(sd, Normalized(g_viewRot.Up()));
			float* liveFov = LiveFov();
			if (sy > 0.1f)
				CP_LOG("shot screen: bullet du=%.4f dv=%.4f | crosshair du=%.4f dv=%.4f | frustum R=%.4f T=%.4f | k=%.3f muzzleFix=%.2f "
					"| 1stFOV=%.1f (base %.1f, applied %+.1f) worldFOV=%.1f | hold=%d ads=%.2f",
					(sx / sy) / (2.0f * g_crossR), -(sz / sy) / (2.0f * g_crossT), g_crossDu, g_crossDv,
					g_crossR, g_crossT, g_fovK, g_muzzleFix,
					liveFov ? *liveFov : -1.0f, g_fovBase, g_fovApplied, WorldFov(),
					c.weaponHold ? 1 : 0, g_holdAds);
				// Both hypotheses in SCREEN PIXELS for a 2560x1440 grab, so the answer is read off
				// a screenshot instead of reprojected by hand. tan() recovered from whichever
				// projection is live, then re-divided by each candidate.
				{
					float tx = (sx / sy), tz = -(sz / sy);
					double w = 2560.0, h = 1440.0;
					CP_LOG("shot hypo: using R=%.4f T=%.4f -> (%.0f, %.0f) | alt R=%.4f T=%.4f -> (%.0f, %.0f) | raised=%d  [2560x1440, centre 1280,720]",
						g_crossR, g_crossT,
						w * 0.5 + (tx / (2.0f * g_crossR)) * w, h * 0.5 + (tz / (2.0f * g_crossT)) * h,
						g_crossRAlt, g_crossTAlt,
						w * 0.5 + (tx / (2.0f * g_crossRAlt)) * w, h * 0.5 + (tz / (2.0f * g_crossTAlt)) * h,
						g_fovRaised ? 1 : 0);
				}
		}
	}

	static uint64_t Hook_Launch(void* outHandle, void* data, void* a3, void* a4)
	{
		uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress()) - g_base;
		__try
		{
			AdjustLaunch(reinterpret_cast<LaunchDataView*>(data), ret);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			CP_LOG("Hook_Launch: caught access violation, shot left vanilla");
		}
		// Did the engine keep the direction we set? If Fallout 4 applies its own spread INSIDE
		// Launch, our angles are re-perturbed after the hook and no aim correction can make the
		// shot land on the sights - the variance would be unavoidable while spread is on. This
		// answers that from the inside instead of from decal positions.
		float setZ = 0.0f, setX = 0.0f;
		bool haveSet = false;
		__try
		{
			if (auto* d = reinterpret_cast<LaunchDataView*>(data))
			{
				setZ = d->zAngle; setX = d->xAngle; haveSet = true;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}

		uint64_t result = g_launchOrig(outHandle, data, a3, a4);

		__try
		{
			auto* d = reinterpret_cast<LaunchDataView*>(data);
			if (haveSet && d && g_config.debugLog && g_shotLogCount > 0 && g_shotLogCount <= 40)
			{
				float dz = Wrap(d->zAngle - setZ) / kDegToRad, dx = (d->xAngle - setX) / kDegToRad;
				if (std::fabs(dz) > 0.01f || std::fabs(dx) > 0.01f)
					CP_LOG("after launch: ENGINE CHANGED the direction by z=%+.3f x=%+.3f deg (set z=%.2f x=%.2f, now z=%.2f x=%.2f)",
						dz, dx, setZ / kDegToRad, setX / kDegToRad, d->zAngle / kDegToRad, d->xAngle / kDegToRad);
				else
					CP_LOG("after launch: direction unchanged (z=%.2f x=%.2f)", setZ / kDegToRad, setX / kDegToRad);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
		return result;
	}

	static void WriteAbsJmp(uint8_t* at, const void* to)
	{
		at[0] = 0xFF; at[1] = 0x25; // jmp qword ptr [rip+0]
		std::memset(at + 2, 0, 4);
		std::memcpy(at + 6, &to, 8);
	}

	static void InstallLaunchHook()
	{
		auto* target = reinterpret_cast<uint8_t*>(g_base + Offsets::kRVA_Projectile_Launch);
		if (std::memcmp(target, Offsets::kLaunchPrologue, kLaunchPatchLen) != 0)
		{
			CP_LOG("Shots Follow Gun: Projectile::Launch prologue differs (another mod hooks it, or a different exe) - feature off. Bytes: %02X %02X %02X %02X %02X %02X",
				target[0], target[1], target[2], target[3], target[4], target[5]);
			return;
		}
		auto* tramp = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
		if (!tramp)
			return;
		std::memcpy(tramp, target, kLaunchPatchLen);
		WriteAbsJmp(tramp + kLaunchPatchLen, target + kLaunchPatchLen);
		g_launchOrig = reinterpret_cast<LaunchFn>(tramp);

		DWORD old = 0;
		VirtualProtect(target, kLaunchPatchLen, PAGE_EXECUTE_READWRITE, &old);
		WriteAbsJmp(target, reinterpret_cast<const void*>(&Hook_Launch));
		std::memset(target + 14, 0x90, kLaunchPatchLen - 14); // pad the rest of the stolen prologue
		VirtualProtect(target, kLaunchPatchLen, old, &old);
		FlushInstructionCache(GetCurrentProcess(), target, kLaunchPatchLen);
		CP_LOG("Shots Follow Gun: hooked Projectile::Launch at RVA 0x%llX", static_cast<unsigned long long>(Offsets::kRVA_Projectile_Launch));
	}

	void Init(uintptr_t moduleBase)
	{
		g_base = moduleBase;
		QueryPerformanceFrequency(&g_qpcFreq);
		CP_LOG("Bodycam initialized, module base = 0x%p", reinterpret_cast<void*>(moduleBase));
		InstallLaunchHook();
	}

	extern "C" __declspec(dllexport) bool Bodycam_RegisterRecoilV1(const BodycamRecoilHostV1* host)
	{
		if (!host || host->size < sizeof(BodycamRecoilHostV1) || host->version != kBodycamRecoilAPIVersion
			|| !host->OnShot || !host->Update)
			return false;
		g_recoilHost = *host;
		RecoilModel::Reset(); // the add-on takes over from the built-in model
		CP_LOG("Recoil API: add-on registered (v%u) - built-in recoil disabled", host->version);
		return true;
	}

	// Live MCM reload + old-plugin guard. Kept out of MaintainHook because it uses std::string,
	// which MSVC can't mix with __try in the same function. Returns false if hooks must not install.
	static bool PreInstallChecks()
	{
		static unsigned long long lastStamp = Config::Stamp();
		unsigned long long stamp = Config::Stamp();
		if (stamp != lastStamp)
		{
			lastStamp = stamp;
			g_config = Config::LoadAll();
			CP_LOG("MCM settings changed -> reloaded (enabled=%d preset=%d)", g_config.enabled, g_config.preset);
		}

		// If the old CornerPeek plugin is still enabled in MO2, stay out of its way: two plugins
		// hooking the same camera nodes would fight every 300 ms.
		static bool warnedOld = false;
		if (GetModuleHandleA("CornerPeek.dll"))
		{
			if (!warnedOld)
			{
				CP_LOG("CornerPeek.dll is also loaded - Bodycam stays inactive. Disable the old CornerPeek mod in MO2.");
				warnedOld = true;
			}
			return false;
		}
		return true;
	}

	void MaintainHook()
	{
		bool expected = false;
		if (!g_installing.compare_exchange_strong(expected, true))
			return;

		if (!PreInstallChecks())
		{
			g_installing = false;
			return;
		}

		__try
		{
			auto* cam = *reinterpret_cast<PlayerCameraView**>(g_base + Offsets::kRVA_g_playerCamera);
			if (cam && cam->cameraNode)
				g_cameraShadow = InstallOn(cam->cameraNode, &Hook_Camera, g_cameraShadow, "camera node");

			void* player = *reinterpret_cast<void**>(g_base + Offsets::kRVA_g_player);
			if (player)
			{
				auto* rig = *reinterpret_cast<NiAVObjectView**>(reinterpret_cast<uintptr_t>(player) + Offsets::kOff_Player_firstPersonSkeleton);
				if (rig)
				{
					g_rigShadow = InstallOn(rig, &Hook_Rig, g_rigShadow, "first-person skeleton");
					g_rigNode.store(rig);
				}
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			CP_LOG("MaintainHook: caught access violation while probing camera/player");
		}

		g_installing = false;
	}

	// F4SE PreSaveGame: never let a save capture our weapon FOV offset. The next frame puts it back.
	void OnPreSave()
	{
		__try
		{
			float* fov = LiveFov();
			if (fov && g_fovOwned)
			{
				*fov = g_fovBase;
				g_fovWritten = g_fovBase;
				CP_LOG("weapon hold: save - weapon FOV restored to %.1f for the save", g_fovBase);
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {}
	}
}
