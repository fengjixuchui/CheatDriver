#pragma once

#ifdef _KERNEL_MODE
#include <ntddk.h>

#define DbgLog(x, ...) DbgPrintEx(0, 0, x "\n", __VA_ARGS__)
#else
#include <Windows.h>
#include <iostream>

#ifdef _DEBUG
#define DbgLog(x, ...) printf(x "\n", __VA_ARGS__)
#else
#define DbgLog(x, ...)
#endif
#endif