#include "comms.h"
#include <threading.h>
#include <paging.h>

void comms::CommsThread(PVOID pCtx)
{
	int3::bCommsStopRequested = false;

	while (!int3::bCommsStopRequested) {
		threading::Sleep(1);

		COMMS_INFO commsInfo = int3::GetLatestRequest();
		if (!commsInfo.pDbInfo
			|| !commsInfo.cr3)
			continue;

		DbgMsg("[PRE-HV] Got request: 0x%x", commsInfo.code);
		CR3 cr3 = { 0 };
		cr3.Flags = commsInfo.cr3;
		PPML4T pml4 = paging::GetPML4Base(cr3);

		PHYSICAL_ADDRESS pa = { 0 };
		pa.QuadPart = (DWORD64)paging::GuestVirtToPhy(commsInfo.pDbInfo, (PVOID)cr3.Flags);
		DB_INFO* pDbInfo = (DB_INFO*)MmMapIoSpace(pa, sizeof(*pDbInfo), MmCached);

		switch (commsInfo.code) {
		case DB_MAP: {
			NTSTATUS ntStatus = STATUS_UNSUCCESSFUL;

			pa.QuadPart = pDbInfo->map.pa;
			PVOID pMapped = MmMapIoSpace(pa, pDbInfo->map.sz, MmCached);

			auto ppte = paging::GetPPTE(pml4, pDbInfo->map.pOut, true);
			PVOID* pOut = (PVOID*)paging::MapToGuest((PVOID)(ppte->PageFrameNumber * PAGE_SIZE));
			if (pOut)
				*pOut = pMapped;

			ppte = paging::GetPPTE(pml4, pDbInfo->map.pNtStatus, true);
			NTSTATUS* pNtOut = (NTSTATUS*)paging::MapToGuest((PVOID)(ppte->PageFrameNumber * PAGE_SIZE));
			if (pNtOut)
				*pNtOut = ntStatus;
			break;
		}
		case DB_ALLOCATE: {
			DbgMsg("[PRE-HV] Requested allocation for 0x%llx bytes", pDbInfo->allocate.sz);
			PVOID pMapped = cpp::kMalloc(pDbInfo->allocate.sz, PAGE_EXECUTE_READWRITE);
			PVOID* pOut = (PVOID*)paging::MapToGuest(paging::GuestVirtToPhy(pDbInfo->allocate.pOut, (PVOID)cr3.Flags));
			DbgMsg("[PRE-HV] Setting out pointer: %p = %p", pDbInfo->allocate.pOut, pMapped);
			*pOut = pMapped;
			break;
		}
		}

		MmUnmapIoSpace(pDbInfo, sizeof(*pDbInfo));
	}
	DbgMsg("[PRE-HV] Comms thread exiting!");
}
