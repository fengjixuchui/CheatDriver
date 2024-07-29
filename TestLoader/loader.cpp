#include "loader.h"

#include <thread>
#include <fstream>
#include <Psapi.h>

#include "cpu.h"
#include "Vmcall.h"
#include <comms.h>
#include <vdm.hpp>
#include <kernel_ctx/dbrequest.h>
#include <util/util.hpp>
#include <kernel_ctx/kernel_ctx.h>
#include <drv_image/drv_image.h>

SC_HANDLE hSc = NULL;

DWORD64 GetBaseAddress(const HANDLE hProcess) {
    if (hProcess == NULL)
        return NULL; // No access to the process

    HMODULE lphModule[1024]; // Array that receives the list of module handles
    DWORD lpcbNeeded(NULL); // Output of EnumProcessModules, giving the number of bytes requires to store all modules handles in the lphModule array

    if (!EnumProcessModules(hProcess, lphModule, sizeof(lphModule), &lpcbNeeded))
        return NULL; // Impossible to read modules

    TCHAR szModName[MAX_PATH];
    if (!GetModuleFileNameEx(hProcess, lphModule[0], szModName, sizeof(szModName) / sizeof(TCHAR)))
        return NULL; // Impossible to get module info

    return (DWORD64)lphModule[0]; // Module 0 is apparently always the EXE itself, returning its address
}

std::wstring getExecutablePath() {
    wchar_t rawPathName[MAX_PATH];
    GetModuleFileNameW(NULL, rawPathName, MAX_PATH);
    return std::wstring(rawPathName);
}

std::wstring getExecutableDir() {
    std::wstring directory = getExecutablePath();

    directory = directory.substr(0, directory.find_last_of('\\') + 1);

    return directory;
}

bool LoadTestSKLib()
{
    std::wstring dir = getExecutableDir();
    std::wstring name = L"CheatDriver";
    std::wstring path = dir + name + L".sys";

    BOOL bRet = FALSE;

    SC_HANDLE hServiceMgr = NULL;
    SC_HANDLE hServiceDDK = NULL;

    hServiceMgr = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);

    if (hServiceMgr == NULL)
    {

        printf("OpenSCManager() Failed %x!\n", GetLastError());
        bRet = FALSE;
        goto BeforeExit;
    }

    hServiceDDK = CreateServiceW(hServiceMgr,
        name.c_str(),
        name.c_str(),
        SERVICE_ALL_ACCESS,
        SERVICE_KERNEL_DRIVER,
        SERVICE_DEMAND_START,
        SERVICE_ERROR_IGNORE,
        path.c_str(),
        NULL,
        NULL,
        NULL,
        NULL,
        NULL);

    DWORD dwRtn;

    if (hServiceDDK == NULL)
    {
        dwRtn = GetLastError();
        if (dwRtn != ERROR_IO_PENDING && dwRtn != ERROR_SERVICE_EXISTS)
        {
            printf("CrateService() Failed %x!\n", dwRtn);
            bRet = FALSE;
            goto BeforeExit;
        }

        hServiceDDK = OpenService(hServiceMgr, name.c_str(), SERVICE_ALL_ACCESS);
        if (hServiceDDK == NULL)
        {
            dwRtn = GetLastError();
            printf("OpenService() Failed %x!\n", dwRtn);
            bRet = FALSE;
            goto BeforeExit;
        }
    }

    bRet = StartService(hServiceDDK, NULL, NULL);
    if (!bRet)
    {
        DWORD dwRtn = GetLastError();
        DeleteService(hServiceDDK);
        if (dwRtn != ERROR_IO_PENDING && dwRtn != ERROR_SERVICE_ALREADY_RUNNING)
        {
            printf("StartService() Failed %x!\n", dwRtn);
            bRet = FALSE;
            goto BeforeExit;
        }
        else
        {
            if (dwRtn == ERROR_IO_PENDING)
            {
                printf("StartService() Faild ERROR_IO_PENDING !\n");
                bRet = FALSE;
                goto BeforeExit;
            }
            else
            {
                printf("StartService() Faild ERROR_SERVICE_ALREADY_RUNNING !\n");
                bRet = TRUE;
                goto BeforeExit;
            }
        }
    }

    bRet = TRUE;
    printf("Loaded SKLibTest!\n");

BeforeExit:
    if (hServiceDDK)
    {
        hSc = hServiceDDK;
    }
    if (hServiceMgr)
    {
        CloseServiceHandle(hServiceMgr);
    }
    return bRet;
}

void UnloadTestSKLib()
{
    SERVICE_STATUS scStatus = { 0 };
    if (hSc) {
        ControlService(hSc, SERVICE_CONTROL_STOP, &scStatus);
        CloseServiceHandle(hSc);
        printf("Unloaded SKLibTest!\n");
    }
}

void DetectionLog() {
    Log("WOW! You have been detected!");
    system("pause");
    ExitProcess(1);
}

bool loader::LoadSKLib(DWORD64 hyperKey, ULONG64* pCallbackAddress, bool bLoadPreHv)
{
    IMAGE_NT_HEADERS* pOldNtHeader = nullptr;
    IMAGE_OPTIONAL_HEADER* pOldOptHeader = nullptr;
    IMAGE_FILE_HEADER* pOldFileHeader = nullptr;

    pOldNtHeader = reinterpret_cast<IMAGE_NT_HEADERS*>(GetBaseAddress(GetCurrentProcess()) + reinterpret_cast<IMAGE_DOS_HEADER*>(GetBaseAddress(GetCurrentProcess()))->e_lfanew); //e_lfanew is at the bottom of the MZ header and contains the offset to the start of the NT header
    pOldOptHeader = &pOldNtHeader->OptionalHeader; //NT header contains the address of the FileHeader and OptionalHeader, together with a 4-byte signature "PE\0\0"
    pOldFileHeader = &pOldNtHeader->FileHeader;

    NTSTATUS ntStatus = 0;
    if (CPU::IsHypervOn(hyperKey)) {
        Log("Hypervisor already loaded");
        CPU::CPUIDVmCall(0x9898, (ULONG64)pCallbackAddress, 0, hyperKey);

        SKLibVdm vdm(hyperKey, *pCallbackAddress);

        KERNEL_REQUEST kernelRequest;
        kernelRequest.instructionID = INST_HIDE;
        kernelRequest.procInfo.dwPid = (HANDLE)GetCurrentProcessId();

        if (!SetProcessWorkingSetSize(OpenProcess(PROCESS_ALL_ACCESS | PROCESS_SET_QUOTA, FALSE, GetCurrentProcessId()), pOldOptHeader->SizeOfImage, pOldOptHeader->SizeOfImage * 2))
            Log("Failed setting working set size: 0x%x", GetLastError());

        auto* pSectionHeader = IMAGE_FIRST_SECTION(pOldNtHeader);
        for (UINT i = 0; i != pOldFileHeader->NumberOfSections; ++i, ++pSectionHeader) {
            if (pSectionHeader->SizeOfRawData) {
                //Virtual Address is the offset where the section must be loaded in memory from the start of the module
                if (
                    strcmp((char*)pSectionHeader->Name, ".text") == 0
                    ) {
                    kernelRequest.memoryInfo.opDstAddr = (DWORD64)GetBaseAddress(GetCurrentProcess()) + pSectionHeader->VirtualAddress;
                    kernelRequest.memoryInfo.opSize = pSectionHeader->SizeOfRawData;
        
                    if (!vdm.CallbackInvoke(&kernelRequest)) {
                        Log("Failed hiding memory: 0x%llx - 0x%llx", kernelRequest.memoryInfo.opDstAddr, kernelRequest.memoryInfo.opSize);
                    }
                    else {
                        Log("Memory successfully hidden with EPT: 0x%llx - 0x%llx", kernelRequest.memoryInfo.opDstAddr, kernelRequest.memoryInfo.opSize);
                    }
                }
            }
        }

        return true;
    }
    else {
        Log("Hypervisor not loaded!");
    }

    USERMODE_INFO info = { 0 };
#ifdef _KDMAPPED
    if (!setup::InitOffsets(info.offsets)) {
        Log("Could not initialise offsets");
        return false;
    }
    info.loaderProcId = GetCurrentProcessId();
    info.spooferSeed = 0x4712abb38924443;

    physmeme::Init();

    info.cleanupData.hDevice = physmeme::DriverHandle();
    strcpy(info.cleanupData.pDriverName, physmeme::DriverName().c_str());

    Log("Loading pre-hv driver!");
    FILETIME time = { 0 };
    GetSystemTimeAsFileTime(&time);

    info.callTime = time;
    info.pDetectionCallback = DetectionLog;
    uintptr_t base = 0;
    ntStatus = physmeme::map_driver(
        "PreHvDriver.sys",
        (ULONG64)0,
        (ULONG64)&info,
        true,
        false,
        &base
    );

    if (!NT_SUCCESS(ntStatus)) {
        Log("Failed to load pre-hv module: 0x%x", ntStatus);
        DetectionLog();
        return false;
    }

    info.cleanupData.pPreHv = (PVOID)base;
    for (int i = 0; i < 10; i++) {
        if (GetCurrentProcessorNumber() != info.cpuIdx) {
            Log("Manipulation detected!");
            DetectionLog();
            return false;
        }
        Sleep(100);
    }

    std::vector<std::uint8_t> drv_buffer;
    util::open_binary_file("CheatDriver.sys", drv_buffer);
    if (!drv_buffer.size())
    {
        Log("[-] invalid drv_buffer size");
        return false;
    }

    physmeme::drv_image image(drv_buffer);

    PVOID pOut = db::AllocatePool(image.size());
    if (!pOut) {
        Log("Allocation took too long");
        DetectionLog();
        return false;
    }

    db::DbRequest(DB_STOP, 0);

    Log("Allocation at: %p - 0x%llx", pOut, image.size());

    Log("No debugger detected!");
    info.cleanupData.dwTimestamp = util::get_file_header((void*)raw_driver)->TimeDateStamp;
    ntStatus = physmeme::map_driver(
        drv_buffer,
        (ULONG64)0,
        (ULONG64)&info,
        true,
        false,
        (uintptr_t*)&pOut
    );

    if (!NT_SUCCESS(ntStatus)) {
        Log("Failed to load hv module: 0x%x", ntStatus);
        return false;
    }

    physmeme::unload_drv();
#else
    LoadTestSKLib();
#endif

    CPU::CPUIDVmCall(VMCALL_SET_COMM_KEY, hyperKey, 0, 0);
    Log("Correctly set communication key: 0x%llx", hyperKey);

    if (pCallbackAddress)
        *pCallbackAddress = info.callbackAddress;

    SKLibVdm vdm(hyperKey, info.callbackAddress);
#ifdef BUILD_SPOOFER
    KERNEL_REQUEST req;
    req.instructionID = INST_SPOOF;
    req.seed = 0xdeadbeef;
    //vdm.CallbackInvoke(&req);
    spoofer::SpoofAll();
#endif

    return true;
}

bool loader::UnloadSKLib()
{
#ifndef _KDMAPPED
    UnloadTestSKLib();
#endif

    return true;
}