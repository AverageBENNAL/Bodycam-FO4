#pragma once
// Native functions on the BodycamPresets script, so a preset can be pushed into MCM through MCM's
// own setters and its sliders and the Preset Name box show the new values without a restart.
//
//   float Begin()      reads the preset named in the box; returns how many values follow, 0 if
//                      there is no such preset, -1 while an earlier Apply is still running
//   float Value()      the next value, in the order of the shipped settings.ini
//   string NextName()  the preset after the one in the box
//   string PrevName()  the one before
//
// No arguments on purpose: reading them means walking the VM stack, and this way the only game
// code touched is the NativeFunction constructor, the four base-class helpers below and the
// string constructor.
//
// Offsets are from F4SE's PapyrusNativeFunctions.h and GameTypes.h for each runtime (0.7.9, 0.7.2,
// 0.6.23); the class layout and the virtual order are the same in all three and have to match
// exactly. Run on AE. The Next-Gen functions are byte for byte the AE ones in a decrypted 1.10.984
// image. The Old-Gen numbers are read off the source only.
#include "F4SEPluginApiMinimal.h"
#include "Logger.h"
#include "Presets.h"
#include <windows.h>
#include <cstdint>
#include <string>

namespace PapyrusBridge
{
#if defined(BODYCAM_OG)
	// 1.10.163: GetParam is still ParameterInfo's own member there, called on m_params (+0x30)
	constexpr uintptr_t kImplGetParam      = 0x0270DE00;
	constexpr uintptr_t kGetParamThis      = 0x30;
	constexpr uintptr_t kImplInvoke        = 0x0270D550;
	constexpr uintptr_t kImplGetSourceFile = 0x0270D420;
	constexpr uintptr_t kImplGetParamName  = 0x0270D440;
	constexpr uintptr_t kImplCtor          = 0x0270DA50;
	constexpr uintptr_t kImplDtor          = 0x0270DC70;
	constexpr uintptr_t kStringCtor        = 0x01B41D40; // StringCache::Ref(const char*)
#elif defined(BODYCAM_NG)
	constexpr uintptr_t kImplGetParam      = 0x0200B040;
	constexpr uintptr_t kGetParamThis      = 0;
	constexpr uintptr_t kImplInvoke        = 0x01FA40B0;
	constexpr uintptr_t kImplGetSourceFile = 0x01FA4010;
	constexpr uintptr_t kImplGetParamName  = 0x01FA4030;
	constexpr uintptr_t kImplCtor          = 0x01FA4620;
	constexpr uintptr_t kImplDtor          = 0x01FA4810;
	constexpr uintptr_t kStringCtor        = 0x01561AD0; // StringCache::Ref(const char*)
#else
	constexpr uintptr_t kImplGetParam      = 0x021681E0;
	constexpr uintptr_t kGetParamThis      = 0;
	constexpr uintptr_t kImplInvoke        = 0x021012A0;
	constexpr uintptr_t kImplGetSourceFile = 0x02101200;
	constexpr uintptr_t kImplGetParamName  = 0x02101220;
	constexpr uintptr_t kImplCtor          = 0x02101810;
	constexpr uintptr_t kImplDtor          = 0x02101A00;
	constexpr uintptr_t kStringCtor        = 0x0167C1E0; // StringCache::Ref(const char*)
#endif

	// VirtualMachine vtable slots
	constexpr size_t kVmRegisterFunction = 0x1B;
	constexpr size_t kVmSetFunctionFlags = 0x1D;
	constexpr UInt32 kFunctionFlagNoWait = 1;

	constexpr uint64_t kTypeNone = 0, kTypeString = 2, kTypeInt = 3, kTypeFloat = 4, kTypeBool = 5;

	struct VMValue
	{
		uint64_t type;
		union { float f; void* p; } data;
	};

	template <typename T>
	inline T GameFn(uintptr_t offset)
	{
		return reinterpret_cast<T>(reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)) + offset);
	}

	// IFunction + NativeFunctionBase + NativeFunction, 0x58 bytes.
	class NativeFunction
	{
	public:
		NativeFunction(const char* fnName, const char* className)
		{
			GameFn<void* (*)(NativeFunction*, const char*, const char*, UInt32, UInt32)>(kImplCtor)(this, fnName, className, 1, 0);
		}
		virtual ~NativeFunction() { GameFn<void (*)(NativeFunction*)>(kImplDtor)(this); }

		virtual void**   GetName()                      { return &m_fnName; }
		virtual void**   GetClassName()                 { return &m_className; }
		virtual void**   GetStr20()                     { return &m_unk20; }
		virtual uint64_t* GetReturnType(uint64_t* dst)  { *dst = m_retnType; return dst; }
		virtual uint64_t GetNumParams()                 { return m_realNumParams; }
		virtual uint64_t GetParam(UInt32 idx, void** outName, uint64_t* outType)
		{
			void* self = reinterpret_cast<uint8_t*>(this) + kGetParamThis;
			return GameFn<uint64_t (*)(void*, UInt32, void**, uint64_t*)>(kImplGetParam)(self, idx, outName, outType);
		}
		virtual uint64_t GetNumParams2()                { return m_numParams; }
		virtual bool     IsNative()                     { return true; }
		virtual bool     IsStatic()                     { return m_isStatic; }
		virtual bool     Unk_0A()                       { return false; }
		virtual UInt32   Unk_0B()                       { return 0; }
		virtual UInt32   GetUnk44()                     { return m_unk44; }
		virtual void**   GetStr48()                     { return &m_unk48; }
		virtual void     Unk_0E()                       {}
		virtual UInt32   Invoke(void* a0, void* a1, void* vm, void* state)
		{
			return GameFn<UInt32 (*)(NativeFunction*, void*, void*, void*, void*)>(kImplInvoke)(this, a0, a1, vm, state);
		}
		virtual void**   GetSourceFile()                { return GameFn<void** (*)(NativeFunction*)>(kImplGetSourceFile)(this); }
		virtual bool     Unk_11(UInt32, UInt32* out)    { *out = 0; return false; }
		virtual bool     GetParamName(UInt32 idx, void** out)
		{
			return GameFn<bool (*)(NativeFunction*, UInt32, void**)>(kImplGetParamName)(this, idx, out);
		}
		virtual UInt32   GetUnk41()                     { return m_unk41; }
		virtual void     SetUnk41(uint8_t v)            { m_unk41 = v; }
		virtual bool     HasCallback()                  { return true; }
		virtual bool     Run(VMValue* base, void* vm, UInt32 stackId, VMValue* result, void* state) = 0;

	protected:
		UInt32   m_refCount = 0;      // 08
		UInt32   m_pad0C = 0;
		void*    m_fnName = nullptr;  // 10  BSFixedString
		void*    m_className = nullptr;
		void*    m_unk20 = nullptr;
		uint64_t m_retnType = 0;      // 28
		void*    m_paramData = nullptr; // 30
		uint16_t m_numParams = 0;
		uint16_t m_realNumParams = 0;
		UInt32   m_pad3C = 0;
		bool     m_isStatic = false;  // 40
		uint8_t  m_unk41 = 0;
		bool     m_isLatent = false;
		uint8_t  m_pad43 = 0;
		UInt32   m_unk44 = 0;
		void*    m_unk48 = nullptr;
		void*    m_callback = nullptr; // 50
	};
	static_assert(sizeof(NativeFunction) == 0x58, "NativeFunction layout");

	// The game's constructor leaves the game's own vtable in place; deriving puts ours back.
	class FloatFunction final : public NativeFunction
	{
	public:
		FloatFunction(const char* fnName, const char* className, float (*fn)())
			: NativeFunction(fnName, className), m_fn(fn)
		{
			m_retnType = kTypeFloat;
			m_callback = reinterpret_cast<void*>(fn);
		}
		bool Run(VMValue*, void*, UInt32, VMValue* result, void*) override
		{
			float v = m_fn();
			// anything else would need the game's destructor first; a fresh result is always None
			if (result->type != kTypeNone && result->type != kTypeInt && result->type != kTypeFloat && result->type != kTypeBool)
				return false;
			result->type = kTypeFloat;
			result->data.p = nullptr;
			result->data.f = v;
			return true;
		}

	private:
		float (*m_fn)();
	};

	// The game's string constructor takes the pooled entry and one reference to it, which the VM
	// gives back when it drops the value. Kept apart so a bad offset faults in here and is caught.
	inline bool MakeString(void* slot, const char* text)
	{
		__try
		{
			GameFn<void* (*)(void*, const char*)>(kStringCtor)(slot, text);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	class StringFunction final : public NativeFunction
	{
	public:
		StringFunction(const char* fnName, const char* className, std::string (*fn)())
			: NativeFunction(fnName, className), m_fn(fn)
		{
			m_retnType = kTypeString;
			m_callback = reinterpret_cast<void*>(fn);
		}
		bool Run(VMValue*, void*, UInt32, VMValue* result, void*) override
		{
			const std::string v = m_fn();
			if (result->type != kTypeNone && result->type != kTypeInt && result->type != kTypeFloat && result->type != kTypeBool)
				return false;
			result->data.p = nullptr;
			if (!MakeString(&result->data, v.c_str()))
			{
				result->data.p = nullptr;
				return false;
			}
			result->type = kTypeString;
			return true;
		}

	private:
		std::string (*m_fn)();
	};

	inline void RegisterAll(void* vm)
	{
		static const char* kScript = "BodycamPresets";
		void** vtbl = *reinterpret_cast<void***>(vm);
		// the VM keeps them for the life of the process
		NativeFunction* fns[] = {
			new FloatFunction("Begin", kScript, &Presets::BridgeBegin),
			new FloatFunction("Value", kScript, &Presets::BridgeValue),
			new StringFunction("NextName", kScript, &Presets::BridgeNextName),
			new StringFunction("PrevName", kScript, &Presets::BridgePrevName),
		};
		const char* names[] = { "Begin", "Value", "NextName", "PrevName" };
		for (size_t i = 0; i < 4; ++i)
		{
			reinterpret_cast<void (*)(void*, NativeFunction*)>(vtbl[kVmRegisterFunction])(vm, fns[i]);
			reinterpret_cast<void (*)(void*, const char*, const char*, UInt32)>(vtbl[kVmSetFunctionFlags])(vm, kScript, names[i], kFunctionFlagNoWait);
		}
	}

	// A wrong offset shows up here as an access violation, at game start. Caught, the script's
	// Begin stays unbound, returns nothing, and Apply Preset falls back to the file-only load.
	inline bool Register(void* vm)
	{
		__try
		{
			RegisterAll(vm);
			CP_LOG("Papyrus: BodycamPresets natives registered");
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			CP_LOG("Papyrus: registering the natives faulted - presets load without refreshing MCM");
		}
		return true;
	}

	inline void Install(const F4SEInterface* f4se)
	{
		auto* papyrus = static_cast<F4SEPapyrusInterface*>(f4se->QueryInterface(F4SEPapyrusInterface::kInterface_Papyrus));
		bool ok = papyrus && papyrus->Register(&Register);
		CP_LOG("Papyrus interface: %s", ok ? "callback queued" : "not available, presets load without refreshing MCM");
	}
}
