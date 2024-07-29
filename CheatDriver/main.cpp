#include "main.h"
#include "spoof.h"
#include "comms.h"
#include "iommu.h"

#pragma warning (disable:4302)
#pragma warning (disable:4311)

NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObj, PUNICODE_STRING pRegistryPath) {
    SKLib::Init();
    DbgMsg("[ENTRY] Current driver name: %ls", SKLib::CurrentDriverName);

    if (!MmIsAddressValid(SKLib::pUserInfo)
        //|| !MmIsAddressValid(SKLib::pUserInfo->cleanupData.pPreHv)
        ) {
        DbgMsg("[ENTRY] User info is invalid: %p", SKLib::pUserInfo);
        return SKLIB_USER_INFO_INVALID;
    }
    *SKLib::pUserInfo = *(USERMODE_INFO*)pRegistryPath;

    offsets = SKLib::pUserInfo->offsets;

    winternl::InitImageInfo(pDriverObj);

    identity::Init();

    if (SKLib::pUserInfo->pIdtCopy) {
        KeSetSystemAffinityThread(1ull << SKLib::pUserInfo->cpuIdx);
        IDTGateDescriptor64* pOrigIDT = (IDTGateDescriptor64*)CPU::GetIdtBase();
        Memory::WriteProtected(pOrigIDT, SKLib::pUserInfo->pIdtCopy, 20 * sizeof(IDTGateDescriptor64));
        DbgMsg("[POST-HV] Restored modified IDT for core: 0x%llx", SKLib::pUserInfo->cpuIdx);
        KeRevertToUserAffinityThread();

        KAFFINITY affinity = { 0 };
        affinity = (1ULL << CPU::GetCPUCount()) - 1;
        ULONG ulLen = 0;
        winternl::ZwSetInformationProcess(NtCurrentProcess(), PROCESSINFOCLASS::ProcessAffinityMask, &affinity, sizeof(affinity));
    }

    if (SKLib::pUserInfo->cleanupData.pDriverName[0]) {
        if (SKLib::pUserInfo->cleanupData.hDevice) {
            if (!winternl::ClearMmUnloadedDrivers(SKLib::pUserInfo->cleanupData.hDevice)) {
                DbgMsg("[CLEANUP] MmUnloadedDrivers could not be cleared!");
            }
        }
        if (SKLib::pUserInfo->cleanupData.dwTimestamp) {
            //On windows 11 this causes a kernel security bugcheck, do this cleanup inside the loader!
            if (!winternl::ClearPIDDBCacheTable(SKLib::pUserInfo->cleanupData.pDriverName, SKLib::pUserInfo->cleanupData.dwTimestamp)) {
                DbgMsg("[CLEANUP] PIDDBCacheTable could not be cleared!");
            }
        }
        if (!winternl::ClearKernelHashBucketList(SKLib::pUserInfo->cleanupData.pDriverName)) {
            DbgMsg("[CLEANUP] KernelHashBucketList could not be cleared!");
        }
    }

    vmm::Init();

    if (!iommu::Init()) {
        DbgMsg("[DMA] Failed initializing DMA protection!");
        return SKLIB_IOMMU_NOT_PRESENT;
    }

    if (!EPT::HideDriver()) {
        DbgMsg("[EPT] Could not hide driver!");
        return SKLIB_EPT_FAILED;
    }
    else {
        DbgMsg("[EPT] Driver hidden!");
    }

    if (comms::Init()) {
        SKLib::pUserInfo->callbackAddress = (ULONG64)comms::Entry;
    }
    else {
        DbgMsg("[COMMS] Failed initialising!");
        return SKLIB_SETUP_FAILED;
    }

    if (SKLib::pUserInfo->spooferSeed) {
        NTSTATUS spoofStatus = spoofer::SpoofAll(SKLib::pUserInfo->spooferSeed);
        if (spoofStatus != STATUS_SUCCESS) {
            DbgMsg("[SPOOFER] Failed to spoof machine!");
            return spoofStatus;
        }
    }

    vmm::Virtualise();

    if (!CPU::IsHypervOn(vmcall::GetCommunicationKey())) {
        DbgMsg("[VIRTUALIZATION] Failed virtualizing!");
        return SKLIB_VIRTUALIZATION_FAILED;
    }

    paging::RestoreMapPage();

    winternl::FixSectionPermissions();

    if (MmIsAddressValid(SKLib::pUserInfo->cleanupData.pPreHv)) {
        threading::Sleep(1000);
        PE pe(SKLib::pUserInfo->cleanupData.pPreHv);
        RtlZeroMemory(SKLib::pUserInfo->cleanupData.pPreHv, pe.imageSize());
        ExFreePool(SKLib::pUserInfo->cleanupData.pPreHv);
        DbgMsg("[CLEANUP] Cleaned up pre-hv driver!");
    }

    return STATUS_SUCCESS;
}