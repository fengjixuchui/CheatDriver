#pragma once

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <Windows.h>
#endif

typedef enum : ULONG
{
	INST_MIN = 0,
	INST_GET_INFO = 1,
	INST_RESET_INFO,
	INST_MAP,
	INST_HIDE,
	INST_UNHIDE,
	INST_GET_OVERLAY_HANDLE,
	INST_SET_OVERLAY_HANDLE,
	INST_SET_DEFAULT_OVERLAY_HANDLE,
	INST_UNLOCK,
	INST_UNLOCK_ALL,
	INST_SUBSCRIBE_GAME,
	INST_UNSUBSCRIBE_GAME,
	INST_CRASH_SETUP,
	INST_IDENTITY_MAP,
	INST_IDENTITY_UNMAP,
	INST_LOCK_MODULE,
	INST_SHADOW,
	INST_BLOCK_IMAGE,
	INST_UNBLOCK_IMAGE,
	INST_PROTECT,
	INST_UNPROTECT,
	INST_REGISTER_SCORE_NOTIFY,
	INST_GET_SCORE,
	INST_GET_MOD_TRACKING,
	INST_SET_MOD_TRACKING,
	INST_SET_MOD_BACKUP,
	INST_ADD_SHADOW,
	INST_SPOOF,
	INST_MAX
} COMM_CODE, * PCOMM_CODE;

#pragma pack(push, 1)
typedef struct _MAP_INFO {
	PVOID pBuffer;
	SIZE_T sz;
	PVOID pOutBuffer;
	SIZE_T szOut;
	BOOLEAN bMapped;
	BOOLEAN bEPTHide;
} MAP_INFO, * PMAP_INFO;

typedef struct _DLL_TRACK_INFO {
	char* pDllName;
	unsigned long long dllBase;

	__forceinline bool operator==(_DLL_TRACK_INFO& rhs) {
		return !memcmp(&rhs, this, sizeof(rhs));
	}
	__forceinline bool operator!=(_DLL_TRACK_INFO& rhs) {
		return !(*this == rhs);
	}
} DLL_TRACK_INFO, * PDLL_TRACK_INFO;

__declspec(align(256)) typedef struct _PROC_INFO {
	unsigned long long* cr3;
	unsigned long long imageBase;
	unsigned long long pEprocess;
	unsigned long long pPeb;
	union {
		unsigned long long pTeb;
		unsigned long long lastTrackedCr3;
	};
	unsigned long long threadsStarted;
	unsigned long long dllsQueueShadow;
	unsigned long long lastDllBase;
	char* pImageName;

	MAP_INFO mapInfo;

	union {
		HANDLE hProc;
		unsigned long long pRequestor;
	};
	HANDLE dwPid;
	//Window to hide from this process
	HANDLE hWnd;

	bool bDead;
	bool bSetCr3;
	bool bMainThreadHidden;
	bool bDllInjected;
	bool bMapping;

	union {
		char* dllsToShadow;
		PDLL_TRACK_INFO dllToTrack;
	};

	//Used for EAC protection bypass
	bool lock;

	_PROC_INFO() {
		RtlZeroMemory(this, sizeof(*this));
#ifdef _KERNEL_MODE
		dllsToShadow = (char*)cpp::kMalloc(MAX_PATH);
#endif
	}

	__forceinline bool operator==(_PROC_INFO& rhs) {
		return !memcmp(&rhs, this, sizeof(rhs));
	}
	__forceinline bool operator!=(_PROC_INFO& rhs) {
		return !(*this == rhs);
	}
} PROC_INFO, * PPROC_INFO;

typedef struct _MEMORY_INFO {
	unsigned long long opSrcAddr;
	unsigned long long opDstAddr;
	unsigned long long opSize;
} MEMORY_INFO, * PMEMORY_INFO;

typedef struct _SCORE_INFO {
	DWORD score;
	DWORD halfLife;
	unsigned long long warningScore;
	unsigned long long untrustedScore;
	PBOOLEAN pDetected;
	PBOOLEAN pWarned;
} SCORE_INFO, * PSCORE_INFO;

typedef struct _BLOCK_INFO {
	char* pName;
	unsigned long long score;
} BLOCK_INFO, * PBLOCK_INFO;

typedef struct _BUGCHECK_INFO_EX {
	unsigned int ulBugCheckCode;
	wchar_t* pCaption;
	wchar_t* pMessage;
	wchar_t* pLink;
	unsigned int bgColor;

	__int64 pBugCheckTrampoline;
} BUGCHECK_INFO_EX, * PBUGCHECK_INFO_EX;

typedef struct _MOD_BACKUP_INFO {
	PVOID pBase;
	unsigned long long szMod;
	unsigned long long pEprocess;
} MOD_BACKUP_INFO, *PMOD_BACKUP_INFO;

typedef struct _KERNEL_REQUEST
{
	DWORD64 identifier;
	MEMORY_INFO memoryInfo;
	PROC_INFO procInfo;
	COMM_CODE instructionID;
	BUGCHECK_INFO_EX bugCheckInfo;
	SCORE_INFO scoreInfo;
	BLOCK_INFO blockInfo;
	union {
		unsigned long long seed;
		unsigned long long pIdentityMapping;
	};

	_KERNEL_REQUEST() {
		memset(this, 0, sizeof(*this));
		Init();
	}

	void Init() {
		identifier = 0xdeaddeadbeefbeef;
	}

	bool IsValid() {
		return identifier == 0xdeaddeadbeefbeef;
	}
} KERNEL_REQUEST, * PKERNEL_REQUEST;

typedef enum _CUSTOM_VMCALL_CODE : unsigned long long {
	VMCALL_GET_CALLBACK = 0x9898,
	VMCALL_ENABLE_CR3_EXIT,
	VMCALL_GET_HV_BUILD_FLAGS,
	VMCALL_GET_INFO
} CUSTOM_VMCALL_CODE, * PCUSTOM_VMCALL_CODE;

enum VMCALL_TYPE {
	VMCALL_TEST = 0x1,
	VMCALL_VMXOFF,
	VMCALL_INVEPT_CONTEXT,
	VMCALL_HOOK_PAGE,
	VMCALL_UNHOOK_PAGE,
	VMCALL_HOOK_PAGE_RANGE,
	VMCALL_HOOK_PAGE_INDEX,
	VMCALL_SUBSTITUTE_PAGE,
	VMCALL_CRASH,               //Test VMCALL
	VMCALL_PROBE,               //Test VMCALL
	VMCALL_READ_VIRT,
	VMCALL_WRITE_VIRT,
	VMCALL_READ_PHY,
	VMCALL_WRITE_PHY,
	VMCALL_DISABLE_EPT,
	VMCALL_SET_COMM_KEY,
	VMCALL_GET_CR3,
	VMCALL_GET_EPT_BASE,
	VMCALL_VIRT_TO_PHY,
	VMCALL_STORAGE_QUERY,
	VMCALL_SET_EPT_BASE,
	VMCALL_GET_CR3_ROOT,
	VMCALL_ENABLE_EPT,
	VMCALL_GET_VMCB
};

typedef struct _READ_DATA {
	PVOID pOutBuf;
	PVOID pTarget;
	UINT64 length;
} READ_DATA, * PREAD_DATA;

typedef struct _WRITE_DATA {
	PVOID pInBuf;
	PVOID pTarget;
	UINT64 length;
} WRITE_DATA, * PWRITE_DATA;

typedef struct _CR3_DATA {
	UINT64 value;
} CR3_DATA, *PCR3_DATA;

typedef struct _TRANSLATION_DATA {
	PVOID va;
	UINT64 pa;
} TRANSLATION_DATA, *PTRANSLATION_DATA;

typedef struct _STORAGE_DATA {
	UINT64 id;
	union {
		PVOID pvoid;
		UINT64 uint64;
	};
	BOOLEAN bWrite;
} STORAGE_DATA, *PSTORAGE_DATA;

typedef union _COMMAND_DATA {
	READ_DATA read;
	WRITE_DATA write;
	CR3_DATA cr3;
	TRANSLATION_DATA translation;
	STORAGE_DATA storage;
	PVOID handler;
	UINT64 pa;
} COMMAND_DATA, * PCOMMAND_DATA;

enum VMX_ROOT_ERROR : unsigned long long
{
	SUCCESS,
	PML4E_NOT_PRESENT,
	PDPTE_NOT_PRESENT,
	PDE_NOT_PRESENT,
	PTE_NOT_PRESENT,
	VMXROOT_TRANSLATE_FAILURE,
	INVALID_SELF_REF_PML4E,
	INVALID_MAPPING_PML4E,
	INVALID_HOST_VIRTUAL,
	INVALID_GUEST_PHYSICAL,
	INVALID_GUEST_VIRTUAL,
	PAGE_TABLE_INIT_FAILED,
	PAGE_FAULT,
	INVALID_GUEST_PARAM
};

enum VMX_ROOT_STORAGE : unsigned long long
{
	CALLBACK_ADDRESS = 0,
	EPT_HANDLER_ADDRESS,
	EPT_OS_INIT_BITMAP,
	EPT_OS_INIT_BITMAP_END = EPT_OS_INIT_BITMAP + 8,
	DRIVER_BASE_PA,
	NTOSKRNL_CR3,
	CURRENT_CONTROLLER_PROCESS,
	MAX_STORAGE = 127
};

#pragma pack(pop)
