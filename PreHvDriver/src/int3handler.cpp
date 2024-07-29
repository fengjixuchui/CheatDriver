#include "int3handler.h"
#include "paging.h"

#define MAX_INFO_BUFFER_SZ 256

Spinlock reqLock;
COMMS_INFO commsInfos[MAX_INFO_BUFFER_SZ] = { DB_NULL };
DWORD64 currCommsInfo = 0;
bool int3::bCommsStopRequested = false;

DWORD64 registeredCallback = 0;
PVOID pImageBase = 0;

void int3::Init()
{
	pImageBase = (char*)winternl::PsGetProcessSectionBaseAddress(CurrentProcess()) + PAGE_SIZE;
}

void int3::Handler(PIDT_REGS regs)
{
	if (!paging::GetPPTE(paging::GetPML4Base(), pImageBase, TRUE))
		return;

	DB_CODE code = (DB_CODE)regs->rcx;

	switch (code) {
	case DB_STOP: {
		bCommsStopRequested = true;
		break;
	}
	case DB_MAP:
	case DB_ALLOCATE: {
		commsInfos[currCommsInfo].pDbInfo = (DB_INFO*)regs->rdx;
		commsInfos[currCommsInfo].code = code;
		commsInfos[currCommsInfo].cr3 = __readcr3();
		currCommsInfo++;
		regs->rax = 0xdeaddead;
		break;
	}
	case DB_REGISTER_CALLBACK: {
		registeredCallback = regs->rdx;
		break;
	}
	case DB_TEST: {
		break;
	}
	default: {
		if(registeredCallback)
			regs->rip = registeredCallback;
		break;
	}
	}
}

void int3::RegisterCallback(PVOID callback)
{
	registeredCallback = (DWORD64)callback;
}

COMMS_INFO int3::GetLatestRequest()
{
	reqLock.Lock();
	COMMS_INFO commsInfo;
	commsInfo.pDbInfo = 0;
	commsInfo.cr3 = 0;

	if (!currCommsInfo)
		goto _end;

	commsInfo = commsInfos[currCommsInfo - 1];
	RtlZeroMemory(&commsInfos[currCommsInfo - 1], sizeof(commsInfos[currCommsInfo - 1]));
	currCommsInfo--;
_end:
	reqLock.Unlock();
	return commsInfo;
}

void int3::Test(volatile DWORD64 testParam)
{
	__debugbreak();
}
