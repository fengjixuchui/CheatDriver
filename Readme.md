# Requirements

Virtualization enabled in BIOS.  
IOMMU/VT-d enabled in BIOS.  
AMD or Intel CPU with SLAT.  
Driver blocklist disabled.  
HVCI disabled.  
msdia140.dll registered.  

```
git submodule update --init --recursive
```
in a vs command prompt

Latest WDK installed.

# Compilation
You can find pre-compiled versions in the github action artifacts.
You can also just open in vs2022 and it should build properly.

# How does it work?
This is an example solution for vs2022 leveraging [SKLib](https://github.com/cutecatsandvirtualmachines/SKLib.git).

It contains 2 drivers, namely CheatDriver and PreHvDriver, which will be explained further in a second.
It also contains a sample loader leveraging physmeme_lib and a sample internal named TestDll, which *was* a working Rust cheat.

## CheatDriver
During initialization it will set up SKLib, and fetch some offsets and other data from the usermode loader using a passed pointer:
https://github.com/cutecatsandvirtualmachines/CheatDriver/blob/31630c6ef5b71f950e5bdf01cd9312a8d8717ded/CheatDriver/main.cpp#L21

It will also perform post exploitation cleanup for PIDDB cache, MmUnloadedDrivers, KernelHashBucketList, and eventually Defender list:
https://github.com/cutecatsandvirtualmachines/CheatDriver/blob/31630c6ef5b71f950e5bdf01cd9312a8d8717ded/CheatDriver/main.cpp#L40

Next on it will use SKLib-v to initialize the vm, initialize the IOMMU and leverage them
to hide the entire module from memory accesses:
https://github.com/cutecatsandvirtualmachines/CheatDriver/blob/31630c6ef5b71f950e5bdf01cd9312a8d8717ded/CheatDriver/main.cpp#L57

The memory hiding is also set up in debug mode instead of "zero-out" mode, where fake pages will be marked with special bytes
indicating offsets and build flags, useful for debugging bsods.

Finally it will initialize all useful SLAT hooks, such as process creation, deletion and thread creation, then, if compiled for the
"ReleaseWithSpoofer" target, it will attempt to spoof:
*SMBIOS, GPU, NIC, Volume, Disk, Monitor, USB and EFI*

Before exiting it will start virtualization and do some final cleanup:
https://github.com/cutecatsandvirtualmachines/CheatDriver/blob/31630c6ef5b71f950e5bdf01cd9312a8d8717ded/CheatDriver/main.cpp#L88

### Features
It is capable of:
- Hiding a window (handle) from a specific process or all processes in the system
- Detect and block attempts at running certain software (re tools, debuggers, etc.)
- Decrypt CR3 for EAC
- UM SLAT hiding (has many drawbacks and it's usually slow unless you know 100% what you're doing)
- Automatically map and hide DLL inside target process
- Ability to target and track multiple processes
- Customizable vmcall interface
- Customizable direct call interface for main communication handler
- Set up identity mapping for a target process and use that in usermode to do r/w with 0 transition costs to kernel
- Protect processes from handle accesses

#### Note:
Some of these features are disabled/commented out.

### Documentation
https://github.com/cutecatsandvirtualmachines/CheatDriver/blob/master/CheatDriver/Docs.md
https://github.com/cutecatsandvirtualmachines/CheatDriver/blob/master/CheatDriver/Calls.md

## PreHvDriver
This is a second, additional and completely optional-to-load driver that performs some simple checks and preparation steps to avoid re,
debug and analysis attempts in general to this piece of software.

It uses various techniques such as:
- Timing checks for debuggers
- IDT partial temporary destruction to block debuggers from working
- Custom memory mapper that allocates memory for the main driver
- Custom communication engine based on int3 handling from previous IDT modifications

It's a sample of what a custom kernel protection system could be like leveraging SKLib.
