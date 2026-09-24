#pragma once
#include <cstdint>

// Address map. One source, three builds (see CMakeLists.txt):
//   Bodycam     -> Fallout4.exe 1.11.240 (AE)       / F4SE 0.7.9
//   Bodycam_NG  -> Fallout4.exe 1.10.984 (next-gen) / F4SE 0.7.2    (BODYCAM_NG defined)
//   Bodycam_OG  -> Fallout4.exe 1.10.163 (old-gen)  / F4SE 0.6.23   (BODYCAM_OG defined)
//
// Offsets are RVAs off the exe's load base. How each was found:
//   [F4SE]   cross-checked against the addresses F4SE publishes for this runtime.
//   [RTTI]   MSVC RTTI walk: ".?AV<Class>@@" -> CompleteObjectLocator -> vtable.
//   [DISASM] read out of the game's own call sites with capstone.
//
// Struct layouts are identical across all three runtimes; only RVAs differ. The 1.10.163 exe is
// Steam-DRM encrypted on disk, so its [DISASM] addresses came from a decrypted dump, matched
// to 1.11.240 by callers / constants / control flow - see the per-address notes.
//
// 1.10.984 is encrypted the same way (Steamless strips it). It is close enough to 1.11.240 that
// every function below matched as a single masked-byte hit (rel32 / RIP displacements wildcarded),
// with the same length in .pdata. F4SE 0.7.2 headers diffed against 0.7.9: renames only, no layout
// change in anything read here.
// =====================================================================================

namespace Offsets
{
#if defined(BODYCAM_OG)
	// ---- [F4SE 0.6.23] globals / functions (1.10.163) -------------------------------------
	constexpr uintptr_t kRVA_g_playerCamera = 0x058CEB28; // GameCamera.cpp
	constexpr uintptr_t kRVA_g_player       = 0x05AA4388; // GameReferences.cpp
	constexpr uintptr_t kRVA_g_ui           = 0x058D0898; // GameMenus.cpp (UI*)
	constexpr uintptr_t kRVA_UI_IsMenuOpen       = 0x02042160; // GameMenus.h
	constexpr uintptr_t kRVA_BSFixedString_ctor  = 0x01B41D40; // GameTypes.h StringCache::Ref::ctor
#elif defined(BODYCAM_NG)
	// ---- [F4SE 0.7.2] globals / functions (1.10.984) --------------------------------------
	constexpr uintptr_t kRVA_g_playerCamera = 0x02E649D8; // GameCamera.cpp
	constexpr uintptr_t kRVA_g_player       = 0x0303ACA0; // GameReferences.cpp
	constexpr uintptr_t kRVA_g_ui           = 0x02E66400; // GameMenus.cpp (UI*)
	constexpr uintptr_t kRVA_UI_IsMenuOpen       = 0x01965050; // GameMenus.h
	// Two byte-identical copies 0x540 apart in both exes; the first is the char* one (F4SE agrees).
	constexpr uintptr_t kRVA_BSFixedString_ctor  = 0x01561AD0; // GameTypes.h StringCache::Ref::ctor
#else
	// ---- [F4SE] globals ----------------------------------------------------------------
	constexpr uintptr_t kRVA_g_playerCamera = 0x030E6E58; // GameCamera.cpp
	constexpr uintptr_t kRVA_g_player       = 0x032DD370; // GameReferences.cpp
	constexpr uintptr_t kRVA_g_ui           = 0x030E8930; // GameMenus.cpp (UI*)

	// ---- [F4SE] functions ------------------------------------------------------------------
	// bool UI::IsMenuOpen(const BSFixedString& name)             - GameMenus.h
	// BSFixedString* BSFixedString::ctor(this, const char* name) - GameTypes.h (StringCache::Ref)
	constexpr uintptr_t kRVA_UI_IsMenuOpen       = 0x01A80320;
	constexpr uintptr_t kRVA_BSFixedString_ctor  = 0x0167C1E0;
#endif

	// ---- [F4SE] struct offsets -----------------------------------------------------------
	constexpr uintptr_t kOff_REFR_parentCell              = 0xB8;  // STATIC_ASSERT in GameReferences.h
	constexpr uintptr_t kOff_REFR_rot                     = 0xC0;  // NiPoint3 radians, z = heading (before asserted baseForm @ 0xE0)
	constexpr uintptr_t kOff_REFR_pos                     = 0xD0;  // NiPoint3 world position (feet), no head bob
	constexpr uintptr_t kOff_Player_firstPersonSkeleton   = 0xB78; // follows playerEquipData (asserted @ 0xB70)
	// PlayerCamera::cameraStates[] order (GameCamera.h). Only kFirstPerson is ours: every other
	// state is an engine-animated camera (dialogue pans, VATS, furniture, bleedout, transitions).
	constexpr int kCameraState_FirstPerson = 0;

	// ---- Weapon Hold (1.0.4): equipped weapon + its type keywords, viewmodel FOV ---------------
	// All identical in F4SE 0.6.23 and 0.7.9 (GameReferences.h / GameObjects.h / GameFormComponents.h
	// / GameCamera.h / GameForms.h diffed). Keyword form IDs are Fallout4.esm's own (read from the esm),
	// so they are the same in every game version.
	constexpr uintptr_t kOff_Actor_middleProcess   = 0x300; // AIProcess* (asserted layout around it)
	// ActorState at 0x128, second dword: bits 1-3 = weapon state (0 sheathed, 1 want to draw, 2 drawing,
	// 3 drawn, 4 want to sheathe, 5 sheathing).
	constexpr uintptr_t kOff_Actor_weaponStateBits = 0x134;
	constexpr uintptr_t kOff_Process_data08        = 0x08;  // AIProcess::Data08*
	constexpr uintptr_t kOff_Data08_equipData      = 0x288; // tArray<EquipData> {entries@0, count@0x10}
	constexpr uintptr_t kEquipDataSize             = 0x28;  // {item@0, instanceData@8, slot@10, unk18, equippedData@20}
	constexpr uintptr_t kOff_Form_formID           = 0x14;
	constexpr uintptr_t kOff_Form_formType         = 0x1A;
	constexpr uint8_t   kFormType_WEAP             = 43;    // FormType enum index (both versions)
	constexpr uintptr_t kOff_WEAP_keywordForm      = 0x150; // BGSKeywordForm (embedded) on TESObjectWEAP
	constexpr uintptr_t kOff_WeapInstance_keywords = 0x80;  // BGSKeywordForm* on TESObjectWEAP::InstanceData (mods change it)
	constexpr uintptr_t kOff_WeapInstance_aimModel = 0x88;  // BGSAimModel* (F4SE 0.6.23 and 0.7.9 agree)
	// BGSAimModel: TESForm (0x20) then the DNAM block, in the order FO4Edit lists it.
	constexpr uintptr_t kOff_AimModel_recoilMax    = 0x40;  // float, degrees per shot
	constexpr uintptr_t kOff_AimModel_recoilMin    = 0x44;  // float, degrees per shot
	constexpr uintptr_t kOff_AimModel_recoilHip    = 0x48;  // float multiplier, read for the sanity check only
	// The live copy the game fires from: EquippedItem +0x20 -> EquippedWeaponData +0x20 -> AimModel,
	// which starts with its own copy of the form's data block (so recoil max/min at +0x20/+0x24).
	// It is copied on equip - writing only the form did nothing until a re-equip (tested 2026-09-24).
	constexpr uintptr_t kOff_EquipData_data        = 0x20;  // NiPointer<EquippedItemData>
	constexpr uintptr_t kOff_EquipWeapData_aimModel = 0x20; // AimModel*
	constexpr uintptr_t kOff_LiveAim_recoilMax     = 0x20;
	constexpr uintptr_t kOff_LiveAim_recoilMin     = 0x24;
	constexpr uintptr_t kOff_LiveAim_actor         = 0x88;  // Actor*, checked against the player before writing
	constexpr uintptr_t kOff_KeywordForm_keywords  = 0x10;  // BGSKeyword**
	constexpr uintptr_t kOff_KeywordForm_count     = 0x18;  // UInt32
	constexpr uintptr_t kOff_PlayerCamera_fovWorld = 0x168; // float fDefaultWorldFOV (live)
	constexpr uintptr_t kOff_PlayerCamera_fov1st   = 0x16C; // float fDefault1stPersonFOV (live; what the 'fov' console command sets)

	constexpr uint32_t kKW_WeaponTypePistol     = 0x0004A0A0;
	constexpr uint32_t kKW_WeaponTypeRifle      = 0x0004A0A1;
	constexpr uint32_t kKW_WeaponTypeHeavyGun   = 0x0004A0A3;
	constexpr uint32_t kKW_WeaponTypeMelee1H    = 0x0004A0A4;
	constexpr uint32_t kKW_WeaponTypeMelee2H    = 0x0004A0A5;
	constexpr uint32_t kKW_WeaponTypeThrown     = 0x0004A0A6;
	constexpr uint32_t kKW_WeaponTypeUnarmed    = 0x0005240E;
	constexpr uint32_t kKW_WeaponTypeMine       = 0x0010C414;
	constexpr uint32_t kKW_WeaponTypeGrenade    = 0x0010C415;
	constexpr uint32_t kKW_WeaponTypeHandToHand = 0x00226453;
	constexpr uint32_t kKW_WeaponTypeShotgun    = 0x00226454;
	constexpr uint32_t kKW_HasScope             = 0x0009F425; // on the instance once a scope mod is fitted
	constexpr uint32_t kKW_HasIronSights        = 0x0016304F; // dn_HasScope_IronSights

	constexpr uintptr_t kOff_REFR_extraDataList           = 0x100; // ExtraDataList* (GameReferences.h, sits after inventoryList @ 0xF8)
	constexpr uintptr_t kOff_ExtraDataList_presence       = 0x18;  // PresenceBitfield* (GameExtraData.h, sizeof asserted 0x28)
	constexpr uint32_t  kExtraData_PowerArmor             = 0xBB;  // kExtraData_PowerArmor, marked "Confirmed" in GameExtraData.h

	// ---- [RTTI] NiAVObject::UpdateWorldData(NiUpdateData*) -------------------------------
	// Counted from NiObjects.h's declaration order (index 52), then cross-checked in the
	// exe: NiNode's slot 52 equals NiAVObject's base implementation, NiCamera's is its own
	// override. Called for every attached node, every frame, BEFORE its children update -
	// so changing a node's world transform right after the original runs carries through
	// to everything parented under it (the camera, or the whole first-person arms + gun rig).
	constexpr int kVtblIndex_UpdateWorldData = 52;
	constexpr int kShadowVtableSize          = 72; // real NiNode-family tables run to ~58+

	// ---- [RTTI+DISASM] Projectile::Launch(ProjectileHandle* out, ProjectileLaunchData& data) --------
	// Found from this exe: the only function (besides the form factory) that constructs every
	// projectile class with launch data - MissileProjectile, Arrow, Grenade, Beam, Flame, Cone and
	// Barrier constructors are all called from it, and it switches on data+0x18 (the BGSProjectile)
	// -> +0xC0 (projectile type flags). Two arguments (rcx, rdx); r8/r9 are overwritten unread.
	// Prologue (15 bytes, no RIP-relative operands) is checked byte-for-byte before patching.
	// The player's weapon fire reaches it from +0x47A09B (in-game log).
#if defined(BODYCAM_OG)
	// 1.10.163: same function, found by its unique body (TLS slot 0x9C0 <- 0x6A, then
	// data+0x18 -> +0xC0 & 0x7F0000 type switch). Different prologue: 20 bytes, no RIP-relative.
	// Player fire caller = the one whose pellet loop stores angles from [r12+rax] / [+4] with the
	// xmm6/xmm7 fallback, exactly like 1.11.240's; LaunchData lives at rbp-0x20 and the angles
	// at rbp+0x2C/+0x30, i.e. zAngle 0x4C / xAngle 0x50 - same layout.
	constexpr uintptr_t kRVA_Projectile_Launch = 0x00FCA260;
	constexpr uintptr_t kRVA_PlayerWeaponFireReturn = 0x0034D983;
	inline constexpr uint8_t kLaunchPrologue[] = {
		0x4C, 0x8B, 0xDC,                      // mov r11, rsp
		0x49, 0x89, 0x4B, 0x08,                // mov [r11+8], rcx
		0x53, 0x57, 0x41, 0x54, 0x41, 0x56,    // push rbx / rdi / r12 / r14
		0x48, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00 }; // sub rsp, 0A8h
#elif defined(BODYCAM_NG)
	// 1.10.984: same 763-byte body and prologue as 1.11.240. Fire caller matched as a whole
	// function; the return is the same +0x42C in, right after `lea rcx,[rbp+44h] / call Launch`.
	constexpr uintptr_t kRVA_Projectile_Launch = 0x00DBC0B0;
	constexpr uintptr_t kRVA_PlayerWeaponFireReturn = 0x00425F4B;
	inline constexpr uint8_t kLaunchPrologue[] = {
		0x48, 0x89, 0x4C, 0x24, 0x08,          // mov [rsp+8], rcx
		0x55, 0x53, 0x56, 0x57,                // push rbp / rbx / rsi / rdi
		0x41, 0x54, 0x41, 0x55, 0x41, 0x57 };  // push r12 / r13 / r15
#else
	constexpr uintptr_t kRVA_Projectile_Launch = 0x00E421E0;
	constexpr uintptr_t kRVA_PlayerWeaponFireReturn = 0x0047A09B; // return address inside weapon fire (in-game log)
	inline constexpr uint8_t kLaunchPrologue[] = {
		0x48, 0x89, 0x4C, 0x24, 0x08,          // mov [rsp+8], rcx
		0x55, 0x53, 0x56, 0x57,                // push rbp / rbx / rsi / rdi
		0x41, 0x54, 0x41, 0x55, 0x41, 0x57 };  // push r12 / r13 / r15
#endif
	static_assert(sizeof(kLaunchPrologue) >= 14, "the absolute jmp patch needs 14 bytes");

	// ---- [DISASM] Engine ray cast helper ---------------------------------------------------
	// bool RayCastWorld(bhkWorld* world, const NiPoint3* from, NiPoint3* inOutTo)
	// The game's own camera-collision ray (callers 0x102fe33 / 0x102fe6b / 0x10307e8). Internally it
	// scales game->Havok units by 0.0142875 (constant @ 0x2468474), builds the hknpRayCastQuery
	// (filter@0x00, origin@0x20, direction@0x30, inverse dir + sign mask@0x40) with a closest-hit
	// collector, read-locks the hknpWorld (0x1873060 / unlock 0x1873080), and on a hit writes the
	// point back into *inOutTo scaled by 69.99125.
	// Use this rather than hand-building the query: that was tried and could never hit anything
	// (no unit scale, wrong field layout, collector best-fraction starting at 0 instead of FLT_MAX).
#if defined(BODYCAM_OG)
	// 1.10.163: 0x822D90. Same prologue shape and 3 callers; reached the same way as in
	// 1.11.240 (FurnitureCameraState vtable slot 11 -> camera collision -> the helper that also
	// calls GetbhkWorld), where it is the pair of back-to-back calls 0x38 bytes apart. The Havok
	// scale here is a runtime-initialised .data global instead of an .rdata constant.
	constexpr uintptr_t kRVA_RayCastWorld = 0x00822D90;
#elif defined(BODYCAM_NG)
	constexpr uintptr_t kRVA_RayCastWorld = 0x007C9350;
#else
	constexpr uintptr_t kRVA_RayCastWorld = 0x0081CDE0;
#endif

	// ---- [DISASM] TESObjectCELL::GetbhkWorld() --------------------------------------------
	// Interior cell -> the cell's own world; exterior -> the global exterior world. This is
	// exactly how the camera collision caller above obtains the world it passes in
	// (ref->parentCell -> this function -> RayCastWorld).
#if defined(BODYCAM_OG)
	constexpr uintptr_t kRVA_Cell_GetbhkWorld = 0x003B49A0; // byte-identical body; called by the collision helper above
#elif defined(BODYCAM_NG)
	constexpr uintptr_t kRVA_Cell_GetbhkWorld = 0x004768C0;
#else
	constexpr uintptr_t kRVA_Cell_GetbhkWorld = 0x004CAA60;
#endif
}
