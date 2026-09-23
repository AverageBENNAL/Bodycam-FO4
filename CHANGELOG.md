# Changelog

## 1.0.6 - 2026-09-23
- **New: Recoil** (its own MCM page). Every shot drives a spring rather than replaying a canned kick, so automatic fire stacks and climbs instead of resetting between rounds.
- The weapon, your point of aim and the view are three **separate** springs running at three different speeds. The weapon snaps up hard and is back within a fraction of a second; the view follows heavier and slower, peaking later and lower; your aim comes back slowest of all, which is what makes a burst walk up a wall. That difference between them is what makes a gun feel heavy - making everything kick harder together just makes it hard to watch.
- Recoil moves your **real** point of aim, so bullets genuinely climb and have to be controlled. Aim Impact turns that down to 0 if you would rather it stayed cosmetic.
- Per weapon type, set as the **peak muzzle climb in degrees** you actually see - pistol 6, rifle 3.7, shotgun 10.7, heavy 7.4 - so the numbers keep their meaning when you retune the springs. A shotgun kicks once per shot, not once per pellet.
- Recoil is reduced while aiming down sights, since a braced weapon moves less.
- An external recoil add-on registering through Bodycam's plugin interface still replaces the built-in model entirely.
- Footstep Impact and Breathing are now **per pace** (Movement page) instead of one value for every speed, so a walk no longer lands as hard as a sprint. Breathing does not stop when you do: below walking pace it blends down into the new Standing Still sliders.
- **Fixed: with a raised weapon FOV, shots landed well outside the sights.** The gun is deliberately drawn swung out so its sights cover the impact point, but the shot was being read off that drawn pose, so it inherited the swing and landed about twice as far from the centre of the screen as the crosshair. Shots now ignore it, and the sights are honest again at any position in the free-aim box.
- **New: Shots Follow** (General page) - choose whether shots go to the crosshair like vanilla, or along the barrel so Weapon Hold, cant, lean and bob move where rounds land. With Free Aim on, shots always follow the barrel. Default is the barrel.
- **Weapon Hold now covers hip fire too, set per weapon type.** Each type has its own Hip Push, Hip Height, Hip Center, Hip Muzzle Angle and Hip Weapon FOV Change, alongside Iron Sights Distance and Sights FOV Change (renamed from Weapon FOV Change). Melee weapons push the arms out in hip fire by default.
- **New: Hip Raise On Fire** (per weapon type). Lifts a low-held weapon while you shoot, then lets it settle back. **Level Muzzle When Firing** takes the hip muzzle dip out at the same time, so the barrel points at the crosshair.
- **Realistic Weapon Hold is back, and its aim is correct.** Each gun is held out in front of you and made to look further away with its own weapon FOV, and the iron sights now mark where the shot actually lands. Fallout 4 draws the gun with the weapon FOV but the world with the world FOV, so a gun held away from the centre of the screen is drawn nearer the centre than its bullet goes - dead centre they agree, which is why only Free Aim ever showed it. Bodycam now corrects for that automatically; there is no setting to get wrong. Measured after the fix: the sights sit within about two pixels of the aim point at 1440p, the rest being the gun's sights sitting above its barrel, as on a real gun.
- **Fixed: your shots were being pushed off by the gun's own pose.** Holding the gun further forward moves the muzzle, and the game's spread is worked out relative to the muzzle, so the movement was being mistaken for spread and added to every shot. Measured before the fix, the error grew the further you aimed from the centre of the screen; after it, shot scatter is even in both directions again. This affected the weapon FOV path only.
- **New: Shots Go Where The Sights Point** (Free Aim page). Vanilla spread on a 10mm is about a degree and a half - roughly 45 pixels at 1440p on a target four metres away - and it is applied after your aim, so no aiming fix can remove it. This drops it so the bullet lands exactly on the sights. Set to iron sights only by default, so hip fire still scatters like vanilla. Shotguns and other multi-projectile weapons always keep their spread, because that spread is their pattern.
- **New: Hit Marker** (Free Aim page). The red marker that flashes when you damage something sits outside the crosshair, so with Free Aim it stayed at the centre of the screen while your shot landed elsewhere. It can now follow the gun, or be hidden.
- Fixed: the crosshair is now placed using the world camera rather than the weapon FOV, so it marks where the bullet lands rather than where the gun is drawn. This only changes anything when a Weapon FOV Change is raised.
- Bodycam now checks its own work each frame and drops back to the game's own animation if it would ever produce a nonsensical pose, rather than letting a bad frame reach the screen.
- **Fixed: grenades threw low.** Fallout 4 builds a throw from your hand, and Bodycam moves your whole first-person body, so free aim, cant, bob and the weapon hold were all quietly steering your grenades - they followed the barrel instead of the crosshair. Throws now have Bodycam's own movement taken back out of them, so they go exactly where the game would have sent them, arc and all. Guns are unaffected.
- **Corner peek is now automatic.** Stand at cover and turn into the wall: looking along it there is no lean, and the further you turn into it the further you lean out past the edge (Peek Start Angle and Full Lean Angle on the Corner Peek page). The edge is found at any angle of approach, including a wall right beside you that ends just ahead; a long wall or a doorway you walk past never counts. A tap of A or D toward the wall steps back into cover. It leans while aiming (Hip Fire Lean adds some from the hip if you want it), and the view lowers slightly as you lean out, like leaning from the hips. While peeking, the corner lean takes over the gun's tilt completely, so both sides lean the same whatever your resting tilt is. Aiming while peeking keeps the sights on the shot: the gun moves exactly with the view, Corner: Gun Turn is hip fire only, and shots converge where the leaned-out view is looking.
- Weapon inertia retuned (Inertia 8, Max 12, Recovery 5, Smoothness 0.6) and now also tilts and slides the gun with the swing (new Inertia Tilt and Inertia Slide sliders, 1.25 each). With Free Aim on it only tilts and slides, since swinging the gun back cancelled Free Aim's own lead. Strafing gun drag is stronger (0.075, speed 15). Both now scale with the Intensity preset. Gun Handling has its own MCM page, and Lean Response moved to Look & Lean.
- **New: resting gun pose.** Resting Gun Tilt is now hip fire only, with its own Resting Gun Tilt (Aiming), plus a hip-fire Resting Muzzle Angle, so the gun can rest on a diagonal. Every lean and tilt slider can now go negative to flip its direction, and Inertia Tilt leans with Gun Lean Into Turns instead of cancelling it.
- **Low Ready is back** and on by default, for every weapon, with or without Weapon Hold. In hip fire the gun rests down and to the left and snaps up when you shoot or aim. The pose is set per weapon type: Muzzle Angle (which can now tip the muzzle up as well as down), Swing Left, Height and Center, plus Arm Swing, which lets a pistol turn in the hand while the arms stay put. Two-handed weapons keep Arm Swing at 1 so both hands stay on the gun.
- **Fixed: the view could jump to somewhere else entirely** with a large Low Ready swing. The point the gun turns about was read back from the previous frame's pose, so at big angles the error built up every frame until the view was hundreds of units away. It is now worked out fresh every frame.
- Corner peek only leans while aiming by default; Hip Fire Lean brings some back from the hip.
- **Fixed: the corner peek could see through walls.** The view was checked against walls with one ray from the eye, but it is drawn from a little in front of the eye, so a door frame just ahead slipped past the check. It now checks from both points and keeps further back from the wall.
- Camera lean into turns is calmer (Lean Into Turns -2, Max Camera Lean 4), and the gun's own turn lean is scaled down with it (Gun Lean Into Turns 3.1, Max Extra Gun Lean 7) so the two still balance. Turning around a room full of doorways was rolling the view hard enough to feel like being pulled sideways.
- **New: Scoped Weapons** (Weapon Hold page). A gun with a scope fitted now gets 0.65 of the sights distance and weapon FOV change, so the picture through the scope is no longer pushed out to a dot while you raise it, or at all with scopes that keep the gun on screen. Iron sights are unaffected, including the assault rifle's, which the base game marks as a scope.
- **New: on/off switches for each feature section** - View Lag, Camera Lean, Gun Lean, Corner Peek, Weapon Inertia, Gun Drag, Head Bob, Jumping & Landing, Footstep Impact and Gun Retract - so any one of them can be handed to another mod without zeroing its sliders. Switching one off keeps your slider values for when you switch it back on.
- **New: Jumping & Landing** (Movement page). The view dips and rebounds as you leave the ground, and lands with a kick and a short camera shake sized by how fast you were actually falling - a hop off a kerb is nothing, a drop off a roof is not. The shake is camera-only and never moves your aim. Every part has its own slider and 0 turns it off.

## 1.0.5 - 2026-09-20 (hotfix)
- **Fixed: Free Aim now shoots where you are pointing.** Shots, the crosshair and the iron sights all land on the same spot, at any position in the free-aim box and at any range. Two faults were behind it: the gun was being turned about your feet instead of your eye, which lifted the sight line off your eye so the sight picture never matched the shot, and the crosshair was placed using the wrong HUD coordinate space, which pushed it further off the more the gun moved from the centre. Measured after the fix: the aim point sits within half a pixel of the sight at 1440p with the gun at the edge of the box.
- Changed: Lean Into Turns and Camera Lean From Strafing can now be negative. Negative leans out of the turn, which is what real bodycams do, and is the new default (-2). Positive leans into it, 0 is no lean. The old Camera Lean Direction setting is gone.
- New: Footstep Impact (Movement page) - a short sharp kick each time a foot lands, with Sharpness and Fade sliders. 0 turns it off.
- New: Bodycam now pauses in settlement build mode, as it already did for the Pip-Boy and pause menu.
- Fixed: a changed weapon FOV can no longer be left behind in your save. Your normal FOV is read from your game INI and restored before every save.
- Realistic Weapon Hold is not in this release. It is still being worked on.

## 1.0.4 - 2026-09-19
- New: Old-Gen support - Fallout 4 1.10.163 with F4SE 0.6.23. Pick your game version in the installer.
- New: the installer asks whether Free Aim starts On or Off (On by default) and which Intensity Preset to start with (Subtle / Default / Intense, with a note on what each feels like). You can still change both any time in the MCM.
- Fixed: turning off Resting Camera Tilt also stopped strafe tilt. The two switches are now independent - Resting Camera Tilt only controls the constant tilt, Camera Tilt When Strafing only controls strafe lean, and neither changes the gun's lean. Both now ease in and out instead of snapping.
- New: the MCM title shows the version number.
- New: Free Aim MCM page with all its sliders - Box Width/Height, Recenter Speed, Crosshair Offset Scale and a new Gun Speed slider (how much of your mouse movement moves the gun inside the box; the rest turns the view). Gun Speed defaults to 0.5 and follows the Intensity Preset: 0.25 Subtle, 0.5 Default, 0.75 Intense. The Free Aim switch stays on the General page.
- Free Aim now notes that it does not affect scopes.
- All 1.0.3 features are included in the Old-Gen build. Old-Gen is still being tested - please report any issues!

## 1.0.3 - 2026-09-18 (experimental)
- New: Free Aim (General page) - your gun moves freely in a box on screen, like Bodycam.
- New: with Free Aim on, bullets fire along the barrel instead of to the screen centre.
- New: with Free Aim on, the crosshair follows the gun (frame gen can add artifacting - can be switched off).
- Scopes, VATS and thrown grenades stay vanilla. Box size is on the Look & Lean page.
- Not yet tested with every weapon - please report any issues!

## 1.0.2 - 2026-09-17
- Fixed: Pip-Boy clicks picking the wrong item (Bodycam now pauses in the Pip-Boy and pause menu).
- Fixed: gun's resting tilt tilting the whole view in hip fire.
- New: Camera Tilt slider (Look & Lean) - + right, - left, 0 level. Default 3.
- New: Resting Camera Tilt, Camera Tilt When Strafing and Pause In Menus switches.
- Changed: camera and gun lean when strafing by default (3, scales with preset).
- Thanks Fabishere18, AlinaFaas and GamerJanos76 for the reports!

## 1.0.1 - 2026-09-16

- Fixes from two tester reports and a session of logged testing. The walk/jog/sprint bob,
  stride sway and turn lean feel the same as 1.0.0.
