#include "timing.h"

bool timing::IsDelayedCall(DWORD64 delay)
{
    DWORD64 callTime = (DWORD64)SKLib::pUserInfo->callTime.dwLowDateTime | ((DWORD64)SKLib::pUserInfo->callTime.dwHighDateTime << 32);
    return IsDelayedCall(callTime, delay);
}

bool timing::IsDelayedCall(DWORD64 start, DWORD64 delay)
{
    DWORD64 nowTicks = timing::CurrentTime().QuadPart;

    if ((nowTicks - start) >= S_TO_NS100(delay)) {
        return true;
    }
    return false;
}

LARGE_INTEGER timing::CurrentTime()
{
    LARGE_INTEGER currTime = { 0 };
    KeQuerySystemTime(&currTime);
    return currTime;
}
