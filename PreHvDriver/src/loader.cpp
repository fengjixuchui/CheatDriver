#include "loader.h"

PVOID loader::MapFromDisk(PVOID pBuffer)
{
	if (!MmIsAddressValid(pBuffer))
		return nullptr;

	PE peDisk(pBuffer);
	char* pNewMap = (char*)cpp::kMalloc(peDisk.imageSize());
	RtlZeroMemory(pNewMap, peDisk.imageSize());

	for (auto& section : peDisk.sections()) {
		RtlCopyMemory(pNewMap + section.VirtualAddress, (char*)pBuffer + section.PointerToRawData, section.SizeOfRawData);

		ULONG protect = section.Characteristics & IMAGE_SCN_MEM_EXECUTE ? PAGE_EXECUTE_READ : PAGE_READWRITE;
		cpp::kProtect(pNewMap + section.VirtualAddress, section.SizeOfRawData, protect);
	}

	PE peMem(pNewMap);

	peMem.relocate();
	peMem.fixImports((DWORD64)PsGetProcessPeb(PsGetCurrentProcess()));

	return (PVOID)(pNewMap + peMem.ntHeaders()->OptionalHeader.AddressOfEntryPoint);
}
