#pragma once

#ifndef _KERNEL_MODE

#include "memory.hpp"
#include "communication.hpp"
#include "lazy_importer.hpp"
#include "log.h"

#define ZYDIS_DISABLE_FORMATTER

#include <Zydis/Zydis.h>
#include <unordered_map>

#define SHARED_DATA_SIZE (PAGE_SIZE * 0x10)
#define INTERNAL_HOOK_SIZE (32ull)
#define API __declspec(dllexport)

typedef enum _INTERNAL_JTABLE_TARGETS : unsigned long long {
	MainHook = 0,
#pragma region rust
	StartCoroutine = MainHook,
	ConsoleSystemRun,
	OnLand,
	ViewModelPlay,
	ClientInput,
	VisUpdateUsingCulling,
	RegisterForCulling,
	HandleJumping,
	ProcessAttack,
	RustMaxHookIndex = 64
#pragma endregion
} INTERNAL_JTABLE_TARGETS;

__declspec(align(PAGE_SIZE)) typedef union _INTERNAL_JUMP_TABLE {
	union {
		//Contains pointers to the trampolines for the respective functions
		char raw[PAGE_SIZE];
	};
} INTERNAL_JUMP_TABLE, *PINTERNAL_JUMP_TABLE;

typedef struct _INTERNAL_TRAMPOLINE_FOOTER {
	unsigned long long currTrampolineOffset;
} INTERNAL_TRAMPOLINE_FOOTER, *PINTERNAL_TRAMPOLINE_FOOTER;

__declspec(align(PAGE_SIZE)) typedef struct _INTERNAL_TRAMPOLINE_DATA {
	char trampolines[PAGE_SIZE - sizeof(INTERNAL_TRAMPOLINE_FOOTER)];
	INTERNAL_TRAMPOLINE_FOOTER footer;
} INTERNAL_TRAMPOLINE_DATA, *PINTERNAL_TRAMPOLINE_DATA;

__declspec(align(PAGE_SIZE)) typedef struct _INTERNAL_MODULE_DATA {
	//Main executable base
	PVOID pImageBase;
	//Main DLL base (ex. GameAssembly.dll)
	PVOID pMainModuleBase;
	//Game PEB
	PVOID pPeb;
	//Injected DLL base
	PVOID pInjectedBase;

	BOOLEAN bCRTInit;
	BOOLEAN bDebug;
	BOOLEAN bEPTHidden;
	BOOLEAN bShouldEject;
	BOOLEAN bEjected;

	unsigned long long dwSetupTime;
} INTERNAL_MODULE_DATA, *PINTERNAL_MODULE_DATA;

typedef enum _CMD_ERROR {
	Success = 0,
	HeadOverTail = 1,
	HeadOverLimit,
	TailOverLimit
} CMD_ERROR;

typedef enum _CMD_CODE {
	Invalid = 0,
	Hook,
	ReceiveAsset,
	StreamAssets,
	LoadAsset
} CMD_CODE;

typedef union _CMD_UNION {
	char raw[24];
	struct {
		PVOID src;
		PVOID dst;
		unsigned long long jTableOffset;
	} hook;
	size_t size;
	uintptr_t address;
} CMD_UNION, *PCMD_UNION;

typedef struct _CMD {
	unsigned long long code;
	CMD_UNION cmd;
} CMD, * PCMD;

__declspec(align(sizeof(CMD))) typedef struct _CMD_CTL_DATA {
	/*
	* Offset from raw that the controller will start reading commands from.
	* Controller sets this value.
	*/
	unsigned long long head;
	/*
	* Offset from raw that the controller will stop reading commands to.
	* Controllee sets this value.
	*/
	unsigned long long tail;
	/*
	* Last known error. It is 0 until failure.
	*/
	unsigned long long errorCode;
} CMD_CTL_DATA, *PCMD_CTL_DATA;


__declspec(align(PAGE_SIZE)) typedef struct _CMD_DATA {
	char raw[PAGE_SIZE - sizeof(CMD_CTL_DATA)];
	CMD_CTL_DATA ctl;
} CMD_DATA, *PCMD_DATA;


__declspec(align(PAGE_SIZE)) typedef union _INTERNAL_FEATURE_CTL_DATA {
	char raw[PAGE_SIZE];
	struct {
		bool bChamsOn;
	} rust;
	struct {

	} eft;
} INTERNAL_FEATURE_CTL_DATA, *PINTERNAL_FEATURE_CTL_DATA;

typedef union _INTERNAL_SHARED_DATA {
	struct {
		char sharedData[SHARED_DATA_SIZE];
	};
	struct {
		INTERNAL_JUMP_TABLE jTable;
		INTERNAL_TRAMPOLINE_DATA trampolineData;
		INTERNAL_MODULE_DATA modData;
		CMD_DATA internalCmdData;
		CMD_DATA externalCmdData;
		INTERNAL_FEATURE_CTL_DATA featureData;
	};
} INTERNAL_SHARED_DATA, *PINTERNAL_SHARED_DATA;


typedef struct _INTERNAL_HOOK_DATA {
	PVOID pHook;
	char origBytes[32];
	char hookBytes[32];
} INTERNAL_HOOK_DATA, *PINTERNAL_HOOK_DATA;

API INTERNAL_SHARED_DATA sharedData = { 0 };

char* heap_str_global = new char[PAGE_SIZE * 0x10];
size_t strOffset = 0;
char* fast_heap_str(const char* pStr, int strlen) {
	memcpy(heap_str_global + strOffset, pStr, strlen);
	size_t strOffsetOld = strOffset;
	strOffset += strlen;
	return heap_str_global + strOffsetOld;
}

#define unprotect_str_const(x) (sharedData.modData.bEPTHidden ? fast_heap_str(x, sizeof(x)) : x)
#define unprotect_str_runtime(x) (sharedData.modData.bEPTHidden ? std::string(x).c_str() : x)

class InternalJTableManager {
public:
	static unsigned long long offset(INTERNAL_JTABLE_TARGETS jTableTarget) {
		return jTableTarget * 8;
	}

	static PVOID& target(INTERNAL_JTABLE_TARGETS jTableTarget) {
		return *(PVOID*)(sharedData.jTable.raw + offset(jTableTarget));
	}
};

class ExternalCmdController {
private:
	PCMD_DATA cmdData;
	constexpr static int cmdLimit = sizeof(CMD_DATA::raw);
public:
	ExternalCmdController() : cmdData(0) {};
	ExternalCmdController(PCMD_DATA pCmdData) : cmdData(pCmdData) {};

	VOID Init(PCMD_DATA cmd) {
		cmdData = cmd;
	}

	PCMD_DATA data() {
		return cmdData;
	}

	bool CommandsAvailable() {
		if (cmdData->ctl.head > cmdData->ctl.tail)
			cmdData->ctl.errorCode = CMD_ERROR::HeadOverTail;
		else if (cmdData->ctl.head >= cmdLimit)
			cmdData->ctl.errorCode = CMD_ERROR::HeadOverLimit;
		else if (cmdData->ctl.tail >= cmdLimit)
			cmdData->ctl.errorCode = CMD_ERROR::TailOverLimit;
		else
			return cmdData->ctl.tail > cmdData->ctl.head;
		return false;
	}

	void Queue(CMD cmd) {
		*(CMD*)(cmdData->raw + cmdData->ctl.tail) = cmd;
		cmdData->ctl.tail += sizeof(CMD);
	}

	void Queue(std::vector<CMD> cmds) {
		for (auto& cmd : cmds) {
			Queue(cmd);
		}
	}

	CMD Pop() {
		if (!CommandsAvailable())
			return CMD();

		CMD cmd = { 0 };
		cmd = *(CMD*)(cmdData->raw + cmdData->ctl.head);
		cmdData->ctl.head += sizeof(CMD);
		return cmd;
	}

	std::vector<CMD> PopAll() {
		std::vector<CMD> cmds;
		while (CommandsAvailable()) {
			cmds.push_back(Pop());
		}
		ResetHead();
		return cmds;
	}

	void ResetHead() {
		cmdData->ctl.tail = 0;
		cmdData->ctl.head = cmdData->ctl.tail;
	}

	void ResetErrorCode() {
		cmdData->ctl.errorCode = 0;
	}

	void SetErrorCode(unsigned long long code) {
		cmdData->ctl.errorCode = code;
	}

	unsigned long long GetErrorCode() {
		return cmdData->ctl.errorCode;
	}
};


class InternalCmdController {
private:
	PCMD_DATA cmdData;
	constexpr static int cmdLimit = sizeof(CMD_DATA::raw);

public:
	InternalCmdController() : cmdData(0) {};
	InternalCmdController(PCMD_DATA pCmdData) : cmdData(pCmdData) {};

	VOID Init(PCMD_DATA cmd) {
		cmdData = cmd;
	}

	PCMD_DATA data() {
		return cmdData;
	}

	bool CommandsAvailable() {
		if (cmdData->ctl.head > cmdData->ctl.tail)
			cmdData->ctl.errorCode = CMD_ERROR::HeadOverTail;
		else if (cmdData->ctl.head >= cmdLimit)
			cmdData->ctl.errorCode = CMD_ERROR::HeadOverLimit;
		else if (cmdData->ctl.tail >= cmdLimit)
			cmdData->ctl.errorCode = CMD_ERROR::TailOverLimit;
		else
			return cmdData->ctl.tail > cmdData->ctl.head;
		return false;
	}

	void Queue(CMD cmd) {
		*(CMD*)(cmdData->raw + cmdData->ctl.tail) = cmd;
		cmdData->ctl.tail += sizeof(CMD);
	}

	void Queue(std::vector<CMD> cmds) {
		for (auto& cmd : cmds) {
			Queue(cmd);
		}
	}

	CMD Pop() {
		if (!CommandsAvailable())
			return CMD();

		CMD cmd = { 0 };
		cmd = *(CMD*)(cmdData->raw + cmdData->ctl.head);
		cmdData->ctl.head += sizeof(CMD);
		return cmd;
	}

	std::vector<CMD> PopAll() {
		std::vector<CMD> cmds;
		while (CommandsAvailable()) {
			cmds.push_back(Pop());
		}
		ResetHead();
		return cmds;
	}

	void ResetHead() {
		cmdData->ctl.tail = 0;
		cmdData->ctl.head = cmdData->ctl.tail;
	}

	void ResetErrorCode() {
		cmdData->ctl.errorCode = 0;
	}

	void SetErrorCode(unsigned long long code) {
		cmdData->ctl.errorCode = code;
	}

	unsigned long long GetErrorCode() {
		return cmdData->ctl.errorCode;
	}

	void QueueHook(PVOID src, PVOID dst, unsigned long long jTableOffset) {
		CMD cmd;
		cmd.code = CMD_CODE::Hook;
		cmd.cmd.hook.src = src;
		cmd.cmd.hook.dst = dst;
		cmd.cmd.hook.jTableOffset = jTableOffset;
		Queue(cmd);
	}
};

class InternalManager {
private:
	IdentityMapping _identity;
	PINTERNAL_JUMP_TABLE _jmpTable;
	PINTERNAL_TRAMPOLINE_DATA _trampolines;
	PVOID _pShared;

	std::unordered_map<HANDLE, INTERNAL_HOOK_DATA> _hookData;

public:
	InternalCmdController cmd;
	ExternalCmdController externalCmd;

	InternalManager(PVOID pIdentity, unsigned long long* pGameCr3, PVOID pShared) {
		_identity.Init(pIdentity, pGameCr3);
		_jmpTable = (PINTERNAL_JUMP_TABLE)_identity.VirtToIdentityVirt((size_t)pShared + offsetof(INTERNAL_SHARED_DATA, jTable));
		_trampolines = (PINTERNAL_TRAMPOLINE_DATA)_identity.VirtToIdentityVirt((size_t)pShared + offsetof(INTERNAL_SHARED_DATA, trampolineData));
		_pShared = pShared;
		cmd.Init((PCMD_DATA)_identity.VirtToIdentityVirt((size_t)pShared + offsetof(INTERNAL_SHARED_DATA, internalCmdData)));
		externalCmd.Init((PCMD_DATA)_identity.VirtToIdentityVirt((size_t)pShared + offsetof(INTERNAL_SHARED_DATA, externalCmdData)));
	}

	static SIZE_T GetInstrBoundaryLen(PVOID pBase, int targetLen) {
		ZyanUSize instrLen = 0;

		/* Determine the number of instructions necessary to overwrite using Length Disassembler Engine */
		// Initialize decoder context
		ZydisDecoder* pDecoder = (ZydisDecoder*)malloc(sizeof(*pDecoder));
		ZyanStatus status = ZydisDecoderInit(pDecoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_ADDRESS_WIDTH_64);
		if (!ZYAN_SUCCESS(status)) {
			return 0;
		}
		// Loop over the instructions in our buffer.
		// The runtime-address (instruction pointer) is chosen arbitrary here in order to better
		// visualize relative addressing

//This macro does not exist in Zydis 3.1.x
#ifndef ZYDIS_STATUS_IMPOSSIBLE_INSTRUCTION
		ZyanU8* data = (ZyanU8*)pBase;
		const ZyanUSize length = PAGE_SIZE;
		ZydisDecodedInstruction* instruction = (ZydisDecodedInstruction*)malloc(sizeof(*instruction));
		while (ZYAN_SUCCESS(ZydisDecoderDecodeBuffer(pDecoder, (const void*)(data + instrLen), length - instrLen,
			instruction)))
#else
		ZyanU8* data = (ZyanU8*)pBase;
		const ZyanUSize length = PAGE_SIZE;
		ZydisDecodedInstruction* instruction = (ZydisDecodedInstruction*)malloc(sizeof(*instruction));
		ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];
		while (ZYAN_SUCCESS(ZydisDecoderDecodeFull(pDecoder, (const void*)(data + instrLen), length - instrLen,
			instruction, operands)))
#endif
		{
			if (instruction->opcode == 0xe8 //Call near
				|| instruction->opcode == 0xeb //Jmp near
				)
				break;
			instrLen += instruction->length;
			if (instrLen >= targetLen)
				break;
		}
		free(instruction);
		free(pDecoder);

		return instrLen;
	}

	INTERNAL_HOOK_DATA WriteAbsJump(PVOID src, PVOID dst, PVOID pTrampoline) {
		INTERNAL_HOOK_DATA hkData = { 0 };

		char pHook[13] = { 0 };
		/* mov r11, Target */
		pHook[0] = 0x49;
		pHook[1] = 0xBB;

		/* push r11 */
		pHook[10] = 0x41;
		pHook[11] = 0x53;

		/* ret */
		pHook[12] = 0xC3;

		char pSrcCopy[32] = { 0 };
		if (!_identity.Read((size_t)src, pSrcCopy, 32)) {
			DbgLog("Failed reading 32 bytes from source: %p", src);
			return hkData;
		}
		SIZE_T SizeOfHookedInstructions = GetInstrBoundaryLen(pSrcCopy, 13);
		if (SizeOfHookedInstructions < 13) {
			DbgLog("Not enough space in instructions for a trampoline!");
			return hkData;
		}
		if (!_identity.Write((size_t)pTrampoline, pSrcCopy, SizeOfHookedInstructions)) {
			DbgLog("Failed writing original trampoline bytes to: %p", pTrampoline);
			return hkData;
		}

		/* Target */
		*((PSIZE_T)&pHook[2]) = (size_t)src + SizeOfHookedInstructions;
		if (!_identity.Write((size_t)pTrampoline + SizeOfHookedInstructions, pHook, 13)) {
			DbgLog("Failed writing trampoline bytes to: 0x%llx", (size_t)pTrampoline + SizeOfHookedInstructions);
			return hkData;
		}

		*((PSIZE_T)&pHook[2]) = (size_t)dst;
		if (!_identity.Write((size_t)src, pHook, 13)) {
			DbgLog("Failed writing end trampoline bytes to: 0x%llx", (size_t)src);
			return hkData;
		}

		hkData.pHook = src;
		memcpy(hkData.origBytes, pSrcCopy, 13);
		memcpy(hkData.hookBytes, pHook, 13);
		return hkData;
	}

	HANDLE Hook(PVOID src, PVOID dst, unsigned long long jTableOffset = MAXULONG64) {
		if (_trampolines->footer.currTrampolineOffset >= PAGE_SIZE)
			return 0;

		size_t trampolines = (size_t)this->_pShared + offsetof(INTERNAL_SHARED_DATA, trampolineData) + offsetof(INTERNAL_TRAMPOLINE_DATA, trampolines);
		PVOID pCurrTrampoline = (PVOID)(trampolines + _trampolines->footer.currTrampolineOffset);
		if (_hookData.contains(pCurrTrampoline)) {
			INTERNAL_HOOK_DATA& hkData = _hookData[pCurrTrampoline];
			_identity.Write((GAME_PTR)hkData.pHook, (PVOID)hkData.hookBytes, 13);
			DbgLog("Rehooked at: %p", hkData.pHook);
			return pCurrTrampoline;
		}
		_trampolines->footer.currTrampolineOffset += 32;
		if (jTableOffset != MAXULONG64) {
			*(PVOID*)&_jmpTable->raw[jTableOffset] = pCurrTrampoline;
		}
		INTERNAL_HOOK_DATA hkData = WriteAbsJump(src, dst, pCurrTrampoline);
		if (hkData.pHook) {
			_hookData.insert(std::make_pair(pCurrTrampoline, hkData));
			DbgLog("Hooked at: %p", src);
		}
		else {
			DbgLog("Could not hook: %p", src);
			return nullptr;
		}
		return pCurrTrampoline;
	}

	VOID Unhook(HANDLE hHook) {
		if (_hookData.contains(hHook)) {
			INTERNAL_HOOK_DATA& hkData = _hookData[hHook];
			_identity.Write((GAME_PTR)hkData.pHook, (PVOID)hkData.origBytes, 13);
			DbgLog("Unhooked at: %p", hkData.pHook);
		}
	}

	VOID ClearHooks() {
		for (auto& hook : _hookData) {
			INTERNAL_HOOK_DATA& hkData = hook.second;
			_identity.Write((GAME_PTR)hkData.pHook, (PVOID)hkData.origBytes, 13);
			DbgLog("Unhooked at: %p", hkData.pHook);
		}
		_hookData.clear();
	}

	PINTERNAL_JUMP_TABLE InternalJmpTable() {
		return _jmpTable;
	}

	PINTERNAL_MODULE_DATA InternalModData() {
		return (PINTERNAL_MODULE_DATA)_identity.VirtToIdentityVirt((size_t)this->_pShared + offsetof(INTERNAL_SHARED_DATA, modData));
	}

	VOID FillModuleData(PROC_INFO& procInfo) {
		PINTERNAL_MODULE_DATA pModData = (PINTERNAL_MODULE_DATA)_identity.VirtToIdentityVirt((size_t)this->_pShared + offsetof(INTERNAL_SHARED_DATA, modData));
		pModData->pImageBase = (PVOID)procInfo.imageBase;
		pModData->pMainModuleBase = (PVOID)procInfo.lastDllBase;
		pModData->pPeb = (PVOID)procInfo.pPeb;
		pModData->pInjectedBase = (PVOID)procInfo.mapInfo.pOutBuffer;
		pModData->bEPTHidden = procInfo.mapInfo.bEPTHide;
	}

	VOID Eject(bool bWaitForCompletion = false) {
		PINTERNAL_MODULE_DATA pModData = (PINTERNAL_MODULE_DATA)_identity.VirtToIdentityVirt((size_t)this->_pShared + offsetof(INTERNAL_SHARED_DATA, modData));
		pModData->bShouldEject = true;
		if (bWaitForCompletion) {
			while (!pModData->bEjected) {
				Sleep(100);
			}
		}
		ClearHooks();
	}
};

#ifdef NtCurrentProcess
#undef NtCurrentProcess
#endif
#define NtCurrentProcess() ((HANDLE)MAXULONG64)

typedef struct _UNICODE_STRING_INTERNAL {
	USHORT Length;
	USHORT MaximumLength;
	PWCH   Buffer;
} UNICODE_STRING_INTERNAL;

typedef struct _LDR_DATA_TABLE_ENTRY_INTERNAL {
	LIST_ENTRY     LoadOrder;
	LIST_ENTRY     MemoryOrder;
	LIST_ENTRY     InitializationOrder;
	PVOID          ModuleBaseAddress;
	PVOID          Entry;
	ULONG          ModuleSize;
	UNICODE_STRING_INTERNAL FullModuleName;
	UNICODE_STRING_INTERNAL ModuleName;
	ULONG          Flags;
	USHORT         LoadCount;
	USHORT         TlsIndex;
	union {
		LIST_ENTRY Hash;
		struct {
			PVOID SectionPointer;
			ULONG CheckSum;
		};
	};
	ULONG   TimeStamp;
} LDR_DATA_TABLE_ENTRY_INTERNAL, * PLDR_DATA_TABLE_ENTRY_INTERNAL;

typedef struct _RTL_USER_PROCESS_PARAMETERS_INTERNAL {
	BYTE Reserved1[16];
	PVOID Reserved2[10];
	UNICODE_STRING_INTERNAL ImagePathName;
	UNICODE_STRING_INTERNAL CommandLine;
} RTL_USER_PROCESS_PARAMETERS_INTERNAL, * PRTL_USER_PROCESS_PARAMETERS_INTERNAL;

typedef struct _PEB_INTERNAL {
	UCHAR InheritedAddressSpace;
	UCHAR ReadImageFileExecOptions;
	UCHAR BeingDebugged;
	UCHAR BitField;
	PVOID Mutant;
	PVOID ImageBaseAddress;
	PLDR_DATA_TABLE_ENTRY_INTERNAL Ldr;
	PRTL_USER_PROCESS_PARAMETERS_INTERNAL ProcessParameters;
	PVOID SubSystemData;
	PVOID ProcessHeap;
	PVOID FastPebLock;
	PVOID AtlThunkSListPtr;
	PVOID IFEOKey;
	PVOID CrossProcessFlags;
	PVOID KernelCallbackTable;
	ULONG SystemReserved;
	ULONG AtlThunkSListPtr32;
	PVOID ApiSetMap;
} PEB_INTERNAL, * PPEB_INTERNAL;

typedef struct _API_SET_VALUE_ENTRY_10
{
	ULONG Flags;
	ULONG NameOffset;
	ULONG NameLength;
	ULONG ValueOffset;
	ULONG ValueLength;
} API_SET_VALUE_ENTRY_10, * PAPI_SET_VALUE_ENTRY_10;

typedef struct _API_SET_VALUE_ARRAY_10
{
	ULONG Flags;
	ULONG NameOffset;
	ULONG Unk;
	ULONG NameLength;
	ULONG DataOffset;
	ULONG Count;

	inline PAPI_SET_VALUE_ENTRY_10 entry(void* pApiSet, DWORD i)
	{
		return (PAPI_SET_VALUE_ENTRY_10)((BYTE*)pApiSet + DataOffset + i * sizeof(API_SET_VALUE_ENTRY_10));
	}
} API_SET_VALUE_ARRAY_10, * PAPI_SET_VALUE_ARRAY_10;

typedef struct _API_SET_NAMESPACE_ENTRY_10
{
	ULONG Limit;
	ULONG Size;
} API_SET_NAMESPACE_ENTRY_10, * PAPI_SET_NAMESPACE_ENTRY_10;

typedef struct _API_SET_NAMESPACE_ARRAY_10
{
	ULONG Version;
	ULONG Size;
	ULONG Flags;
	ULONG Count;
	ULONG Start;
	ULONG End;
	ULONG Unk[2];

	inline PAPI_SET_NAMESPACE_ENTRY_10 entry(DWORD i)
	{
		return (PAPI_SET_NAMESPACE_ENTRY_10)((BYTE*)this + End + i * sizeof(API_SET_NAMESPACE_ENTRY_10));
	}

	inline PAPI_SET_VALUE_ARRAY_10 valArray(PAPI_SET_NAMESPACE_ENTRY_10 pEntry)
	{
		return (PAPI_SET_VALUE_ARRAY_10)((BYTE*)this + Start + sizeof(API_SET_VALUE_ARRAY_10) * pEntry->Size);
	}

	inline ULONG apiName(PAPI_SET_NAMESPACE_ENTRY_10 pEntry, wchar_t* output)
	{
		auto pArray = valArray(pEntry);
		memcpy(output, (char*)this + pArray->NameOffset, pArray->NameLength);
		return  pArray->NameLength;
	}
} API_SET_NAMESPACE_ARRAY_10, * PAPI_SET_NAMESPACE_ARRAY_10;

typedef PAPI_SET_VALUE_ENTRY_10     PAPISET_VALUE_ENTRY;
typedef PAPI_SET_VALUE_ARRAY_10     PAPISET_VALUE_ARRAY;
typedef PAPI_SET_NAMESPACE_ENTRY_10 PAPISET_NAMESPACE_ENTRY;
typedef PAPI_SET_NAMESPACE_ARRAY_10 PAPISET_NAMESPACE_ARRAY;

class PE {
private:
	PIMAGE_NT_HEADERS64 pNtHeaders;
	unsigned long long szHeader;
	unsigned long long pImageBase;
	char* pMappedBase;

	PIMAGE_NT_HEADERS64 getNtHeaders() {
		const auto dos_header = (PIMAGE_DOS_HEADER)(pImageBase);
		const auto nt_headers = (PIMAGE_NT_HEADERS64)((unsigned long long)(pImageBase)+dos_header->e_lfanew);
		return nt_headers;
	}

	wchar_t* resolveAPISet(wchar_t* pImport) {
		PPEB_INTERNAL peb = (PPEB_INTERNAL)sharedData.modData.pPeb;
		PAPISET_NAMESPACE_ARRAY pApiSetMap = (PAPISET_NAMESPACE_ARRAY)(peb->ApiSetMap);

		// Iterate api set map
		for (ULONG i = 0; i < pApiSetMap->Count; i++)
		{
			PAPISET_NAMESPACE_ENTRY pDescriptor = NULL;
			PAPISET_VALUE_ARRAY pHostArray = NULL;
			wchar_t apiNameBuf[255] = { 0 };

			pDescriptor = (PAPISET_NAMESPACE_ENTRY)((PUCHAR)pApiSetMap + pApiSetMap->End + i * sizeof(API_SET_NAMESPACE_ENTRY_10));
			pHostArray = (PAPISET_VALUE_ARRAY)((PUCHAR)pApiSetMap + pApiSetMap->Start + sizeof(API_SET_VALUE_ARRAY_10) * pDescriptor->Size);

			memcpy(apiNameBuf, (PUCHAR)pApiSetMap + pHostArray->NameOffset, pHostArray->NameLength);

			if (wcscmp(pImport, apiNameBuf) != 0) {
				continue;
			}

			PAPISET_VALUE_ENTRY pHost = NULL;
			wchar_t apiHostNameBuf[255] = { 0 };

			pHost = (PAPISET_VALUE_ENTRY)((PUCHAR)pApiSetMap + pHostArray->DataOffset);
			// Sanity check
			if (pHostArray->Count < 1) {
				break;
			}

			memcpy(apiHostNameBuf, (PUCHAR)pApiSetMap + pHost->ValueOffset, pHost->ValueLength);

			return apiHostNameBuf;
		}

		return (wchar_t*)L"";
	}

public:
	PE(PVOID _pImageBase) {
		pImageBase = (unsigned long long)_pImageBase;
		if (!pImageBase) {
			pImageBase = (unsigned long long)PAGE_ALIGN(_ReturnAddress());
			while (*(USHORT*)pImageBase != 0x5a4d) {
				pImageBase -= PAGE_SIZE;
			}
		}
		pNtHeaders = getNtHeaders();
		pMappedBase = 0;
	}
	~PE() {
		if (pMappedBase) {
			memset(pMappedBase, 0, imageSize());
			free(pMappedBase);
		}
	}

	PIMAGE_NT_HEADERS64 ntHeaders() {
		return pNtHeaders;
	}
	unsigned long long imageBase() {
		return pImageBase;
	}
	unsigned long long imageSize() {
		if (!pNtHeaders)
			return 0;
		return pNtHeaders->OptionalHeader.SizeOfImage;
	}
	unsigned long long sizeOfCode() {
		return pNtHeaders->OptionalHeader.SizeOfCode;
	}
	unsigned long long headerSize() {
		return szHeader;
	}
	unsigned long long entryPoint() {
		return this->pImageBase + ntHeaders()->OptionalHeader.AddressOfEntryPoint;
	}
	PVOID dataDir(ULONG entry) {
		IMAGE_OPTIONAL_HEADER64* opt_header = &ntHeaders()->OptionalHeader;
		IMAGE_DATA_DIRECTORY* dir = &opt_header->DataDirectory[entry];
		if (!dir->Size
			|| !dir->VirtualAddress)
			return nullptr;

		if(this->pMappedBase)
			return (PVOID)(this->pMappedBase + dir->VirtualAddress);
		return (PVOID)(this->pImageBase + dir->VirtualAddress);
	}

	void fixImports() {
		unsigned long long pPeb = (unsigned long long)sharedData.modData.pPeb;
		auto* pOpt = &ntHeaders()->OptionalHeader;

		char strBuf[MAX_PATH] = { 0 };

		//Fixing imports
		if (pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size) {
			auto* pImportDescr = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(pImageBase + pOpt->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
			while (pImportDescr->Name) {
				char* pImportName = reinterpret_cast<char*>(pImageBase + pImportDescr->Name); //szMod contains the name of the module to be loaded
				strcpy(strBuf, pImportName);
				HMODULE hDll = GetModuleHandleA(strBuf);
				if (!hDll) {
					int impLen = strlen(pImportName);
					if (!impLen) {
						++pImportDescr;
						continue;
					}

					wchar_t wImportName[MAX_PATH];
					for (int i = 0; i < impLen; i++) {
						wImportName[i] = (wchar_t)pImportName[i];
					}
					wImportName[impLen] = 0;
					resolveAPISet(wImportName);
					++pImportDescr;
					continue;
				}
				ULONG_PTR* pThunkRef = reinterpret_cast<ULONG_PTR*>(pImageBase + pImportDescr->OriginalFirstThunk);
				ULONG_PTR* pFuncRef = reinterpret_cast<ULONG_PTR*>(pImageBase + pImportDescr->FirstThunk);

				if (!pThunkRef) {
					pThunkRef = pFuncRef;
				}

				for (; *pThunkRef; ++pThunkRef, ++pFuncRef) {
					if (IMAGE_SNAP_BY_ORDINAL(*pThunkRef)) {
						//In this case pThunkRef contains the ordinal number which represents the function
						*pFuncRef = (ULONG_PTR)GetProcAddress(hDll, reinterpret_cast<char*>(*pThunkRef & 0xffff));
					}
					else {
						//Else pThunkRef contains an offset that will point to the Import where the name can be taken from for the loading
						auto* pImport = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(pImageBase + (*pThunkRef));
						strcpy(strBuf, (char*)pImport->Name);
						*pFuncRef = (ULONG_PTR)GetProcAddress(hDll, strBuf);

						bool bString = true;
						for (int i = 0; i < 4; i++) {
							char currChar = ((char*)*pFuncRef)[i];
							if (
								(currChar >= 'A' && currChar <= 'Z')
								|| (currChar >= 'a' && currChar <= 'z')
								|| (currChar >= '0' && currChar <= '9')
								) {
								continue;
							}
							bString = false;
							break;
						}
						if (!bString)
							continue;

						strcpy(strBuf, (char*)*pFuncRef);
						HMODULE hDllFw = GetModuleHandleA(strBuf);
						if (!hDllFw) {
							continue;
						}

						strcpy(strBuf, (char*)*pFuncRef);
						*pFuncRef = (ULONG_PTR)GetProcAddress(hDllFw, strBuf);
					}
				}
				++pImportDescr;
			}
		}
	}

	std::wstring_view demangleName(std::wstring_view wMangled) {
		size_t firstAt = wMangled.find_first_of(L"@@");
		if (!firstAt)
			firstAt = 1;
		return wMangled.substr(1, firstAt - 1);
	}

	PVOID resolveExportedSymbol(const wchar_t* pName)
	{
		if (!pMappedBase) {
			mapNoResolve();
		}

		PVOID procAddr = 0;
		auto* pExportTable = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(dataDir(IMAGE_DIRECTORY_ENTRY_EXPORT));

		unsigned int* NameRVA = (unsigned int*)(pMappedBase + pExportTable->AddressOfNames);

		//Iterate over AddressOfNames
		for (int i = 0; i < pExportTable->NumberOfNames; i++) {
			//Calculate Absolute Address and cast
			char* name = (char*)(pMappedBase + NameRVA[i]);
			int expLen = strlen(name);
			if (!expLen || expLen >= MAX_PATH) {
				continue;
			}

			wchar_t wname[MAX_PATH];
			for (int i = 0; i < expLen; i++) {
				wname[i] = (wchar_t)name[i];
			}
			wname[expLen] = 0;
			if (wname[0] == L'?') {
				std::wstring_view demangled = demangleName(wname);
				demangled.copy(wname, demangled.length(), 0);
				wname[demangled.length()] = 0;
			}
			if (!_wcsicmp(wname, pName)) {
				//Lookup Ordinal
				unsigned short NameOrdinal = ((unsigned short*)(pMappedBase + pExportTable->AddressOfNameOrdinals))[i];

				//Use Ordinal to Lookup Function Address and Calculate Absolute
				unsigned int addr = ((unsigned int*)(pMappedBase + pExportTable->AddressOfFunctions))[NameOrdinal];

				procAddr = (void*)(pMappedBase + addr);

				break;
			}
		}

		return procAddr;
	}

	unsigned long long resolveExportedSymbolRVA(const wchar_t* pName) {
		PVOID pExp = resolveExportedSymbol(pName);
		if (!pExp)
			return 0;
		return (unsigned long long)pExp - (unsigned long long)pMappedBase;
	}

	PVOID mapNoResolve() {
		if (pMappedBase)
			return pMappedBase;

		char* pBuf = (char*)malloc(imageSize());
		if (!pBuf)
			return 0;

		PIMAGE_SECTION_HEADER current_image_section = IMAGE_FIRST_SECTION(ntHeaders());
		memcpy(pBuf, (PVOID)imageBase(), PAGE_SIZE);
		for (auto i = 0; i < ntHeaders()->FileHeader.NumberOfSections; ++i) {
			memcpy(pBuf + current_image_section[i].VirtualAddress, (PVOID)(imageBase() + current_image_section[i].PointerToRawData), current_image_section[i].SizeOfRawData);
		}
		pMappedBase = (char*)pBuf;
		return pBuf;
	}
};

typedef BOOL (*fnDllMain)(HMODULE hModule, DWORD reasonForCall, LPVOID lpReserved);
typedef VOID(*THISFARCALL)(void* self);

static bool bInternalInit = false;
class InternalModule {
private:
	PE pe;

	VOID MemoryAttributesInit() {
		ULONG oldProtect = 0;
		VirtualProtect(sharedData.trampolineData.trampolines, sizeof(sharedData.trampolineData.trampolines), PAGE_EXECUTE_READWRITE, &oldProtect);
		FlushInstructionCache(NtCurrentProcess(), NULL, NULL);
	}

	HANDLE DebugInit() {
		AllocConsole();
		freopen("CONOUT$", "w", stdout);
		printf("Initialized logging!\n");
		return stdout;
	}

	//Can be used to initalize CRT
	BOOL DllMain(HMODULE hModule, DWORD reasonForCall, LPVOID lpReserved) {
		fnDllMain dllMain = (fnDllMain)pe.entryPoint();
		return dllMain(hModule, reasonForCall, lpReserved);
	}

	
public:
	InternalCmdController cmd;
	ExternalCmdController externalCmd;

	template<typename F>
	InternalModule(bool bDebug, bool bCallDllMain, F onInit) : 
		pe(sharedData.modData.pInjectedBase),
		cmd(&sharedData.internalCmdData),
		externalCmd(&sharedData.externalCmdData) {
		if (Init(bDebug, bCallDllMain) && onInit) {
			((THISFARCALL)onInit)(this);
		}
	};

	/*
	* Returns true on first initialization
	*/
	bool Init(bool bDebug = true, bool bCallDllMain = true) {
		if (bInternalInit)
			return false;

		bInternalInit = true;

#ifndef _DEBUG
		pe.fixImports();
#endif

		MemoryAttributesInit();

		sharedData.modData.dwSetupTime = GetTickCount64();
		if (bCallDllMain) {
			DllMain(NULL, DLL_PROCESS_ATTACH, NULL);
			sharedData.modData.bCRTInit = true;
		}
		else {
			sharedData.modData.bCRTInit = false;
		}
		if (bDebug) {
			sharedData.modData.bDebug = true;
			DebugInit();
		}
		else {
			sharedData.modData.bDebug = false;
		}
		return true;
	}

	VOID TLSCallbacks() {
		PIMAGE_DATA_DIRECTORY pTlsDir = (PIMAGE_DATA_DIRECTORY)pe.dataDir(IMAGE_DIRECTORY_ENTRY_TLS);
		if (pTlsDir && pTlsDir->Size) {
			auto* pTLS = reinterpret_cast<IMAGE_TLS_DIRECTORY*>(pe.imageBase() + pTlsDir->VirtualAddress);
			auto* pCallback = reinterpret_cast<PIMAGE_TLS_CALLBACK*>(pTLS->AddressOfCallBacks);
			//Call all callbacks
			for (; pCallback && *pCallback; ++pCallback) {
				(*pCallback)((PVOID)pe.imageBase(), DLL_PROCESS_ATTACH, nullptr);
			}
		}
	}

	INTERNAL_HOOK_DATA WriteAbsJump(PVOID src, PVOID dst, PVOID pTrampoline) {
		INTERNAL_HOOK_DATA hkData = { 0 };

		char pHook[13] = { 0 };
		/* mov r11, Target */
		pHook[0] = 0x49;
		pHook[1] = 0xBB;

		/* push r11 */
		pHook[10] = 0x41;
		pHook[11] = 0x53;

		/* ret */
		pHook[12] = 0xC3;

		char pSrcCopy[32] = { 0 };
		if (memcpy(pSrcCopy, src, 32)) {
			return hkData;
		}
		SIZE_T SizeOfHookedInstructions = InternalManager::GetInstrBoundaryLen(pSrcCopy, 13);
		if (SizeOfHookedInstructions < 13) {
			return hkData;
		}
		if (memcpy(pTrampoline, pSrcCopy, SizeOfHookedInstructions)) {
			return hkData;
		}

		/* Target */
		*((PSIZE_T)&pHook[2]) = (size_t)src + SizeOfHookedInstructions;
		if (memcpy((char*)pTrampoline + SizeOfHookedInstructions, pHook, 13)) {
			return hkData;
		}

		ULONG oldProtect;
		VirtualProtect(src, 32, PAGE_EXECUTE_READWRITE, &oldProtect);
		*((PSIZE_T)&pHook[2]) = (size_t)dst;
		if (memcpy(src, pHook, 13)) {
			return hkData;
		}
		VirtualProtect(src, 32, oldProtect, &oldProtect);

		hkData.pHook = src;
		memcpy(hkData.origBytes, pSrcCopy, 13);
		memcpy(hkData.hookBytes, pHook, 13);
		return hkData;
	}

	VOID Hook(PVOID src, PVOID dst, unsigned long long jTableOffset = MAXULONG64) {
		size_t trampolines = (size_t)sharedData.trampolineData.trampolines;
		PVOID pCurrTrampoline = (PVOID)(trampolines + sharedData.trampolineData.footer.currTrampolineOffset);
		sharedData.trampolineData.footer.currTrampolineOffset += 32;
		if (jTableOffset != MAXULONG64) {
			*(PVOID*)&sharedData.jTable.raw[jTableOffset] = pCurrTrampoline;
		}
		INTERNAL_HOOK_DATA hkData = WriteAbsJump(src, dst, pCurrTrampoline);
	}

	static bool ShouldEject() {
		return sharedData.modData.bShouldEject;
	}

	static void NotifyEject() {
		sharedData.modData.bEjected = true;;
	}

	static INTERNAL_FEATURE_CTL_DATA& features() {
		return sharedData.featureData;
	}
};

class EjectionTracker {
private:
	std::unordered_map<unsigned long long, BOOLEAN> _tracker;
	unsigned long long _notifiedCount;
	BOOLEAN _notified;

public:
	void Register(unsigned long long id) {
		if (_notified || _tracker.count(id) > 0)
			return;
		_tracker.emplace(id, false);
	}

	void Notify(unsigned long long id) {
		if (_notified || _tracker.count(id) == 0)
			return;

		_tracker[id] = true;
		_notifiedCount++;
		if (_notifiedCount >= _tracker.size()) {
			InternalModule::NotifyEject();
			_notified = true;
		}
	}
};

#endif