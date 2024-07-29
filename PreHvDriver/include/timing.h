#pragma once
#include "cpp.h"
#include "data.h"

#define NS100_TO_S(ns) ((ns) / 10000000)
#define S_TO_NS100(s) ((s) * 10000000)

namespace timing {
	bool IsDelayedCall(DWORD64 delay);
	bool IsDelayedCall(DWORD64 start, DWORD64 delay);

	LARGE_INTEGER CurrentTime();
}