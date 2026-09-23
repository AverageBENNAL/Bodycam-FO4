#pragma once
#include <windows.h>
#include "Offsets.h"
#include "GameTypes.h"

// Thin wrapper over the game's own camera-collision ray helper (see Offsets.h). Casts
// from `from` to `to` in game units and reports the distance to the first hit.
namespace RayCast
{
	using GetbhkWorldFn  = void* (*)(void* cell);
	using RayCastWorldFn = bool (*)(void* bhkWorld, const NiPoint3* from, NiPoint3* inOutTo);

	// Resolves the physics world the player is currently in. nullptr if not in a cell.
	inline void* GetPlayerWorld(uintptr_t base)
	{
		__try
		{
			void* player = *reinterpret_cast<void**>(base + Offsets::kRVA_g_player);
			if (!player)
				return nullptr;
			void* cell = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(player) + Offsets::kOff_REFR_parentCell);
			if (!cell)
				return nullptr;
			return reinterpret_cast<GetbhkWorldFn>(base + Offsets::kRVA_Cell_GetbhkWorld)(cell);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return nullptr;
		}
	}

	// Returns true on hit and sets outDist to the distance from `from` to the hit point.
	inline bool Cast(uintptr_t base, void* world, const NiPoint3& from, const NiPoint3& to, float& outDist)
	{
		outDist = Length(to - from);
		if (!world)
			return false;

		NiPoint3 hit = to;
		bool didHit = false;
		__try
		{
			didHit = reinterpret_cast<RayCastWorldFn>(base + Offsets::kRVA_RayCastWorld)(world, &from, &hit);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}

		if (didHit)
			outDist = Length(hit - from);
		return didHit;
	}
}
