#pragma once
#include <cstdint>

using UInt32 = uint32_t;
using PluginHandle = UInt32;

struct PluginInfo
{
	enum { kInfoVersion = 1 };
	UInt32      infoVersion;
	const char* name;
	UInt32      version;
};

struct F4SEInterface
{
	UInt32 f4seVersion;
	UInt32 runtimeVersion;
	UInt32 editorVersion;
	UInt32 isEditor;
	void* (*QueryInterface)(UInt32 id);
	PluginHandle (*GetPluginHandle)(void);
	UInt32 (*GetReleaseIndex)(void);
	const PluginInfo* (*GetPluginInfo)(const char* name);
	const char* (*GetSaveFolderName)(void);
};

struct F4SEPluginVersionData
{
	enum { kVersion = 1 };

	enum
	{
		kAddressIndependence_Signatures               = 1 << 0,
		kAddressIndependence_AddressLibrary_1_10_980  = 1 << 1,
		kAddressIndependence_AddressLibrary_1_11_137  = 1 << 2,
	};

	enum
	{
		kStructureIndependence_NoStructs         = 1 << 0,
		kStructureIndependence_1_10_980Layout    = 1 << 1,
		kStructureIndependence_1_11_137Layout    = 1 << 2,
	};

	UInt32 dataVersion;
	UInt32 pluginVersion;
	char   name[256];
	char   author[256];
	UInt32 addressIndependence;
	UInt32 structureIndependence;
	UInt32 compatibleVersions[16];
	UInt32 seVersionRequired;
	UInt32 reservedNonBreaking;
	UInt32 reservedBreaking;
	uint8_t reserved[512];
};

// Fallout4.exe v1.11.240, in F4SE's packed-version encoding.
constexpr UInt32 kRuntimeVersion_1_11_240 = 0x010B0F00;

// Fallout4.exe v1.10.163 (old-gen). F4SE 0.6.x
// predates F4SEPlugin_Version and loads plugins through F4SEPlugin_Query instead.
constexpr UInt32 kRuntimeVersion_1_10_163 = 0x010A0A30;

// F4SEMessagingInterface - identical in F4SE 0.6.23 and 0.7.9. kInterface_Messaging = 1.
struct F4SEMessagingInterface
{
	struct Message
	{
		const char* sender;
		UInt32      type;
		UInt32      dataLen;
		void*       data;
	};
	typedef void (*EventCallback)(Message* msg);
	enum { kInterface_Messaging = 1 };
	enum { kMessage_PostLoadGame = 3, kMessage_PreSaveGame = 4 };

	UInt32 interfaceVersion;
	bool (*RegisterListener)(PluginHandle listener, const char* sender, EventCallback handler);
	bool (*Dispatch)(PluginHandle sender, UInt32 messageType, void* data, UInt32 dataLen, const char* receiver);
};
