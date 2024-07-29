#pragma once

#ifndef _KERNEL_MODE

#include <Windows.h>
#include <cstdint>

#include "Arch/Registers.h"
#include "Arch/Pte.h"
#include "log.h"

#define GAME_PTR uint64_t
#define PAGE_SIZE 0x1000
#define PAGE_ALIGN(Va) ((PVOID)((ULONG_PTR)(Va) & ~(PAGE_SIZE - 1)))

union _VIRT_ADD_MAPPING
{
	unsigned long long Flags;
	struct
	{
		unsigned long long offset_4kb : 12;
		unsigned long long pt_index : 9;
		unsigned long long pd_index : 9;
		unsigned long long pdpt_index : 9;
		unsigned long long pml4_index : 9;
		unsigned long long reserved : 16;
	};

	struct
	{
		unsigned long long offset_2mb : 21;
		unsigned long long pd_index : 9;
		unsigned long long pdpt_index : 9;
		unsigned long long pml4_index : 9;
		unsigned long long reserved : 16;
	};

	struct
	{
		unsigned long long offset_1gb : 30;
		unsigned long long pdpt_index : 9;
		unsigned long long pml4_index : 9;
		unsigned long long reserved : 16;
	};
};

class IdentityMapping {
private:
	PVOID _pIdentity;
	unsigned long long* _pGameCr3;

public:
	IdentityMapping() : _pIdentity(0), _pGameCr3(0) {};
	IdentityMapping(PVOID pIdentity, unsigned long long* pGameCr3) : _pIdentity(pIdentity), _pGameCr3(pGameCr3) {};

	void Init(PVOID pIdentity, unsigned long long* pGameCr3) {
		_pIdentity = pIdentity;
		_pGameCr3 = pGameCr3;
	}

	template <typename Type>
	typename Type::Layout* PhyToVirt(const Type phys)
	{
		uint64_t t = *(uint64_t*)&phys;
		return (typename Type::Layout*)(((uint64_t)(_pIdentity)+t));
	}
	void* PhyToVirt(uintptr_t phys)
	{
		uint64_t t = *(uint64_t*)&phys;
		return (void*)(((uint64_t)(_pIdentity)+t));
	}
#define IS_PT_BAD(pt) ((!pt->layout.P || !pt->layout.A || !pt->layout.US))
	unsigned long long VirtToPhy(uintptr_t virtualAddress)
	{
		__try {
			constexpr auto k_mode = Pte::Mode::longMode4Level;

			using LinearAddress = Pte::LinearAddress<k_mode>;
			using Tables = Pte::Tables<k_mode>;

			LinearAddress addr;
			addr.raw = virtualAddress;

			Regs::Cr3<Regs::Mode::longMode> cr3;
			cr3.raw = *_pGameCr3;
			const auto cr3Pfn = cr3.paging4Level.PML4;

			const auto* const pml4e = PhyToVirt(Tables::pml4e(cr3Pfn, addr));

			//P is for Present, not 'P YOU SICK FUCKS
			if (!pml4e->layout.P)
			{
				DbgLog("PML4 entry not present: 0x%llx", virtualAddress);
				return 0;
			}

			auto physPdpe = pml4e->pdpe(addr);
			_VIRT_ADD_MAPPING map = { 0 };
			map.Flags = physPdpe.physicalAddress;
			if (map.pml4_index != 0
				|| map.reserved != 0) 
			{
				DbgLog("PDPT entry pfn: 0x%llx", virtualAddress);
				return 0;
			}

			const auto* const pdpe = PhyToVirt(physPdpe);
			map.Flags = pdpe->nonPse.pde(addr).physicalAddress;
			if (IS_PT_BAD(pdpe) || pdpe->nonPse.layout.PD == 0)
			{
				DbgLog("PDPT entry invalid: 0x%llx", virtualAddress);
				return 0;
			}
			if (map.pml4_index != 0
				|| map.reserved != 0)
			{
				DbgLog("PDPT entry pfn: 0x%llx", virtualAddress);
				return 0;
			}

			switch (pdpe->pageSize())
			{
			case Pte::PageSize::nonPse:
			{
				const auto* const pde = PhyToVirt(pdpe->nonPse.pde(addr));
				map.Flags = pde->nonPse.pte(addr).physicalAddress;
				if (IS_PT_BAD(pde))
				{
					DbgLog("PDT entry invalid: 0x%llx", virtualAddress);
					return 0;
				}
				if (map.pml4_index != 0
					|| map.reserved != 0)
				{
					DbgLog("PDT entry pfn: 0x%llx", virtualAddress);
					return 0;
				}

				switch (pde->pageSize())
				{
				case Pte::PageSize::nonPse:
				{
					// 4Kb:
					const auto* const pte = PhyToVirt(pde->nonPse.pte(addr));
					if (!pte->page4Kb.P)
					{
						DbgLog("PT entry not present: 0x%llx", virtualAddress);
						return 0;
					}
					const auto phys = pte->physicalAddress(addr);
					return phys.physicalAddress;
				}
				case Pte::PageSize::pse:
				{
					// 2Mb:
					const auto phys = pde->pse.physicalAddress(addr);
					if (!pde->pse.page2Mb.P) //?
						return 0;
					return phys.physicalAddress;
				}
				}
				break;
			}
			case Pte::PageSize::pse:
			{
				// 1Gb:
				const auto phys = pdpe->pse.physicalAddress(addr);
				if (!pdpe->pse.page1Gb.P) //?
					return 0;
				return phys.physicalAddress;
			}
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			DbgLog("Exception while translating: 0x%llx", virtualAddress);
		}

		return 0; // Invalid translation
	}

	bool Read(GAME_PTR address, void* buffer, size_t size)
	{
		while (size) {
			unsigned long long destSize = PAGE_SIZE - _VIRT_ADD_MAPPING{ (unsigned long long)address }.offset_4kb;
			if (size < destSize)
				destSize = size;

			unsigned long long srcSize = PAGE_SIZE - _VIRT_ADD_MAPPING{ (unsigned long long)buffer }.offset_4kb;
			if (size < srcSize)
				srcSize = size;
			uintptr_t physAddr = 0;

			__try {
				physAddr = VirtToPhy(address);

				if (!physAddr) {
					DbgLog("Could not get pa: 0x%llx", address);
					return 0;
				}

				unsigned long long currentSize = min(destSize, srcSize);
				memcpy(buffer, PhyToVirt(physAddr), currentSize);

				buffer = (PVOID)((unsigned long long)buffer + currentSize);
				address = (uintptr_t)((unsigned long long)address + currentSize);
				size -= currentSize;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
				DbgLog("Exception while reading: 0x%llx", address);
				return false;
			}
		}

		return true;
	}
	bool Write(GAME_PTR address, const void* buffer, size_t size)
	{
		while (size) {
			unsigned long long destSize = PAGE_SIZE - _VIRT_ADD_MAPPING{ (unsigned long long)address }.offset_4kb;
			if (size < destSize)
				destSize = size;

			unsigned long long srcSize = PAGE_SIZE - _VIRT_ADD_MAPPING{ (unsigned long long)buffer }.offset_4kb;
			if (size < srcSize)
				srcSize = size;
			__try {
				uintptr_t physAddr = 0;
				physAddr = VirtToPhy(address);
				if (!physAddr) {
					DbgLog("Could not get pa: 0x%llx", address);
					return 0;
				}

				unsigned long long currentSize = min(destSize, srcSize);
				memcpy(PhyToVirt(physAddr), buffer, currentSize);

				buffer = (PVOID)((unsigned long long)buffer + currentSize);
				address = (uintptr_t)((unsigned long long)address + currentSize);
				size -= currentSize;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) {
				DbgLog("Exception while writing: 0x%llx", address);
				return false;
			}
		}

		return true;
	}

	template<typename T>
	T Read(GAME_PTR address) {
		T t;
		Read(address, &t, sizeof(t));
		return t;
	}

	template<typename T>
	bool Write(GAME_PTR address, T& obj) {
		return Write(address, &obj, sizeof(obj));
	}
	template<typename T>
	bool Write(GAME_PTR address, T&& obj) {
		return Write(address, &obj, sizeof(obj));
	}

	size_t IdentityVirtToPhy(size_t identityVirt) {
		return identityVirt - (size_t)_pIdentity;
	}

	PVOID VirtToIdentityVirt(size_t va) {
		return PhyToVirt(VirtToPhy(va));
	}
};

#endif
