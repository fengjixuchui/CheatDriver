#pragma once

#define _USER_MODE
#define DEBUG

#ifndef DEBUG
#define Log(x, ...)
#else
#define Log(x, ...) printf(x "\n", __VA_ARGS__)
#endif

#include <Windows.h>