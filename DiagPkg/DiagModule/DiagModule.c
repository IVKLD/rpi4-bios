/** @file
  Master Orchestration Entry Point for ARM64 Pre-boot Diagnostics Module.

  Copyright (c) 2026, Master's Thesis Project. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "DiagModule.h"

/**
  Prints the final pass/fail diagnostic matrix.
**/
STATIC
VOID
PrintDiagnosticSummary (
  IN CONST DIAG_CONTEXT *Context,
  IN CONST ARM64_CPU_INFO *CpuInfo
  )
{
  Print (L"\n=================================================================\n");
  Print (L"            FINAL HARDWARE DIAGNOSTIC REPORT MATRIX              \n");
  Print (L"=================================================================\n");
  Print (L"  Target SoC Architecture : AArch64 (%s, %d Cores)\n", CpuInfo->CpuName, CpuInfo->CoreCount);
  Print (L"  ARM Generic Counter     : %ld MHz (Freq: %ld Hz)\n",
         Context->TimerFreqHz / 1000000ULL, Context->TimerFreqHz);
  Print (L"  RAM March C- BIST Pass  : %ld ms (~%ld MB/s traffic)\n",
         Context->MarchDurationMs, Context->MarchSpeedMBps);
  Print (L"  RAM Walking Bits Pass   : %ld ms (~%ld MB/s traffic)\n",
         Context->WalkingDurationMs, Context->WalkingSpeedMBps);
  Print (L"  PCIe Devices Detected   : %d\n", Context->PciDeviceCount);
  Print (L"  ACPI Tables Verified    : %d\n", Context->AcpiTablesCount);
  Print (L"  Total Tests Executed    : %d\n", Context->PassCount + Context->FailCount + Context->WarnCount);
  Print (L"  Tests PASSED            : " COLOR_GREEN L"%d" COLOR_RESET L"\n", Context->PassCount);
  Print (L"  Tests FAILED            : " COLOR_RED L"%d" COLOR_RESET L"\n", Context->FailCount);
  Print (L"  Warnings / Skips        : " COLOR_YELLOW L"%d" COLOR_RESET L"\n", Context->WarnCount);
  Print (L"-----------------------------------------------------------------\n");

  if (Context->FailCount == 0) {
    Print (L"  SYSTEM HEALTH STATUS    : [" COLOR_GREEN L" HEALTHY - PASS " COLOR_RESET L"]\n");
    Print (L"  PRE-BOOT SAFEGUARD      : Ready to hand over to OS Kernel.\n");
  } else {
    Print (L"  SYSTEM HEALTH STATUS    : [" COLOR_RED L" HARDWARE MALFUNCTION DETECTED " COLOR_RESET L"]\n");
    Print (L"  PRE-BOOT SAFEGUARD      : HALT recommended to prevent data corruption.\n");
  }
  Print (L"=================================================================\n\n");
}

/**
  UEFI Application Entry Point.
**/
EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  DIAG_CONTEXT   Context;
  ARM64_CPU_INFO CpuInfo;

  ZeroMem (&Context, sizeof (DIAG_CONTEXT));
  Context.ImageHandle   = ImageHandle;
  Context.RamTestSizeMB = 32; // Default 32 MB

  // Parse command line arguments from UEFI Shell
  EFI_SHELL_PARAMETERS_PROTOCOL *ShellParameters = NULL;
  EFI_STATUS ParamStatus = gBS->OpenProtocol (
                                  ImageHandle,
                                  &gEfiShellParametersProtocolGuid,
                                  (VOID **)&ShellParameters,
                                  ImageHandle,
                                  NULL,
                                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                                  );

  if (!EFI_ERROR (ParamStatus) && ShellParameters != NULL && ShellParameters->Argc > 1) {
    for (UINTN i = 1; i < ShellParameters->Argc; i++) {
      CHAR16 *Arg = ShellParameters->Argv[i];
      if (StrCmp (Arg, L"-h") == 0 || StrCmp (Arg, L"--help") == 0) {
        Print (L"\n=================================================================\n");
        Print (L"     ARM64 PRE-BOOT HARDWARE DIAGNOSTICS MODULE - HELP           \n");
        Print (L"=================================================================\n");
        Print (L"Usage: DiagModule.efi [OPTIONS]\n\n");
        Print (L"  -m <MB>   Set RAM BIST buffer size in MB (e.g. -m 16, -m 64, -m 128)\n");
        Print (L"  -b        Continuous Burn-In loop mode for stress testing\n");
        Print (L"  -h        Display this help information\n\n");
        return EFI_SUCCESS;
      } else if (StrCmp (Arg, L"-m") == 0 && (i + 1 < ShellParameters->Argc)) {
        Context.RamTestSizeMB = StrDecimalToUintn (ShellParameters->Argv[++i]);
        if (Context.RamTestSizeMB == 0) {
          Context.RamTestSizeMB = 32;
        }
      } else if (StrCmp (Arg, L"-b") == 0) {
        Context.BurnInMode = TRUE;
      }
    }
  }

  if (SystemTable->ConOut != NULL) {
    SystemTable->ConOut->ClearScreen (SystemTable->ConOut);
    SystemTable->ConOut->SetAttribute (SystemTable->ConOut, EFI_LIGHTGREEN | EFI_BACKGROUND_BLACK);
  }

  Print (L"=================================================================\n");
  Print (L"      ARM64 PRE-BOOT HARDWARE DIAGNOSTIC MODULE (UEFI)           \n");
  Print (L"      Master's Thesis Research Project | Native AArch64          \n");
  Print (L"=================================================================\n");

  if (SystemTable->ConOut != NULL) {
    SystemTable->ConOut->SetAttribute (SystemTable->ConOut, EFI_WHITE | EFI_BACKGROUND_BLACK);
  }

  Print (L"[INIT] System Table at 0x%016lx | Firmware: %s (Rev: 0x%08x)\n",
         (UINT64)(UINTN)SystemTable,
         SystemTable->FirmwareVendor,
         SystemTable->FirmwareRevision);

  // Initialize visual frame buffer if GOP protocol is present
  InitializeGopUi ();

  // STAGE 2.1: CPU Core Identification & Architecture Register Check
  RunCpuDiagnostics (&Context, &CpuInfo);

  // STAGE 2.2: SMP Multi-Processor Parallel Workload
  RunSmpDiagnostics (&Context, &CpuInfo);

  // STAGE 3: RAM Subsystem & BIST (March C- / Walking Bits)
  RunMemoryDiagnostics (&Context);

  // STAGE 4.1: PCIe Topology Enumeration
  RunPciDiagnostics (&Context);

  // STAGE 4.2: ACPI & SMBIOS System Table Integrity
  RunAcpiSmbiosDiagnostics (&Context);

  // STAGE 4.3: Device Tree (FDT) Verification
  RunFdtDiagnostics (&Context);

  // STAGE 4.4: Storage Block IO & GPT Verification
  RunBlockIoDiagnostics (&Context);

  // STAGE 5.2: Fault Injection & Resilience Validation
  RunFaultInjectionTest (&Context);

  // STAGE 5.3: Persistent Report Logging to ESP Disk File
  SaveDiagnosticReport (&Context, &CpuInfo);

  // Print Summary Matrix
  PrintDiagnosticSummary (&Context, &CpuInfo);

  return (Context.FailCount == 0) ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}
