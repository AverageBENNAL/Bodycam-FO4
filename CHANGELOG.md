# Changelog

## 1.0.9 - 2026-10-03
Hip fire is rebuilt around how Bodycam holds a gun, and settings can now be saved and shared as presets. A settings guide (PDF) is in the download.

Tested on Anniversary Edition. The Next-Gen and Old-Gen builds have the same changes but I have not been able to run them; if presets or the new hip fire misbehave there, tell me which version you are on.

**Hip fire**
- **New: Barrel Points At Crosshair** (Gun Handling > Turning, default on). In hip fire the gun comes up from the bottom centre of the screen with the tip of the barrel just under the crosshair, and follows the crosshair wherever Free Aim takes it. Before, the gun sat low and to the right and ran parallel to your aim, so the barrel visibly pointed past whatever the crosshair was on. Works with Free Aim on or off. Where shots land does not change. Off in iron sights and the low ready, which place the gun themselves.
- **New: Tip Below Crosshair** and **Pistol Tip Below Crosshair** (default 2 and 10). How far under the crosshair the barrel tip sits. Pistols have their own because they are short, and at the long-gun setting the whole pistol sat high on screen. 0 puts the tip on the crosshair.
- **New: Barrel Tilt** (default 18). Tips the barrel up so the back of the gun hangs low, the way Bodycam holds it. 0 looks straight down the top of the gun.
- **The hold lets go while your off hand is away from the gun** - a reload, a bash, a grenade - and comes back when the hand does. Held in that pose, an arm that left the gun showed its cut end on screen.
- **The cut ends of the arms stay off screen.** The first-person arms stop at the upper arm, and the game keeps that cut below and behind the camera. Tilted up for the new hold and then rolled and swung on a hard turn, it could come round into the bottom corner. Bodycam now watches where the top of each arm would be drawn and pulls the gun in toward you for a moment when it gets close, so on the hardest turns the gun tucks in instead.
- **New: Max Turn In** (default 40). A limit on how far the gun is turned to reach that pose.
- **Changed: with Barrel Points At Crosshair on, Hip Height and Hip Center on the Weapon Hold page no longer move the gun in hip fire.** The new hold decides where the gun sits across and up the screen. Hip Push, Hip Weapon FOV Change and Hip Raise On Fire still work. Turn Barrel Points At Crosshair off to get the old hold back, with those two sliders.
- **Changed: Low Ready is off by default.** It hid the new hip hold most of the time. Turn it back on on the Weapon Hold page; nothing else about it changed.

**Weapon inertia**
- **Reworked: the gun swings about the tip of its barrel.** When you turn, the barrel points ahead of the turn and the butt and hands trail behind it, while the tip stays on the crosshair. It used to swing the whole gun against the turn, which fought Free Aim and was switched off whenever Free Aim was on. It now runs with Free Aim on or off, and never moves the crosshair or where shots land. Fades out in iron sights as before.
- **New: Barrel Lead** (default 1). How far the barrel points ahead of the turn. Negative makes it trail instead; 0 turns it off.
- **New: Inertia Push** (default 0.6). The gun draws in toward you while it swings and eases back out. It never goes further out than its resting place, so it cannot show the cut end of an arm.
- **New: Inertia Sway** (default 0). Slides the hands against the turn. Off by default because it works against Barrel Lead.
- **Removed: Inertia Tilt and Inertia Slide.** Gun Lean does the tilting now.
- **New defaults:** Inertia 10, Max Inertia 18, Inertia Recovery Speed 10, Inertia Smoothness 0.6.
- **Fixed: the gun, crosshair and shots could jump for a few frames on a hard turn.** Bodycam stops trusting the barrel when it is more than about 25° off your aim. A big swing could reach that, and for those frames everything snapped to the aim and back.
- **Fixed: the gun shook on fast sideways turns**, worst at low frame rates. The swing followed the raw mouse speed, which jumps from frame to frame. It is smoothed first now.

**Gun lean**
- **Changed: the gun leans much further into turns and strafes.** Gun Lean Into Turns 3.1 to 14, Gun Lean From Strafing 4.5 to 12, Max Extra Gun Lean 7 to 30, Max Total Gun Lean 16 to 40, Gun Lean Speed 7 to 10, Gun Lean Smoothness 1 to 0.7. In iron sights the lean into turns is scaled back to about what it was.
- **Fixed: leaning the gun moved the crosshair and the shots.** The gun leaned about your line of sight, and the hip pose points the barrel some 10° below that, so a lean swung the barrel sideways. It leans about its own barrel now.

**Presets**
- **New: My Presets** (top of the General page). Every setting can be saved under a name and loaded again. Type a name and press Save Preset; step through what you have with Previous Preset and Next Preset; press Apply Preset to load the one in the box. Sliders update on screen as soon as you apply.
- **Presets are files you can share.** They are saved as `.ini` files in `Data\F4SE\Bodycam Presets` (your overwrite folder in MO2). Drop someone else's `.ini` in that folder and it shows up in the list. A preset carries every setting except the Enable Bodycam switch, Controller Mode, key bindings and the troubleshooting switches, so it plays the same for whoever loads it.
- **Changed: Subtle, Default and Intense are presets now.** The Intensity Preset row is gone; pick them with Previous / Next and press Apply. Their values are unchanged apart from the new defaults listed here.

**Recoil**
- **New: Aim Recovery** (Recoil > Recovery, default on). On, your aim drifts back down after the climb, as before. Off, the climb stays where it put you and you pull it down yourself.
- **Changed: breathing no longer moves the gun.** Standing still it rocked the sights enough to miss with a pistol. The view still breathes.

**Fixes**
- **Fixed: the Pip-Boy light pointed off to the side in the low ready**, with the vanilla light and with Pip-Boy Flashlight. The light hangs from the first-person arms, so it swung away with the gun. It stays on your view now.
- **Fixed: grenades and molotovs thrown with a gun drawn went where the barrel pointed** instead of where the crosshair was.

**MCM**
- **The General page is now Enable Bodycam, My Presets and Comfort only.** The Free Aim switch, Crosshair Follows Gun, Shots Follow, Hit Marker and Crosshair Offset Scale moved to the Free Aim page. The Weapon Hold switch moved to the Weapon Hold page.

## 1.0.8 - 2026-09-27
Thanks to Cyzarl, droname, TommyCreo, Skylarsis, GoldGary, FrostyMosty and Me1Nagano for the reports behind this one.

- **Fixed: VATS, hacking, third person and other menus drew the game zoomed out or off screen** (Cyzarl). When Bodycam handed the camera back to the game it stopped updating but left its raised weapon FOV behind, so everything drawn afterwards used it. The normal FOV is now handed back the moment Bodycam steps aside, and on every camera the game drives itself.
- **Fixed: only the Pip-Boy, pause and workshop menus paused Bodycam** (FrostyMosty). Using a terminal, worst while sneaking, left the view pitched above or below the screen. VATS, terminals (hacking), holotapes, lockpicking, containers, trading, workbenches, cooking, LooksMenu and books now pause it too, so the Pip-Boy opened over a terminal comes up where it should. Walking up to a terminal the game swings the camera onto the screen before the menu opens; Bodycam now notices the game steering the camera and steps aside at once, instead of leaving the view off the screen.
- **Fixed: squeezing through a narrow gap swung the view and could feel like being dragged while aiming** (Cyzarl). With walls close on both sides the corner peek found an edge on one side or the other every frame. A narrow passage no longer counts as cover.
- **Fixed: aimed corner peek swayed slowly left and right.** Aimed, the peek moves the gun sideways with the view, and that shift fed back into the eye the corner probes started from, so the lean kept changing what it found. The probes now start from the body.
- **Fixed: rifles and shotguns swung into the camera while sprinting.** The sprint animation already turns the gun across the body and the low ready turn was added on top. Low ready now eases out as you go from a jog to a sprint and comes back when you slow down.
- **Fixed: Iron Sights Distance did nothing.** It moved the eye along with the gun, so the sight picture never changed and only Sights FOV Change had any effect. It now moves the gun alone. The defaults are 0, which is exactly how aiming has looked until now; negative brings the gun closer, positive pushes it out. Useful for weapon packs whose aiming animations hold the gun further out, such as Combined Arms pistols.
- **New: Auto Sights FOV Distance** (Weapon Hold, per weapon type). Combined Arms pistols looked far away when aiming: their animation already holds the sights about 24 units out, and the pistol Sights FOV Change of +50 was tuned for guns held closer. Bodycam now measures where each gun's sights sit and, past this distance, cuts the FOV boost back so the gun draws the same size. On for pistols (15), off for the other types.
- **New: Automatic Rifle Climb** (Recoil) (Me1Nagano). Rifles and SMGs with an automatic receiver get their own recoil, 2.5° per shot by default, since each round stacks on the last. Rifle / SMG Climb now covers semi-auto and bolt-action rifles. Detected from the game's own automatic keyword, so modded receivers that use it are covered too. Spamming a semi-auto on the sights no longer puts every round through one hole: the first shot is still exact, and quick follow-ups scatter, building over three shots up to the new Rapid Fire Scatter setting (1.5° by default).
- **Fixed: quick follow-up shots out of the low ready could go well left of the crosshair.** Shots fired while the gun is still coming up are sent to the aim point, but that switched off at 20% of the swing - still 14° on a shotgun. It now goes by the degrees of swing left, so it holds until the gun is within a degree or two.
- **Fixed: arms came away from the body while putting a fusion core into power armor.** Low ready and the Weapon Hold pose kept shifting the arms with the weapon holstered, fighting the game's own hand animations. Both now only apply with a weapon drawn.
- **Changed: Resting Camera Tilt is now off by default.** The horizon rests level. Turn it back on on the General page; the tilt amount is unchanged.
- **New: Power Armor Recoil** (Recoil). Recoil is cut by 70% in power armor by default: climb, kick, rattle and all. Set to 1 for the same recoil as out of it.
- **New: Toggle Aim** (General page, default off) (GoldGary). For toggle aim mods such as Toggle Aim F4SE. Bodycam read aiming from the held aim button, so with a tap-to-aim mod the sights pose dropped the moment you let go. With this on it also follows the game's own aim zoom.
- **New: Controller Mode** (General page, default off) (Skylarsis). On a controller you stand and walk more than you shoot, so the gun spends most of its time in the low ready, and at the full swing - 45° for rifles, 70° for shotguns - that looked like the aim drifting left. Controller Mode turns the swing down to under half for every weapon type. The low ready itself can still be turned off, or its angle set per weapon type, under Weapon Hold.

## 1.0.7 - 2026-09-24
Thanks to TommyCreo for the detailed report behind most of this one.

- **Fixed: crash when getting out of power armor.** Power armor swaps your first-person skeleton out and back, and on the way back Bodycam hooked the returning skeleton a second time, so the hook ended up calling itself until the game crashed. It now recognises a skeleton it has already hooked.
- **New: Next-Gen support.** Bodycam now has a build for Fallout 4 1.10.984 (Next-Gen) with F4SE 0.7.2, alongside Anniversary Edition and Old-Gen. Pick your game version in the installer.
- **Fixed: Free Aim got much stronger on weapons that zoom when you aim** (Combined Arms AUG and similar). The free-aim box and the view lag were fixed angles, so a zoomed view made them look far bigger on screen. They now shrink with the zoom and stay the same size. Vanilla's own small aim zoom is left alone, so standard weapons behave exactly as in 1.0.6.
- **Fixed: with those weapons the crosshair drifted away from where rounds landed.** The crosshair now uses the zoomed view whenever a weapon zooms harder than vanilla.
- **Fixed: with Free Aim off, the gun overshot and took a long time to settle after turning**, and it tilted with the turn even with every lean switch off. This was Weapon Inertia, not View Lag. It now fades out while aiming down sights, and the defaults are calmer: Max Inertia 12 -> 6, Inertia Smoothness 0.6 -> 0.85. Enable Weapon Inertia (Gun Handling page) switches it off completely.
- **Fixed: the gun still leaned when turning with Enable Gun Lean off**, with or without Free Aim. Weapon Inertia's tilt was not covered by that switch; it is now.
- **Fixed: gamepad play.** Aiming and firing were only read from the mouse, so on a controller the gun stayed in the low ready while aiming, never took its sights pose, and recoil and Shots Go Where The Sights Point treated every shot as hip fire. LT and RT now count the same as the mouse buttons, and the left stick counts as A/D for stepping back out of a corner peek.
- **New: Vanilla Recoil** (Recoil page). Bodycam's recoil was stacking on top of the weapon's own, so every shot kicked twice, and the vanilla kick also nudged the free-aim box, weapon inertia and view lag as if you had moved the mouse. This slider scales the weapon's own recoil: 1 = vanilla, 0 = Bodycam's recoil only. Spread is untouched. Default 0. The firing animation's kick is part of each weapon and is left alone. Turning Bodycam's recoil off puts the vanilla recoil back to full, and your saves always keep the game's original values.
- **New: Camera Lean Return Speed** (Look & Lean page). The lean into a turn follows how fast you are turning, so it snapped upright about a fifth of a second after you stopped, and Catch-up Speed never touched it (that one only drives the view lag). It now leans in at Camera Lean Speed and settles back at this slower rate, 1.5 by default, so the tilt holds through the end of the turn.
- **New: Free Aim Only With Weapon Drawn** (Free Aim page, on by default). With your weapon holstered there is nothing to aim, so Free Aim switches off and the view stops drifting and recentering while you explore. It comes back as you draw, and the view eases back to centre when you holster rather than snapping, even with View Lag off.
- **New: Corner: Return Speed** (Corner Peek page). The peek came back into cover at the same speed it leaned out, which took one and a half to two seconds. It now returns at its own speed, 6 by default, faster than the lean-out.
- Gun Follows Camera Lean default 1.5 -> 0.5. With the gun tilting well past the camera, the camera lean hardly showed with a weapon drawn.
- Calmer defaults for long sessions: Stride Swing and Stride Tilt are lower at every pace (walk 0.3 / 0.3, jog 0.45 / 0.45, sprint 0.85 / 0.85), and Corner: Camera Drop is now 0, so the view leans straight out instead of dipping.
- **Fixed: the corner peek flickered on and off while aiming along a wall.** Aiming nearly parallel to the wall sits right on Peek Start Angle, so a degree of aim drift found and lost the corner every second. The corner you are already peeking now keeps going until you turn a few degrees further out, and a lost corner is held a little longer.
- **Fixed: at the end of a wall the peek could pick the wrong side and lean into the wall.** Both faces of a thin wall read as corners, and the peek swapped between them, leaning the view into the wall beside you. When only one side is blocked, that side can no longer be chosen.
- Corner peek leans out faster and settles instead of creeping the last part for a second: Corner: Gun Lean Speed 3 -> 4.5, Corner: Camera Lean Speed 3 -> 4, and the camera spring is slightly less damped.
- New defaults for view lag and lean: Max View Lag 10, Catch-up Speed 7.5, Catch-up Smoothness 0.65, Lean Deadzone 0.1, Lean Into Turns -3, Max Camera Lean 5, Movement Motion 0.65.
- **Motion Sickness Mode moved to the General page and now covers more.** Camera lean, corner peek tilt and the resting tilt drop to a quarter (and the tilt no longer drifts side to side), footstep roll and all side-to-side walking sway are removed, and landing shake, recoil camera kick and the Free Aim view overshoot are halved. Bounce, footstep impact and breathing stay. Off by default.
- **Fixed: with Full Body First Person (and other mods that hide the first-person arms), Bodycam switched itself off whenever you holstered**, so the lean and the rest of the camera motion stopped and flickered as the arms were hidden and shown. Bodycam now goes by the game's camera, so it stays on in first person whether the arms are drawn or not.
- **New: Low Ready Toggle Key** (Keys section). Tap it to keep the gun raised, tap it again to let it drop back into the low ready. Shooting and aiming still raise it as before. Off by default (0).
- **New: First Shot Hits From Low** (Weapon Hold page, Low Ready section). Out of the low ready the first shot or two leave while the barrel is still pointing down, and miss, even at max Raise Speed. Those shots now go where the raised gun will point, handing back to the barrel as it comes up. On by default; turn it off for the realistic miss.

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
