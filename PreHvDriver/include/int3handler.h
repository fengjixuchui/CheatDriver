#pragma once
#include <IDT.h>

typedef enum _DB_CODE : ULONG64 {
	DB_NULL = 0,
	DB_MAP = 0xdeada55,
	DB_ALLOCATE,
	DB_REGISTER_CALLBACK,
	DB_TEST,
	DB_STOP
} DB_CODE, *PDB_CODE;

#pragma pack(push, 1)
typedef union _DB_INFO {
	struct {
		SIZE_T sz;
		DWORD64 pa;
		PVOID* pOut;
		NTSTATUS* pNtStatus;
	} map;
	struct {
		SIZE_T sz;
		PVOID* pOut;
	} allocate;
} DB_INFO;

typedef struct _COMMS_INFO {
	DB_CODE code;
	DWORD64 cr3;
	DB_INFO* pDbInfo;
} COMMS_INFO, *PCOMMS_INFO;
#pragma pack(pop)

typedef void (*fnInt3Callback)();

namespace int3 {
	void Init();
	extern "C" void Handler(PIDT_REGS regs);
	extern "C" void __custom_handler();
	extern bool bCommsStopRequested;

	void RegisterCallback(PVOID callback);
	COMMS_INFO GetLatestRequest();
	void Test(volatile DWORD64 testParam = DB_TEST);
}