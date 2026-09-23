#pragma once
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>

// All feel/tuning values, read from the MCM files in order (later wins): the defaults below,
// then Data\MCM\Config\Bodycam\settings.ini, then Data\MCM\Settings\Bodycam.ini. Re-read
// whenever they change on disk, so MCM edits apply live. Keys are type-prefixed the way MCM's
// ModSetting* sources expect (fFloat / iInt / bBool).
//
// Distances are game units (~1.43 cm), angles degrees, rates per-second smoothing speeds.
struct Config
{
	// ---- Main -----------------------------------------------------------------------------------
	bool enabled = true; // master switch; motion fades out/in smoothly (frame-gen-safe)
	int  preset  = 1;    // 0 Subtle (x0.6), 1 Default (x1.0), 2 Intense (x1.5) - scales all motion

	// ---- Weapon Hold: carry position per gun type ------------------------------------------------
	// Arms and gun pushed out along the line of sight (hip and sights separately), a hip low ready,
	// and a per-type viewmodel FOV change. Scopes stay vanilla.
	// Types: 0 Pistol, 1 Rifle/SMG, 2 Shotgun, 3 Heavy, 4 Melee.
	// No scale and no armScale here - both were tried and cannot work; see the notes in Bodycam.cpp.
	// hip*: the raised hip pose (Weapon Hold on). lr*: the low ready pose (Low Ready on).
	struct Hold { float hipForward, hipHeight, hipSide, hipPitchDeg,
	                    lrPitchDeg, lrYawDeg, lrDrop, lrSide,
	                    sightsForward, fovOffset;
	                    float hipRaise = 0.0f;     // extra hip height while firing
	                    // Share of Low Ready Swing Left taken by the arms; the gun turns the rest in the
	                    // hand. Two-handed guns stay at 1: turning in the hand pulls the handguard out of
	                    // the support hand and they read as held one-handed.
	                    float lrArmShare = 1.0f;
	                    float hipFov = 0.0f; };    // weapon FOV change in hip fire
	static constexpr int kHoldTypes = 5;
	bool  weaponHold  = true;
	// Weapon and world FOV are different projections, so the drawn gun is rotated by
	// tan(a') = k*tan(a), k = tan(Fweapon/2)/tan(Fworld/2). No setting, and deliberately not read
	// from the ini: without it the sights do not mark the shot at all, and a stale saved 0 would
	// silently break every raised-FOV shot the way an old Signs/fRoll once flipped the lean.

	// Where shots go when Free Aim is OFF. With Free Aim on the gun can leave the screen centre,
	// so shots always follow the barrel there and this setting does not apply.
	// 0 = crosshair (vanilla: the shot goes to the screen centre whatever the gun is doing),
	// 1 = barrel (Weapon Hold, cant, lean and bob move the point of impact).
	int   hipAim = 1;
	// Shots ignore the weapon's spread and go exactly where the sights point.
	// 0 = off (vanilla spread everywhere), 1 = iron sights only (hip fire keeps its spread),
	// 2 = always. It is a balance change, not an aim fix - vanilla spread is the weapon's
	// accuracy cone and no aim correction can remove it.
	int   pinpoint = 1;
	// The red hit marker. It lives outside the crosshair clip we move, so with Free Aim it stays
	// at screen centre while the shot lands where the gun pointed.
	// 0 = leave it alone, 1 = move it with the gun, 2 = hide it.
	int   hitIndicator = 1;
	Hold  hold[kHoldTypes] = {
		// Sights/FOV reverted 2026-09-20 to the 1.0.5 values: the later tuning traded distance for
		// weapon FOV, and a high weapon FOV widens the WHOLE first-person view, which read as warped.
		//  hipFwd height side pitch | lrAngle lrYaw lrDrop lrSide | sights fov | hipRaise lrArm hipFov
		{ 5.0f, -1.0f, -5.0f, -4.0f,  10.0f, 15.0f, 8.0f, -0.5f,  55.0f, 50.0f,  5.0f, 0.1f, 5.0f }, // Pistol (tuned in game)
		{ 15.0f, -1.0f, 0.0f, -4.0f,  10.0f, 45.0f, 0.0f, 0.0f,  50.0f, 40.0f,  0.5f, 1.0f, 5.0f }, // Rifle / SMG
		{ 15.0f, -20.0f, 15.0f, 9.5f,  -30.0f, 70.0f, 0.0f, 0.0f,  40.0f, 40.0f,  10.0f, 1.0f, 5.0f }, // Shotgun
		{ 20.0f, -3.5f, 0.0f, 0.0f,  -20.0f, 40.0f, -8.0f, 0.0f,  20.0f, 40.0f,  0.0f, 1.0f, 10.0f }, // Heavy
		{ 10.0f, -6.0f, 0.0f, 0.0f,  -5.0f, -20.0f, 0.0f, 0.0f,  15.0f, 0.0f,  0.0f, 0.6f, 10.0f }, // Melee / unarmed
	};
	float sightsUpDown      = 0.0f;  // calibration: raises (+) / lowers (-) the gun while aiming
	bool  lowReady          = true;  // muzzle dips when not shooting (hip); pose is per type in hold[]
	// While firing from the hip, this share of the hip muzzle dip (Hip Muzzle Angle + Resting
	// Muzzle Pitch) is taken out, so the barrel comes level with the crosshair.
	float hipFireLevel      = 1.0f;
	float lowReadyDelay     = 0.8f;  // seconds after your last shot before the gun lowers again
	float lowReadyRaiseRate = 12.0f; // how fast it comes up when you shoot / aim
	float lowReadyLowerRate = 1.0f;  // how slowly it settles back down
	float holdBlendRate     = 12.0f;  // hip <-> sights and weapon-swap blending
	// Share of Iron Sights Distance and Sights FOV Change a scoped gun gets. Pushed out the full
	// distance, the scope sits so far from the eye that the picture through it shrinks to a dot.
	float holdScoped        = 0.65f;

	// ---- Recoil ---------------------------------------------------------------------------
	// Built in; an external add-on registering through Bodycam_RegisterRecoilV1 replaces it.
	// See RecoilModel.h for the model. Weapon and camera have SEPARATE springs so they move at
	// different rates - that difference is the effect, not the raw sizes.
	// climbDeg is the PEAK MUZZLE CLIMB IN DEGREES one shot produces, and kickBack the peak
	// rearward shove in game units - both are what you actually see, so they keep their meaning
	// when the spring speed or damping is retuned. See RecoilModel::ImpulseForPeak.
	struct RecoilProfile { float climbDeg, kickBack; };
	bool  recoil = true;
	//                             climb  back
	RecoilProfile recoilType[kHoldTypes] = {
		// Climb is the HIP figure; iron sights get recoilAdsScale of it. The ADS number was measured
		// off Bodycam footage and verified in game, so it is held fixed at 6.0 x 0.63 = 3.78 deg
		// while the hip value was raised deliberately - hip fire should be the hectic one.
		{ 6.0f, 5.0f },  // Pistol: hip 6.0, sights 3.78 (measured)
		{ 3.7f, 4.0f },  // Rifle / SMG: shouldered, so less. Scaled from the pistol, not measured.
		{ 10.7f, 9.0f }, // Shotgun: scaled, not measured
		{ 7.4f, 6.0f },  // Heavy: scaled, not measured
		{ 0.0f, 0.0f },  // Melee: no shot to recoil from
	};
	// How much of the kick moves your REAL point of aim. This is the balance-affecting part:
	// bullets follow it, so sustained fire climbs and has to be controlled. 0 = purely visual.
	float recoilAimShare   = 0.40f;
	float recoilGunKick    = 1.00f; // extra visual kick on the weapon, on top of the aim
	float recoilCameraKick = 0.34f; // extra kick on the view, through its own spring
	float recoilAdsScale   = 0.63f; // all impulses x this while aiming (a braced weapon moves less)
	float recoilHz         = 3.0f;  // weapon spring: how fast it rings
	float recoilDamping    = 0.75f; // 1 = no overshoot; below that it corrects past rest first
	// The aim's own spring. Much slower than the weapon's on purpose: the weapon is back at rest
	// within ~50 ms, faster than the gap between rounds, so an aim riding the weapon's curve can
	// never accumulate into a climb.
	float recoilAimHz      = 1.25f;
	float recoilAimDamping = 0.75f; // matches the weapon spring; measured Bodycam aim tracks it
	float recoilCameraHz      = 3.5f;  // measured: the view moves WITH the weapon, not behind it
	float recoilCameraDamping = 0.75f;
	float recoilClimbRetention = 0.5f; // the slow spring now stacks on its own, so less is needed
	float recoilHorizontal = 2.0f;    // random left/right per shot, as a share of the climb
	float recoilRoll       = 2.5f;     // random twist per shot
	// Per-shot variation: without it every shot delivers an identical impulse and a burst climbs
	// in a repeatable staircase. Scales the climb, the aim and the punch together.
	float recoilVariation  = 0.5f;
	// Camera rattle - the jolt a body-mounted camera takes, over the top of the smooth spring.
	float recoilRattle      = 1.0f;  // degrees
	float recoilRattleHz    = 22.0f; // how fast it shakes
	float recoilRattleDecay = 18.0f; // how fast it dies (~150 ms)
	// The eye physically shoved back by the shot, in game units.
	float recoilPunch   = 0.35f;
	float recoilPunchHz = 9.0f;
	// Grip re-settle after a burst of 3+ rounds: the weapon drifts and resettles instead of
	// parking cleanly, as if the operator readjusted.
	float recoilSettle = 1.2f; // degrees
	float recoilMaxPitch   = 18.0f;
	float recoilMaxYaw     = 8.0f;

	// ---- Look: the rendered view trails your real aim on a spring ------------------------------
	float lagMaxDeg  = 9.0f;
	float lagFreq    = 14.0f;
	float lagDamping = 0.6f;
	// Free-aim box: the gun roams inside it, the view turns only at its edge.
	bool  freeAim         = true;
	float freeAimYawDeg   = 3.5f;  // half-width of the box (MEASURED off Bodycam footage)
	float freeAimPitchDeg = 2.2f;  // half-height of the box (MEASURED)
	float freeAimRecenter = 1.2f;  // per second; 0 = the gun stays where you left it
	float freeAimSpeed    = 0.8f;  // share of mouse movement that moves the gun in the box; the rest turns the view
	                               // (scaled by the preset: Subtle x0.5, Default x1, Intense x1.5 -> 0.25 / 0.5 / 0.75)
	// Screen float (Free Aim only): how much the view rides its own spring behind the gun rather
	// than tracking it rigidly. Uses Catch-up Speed / Catch-up Smoothness, which Free Aim
	// previously ignored entirely. 0 = the old rigid behaviour.
	float viewFloat       = 0.6f;
	float crosshairScale  = 1.0f;  // calibration for the crosshair's on-screen offset (hidden)
	bool  crosshairFollow = true;  // move the HUD crosshair onto the gun's aim while Free Aim is on

	// ---- Turn response (shared by camera lean and gun cant) ------------------------------------
	// Both leans used to be linear in turn rate and then hard-clamped, so anyone who turns quickly
	// - high mouse DPI, high in-game sensitivity - lived on the clamp and the gun snapped between
	// the two rails. The response now eases into the limit instead (see SoftLimit).
	float turnDeadzone   = 0.12f; // rad/s of turning ignored, so mouse jitter can't drive the lean
	float turnSensitivity = 1.0f; // scales how much turning it takes to lean; lower = calmer

	// ---- Camera lean (into mouse turns) ------------------------------------------------------------
	float rollTurn   = -2.0f; // degrees per rad/s of turning; - leans out of the turn (bodycam-like)
	bool  strafeTilt = true;  // camera leans on strafes (gun unaffected)
	float rollStrafe = -2.0f;  // degrees at full strafe speed (0 = turning only)
	float rollMaxDeg = 4.0f;
	bool  cameraTilt = true;  // master on/off for all camera tilt (turn lean, bob roll, stride tilt)
	float cameraTiltDeg = 1.0f;  // resting camera tilt: + = right, - = left, 0 = level
	// The camera works its way to one side, sits there, then settles the other way - average
	// seconds between side changes. 0 = a fixed tilt that never moves.
	float tiltAlternate = 25.0f;
	float tiltDriftRate = 0.35f; // how slowly it eases across when it does change sides
	float rollRate   = 5.0f;

	// ---- Breathing / bob roll -------------------------------------------------------------------
	// Motion sickness: drops every side-to-side component of the movement motion - the lateral
	// weight shift and the stride swing/tilt, at every pace. Up-and-down bounce, footstep impact
	// and breathing are untouched, because it is the horizontal rocking that triggers it.
	bool  noSideMotion = false;
	// One knob over ALL of the movement motion: bounce, side shift, stride swing/tilt and the
	// footstep kick, at every pace. 0 removes the lot without hunting through fifteen sliders.
	float moveMotion = 1.0f;
	float bobRollDeg = 0.5f;
	// 1.0.5: a short damped kick each time a foot lands (the bottom of the bounce). Its SIZE is
	// per-gait (below) - a walk should not land like a sprint; these two shape every kick.
	float stepImpactHz    = 4.5f;  // how fast it rings
	float stepImpactDecay = 8.0f;  // how fast it dies away
	// 1.0.6: leaving the ground and hitting it again. The launch dip is the body loading and
	// releasing (down, then up); the landing is an impact whose size comes from how fast you were
	// falling, so a hop off a kerb is nothing and a drop off a roof is not. Both are damped sines
	// that start and end at zero, like the footstep kick, so they stay frame-generation safe.
	float jumpLaunchDip   = 0.0f;   // units the view dips as you push off (0 = off, the default)
	float jumpLaunchRate  = 6.0f;   // how quickly you extend back up out of the push-off crouch
	// The ringing kick. Legs do NOT ring - they compress once and push back - so this is 0 by
	// default and the landing is carried by the crouch below. Kept as a slider for anyone who
	// wants a sharp floor slap on top.
	float jumpLandImpact  = 0.0f;   // units of vertical kick on landing at full force (0 = off)
	float jumpLandShake   = 0.9f;   // degrees of rotational shake on landing (view only, 0 = off)
	// The body ABSORBING the landing, as opposed to the ringing kick above: the view settles down
	// and comes back up, never oscillating. Its envelope rises from zero (a step would be
	// frame-generation hostile) and decays, so it reads as taking the weight.
	float jumpLandCrouch     = 6.0f; // units the view settles down on impact (0 = off)
	float jumpLandCrouchRate = 1.8f; // how quickly it comes back up (higher = quicker)
	// Speed at which we call it a take-off. The ground ray cannot do this job: it probes 220 units
	// and reads ~120 standing, so you must rise ~100 units before it misses - most of a vanilla
	// jump - which is why the launch dip appeared not to fire at all.
	float jumpTakeoffSpeed   = 120.0f;
	// Extra dip on the WEAPON only. The landing kick already moves the gun with the view, so on its
	// own there is no relative movement to see - this is what makes the hands visibly take the hit.
	float jumpLandGunDip  = 3.0f;   // units the gun drops on landing, on top of the view (0 = off)
	float jumpHz          = 2.5f;   // how fast the launch/landing kicks ring
	float jumpDecay       = 4.0f;   // how fast they die away
	float jumpLandRefSpeed = 700.0f; // fall speed that counts as a full-force landing
	float jumpMinAirTime  = 0.15f;  // shorter hops than this do not get a landing kick
	// Breathing while STANDING. Walking, jogging and sprinting have their own pair below and the
	// three blend into these as you slow to a stop, so you breathe harder the harder you work.
	float breathAmp  = 0.35f;
	float breathHz   = 0.3f;

	// ---- Per-pace bob + stride sway (blended by actual speed) ----------------------------------
	struct Gait { float speed, vertical, lateral, cycleHz, swayYawDeg, swayRollDeg,
	                    stepImpact, breathAmp, breathHz; };
	//             speed  vert  lat   hz     swayY  swayR  step  brAmp  brHz
	// Stride TILT was measured off Bodycam movement footage at 2.18 deg of camera roll (5-95 spread)
	// at a walk and 5.94 at a jog, i.e. an amplitude of ~1.1 and ~3 - ours was 0.25. That shortfall
	// is what read as the movement not being lofty enough. Swing and rhythm already matched.
	Gait walk   { 130.0f, 1.8f, 0.5f, 0.90f, 0.50f, 0.6f,  1.0f, 0.40f, 0.45f };
	Gait jog    { 340.0f, 4.0f, 1.0f, 1.65f, 0.65f, 0.75f, 1.5f, 0.55f, 0.85f };
	Gait sprint { 540.0f, 7.0f, 5.0f, 1.75f, 1.5f,  2.3f,  3.0f, 0.90f, 1.40f };

	// ---- Gun lean + motion --------------------------------------------------------------------
	float gunFollowRoll  = 1.5f;  // gun lean = this x camera lean + extra cant
	float cantTurn       = 3.1f;  // extra gun cant per rad/s of turning
	float cantStrafe     = 4.5f;
	float cantMaxDeg     = 7.0f;
	float cantRate       = 7.0f;  // spring frequency (rad/s) now, not an exponential rate
	float cantDamping    = 1.0f;  // 1 = rolls through without overshooting; below 1 it rocks past
	float gunRollMaxDeg  = 16.0f;
	float gunRestCantDeg = -2.0f;  // constant resting tilt, hip fire
	float gunRestCantAdsDeg = -1.0f; // same, aiming down sights
	float gunRestPitchDeg = -6.0f;   // resting muzzle up (+) / down (-), hip fire only
	// Weapon inertia from mouse look (1.0.6). The gun swings against the turn and springs back.
	// Separate from `inertia` below, which is a strafe-driven positional drag and does nothing
	// when you only turn the view.
	float lookInertia        = 8.0f; // degrees of swing per rad/s of mouse look
	float lookInertiaMax     = 12.0f; // largest swing, degrees
	float lookInertiaRate    = 5.0f; // spring frequency: how fast it catches up and settles
	float lookInertiaCant    = 1.25f; // degrees of cant per degree of swing
	float lookInertiaSlide   = 1.25f; // game units of slide per degree of swing
	float lookInertiaDamping = 0.6f; // below 1 it settles past centre, which feels organic
	float inertia        = 0.075f;
	float inertiaMax     = 8.0f;
	float inertiaRate    = 15.0f;
	float gunBobScale    = 1.0f;
	float strafeRefSpeed = 120.0f; // speed treated as "full strafe" for strafe lean / gun drag
	int   rigSpace       = -1;  // -1 auto-detect, 0 world, 1 camera-relative
	int   rigFollowsView = -1;
	int   cameraFollowsRigShift = -1;

	// ---- Scripted cameras -------------------------------------------------------------------------
	bool firstPersonOnly = true; // fade out unless the plain first-person camera is driving
	// The Pip-Boy screen is strapped to the first-person arm, and the game maps the mouse onto it
	// assuming the arm sits where vanilla put it. Any lean/cant/bob on the rig makes clicks land on
	// the wrong row, so fade out while the Pip-Boy (or the pause menu) is open.
	bool suspendInMenus = true;

	// ---- Stance: airborne + power armor ---------------------------------------------------------
	// Bob is a footstep effect: with nothing under your feet it reads as floating, so it fades out
	// whenever the body leaves the ground (jumps, the drop off the Museum roof, knockback).
	float groundedProbe     = 220.0f; // eye->floor ray length that still counts as standing on it
	float airborneBobScale  = 0.0f;   // bob amplitude while airborne (0 = no footsteps in mid-air)
	float airborneFallSpeed = 400.0f; // |vertical speed| that counts as airborne on its own
	float groundedRate      = 7.0f;   // fade speed across a jump or a landing
	// Power armor has heavy camera motion of its own and a much bigger frame; stacking the full
	// bodycam effect on top of it is what makes it feel unstable.
	float powerArmorScale   = 0.5f;   // all motion amplitudes x this while piloting power armor
	float powerArmorRate    = 4.0f;   // fade speed when climbing in or out

	// ---- Walls & corners (while aiming) ---------------------------------------------------------
	float probeForward = 75.0f;
	float sideProbe    = 60.0f;
	float peekDetect   = 100.0f;
	// Proximity ramp for the corner lean: full commitment at peekFull, tapering to peekMinScale
	// at peekDetect. Before 1.0.6 the lean was flat +-1 at any distance inside peekDetect.
	float peekFull     = 30.0f;  // at or inside this, the lean is at full strength
	float peekMinScale = 0.5f;   // floor on the ramp (0 = the far edge leans not at all)
	float peekStartDeg = 5.0f;   // turned this far into the wall before the lean starts
	float peekFullDeg  = 30.0f;  // turned this far into the wall = full lean
	float peekOverDeg  = 120.0f;
	float peekHipScale = 0.0f;   // share of the aimed lean in hip fire (0 = aiming only)  // how far past square you can keep turning before it lets go

	float retractStart    = 70.0f;
	float retractFull     = 18.0f;
	float retractBack     = 14.0f;
	float retractDrop     = 6.0f;
	float retractPitchDeg = 22.0f;
	float retractRate     = 10.0f;

	float gunPeekDist = 65.0f;
	float gunYawDeg   = 6.0f;
	float gunCantDeg  = 8.0f;
	float gunRate     = 3.0f;   // spring FREQUENCY since 1.0.6, not an exponential rate
	float gunPeekDamping = 0.8f; // 1 = swings out and settles without overshooting
	float camPeekDist = 60.0f;
	float camRollDeg  = 10.0f;
	float camRate     = 3.0f;
	float camPeekDamping = 1.0f;
	float camMargin   = 16.0f; // clearance for the near plane, not just the eye point
	float camPeekDrop = 8.0f;  // eye lowers this much at full lean, as when leaning from the hips

	// ---- Direction flips (1 or -1) ----------------------------------------------------------------
	float yawSign   = 1.0f;
	float rollSign  = 1.0f;
	float cantSign  = 1.0f;
	float pitchSign = 1.0f;

	// ---- Keys (Windows virtual-key codes): right mouse, A, D -----------------------------------
	int aimKey   = 0x02;
	int leftKey  = 0x41;
	int rightKey = 0x44;

	// ---- Debug ------------------------------------------------------------------------------------
	// Keep the gun's motion vectors honest for DLSS / frame generation (see Hook_Rig).
	bool  fixMotionVectors = true;
	bool  debugLog    = true;
	bool  aimCrosshair = false; // debug: keep the crosshair visible in iron sights, on the shot point
	bool  holdProbe   = false; // 1.0.5 diag: force Weapon Hold on with extreme values, pulsing every 4 s
	// 1.0.6 diag: can the eye be separated from the gun? The node dump puts 'Camera' (the eye) as
	// a SIBLING of 'COM', with the arms, weapon and Pip-Boy all under COM. 0 = off, 1 = rotate COM
	// by a fixed 15 degrees, 2 = rotate the skeleton ROOT by the same angle (the control case).
	// F8 cycles it in game. DIAG BUILD ONLY.
	int   comProbe = 0;   // OFF by default - F8 turns it on; it replaces the normal gun pose
	bool  viewMotion  = true;
	bool  gunMotion   = true;
	float blendInRate = 4.0f;

	// Gun Speed's own preset factor (0.25 / 0.5 / 0.75 at the default 0.5).
	float FreeAimSpeedPresetScale() const
	{
		switch (preset)
		{
		case 0:  return 0.5f;
		case 2:  return 1.5f;
		default: return 1.0f;
		}
	}

	float PresetScale() const
	{
		switch (preset)
		{
		case 0:  return 0.6f;
		case 2:  return 1.5f;
		default: return 1.0f;
		}
	}

	static std::string DataPath(const char* relative)
	{
		char exe[MAX_PATH]{};
		GetModuleFileNameA(nullptr, exe, MAX_PATH); // ...\Fallout 4\Fallout4.exe
		std::string p(exe);
		auto slash = p.find_last_of("\\/");
		return (slash == std::string::npos ? std::string() : p.substr(0, slash + 1)) + "Data\\" + relative;
	}
	static std::string DefaultsPath() { return DataPath("MCM\\Config\\Bodycam\\settings.ini"); }
	static std::string UserPath()     { return DataPath("MCM\\Settings\\Bodycam.ini"); }

	// Combined last-write stamp of both files; changes when MCM saves a setting.
	static unsigned long long Stamp()
	{
		unsigned long long s = 0;
		for (const std::string& p : { DefaultsPath(), UserPath() })
		{
			WIN32_FILE_ATTRIBUTE_DATA a{};
			if (GetFileAttributesExA(p.c_str(), GetFileExInfoStandard, &a))
				s += (static_cast<unsigned long long>(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
		}
		return s;
	}

	// Per-section switches (MCM [Toggles]), for running alongside mods that do the same job.
	// Applied after loading by zeroing that section's amounts, so the code paths stay untouched.
	bool tViewLag = true, tTurnLean = true, tGunLean = true, tCornerPeek = true, tLookInertia = true,
	     tGunDrag = true, tHeadBob = true, tJumpLand = true, tStepImpact = true, tRetract = true;

	void ApplyToggles()
	{
		if (!tViewLag) lagMaxDeg = 0.0f;
		if (!tTurnLean) rollTurn = 0.0f;
		if (!tGunLean) { cantTurn = cantStrafe = 0.0f; gunFollowRoll = 0.0f; gunRestCantDeg = gunRestCantAdsDeg = gunRestPitchDeg = 0.0f; }
		if (!tLookInertia) lookInertia = 0.0f;
		if (!tGunDrag) inertia = 0.0f;
		if (!tHeadBob) moveMotion = 0.0f;
		if (!tJumpLand) { jumpLaunchDip = jumpLandImpact = jumpLandShake = jumpLandCrouch = jumpLandGunDip = 0.0f; }
		if (!tStepImpact) walk.stepImpact = jog.stepImpact = sprint.stepImpact = 0.0f;
		if (!tRetract) retractBack = retractDrop = retractPitchDeg = 0.0f;
	}

	void LoadFile(const std::string& path)
	{
		if (GetFileAttributesA(path.c_str()) == INVALID_FILE_ATTRIBUTES)
			return;

		auto f = [&](const char* sec, const char* key, float& v) {
			char buf[64]{};
			GetPrivateProfileStringA(sec, key, "", buf, sizeof(buf), path.c_str());
			if (buf[0]) v = static_cast<float>(std::atof(buf));
		};
		auto i = [&](const char* sec, const char* key, int& v) {
			char buf[64]{};
			GetPrivateProfileStringA(sec, key, "", buf, sizeof(buf), path.c_str());
			if (buf[0]) v = static_cast<int>(std::strtol(buf, nullptr, 0));
		};
		auto b = [&](const char* sec, const char* key, bool& v) {
			int t = v ? 1 : 0;
			i(sec, key, t);
			v = t != 0;
		};
		auto gait = [&](const char* sec, Gait& g) {
			f(sec, "fSpeed", g.speed); f(sec, "fVertical", g.vertical); f(sec, "fLateral", g.lateral);
			f(sec, "fCycleHz", g.cycleHz); f(sec, "fSwayYawDeg", g.swayYawDeg); f(sec, "fSwayRollDeg", g.swayRollDeg);
			f(sec, "fStepImpact", g.stepImpact); f(sec, "fBreathAmp", g.breathAmp); f(sec, "fBreathHz", g.breathHz);
		};

		b("Main", "bEnabled", enabled);
		i("Main", "iPreset", preset);

		f("Look", "fLagMaxDeg", lagMaxDeg);
		f("Look", "fLagFreq", lagFreq);
		f("Look", "fLagDamping", lagDamping);
		b("Look", "bFreeAim", freeAim);
		f("Look", "fFreeAimYawDeg", freeAimYawDeg);
		f("Look", "fFreeAimPitchDeg", freeAimPitchDeg);
		f("Look", "fFreeAimRecenter", freeAimRecenter);
		f("Look", "fFreeAimSpeed", freeAimSpeed);
		f("Look", "fViewFloat", viewFloat);
		f("Look", "fCrosshairScale", crosshairScale);
		b("Main", "bCrosshairFollow", crosshairFollow);

		b("Main", "bWeaponHold", weaponHold);
		i("Look", "iHipAim", hipAim);
		i("Look", "iPinpoint", pinpoint);
		i("Look", "iHitIndicator", hitIndicator);
		{
			static const char* const kTypes[kHoldTypes] = { "Pistol", "Rifle", "Shotgun", "Heavy", "Melee" };
			for (int t = 0; t < kHoldTypes; ++t)
			{
				char k[64];
				snprintf(k, sizeof(k), "f%sHipForward", kTypes[t]);    f("Hold", k, hold[t].hipForward);
				snprintf(k, sizeof(k), "f%sHipHeight", kTypes[t]);     f("Hold", k, hold[t].hipHeight);
				snprintf(k, sizeof(k), "f%sHipSide", kTypes[t]);       f("Hold", k, hold[t].hipSide);
				snprintf(k, sizeof(k), "f%sLowReadyArmShare", kTypes[t]); f("Hold", k, hold[t].lrArmShare);
				snprintf(k, sizeof(k), "f%sHipFovOffset", kTypes[t]);   f("Hold", k, hold[t].hipFov);
				snprintf(k, sizeof(k), "f%sHipRaise", kTypes[t]);      f("Hold", k, hold[t].hipRaise);
				snprintf(k, sizeof(k), "f%sHipPitchDeg", kTypes[t]);   f("Hold", k, hold[t].hipPitchDeg);
				snprintf(k, sizeof(k), "f%sLowReadyPitchDeg", kTypes[t]); f("Hold", k, hold[t].lrPitchDeg);
				snprintf(k, sizeof(k), "f%sLowReadyYawDeg", kTypes[t]);   f("Hold", k, hold[t].lrYawDeg);
				snprintf(k, sizeof(k), "f%sLowReadyDrop", kTypes[t]);     f("Hold", k, hold[t].lrDrop);
				snprintf(k, sizeof(k), "f%sLowReadySide", kTypes[t]);     f("Hold", k, hold[t].lrSide);
				snprintf(k, sizeof(k), "f%sSightsForward", kTypes[t]); f("Hold", k, hold[t].sightsForward);
				snprintf(k, sizeof(k), "f%sFovOffset", kTypes[t]);     f("Hold", k, hold[t].fovOffset);
			}
		}
		f("Hold", "fSightsUpDown", sightsUpDown);
		b("Hold", "bLowReady", lowReady);
		f("Hold", "fHipFireLevel", hipFireLevel);
		f("Hold", "fLowReadyDelay", lowReadyDelay);
		f("Hold", "fLowReadyRaiseRate", lowReadyRaiseRate);
		f("Hold", "fLowReadyLowerRate", lowReadyLowerRate);
		f("Hold", "fBlendRate", holdBlendRate);
		f("Hold", "fScoped", holdScoped);

		b("Recoil", "bEnabled", recoil);
		{
			static const char* const kTypes[kHoldTypes] = { "Pistol", "Rifle", "Shotgun", "Heavy", "Melee" };
			for (int t = 0; t < kHoldTypes; ++t)
			{
				char k[64];
				snprintf(k, sizeof(k), "f%sClimbDeg", kTypes[t]); f("Recoil", k, recoilType[t].climbDeg);
				snprintf(k, sizeof(k), "f%sBack", kTypes[t]);    f("Recoil", k, recoilType[t].kickBack);
			}
		}
		f("Recoil", "fAimShare", recoilAimShare);
		f("Recoil", "fGunKick", recoilGunKick);
		f("Recoil", "fCameraKick", recoilCameraKick);
		f("Recoil", "fADSScale", recoilAdsScale);
		f("Recoil", "fSpringHz", recoilHz);
		f("Recoil", "fDamping", recoilDamping);
		f("Recoil", "fAimHz", recoilAimHz);
		f("Recoil", "fAimDamping", recoilAimDamping);
		f("Recoil", "fCameraHz", recoilCameraHz);
		f("Recoil", "fCameraDamping", recoilCameraDamping);
		f("Recoil", "fClimbRetention", recoilClimbRetention);
		f("Recoil", "fHorizontal", recoilHorizontal);
		f("Recoil", "fRoll", recoilRoll);
		f("Recoil", "fVariation", recoilVariation);
		f("Recoil", "fRattle", recoilRattle);
		f("Recoil", "fRattleHz", recoilRattleHz);
		f("Recoil", "fRattleDecay", recoilRattleDecay);
		f("Recoil", "fPunch", recoilPunch);
		f("Recoil", "fPunchHz", recoilPunchHz);
		f("Recoil", "fSettle", recoilSettle);
		f("Recoil", "fMaxPitch", recoilMaxPitch);
		f("Recoil", "fMaxYaw", recoilMaxYaw);

		f("Turn", "fDeadzoneRadS", turnDeadzone);
		f("Turn", "fSensitivity", turnSensitivity);

		f("Roll", "fTurnDegPerRadS", rollTurn);
		f("Roll", "fStrafeDeg", rollStrafe);
		b("Main", "bStrafeTilt", strafeTilt);
		f("Roll", "fMaxDeg", rollMaxDeg);
		b("Main", "bCameraTilt", cameraTilt);
		f("Roll", "fCameraTiltDeg", cameraTiltDeg);
		f("Roll", "fTiltAlternate", tiltAlternate);
		f("Roll", "fTiltDriftRate", tiltDriftRate);
		f("Roll", "fRate", rollRate);

		b("Bob", "bNoSideMotion", noSideMotion);
		f("Bob", "fMoveMotion", moveMotion);
		f("Bob", "fRollDeg", bobRollDeg);
		f("Bob", "fStepImpactHz", stepImpactHz);
		f("Bob", "fStepImpactDecay", stepImpactDecay);
		f("Jump", "fLaunchDip", jumpLaunchDip);
		f("Jump", "fLandImpact", jumpLandImpact);
		f("Jump", "fLandShakeDeg", jumpLandShake);
		f("Jump", "fHz", jumpHz);
		f("Jump", "fDecay", jumpDecay);
		f("Jump", "fLandRefSpeed", jumpLandRefSpeed);
		f("Jump", "fLandCrouch", jumpLandCrouch);
		f("Jump", "fLandCrouchRate", jumpLandCrouchRate);
		f("Jump", "fLandGunDip", jumpLandGunDip);
		f("Jump", "fLaunchRate", jumpLaunchRate);
		f("Jump", "fTakeoffSpeed", jumpTakeoffSpeed);
		f("Jump", "fMinAirTime", jumpMinAirTime);
		f("Bob", "fBreathAmp", breathAmp);
		f("Bob", "fBreathHz", breathHz);
		gait("BobWalk", walk);
		gait("BobJog", jog);
		gait("BobSprint", sprint);

		f("GunMotion", "fFollowViewRoll", gunFollowRoll);
		f("GunMotion", "fCantTurnDegPerRadS", cantTurn);
		f("GunMotion", "fCantStrafeDeg", cantStrafe);
		f("GunMotion", "fCantMaxDeg", cantMaxDeg);
		f("GunMotion", "fCantRate", cantRate);
		f("GunMotion", "fCantDamping", cantDamping);
		f("GunMotion", "fRollMaxDeg", gunRollMaxDeg);
		f("GunMotion", "fRestCantDeg", gunRestCantDeg);
		f("GunMotion", "fRestCantAdsDeg", gunRestCantAdsDeg);
		f("GunMotion", "fRestPitchDeg", gunRestPitchDeg);
		f("GunMotion", "fLookInertia", lookInertia);
		f("GunMotion", "fLookInertiaMax", lookInertiaMax);
		f("GunMotion", "fLookInertiaCant", lookInertiaCant);
		f("GunMotion", "fLookInertiaSlide", lookInertiaSlide);
		f("GunMotion", "fLookInertiaRate", lookInertiaRate);
		f("GunMotion", "fLookInertiaDamping", lookInertiaDamping);
		f("GunMotion", "fInertia", inertia);
		f("GunMotion", "fInertiaMax", inertiaMax);
		f("GunMotion", "fInertiaRate", inertiaRate);
		f("GunMotion", "fBobScale", gunBobScale);
		f("GunMotion", "fStrafeRefSpeed", strafeRefSpeed);
		i("GunMotion", "iRigSpace", rigSpace);
		i("GunMotion", "iRigFollowsView", rigFollowsView);
		i("GunMotion", "iCameraFollowsRigShift", cameraFollowsRigShift);

		b("Stance", "bFirstPersonOnly", firstPersonOnly);
		b("Stance", "bSuspendInMenus", suspendInMenus);
		f("Stance", "fGroundedProbe", groundedProbe);
		f("Stance", "fAirborneBobScale", airborneBobScale);
		f("Stance", "fAirborneFallSpeed", airborneFallSpeed);
		f("Stance", "fGroundedRate", groundedRate);
		f("Stance", "fPowerArmorScale", powerArmorScale);
		f("Stance", "fPowerArmorRate", powerArmorRate);

		f("Detection", "fProbeForward", probeForward);
		f("Detection", "fSideProbe", sideProbe);
		f("Detection", "fPeekDetect", peekDetect);
		f("Detection", "fPeekFull", peekFull);
		f("Detection", "fPeekMinScale", peekMinScale);
		f("Detection", "fPeekStartDeg", peekStartDeg);
		f("Detection", "fPeekFullDeg", peekFullDeg);
		f("Detection", "fPeekOverDeg", peekOverDeg);
		f("Detection", "fPeekHipScale", peekHipScale);

		f("Retract", "fStart", retractStart);
		f("Retract", "fFull", retractFull);
		f("Retract", "fBack", retractBack);
		f("Retract", "fDrop", retractDrop);
		f("Retract", "fPitchDeg", retractPitchDeg);
		f("Retract", "fRate", retractRate);

		f("CornerGun", "fPeekDist", gunPeekDist);
		f("CornerGun", "fYawDeg", gunYawDeg);
		f("CornerGun", "fCantDeg", gunCantDeg);
		f("CornerGun", "fRate", gunRate);
		f("CornerGun", "fDamping", gunPeekDamping);

		f("CornerCamera", "fPeekDist", camPeekDist);
		f("CornerCamera", "fRollDeg", camRollDeg);
		f("CornerCamera", "fRate", camRate);
		f("CornerCamera", "fDamping", camPeekDamping);
		f("CornerCamera", "fMargin", camMargin);
		f("CornerCamera", "fDrop", camPeekDrop);

		f("Signs", "fYaw", yawSign);
		// "Signs/fRoll" is no longer read: Lean Into Turns and Camera Lean From Strafing are signed
		// themselves since 1.0.5, and an old saved -1 would silently flip them back.
		f("Signs", "fCant", cantSign);
		f("Signs", "fPitch", pitchSign);

		i("Keys", "iAim", aimKey);
		i("Keys", "iLeft", leftKey);
		i("Keys", "iRight", rightKey);

		b("Debug", "bFixMotionVectors", fixMotionVectors);
		b("Debug", "bLog", debugLog);
		b("Debug", "bHoldProbe", holdProbe);
		i("Debug", "iComProbe", comProbe);
		b("Debug", "bAimCrosshair", aimCrosshair);
		b("Debug", "bViewMotion", viewMotion);
		b("Debug", "bGunMotion", gunMotion);
		f("Debug", "fBlendInRate", blendInRate);

		b("Toggles", "bViewLag", tViewLag);
		b("Toggles", "bTurnLean", tTurnLean);
		b("Toggles", "bGunLean", tGunLean);
		b("Toggles", "bCornerPeek", tCornerPeek);
		b("Toggles", "bLookInertia", tLookInertia);
		b("Toggles", "bGunDrag", tGunDrag);
		b("Toggles", "bHeadBob", tHeadBob);
		b("Toggles", "bJumpLand", tJumpLand);
		b("Toggles", "bStepImpact", tStepImpact);
		b("Toggles", "bRetract", tRetract);
	}

	// Rebuilds from compiled defaults, then the MCM defaults, then the player's MCM changes.
	// A fresh install's choices are otherwise invisible to a returning player: MCM writes the saved
	// settings to Data\MCM\Settings\Bodycam.ini, which in MO2 sits in Overwrite - above every mod -
	// so it beats whatever the FOMOD installed and the installer's questions do nothing. So the
	// shipped defaults carry [Install] sStamp = version + ticked boxes; a different stamp replaces
	// the saved file once, the same stamp does nothing, and later edits are never touched.
	// Returns: 0 nothing to do, 1 applied, -1 tried and failed (logged by the caller).
	static int ApplyInstallStamp(std::string& shipped, std::string& saved)
	{
		auto stamp = [](const std::string& path) {
			char buf[128]{};
			GetPrivateProfileStringA("Install", "sStamp", "", buf, sizeof(buf), path.c_str());
			return std::string(buf);
		};
		shipped = stamp(DefaultsPath());
		saved = stamp(UserPath());
		if (shipped.empty() || shipped == saved)
			return 0;
		if (GetFileAttributesA(UserPath().c_str()) == INVALID_FILE_ATTRIBUTES)
			return 0; // no saved settings yet: the shipped defaults already apply on their own
		return CopyFileA(DefaultsPath().c_str(), UserPath().c_str(), FALSE) ? 1 : -1;
	}

	static Config LoadAll()
	{
		Config c;
		c.LoadFile(DefaultsPath());
		c.LoadFile(UserPath());
		c.ApplyToggles();
		return c;
	}
};

inline Config g_config;
