# Bodycam

by **AverageBENNAL**

Bodycam-style first person for Fallout 4, Anniversary Edition and Old-Gen.

- The gun leads and the view catches up (the view trails your real aim on a spring; shots still go where the gun points).
- Camera and gun lean into mouse turns; the gun rests at a slight natural tilt.
- Walking, jogging and sprinting each have their own chest-cam bob, side shift and stride sway, blended by your actual speed.
- The gun retracts when you're tight against a wall, and turning into cover leans you out past the edge.
- Per weapon type hip, iron sights and low ready poses, and spring-driven recoil that climbs under sustained fire.
- Built to stay smooth with frame generation. Everything is configurable in MCM and applies live.

## Requirements

One of:

- **Anniversary Edition** - Fallout 4 1.11.240 with F4SE 0.7.9
- **Old-Gen** - Fallout 4 1.10.163 with F4SE 0.6.23

plus:

- Mod Configuration Menu (MCM)
- Microsoft Visual C++ 2015-2022 Redistributable (x64)

Both builds come from one source tree and are selected in the installer. Each
DLL only loads on its own F4SE, so the wrong choice fails safely rather than
misbehaving.

## Install (MO2)

Install `Bodycam x.y.z.zip` with MO2 or Vortex. It is a FOMOD installer: pick your game
version, then the starting settings. Everything it asks can be changed later in MCM.

If you used the earlier **CornerPeek** test build, disable or remove it. Bodycam stays inactive while `CornerPeek.dll` is loaded, so the two never fight.

## Settings

Open **Mod Config -> Bodycam**:

- **General**: enable/disable (fades smoothly), intensity preset (Subtle x0.6 / Default x1.0 / Intense x1.5; scales all motion, sliders still apply on top), Free Aim, Shots Follow.
- **Free Aim**: box size, recenter speed, gun speed, hit marker, sight-accurate shots.
- **Weapon Hold**: per weapon type hip pose, hip raise on fire, iron sights distance, sights and hip FOV, and Low Ready pose.
- **Look & Lean**: view lag, camera lean, resting gun tilt, gun lean, lean response.
- **Corner Peek**: automatic lean out from cover, angles and hip fire lean.
- **Gun Handling**: weapon inertia, gun drag, resting gun pose.
- **Recoil**: per weapon type climb, aiming multiplier, spring speeds.
- **Movement**: bob, side shift and stride sway per pace, footstep impact, breathing, jumping and landing.
- **Advanced**: pace speeds, walls & corners, direction fixes, keys, troubleshooting toggles.

MCM saves your changes to `Data/MCM/Settings/Bodycam.ini`; the plugin notices within a fraction of a second.

## Troubleshooting

`Data/F4SE/Plugins/Bodycam.log` records hook installation, detected rig/camera behavior and motion values.
If one-frame glitches appear with frame generation, turn off **Advanced -> Troubleshooting -> Gun Motion** and report the log.

## Building

`rebuild.bat` (VS 2022 Build Tools CMake, Visual Studio 17 2022 generator). Defaults in
`MCM/Config/Bodycam/settings.ini` must match `src/Config.h`.

## Licence

Copyright (c) 2026 AverageBENNAL. All rights reserved. The source is public to be
read and checked, not open source: reusing it needs my permission first. Terms are in
[LICENSE](LICENSE).

## Notice

Bodycam uses a small local declaration of the F4SE plugin interface and does not
include or redistribute any F4SE source or binaries. The game structure layouts and
offsets it relies on are in `src/GameTypes.h` and `src/Offsets.h`. No files from MCM,
HUDFramework or See-Through-Scopes are included; Bodycam works alongside them.

Fallout 4 is a trademark of Bethesda Softworks / ZeniMax Media. This is an unofficial
fan mod, not affiliated with or endorsed by Bethesda or Reissad Studio.
