/** @file
  ARM64 CPU Identification and Multiprocessor (SMP) Diagnostics.

  Copyright (c) 2026, Master's Thesis Project. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "DiagModule.h"

//
// Assembly helpers for reading ARM64 system registers
//
STATIC
UINT64
ArmReadMidr (
  VOID
  )
{
  UINT64 Val;
  __asm__ volatile ("mrs %0, midr_el1" : "=r" (Val));
  return Val;
}

STATIC
UINT64
ArmReadMpidr (
  VOID
  )
{
  UINT64 Val;
  __asm__ volatile ("mrs %0, mpidr_el1" : "=r" (Val));
  return Val;
}

STATIC
UINT64
ArmReadCurrentEl (
  VOID
  )
{
  UINT64 Val;
  __asm__ volatile ("mrs %0, CurrentEL" : "=r" (Val));
  return (Val >> 2) & 0x3;
}

STATIC
UINT64
ArmReadIdAa64Pfr0 (
  VOID
  )
{
  UINT64 Val;
  __asm__ volatile ("mrs %0, id_aa64pfr0_el1" : "=r" (Val));
  return Val;
}

/**
  Performs low-level identification of ARM64 CPU.
**/
EFI_STATUS
RunCpuDiagnostics (
  IN OUT DIAG_CONTEXT *Context,
  OUT ARM64_CPU_INFO  *CpuInfo
  )
{
  Print (L"\n=================================================================\n");
  Print (L" [STAGE 2.1] ARM64 CPU IDENTIFICATION & ARCHITECTURE CHECK\n");
  Print (L"=================================================================\n");

  ZeroMem (CpuInfo, sizeof (ARM64_CPU_INFO));

  CpuInfo->RawMidr   = ArmReadMidr ();
  CpuInfo->RawMpidr  = ArmReadMpidr ();
  CpuInfo->CurrentEL = ArmReadCurrentEl ();
  CpuInfo->Pfr0      = ArmReadIdAa64Pfr0 ();

  //
  // Parse MIDR_EL1:
  // [31:24] Implementer (0x41 = ARM, 0x42 = Broadcom)
  // [23:20] Variant
  // [19:16] Architecture (0xF = defined by CPUID registers)
  // [15:4]  Primary PartNum (0xD08 = Cortex-A72)
  // [3:0]   Revision
  //
  CpuInfo->Implementer  = (UINT32)((CpuInfo->RawMidr >> 24) & 0xFF);
  CpuInfo->Variant      = (UINT32)((CpuInfo->RawMidr >> 20) & 0x0F);
  CpuInfo->Architecture = (UINT32)((CpuInfo->RawMidr >> 16) & 0x0F);
  CpuInfo->PartNum      = (UINT32)((CpuInfo->RawMidr >> 4)  & 0xFFF);
  CpuInfo->Revision     = (UINT32)(CpuInfo->RawMidr & 0x0F);

  // Features from ID_AA64PFR0_EL1
  // [19:16] FP (0x0 = FP supported, 0xF = not supported)
  // [23:20] AdvSIMD (0x0 = ASIMD supported, 0xF = not supported)
  // [27:24] GIC (0x1 = SysReg GIC supported)
  CpuInfo->HasFP   = (((CpuInfo->Pfr0 >> 16) & 0x0F) != 0x0F);
  CpuInfo->HasASIMD= (((CpuInfo->Pfr0 >> 20) & 0x0F) != 0x0F);
  CpuInfo->HasGIC  = (((CpuInfo->Pfr0 >> 24) & 0x0F) >= 0x01);
  CpuInfo->CoreCount = 1;

  Context->TimerFreqHz = ArmReadCntFrq ();

  if ((CpuInfo->Implementer == 0x41 || CpuInfo->Implementer == 0x42) && CpuInfo->PartNum == 0xD08) {
    UnicodeSPrint (CpuInfo->CpuName, sizeof (CpuInfo->CpuName), L"Broadcom BCM2711 / ARM Cortex-A72 (r%dp%d)",
                   CpuInfo->Variant, CpuInfo->Revision);
  } else if (CpuInfo->Implementer == 0x41) {
    UnicodeSPrint (CpuInfo->CpuName, sizeof (CpuInfo->CpuName), L"ARM Limited Part 0x%03X (r%dp%d)",
                   CpuInfo->PartNum, CpuInfo->Variant, CpuInfo->Revision);
  } else {
    UnicodeSPrint (CpuInfo->CpuName, sizeof (CpuInfo->CpuName), L"Unknown Implementer 0x%02X Part 0x%03X",
                   CpuInfo->Implementer, CpuInfo->PartNum);
  }

  Print (L"  - Detected CPU Core   : %s\n", CpuInfo->CpuName);
  Print (L"  - MIDR_EL1 Register   : 0x%016lx\n", CpuInfo->RawMidr);
  Print (L"  - MPIDR_EL1 Register  : 0x%016lx (Affinity: %d.%d.%d)\n",
         CpuInfo->RawMpidr,
         (UINT32)((CpuInfo->RawMpidr >> 16) & 0xFF),
         (UINT32)((CpuInfo->RawMpidr >> 8) & 0xFF),
         (UINT32)(CpuInfo->RawMpidr & 0xFF));
  Print (L"  - Current Exception EL: EL%d (Normal UEFI Mode)\n", (UINT32)CpuInfo->CurrentEL);
  Print (L"  - ARM Generic Counter : %ld MHz (Freq: %ld Hz)\n",
         Context->TimerFreqHz / 1000000ULL, Context->TimerFreqHz);
  Print (L"  - Hardware Features   : [FP: %s] [NEON/AdvSIMD: %s] [GIC CPU I/F: %s]\n",
         CpuInfo->HasFP ? L"YES" : L"NO",
         CpuInfo->HasASIMD ? L"YES" : L"NO",
         CpuInfo->HasGIC ? L"YES" : L"NO");

  if (CpuInfo->HasFP && CpuInfo->HasASIMD) {
    Print (L"  ==> Status: [" COLOR_GREEN L"PASS" COLOR_RESET L"] CPU Core instruction sets verified.\n");
    Context->PassCount++;
    return EFI_SUCCESS;
  } else {
    Print (L"  ==> Status: [" COLOR_RED L"FAIL" COLOR_RESET L"] Missing mandatory AArch64 SIMD/FP extensions!\n");
    Context->FailCount++;
    return EFI_DEVICE_ERROR;
  }
}

//
// Secondary Core AP task payload
//
typedef struct {
  UINTN  CoreIndex;
  UINT64 Iterations;
  UINT64 Checksum;
  UINT64 Status;
} AP_TASK_PARAM;

STATIC
VOID
EFIAPI
ApDiagnosticWorker (
  IN VOID *Buffer
  )
{
  AP_TASK_PARAM *Param = (AP_TASK_PARAM *)Buffer;
  UINT64 Midr = ArmReadMidr ();
  UINT64 Mpidr = ArmReadMpidr ();
  UINT64 Sum = 0;

  // Simple arithmetic and memory barrier computation loop
  for (UINT64 i = 0; i < Param->Iterations; i++) {
    Sum += (i ^ Midr ^ Mpidr);
  }

  Param->Checksum = Sum;
  Param->Status   = 1; // Completed
}

/**
  SMP Multi-core Bring-up & Load Diagnostic using EFI_MP_SERVICES_PROTOCOL.
**/
EFI_STATUS
RunSmpDiagnostics (
  IN OUT DIAG_CONTEXT   *Context,
  IN OUT ARM64_CPU_INFO *CpuInfo
  )
{
  EFI_STATUS                 Status;
  EFI_MP_SERVICES_PROTOCOL   *MpServices;
  UINTN                      NumberOfProcessors;
  UINTN                      NumberOfEnabledProcessors;
  EFI_PROCESSOR_INFORMATION  ProcInfo;

  Print (L"\n=================================================================\n");
  Print (L" [STAGE 2.2] SMP MULTI-CORE (SECONDARY AP) DIAGNOSTICS\n");
  Print (L"=================================================================\n");

  Status = gBS->LocateProtocol (
                  &gEfiMpServiceProtocolGuid,
                  NULL,
                  (VOID **)&MpServices
                  );

  if (EFI_ERROR (Status)) {
    Print (L"  [WARN] EFI_MP_SERVICES_PROTOCOL not found on this platform.\n");
    Print (L"  Running on Single Core mode (BSP only).\n");
    Context->WarnCount++;
    return EFI_SUCCESS;
  }

  Status = MpServices->GetNumberOfProcessors (
                         MpServices,
                         &NumberOfProcessors,
                         &NumberOfEnabledProcessors
                         );
  if (EFI_ERROR (Status)) {
    Print (L"  [FAIL] Failed to retrieve processor count: %r\n", Status);
    Context->FailCount++;
    return Status;
  }

  CpuInfo->CoreCount = (UINT32)NumberOfProcessors;

  Print (L"  - Total Processors    : %d\n", NumberOfProcessors);
  Print (L"  - Enabled Processors  : %d\n", NumberOfEnabledProcessors);

  for (UINTN i = 0; i < NumberOfProcessors; i++) {
    Status = MpServices->GetProcessorInfo (MpServices, i, &ProcInfo);
    if (!EFI_ERROR (Status)) {
      Print (L"    Core %d: ID 0x%08x | Status: %s | Role: %s\n",
             i,
             ProcInfo.ProcessorId,
             (ProcInfo.StatusFlag & PROCESSOR_ENABLED_BIT) ? L"ENABLED" : L"DISABLED",
             (ProcInfo.StatusFlag & PROCESSOR_AS_BSP_BIT) ? L"BSP (Primary)" : L"AP (Secondary)");
    }
  }

  if (NumberOfProcessors <= 1) {
    Print (L"  - Only 1 core present. Secondary AP dispatch skipped.\n");
    Context->PassCount++;
    return EFI_SUCCESS;
  }

  //
  // Dispatch parallel arithmetic benchmark across all secondary cores
  //
  UINTN NumAps = NumberOfProcessors - 1;
  AP_TASK_PARAM *Params = AllocateZeroPool (sizeof (AP_TASK_PARAM) * NumAps);
  if (Params == NULL) {
    Context->FailCount++;
    return EFI_OUT_OF_RESOURCES;
  }

  for (UINTN i = 0; i < NumAps; i++) {
    Params[i].CoreIndex  = i + 1;
    Params[i].Iterations = 5000000;
    Params[i].Status     = 0;
  }

  Print (L"  - Dispatching parallel workload to %d Secondary Cores (APs)...\n", NumAps);

  Status = MpServices->StartupAllAPs (
                         MpServices,
                         ApDiagnosticWorker,
                         FALSE, // Single-threaded blocking execution on APs
                         NULL,  // No event needed
                         5000000, // Timeout 5 sec (5000000 us)
                         Params,
                         NULL
                         );

  BOOLEAN AllPassed = TRUE;
  if (!EFI_ERROR (Status)) {
    for (UINTN i = 0; i < NumAps; i++) {
      if (Params[i].Status != 1 || Params[i].Checksum == 0) {
        AllPassed = FALSE;
        Print (L"    [FAIL] AP Core %d did not finish test cleanly!\n", Params[i].CoreIndex);
      } else {
        Print (L"    [OK]   AP Core %d computation verified (Checksum=0x%016lx)\n",
               Params[i].CoreIndex, Params[i].Checksum);
      }
    }
  } else {
    AllPassed = FALSE;
    Print (L"    [FAIL] StartupAllAPs failed with error: %r\n", Status);
  }

  FreePool (Params);

  if (AllPassed) {
    Print (L"  ==> Status: [" COLOR_GREEN L"PASS" COLOR_RESET L"] SMP Parallel computation verified across all cores.\n");
    Context->PassCount++;
    return EFI_SUCCESS;
  } else {
    Context->FailCount++;
    return EFI_DEVICE_ERROR;
  }
}
