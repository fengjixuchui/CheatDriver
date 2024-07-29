#pragma once

#include "common.h"
#include "map_driver.hpp"
#include "umspoof.hpp"
#include "Setup.hpp"

constexpr bool bEPTHide = false;

namespace loader {
	bool LoadSKLib(DWORD64 hyperKey = 0, ULONG64* pCallbackAddress = nullptr, bool bLoadPreHv = false);
	bool UnloadSKLib();
}