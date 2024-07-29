#include <SKLib.h>
#include <IDT.h>
#include <threading.h>

#include "timing.h"
#include "comms.h"
#include <acpi.h>

NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObj, PUNICODE_STRING pRegistryPath) {
    SKLib::Init();
    DbgMsg("[ENTRY] Current driver name: %ls", SKLib::CurrentDriverName);

    *SKLib::pUserInfo = *(USERMODE_INFO*)pRegistryPath;
    if (!MmIsAddressValid(SKLib::pUserInfo)) {
        DbgMsg("[ENTRY] User info is invalid: %p", SKLib::pUserInfo);
        return SKLIB_USER_INFO_INVALID;
    }

    offsets = SKLib::pUserInfo->offsets;

    winternl::InitImageInfo(pDriverObj);

    if (!SKLib::pUserInfo->cpuIdx) {
        SKLib::pUserInfo->cpuIdx = random::Next(1, CPU::GetCPUCount() - 2);
        KAFFINITY affinity = { 0 };
        affinity = 1ull << SKLib::pUserInfo->cpuIdx;
        ULONG ulLen = 0;
        NTSTATUS ntStatus = winternl::ZwSetInformationProcess(NtCurrentProcess(), PROCESSINFOCLASS::ProcessAffinityMask, &affinity, sizeof(affinity));

        if (!NT_SUCCESS(ntStatus)) {
            DbgMsg("[ENTRY] Failed setting process affinity: 0x%x", ntStatus);
            return SKLIB_LOAD_FAILED;
        }

        KeSetSystemAffinityThread(affinity);
    }

    if (timing::IsDelayedCall(10)) {
        KeRevertToUserAffinityThread();
        return SKLIB_DETECTION(1);
    }

    int3::Init();

    SKLib::pUserInfo->pIdtCopy = cpp::kMallocTryAll(PAGE_SIZE);
    int3::RegisterCallback(SKLib::pUserInfo->pDetectionCallback);
    IDT currIdt;
    IDTGateDescriptor64* pOrigIDT = (IDTGateDescriptor64*)CPU::GetIdtBase();
    RtlCopyMemory(SKLib::pUserInfo->pIdtCopy, pOrigIDT, PAGE_SIZE);

    currIdt.setup(pOrigIDT);
    currIdt.setup_entry(EXCEPTION_DEBUG, __db_handler);
    currIdt.setup_entry(EXCEPTION_INT3, int3::__custom_handler);
    currIdt.save();

    LARGE_INTEGER preBreakTime = timing::CurrentTime();
    int3::Test();
    threading::Sleep(10);
    if (timing::IsDelayedCall(preBreakTime.QuadPart, 1)) {
        KeRevertToUserAffinityThread();
        return SKLIB_DETECTION(2);
    }

    preBreakTime = timing::CurrentTime();
    RFLAGS flags = { 0 };
    flags.Flags = CPU::GetRflags();
    flags.TrapFlag = true;
    //CPU::SetRflags(flags.Flags);

    if (timing::IsDelayedCall(preBreakTime.QuadPart, 1)) {
        KeRevertToUserAffinityThread();
        return SKLIB_DETECTION(3);
    }

#ifndef DEBUG_BUILD
    if (!acpi::Init()) {
        KeRevertToUserAffinityThread();
        return SKLIB_IOMMU_NOT_PRESENT;
    }
#endif

    if (!CPU::IsVirtSupported()) {
        KeRevertToUserAffinityThread();
        return SKLIB_VIRTUALIZATION_FAILED;
    }

    threading::Thread comms(comms::CommsThread, 0);

    FILETIME outTime = { 0 };
    *(DWORD32*)(&outTime) = timing::CurrentTime().LowPart;
    SKLib::pUserInfo->callTime = outTime;
    *(USERMODE_INFO*)pRegistryPath = *SKLib::pUserInfo;
    return STATUS_SUCCESS;
}