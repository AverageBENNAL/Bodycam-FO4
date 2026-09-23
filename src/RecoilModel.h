#pragma once
// Built-in recoil. The weapon can be violently unstable while the view stays readable - that
// only works if the two move at DIFFERENT RATES, not one curve scaled down. Hence two springs:
// the weapon's (fast, sharp) and the camera's (slower), the second chasing the first.
//
// Three separate channels. `aim` moves the real point of aim, so bullets follow it and sustained
// fire has to be controlled; `weapon` and `camera` are extra kick on top, and both already
// inherit aim. So weaponKick/cameraKick are the DIFFERENCE between view and gun - which is the
// part that reads as physicality.
#include "BodycamRecoilAPI.h"
#include "Config.h"
#include <algorithm>
#include <cmath>

namespace RecoilModel
{
	inline constexpr float kTwoPiR = 6.2831853f;

	struct Axis { float x = 0.0f, v = 0.0f; };

	// THREE springs, deliberately at three different speeds - this is the whole design.
	inline Axis g_aimPitch, g_aimYaw;             // where the bullets go: slow, so bursts CLIMB
	inline Axis g_pitch, g_yaw, g_roll, g_back;   // the weapon: fast and sharp
	inline Axis g_camPitch, g_camYaw, g_camRoll;  // the view: medium, chasing the weapon
	inline uint32_t g_rng = 0xB0D1CA4Du;
	// Camera rattle: a body-mounted camera does not glide through a gunshot, it jolts. Short,
	// sharp and high-frequency, on the VIEW only, over the top of the smooth spring.
	inline float g_rattleAmp = 0.0f, g_rattleAge = 1.0f;
	// Positional punch on the eye, in game units: back along the view and slightly up.
	inline Axis g_punch;
	// Grip re-settle: after sustained fire the operator readjusts instead of returning cleanly.
	inline int   g_burstShots = 0;
	inline float g_settleAmp = 0.0f, g_settleAge = 1.0f;
	inline unsigned long long g_lastFireMs = 0;
	inline unsigned long long g_lastShotMs = 0;
	inline int g_shots = 0;       // diagnostics: how many impulses were actually delivered
	inline float g_lastV0 = 0.0f; // diagnostics: the velocity the last shot injected

	inline float RandomSigned()
	{
		g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
		return (static_cast<float>(g_rng & 0xffff) / 32767.5f) - 1.0f;
	}

	inline void Reset()
	{
		g_aimPitch = {}; g_aimYaw = {};
		g_pitch = {}; g_yaw = {}; g_roll = {}; g_back = {};
		g_camPitch = {}; g_camYaw = {}; g_camRoll = {};
		g_rattleAmp = 0.0f; g_rattleAge = 1.0f;
		g_punch = {};
		g_burstShots = 0; g_settleAmp = 0.0f; g_settleAge = 1.0f; g_lastFireMs = 0;
	}

	// Damped spring toward `target`, advanced by the EXACT closed form - do not go back to numerical
	// integration here. Euler is only stable while w*dt < 2 and this game sims at ~30 fps under frame
	// gen (already 2.2); sub-stepping fixes stability but not accuracy - w*h = 0.3 still lands 50%
	// short of the true peak. The closed form is exact at any dt and identical on every machine.
	inline void Spring(Axis& a, float target, float hz, float damping, float dt)
	{
		const float w = kTwoPiR * std::max(0.1f, hz);
		const float z = std::clamp(damping, 0.05f, 4.0f);
		const float d = a.x - target; // displacement from rest
		const float v = a.v;
		float nd, nv;
		if (z < 0.999f) // underdamped: rings as it settles
		{
			const float wd = w * std::sqrt(1.0f - z * z);
			const float e = std::exp(-z * w * dt);
			const float c = std::cos(wd * dt), sn = std::sin(wd * dt);
			const float B = (v + z * w * d) / wd;
			nd = e * (d * c + B * sn);
			nv = e * (-z * w * (d * c + B * sn) + wd * (B * c - d * sn));
		}
		else if (z < 1.001f) // critically damped: fastest return with no overshoot
		{
			const float e = std::exp(-w * dt);
			nd = e * (d + (v + w * d) * dt);
			nv = e * (v - w * dt * (v + w * d));
		}
		else // overdamped: two real roots, crawls home
		{
			const float s = w * std::sqrt(z * z - 1.0f);
			const float r1 = -z * w + s, r2 = -z * w - s;
			const float c1 = (v - r2 * d) / (r1 - r2), c2 = d - c1;
			const float e1 = std::exp(r1 * dt), e2 = std::exp(r2 * dt);
			nd = c1 * e1 + c2 * e2;
			nv = r1 * c1 * e1 + r2 * c2 * e2;
		}
		if (!std::isfinite(nd) || !std::isfinite(nv)) { a.x = target; a.v = 0.0f; return; }
		a.x = target + nd;
		a.v = nv;
	}

	// Clamp a position without letting the spring keep pushing into the stop (which sticks there
	// and then releases oddly), and stop the weapon dipping BELOW rest on the way back: recoil is
	// displacement then correction, not a bounce through centre.
	inline void ClampAxis(Axis& a, float lo, float hi)
	{
		if (a.x > hi) { a.x = hi; if (a.v > 0.0f) a.v = 0.0f; }
		if (a.x < lo) { a.x = lo; if (a.v < 0.0f) a.v = 0.0f; }
	}

	// The per-weapon value is PEAK CLIMB IN DEGREES, not an opaque impulse: invert the oscillator
	// peak for the v0 that produces it, so the number keeps its meaning when the spring is retuned.
	//   underdamped  x(t) = (v0/wd) e^(-z w t) sin(wd t),  peak at t = atan(sqrt(1-z^2)/z)/wd
	//   critical/over x(t) = v0 t e^(-w t),                peak v0/(w e)
	inline float ImpulseForPeak(float peakDeg, float hz, float z)
	{
		const float w = kTwoPiR * std::max(0.1f, hz);
		z = std::clamp(z, 0.05f, 4.0f);
		float unitPeak; // peak reached by v0 = 1
		if (z < 0.999f)
		{
			const float r = std::sqrt(1.0f - z * z);
			const float wd = w * r;
			const float tp = std::atan2(r, z) / wd;
			unitPeak = (1.0f / wd) * std::exp(-z * w * tp) * std::sin(wd * tp);
		}
		else
		{
			unitPeak = 1.0f / (w * 2.718281828f);
		}
		return unitPeak > 1e-6f ? peakDeg / unitPeak : 0.0f;
	}

	inline void OnShot(uint32_t, int type, bool aiming)
	{
		const Config& c = g_config;
		if (!c.recoil)
			return;
		// One impulse per discharge: a shotgun fires every pellet as its own Launch call.
		const unsigned long long ms = GetTickCount64();
		if (ms - g_lastShotMs < 40)
			return;
		g_lastShotMs = ms;
		++g_shots;

		const int t = std::clamp(type, 0, Config::kHoldTypes - 1);
		const Config::RecoilProfile& p = c.recoilType[t];
		const float ads = aiming ? c.recoilAdsScale : 1.0f;
		// Every shot used to deliver exactly the same vertical impulse, so a burst climbed in a
		// perfectly repeatable staircase - the metronomic quality that stops recoil feeling chaotic.
		// One multiplier, shared by the climb, the aim and the punch, so a hot shot is hot in every
		// channel at once rather than the parts disagreeing.
		const float vary = 1.0f + c.recoilVariation * RandomSigned();
		const float v0 = ImpulseForPeak(p.climbDeg, c.recoilHz, c.recoilDamping) * vary;

		// Velocity is ADDED, and only partly reset between shots, so automatic fire stacks and
		// climbs instead of replaying one scripted kick. climbRetention is how much of the shot
		// you are still recovering from carries into the next one.
		const float yawMix = c.recoilHorizontal * RandomSigned();
		g_pitch.v = g_pitch.v * c.recoilClimbRetention + v0 * ads;
		g_yaw.v  += v0 * yawMix * ads;
		g_roll.v += v0 * 0.55f * c.recoilRoll * RandomSigned() * ads;
		g_back.v += ImpulseForPeak(p.kickBack, c.recoilHz * 1.25f, c.recoilDamping) * ads;

		// The aim gets its OWN, much slower impulse. The weapon's spring rests inside ~50 ms, quicker
		// than the gap between rounds, so an aim riding that curve resets every shot and never climbs
		// (measured: topped out at 1.2 deg). The slow spring is what makes sustained fire walk up.
		const float av = ImpulseForPeak(p.climbDeg * c.recoilAimShare, c.recoilAimHz, c.recoilAimDamping) * vary;
		g_lastV0 = v0;
		g_aimPitch.v = g_aimPitch.v * c.recoilClimbRetention + av * ads;
		g_aimYaw.v  += av * yawMix * ads;

		g_rattleAmp = c.recoilRattle * ads * vary;
		g_rattleAge = 0.0f;
		g_punch.v += c.recoilPunch * 60.0f * ads * vary;
		if (ms - g_lastFireMs > 400) g_burstShots = 0;   // a new burst
		g_lastFireMs = ms;
		++g_burstShots;
	}

	inline void Update(float dt, bool /*aiming*/, bool active, BodycamRecoilPoseV1* out)
	{
		if (!out)
			return;
		const Config& c = g_config;
		if (!active || !c.recoil)
		{
			Reset();
			return;
		}
		dt = std::clamp(dt, 0.001f, 0.05f);

		// The aim: slow, so consecutive shots stack into a climb you have to pull down against.
		Spring(g_aimPitch, 0.0f, c.recoilAimHz, c.recoilAimDamping, dt);
		Spring(g_aimYaw,   0.0f, c.recoilAimHz, c.recoilAimDamping, dt);
		ClampAxis(g_aimPitch, 0.0f, c.recoilMaxPitch);
		ClampAxis(g_aimYaw, -c.recoilMaxYaw, c.recoilMaxYaw);

		// The weapon: fast and sharp, recovering to rest.
		Spring(g_pitch, 0.0f, c.recoilHz, c.recoilDamping, dt);
		Spring(g_yaw,   0.0f, c.recoilHz, c.recoilDamping, dt);
		Spring(g_roll,  0.0f, c.recoilHz, c.recoilDamping, dt);
		Spring(g_back,  0.0f, c.recoilHz * 1.25f, c.recoilDamping, dt);
		ClampAxis(g_pitch, 0.0f, c.recoilMaxPitch);
		ClampAxis(g_yaw, -c.recoilMaxYaw, c.recoilMaxYaw);

		// The camera: its own slower, heavier spring chasing the weapon's current displacement.
		// This is the decoupling - at the shot the weapon is already up and the view has barely
		// started; on the way down the view is still coming back when the weapon has settled.
		Spring(g_camPitch, g_pitch.x, c.recoilCameraHz, c.recoilCameraDamping, dt);
		Spring(g_camYaw,   g_yaw.x,   c.recoilCameraHz, c.recoilCameraDamping, dt);
		Spring(g_camRoll,  g_roll.x,  c.recoilCameraHz, c.recoilCameraDamping, dt);

		// Camera rattle: three axes at deliberately unrelated frequencies and phases, otherwise the
		// jolt is a straight line rather than a shake. Dies inside ~150 ms.
		float rattleP = 0.0f, rattleY = 0.0f, rattleR = 0.0f;
		g_rattleAge += dt;
		if (g_rattleAmp > 0.0001f)
		{
			const float e = g_rattleAmp * std::exp(-c.recoilRattleDecay * g_rattleAge);
			const float w = kTwoPiR * c.recoilRattleHz * g_rattleAge;
			rattleP = e * std::sin(w);
			rattleY = e * 0.8f * std::sin(w * 1.37f + 1.1f);
			rattleR = e * 1.2f * std::sin(w * 0.79f + 2.3f);
			if (g_rattleAge > 1.0f) g_rattleAmp = 0.0f;
		}

		// Eye punch: shoved back along the view, springing home.
		Spring(g_punch, 0.0f, c.recoilPunchHz, 0.8f, dt);

		// Grip re-settle: once a burst of a few rounds stops, the weapon drifts and resettles
		// rather than parking cleanly - the operator readjusting.
		g_settleAge += dt;
		if (g_burstShots >= 3 && GetTickCount64() - g_lastFireMs > 260)
		{
			g_settleAmp = c.recoilSettle * std::min(1.0f, g_burstShots / 8.0f);
			g_settleAge = 0.0f;
			g_burstShots = 0;
		}
		float settle = 0.0f;
		if (g_settleAmp > 0.0001f)
		{
			settle = g_settleAmp * std::exp(-2.2f * g_settleAge) * std::sin(kTwoPiR * 1.6f * g_settleAge);
			if (g_settleAge > 2.5f) g_settleAmp = 0.0f;
		}

		// Aim: what the bullets follow. Both the view and the weapon are built from it, so the
		// two kick terms below are what each gets ON TOP.
		out->aimPitchUpDeg = g_aimPitch.x;
		out->aimYawDeg     = g_aimYaw.x;

		out->gunPitchUpDeg = g_pitch.x * c.recoilGunKick + settle * 0.6f;
		out->gunYawDeg     = g_yaw.x * c.recoilGunKick;
		out->gunRollDeg    = g_roll.x + settle;
		out->gunBack       = std::max(0.0f, g_back.x);

		out->cameraPitchUpDeg = g_camPitch.x * c.recoilCameraKick + rattleP;
		out->cameraYawDeg     = g_camYaw.x * c.recoilCameraKick + rattleY;
		out->cameraRollDeg    = g_camRoll.x * c.recoilCameraKick + rattleR;
	}
}
