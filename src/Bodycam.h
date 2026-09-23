#pragma once
#include <cstdint>

namespace Bodycam
{
	// Call once from F4SEPlugin_Load with the game's module base address.
	void Init(uintptr_t moduleBase);

	// Call periodically (e.g. every 200-500ms) from a background thread. Cheap:
	// does nothing once the hook is installed and stays installed.
	void MaintainHook();

	// F4SE kMessage_PreSaveGame: puts the player's normal weapon FOV back so saves stay clean.
	void OnPreSave();
}
