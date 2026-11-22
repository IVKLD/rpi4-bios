/** @file
  ARM64 Pre-boot Hardware Diagnostic Module Header.

  Copyright (c) 2026, Master's Thesis Project. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef _DIAG_MODULE_H_
#define _DIAG_MODULE_H_

#include <PiDxe.h>
#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/DebugLib.h>

#include <Pi/PiMultiPhase.h>
#include <Protocol/MpService.h>
#include <Protocol/PciRootBridgeIo.h>
#include <Protocol/BlockIo.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/ShellParameters.h>
#include <Guid/FileInfo.h>

#include <Guid/Acpi.h>
#include <Guid/SmBios.h>
#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/SmBios.h>

//
// Device Tree (FDT) Table Definition
//
#define EFI_FDT_TABLE_GUID \
  { 0xb1b621d5, 0xf19c, 0x41a5, { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 } }

#pragma pack(1)
typedef struct {
  UINT32  Magic;             // 0xd00dfeed (big-endian)
  UINT32  TotalSize;
  UINT32  OffDtStruct;
  UINT32  OffDtStrings;
  UINT32  OffMemRsvmap;
  UINT32  Version;
  UINT32  LastCompVersion;
  UINT32  BootCpuidPhys;
  UINT32  SizeDtStrings;
  UINT32  SizeDtStruct;
} FDT_HEADER;
#pragma pack()

//
// ANSI Colors for Console UI and Serial Port
//
#define COLOR_RESET       L"\033[0m"
#define COLOR_RED         L"\033[1;31m"
#define COLOR_GREEN       L"\033[1;32m"
#define COLOR_YELLOW      L"\033[1;33m"
#define COLOR_CYAN        L"\033[1;36m"
#define COLOR_WHITE_BOLD  L"\033[1;37m"

//
// Precise failure pinpoint structure for memory & bus lines
//
typedef struct {
  BOOLEAN HasFault;
  UINT64  FaultAddress;
  UINT64  Expected;
  UINT64  Actual;
  UINT32  FailingBit;
} RAM_FAULT_INFO;

typedef struct {
  UINT32         PassCount;
  UINT32         FailCount;
  UINT32         WarnCount;
  BOOLEAN        FaultInjectionEnabled;
  EFI_HANDLE     ImageHandle;
  UINT64         TimerFreqHz;
  UINTN          RamTestSizeMB;
  BOOLEAN        BurnInMode;
  RAM_FAULT_INFO LastFault;
  UINT64         MarchDurationMs;
  UINT64         MarchSpeedMBps;
  UINT64         WalkingDurationMs;
  UINT64         WalkingSpeedMBps;
  UINT32         PciDeviceCount;
  UINT32         AcpiTablesCount;
  BOOLEAN        FdtPresent;
} DIAG_CONTEXT;

//
// ARM64 Low-level CPU Register structures & functions
//
typedef struct {
  UINT32  Implementer;
  UINT32  Variant;
  UINT32  Architecture;
  UINT32  PartNum;
  UINT32  Revision;
  UINT64  RawMidr;
  UINT64  RawMpidr;
  UINT64  CurrentEL;
  UINT64  Pfr0;
  BOOLEAN HasFP;
  BOOLEAN HasASIMD;
  BOOLEAN HasGIC;
  UINT32  CoreCount;
  CHAR16  CpuName[64];
} ARM64_CPU_INFO;

//
// Inline helpers for ARM Generic Timer
//
STATIC
inline
UINT64
ArmReadCntFrq (
  VOID
  )
{
  UINT64 Val;
  __asm__ volatile ("mrs %0, cntfrq_el0" : "=r" (Val));
  return Val;
}

STATIC
inline
UINT64
ArmReadCntPct (
  VOID
  )
{
  UINT64 Val;
  __asm__ volatile ("mrs %0, cntpct_el0" : "=r" (Val));
  return Val;
}

STATIC
inline
UINT64
ArmGetElapsedMs (
  IN UINT64 StartPct,
  IN UINT64 EndPct,
  IN UINT64 Freq
  )
{
  if (Freq == 0 || EndPct <= StartPct) {
    return 0;
  }
  return ((EndPct - StartPct) * 1000ULL) / Freq;
}

//
// Clean and Invalidate Data Cache lines across virtual address range
//
STATIC
inline
VOID
ArmDataCacheCleanAndInvalidate (
  IN VOID   *Address,
  IN UINTN  Length
  )
{
  UINTN LineSize = 64; // Standard Cortex-A72 cache line
  UINTN Start = (UINTN)Address & ~(LineSize - 1);
  UINTN End = (UINTN)Address + Length;

  for (UINTN Ptr = Start; Ptr < End; Ptr += LineSize) {
    __asm__ volatile ("dc civac, %0" : : "r" (Ptr) : "memory");
  }
  __asm__ volatile ("dsb sy" : : : "memory");
  __asm__ volatile ("isb" : : : "memory");
}

//
// Function Prototypes for Subsystems
//

// Stage 2: CPU & SMP
EFI_STATUS
RunCpuDiagnostics (
  IN OUT DIAG_CONTEXT *Context,
  OUT ARM64_CPU_INFO  *CpuInfo
  );

EFI_STATUS
RunSmpDiagnostics (
  IN OUT DIAG_CONTEXT *Context,
  IN OUT ARM64_CPU_INFO *CpuInfo
  );

// Stage 3: RAM BIST
EFI_STATUS
RunMemoryDiagnostics (
  IN OUT DIAG_CONTEXT *Context
  );

// Stage 4: Buses, ACPI, SMBIOS, FDT, BlockIO
EFI_STATUS
RunPciDiagnostics (
  IN OUT DIAG_CONTEXT *Context
  );

EFI_STATUS
RunAcpiSmbiosDiagnostics (
  IN OUT DIAG_CONTEXT *Context
  );

EFI_STATUS
RunFdtDiagnostics (
  IN OUT DIAG_CONTEXT *Context
  );

EFI_STATUS
RunBlockIoDiagnostics (
  IN OUT DIAG_CONTEXT *Context
  );

// Stage 5: UI, Fault Injection & ESP Disk Reporting
EFI_STATUS
InitializeGopUi (
  VOID
  );

VOID
RunFaultInjectionTest (
  IN OUT DIAG_CONTEXT *Context
  );

EFI_STATUS
SaveDiagnosticReport (
  IN CONST DIAG_CONTEXT *Context,
  IN CONST ARM64_CPU_INFO *CpuInfo
  );

#endif // _DIAG_MODULE_H_
