/** @file
  RAM Built-In Self Test (BIST) implementation using March C- and Walking Bits.

  Copyright (c) 2026, Master's Thesis Project. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "DiagModule.h"

#define BIST_BLOCK_SIZE  (32 * 1024 * 1024) // 32 Megabytes test buffer

/**
  March C- Algorithm Implementation:
  March C- covers stuck-at faults, transition faults, address decoder faults,
  and coupling faults (CFin, CFid).
  Steps:
  1. (W0) Up: Write 0 to all cells.
  2. (R0, W1) Up: Read 0, then Write 1.
  3. (R1, W0) Up: Read 1, then Write 0.
  4. (R0, W1) Down: Read 0, then Write 1.
  5. (R1, W0) Down: Read 1, then Write 0.
  6. (R0) Down: Read 0.
**/
STATIC
inline
VOID
RecordRamFault (
  IN OUT RAM_FAULT_INFO *FaultInfo,
  IN UINT64             Address,
  IN UINT64             Expected,
  IN UINT64             Actual
  )
{
  if (FaultInfo != NULL && !FaultInfo->HasFault) {
    FaultInfo->HasFault     = TRUE;
    FaultInfo->FaultAddress = Address;
    FaultInfo->Expected     = Expected;
    FaultInfo->Actual       = Actual;
    UINT64 Diff             = Expected ^ Actual;
    FaultInfo->FailingBit   = (Diff != 0) ? (UINT32)__builtin_ctzll (Diff) : 0;
  }
}

/**
  March C- Algorithm Implementation with Precision Error Pinpointing:
  March C- covers stuck-at faults, transition faults, address decoder faults,
  and coupling faults (CFin, CFid).
**/
STATIC
BOOLEAN
ExecuteMarchCTest (
  IN volatile UINT64    *Buffer,
  IN UINTN               ElementCount,
  OUT UINT64             *ErrorCount,
  IN OUT RAM_FAULT_INFO  *FaultInfo
  )
{
  UINTN  i;
  UINT64 Errors = 0;

  // Step 1: Up W0
  for (i = 0; i < ElementCount; i++) {
    Buffer[i] = 0x0ULL;
  }

  // Explicit Clean & Invalidate to flush write buffers and caches to DRAM
  ArmDataCacheCleanAndInvalidate ((VOID *)Buffer, ElementCount * sizeof (UINT64));

  // Step 2: Up R0, W1
  for (i = 0; i < ElementCount; i++) {
    if (Buffer[i] != 0x0ULL) {
      Errors++;
      RecordRamFault (FaultInfo, (UINT64)(UINTN)&Buffer[i], 0x0ULL, Buffer[i]);
    }
    Buffer[i] = 0xFFFFFFFFFFFFFFFFULL;
  }

  ArmDataCacheCleanAndInvalidate ((VOID *)Buffer, ElementCount * sizeof (UINT64));

  // Step 3: Up R1, W0
  for (i = 0; i < ElementCount; i++) {
    if (Buffer[i] != 0xFFFFFFFFFFFFFFFFULL) {
      Errors++;
      RecordRamFault (FaultInfo, (UINT64)(UINTN)&Buffer[i], 0xFFFFFFFFFFFFFFFFULL, Buffer[i]);
    }
    Buffer[i] = 0x0ULL;
  }

  ArmDataCacheCleanAndInvalidate ((VOID *)Buffer, ElementCount * sizeof (UINT64));

  // Step 4: Down R0, W1
  for (i = ElementCount; i > 0; i--) {
    if (Buffer[i - 1] != 0x0ULL) {
      Errors++;
      RecordRamFault (FaultInfo, (UINT64)(UINTN)&Buffer[i - 1], 0x0ULL, Buffer[i - 1]);
    }
    Buffer[i - 1] = 0xFFFFFFFFFFFFFFFFULL;
  }

  ArmDataCacheCleanAndInvalidate ((VOID *)Buffer, ElementCount * sizeof (UINT64));

  // Step 5: Down R1, W0
  for (i = ElementCount; i > 0; i--) {
    if (Buffer[i - 1] != 0xFFFFFFFFFFFFFFFFULL) {
      Errors++;
      RecordRamFault (FaultInfo, (UINT64)(UINTN)&Buffer[i - 1], 0xFFFFFFFFFFFFFFFFULL, Buffer[i - 1]);
    }
    Buffer[i - 1] = 0x0ULL;
  }

  ArmDataCacheCleanAndInvalidate ((VOID *)Buffer, ElementCount * sizeof (UINT64));

  // Step 6: Down R0
  for (i = ElementCount; i > 0; i--) {
    if (Buffer[i - 1] != 0x0ULL) {
      Errors++;
      RecordRamFault (FaultInfo, (UINT64)(UINTN)&Buffer[i - 1], 0x0ULL, Buffer[i - 1]);
    }
  }

  *ErrorCount = Errors;
  return (Errors == 0);
}

/**
  Walking 1s & Walking 0s Test:
  Detects short-circuits and cross-talk between adjacent memory bus data lines.
**/
STATIC
BOOLEAN
ExecuteWalkingBitsTest (
  IN volatile UINT64    *Buffer,
  IN UINTN               ElementCount,
  OUT UINT64             *ErrorCount,
  IN OUT RAM_FAULT_INFO  *FaultInfo
  )
{
  UINT64 Errors = 0;

  // Walking 1s across 64-bit bus
  for (UINT32 bit = 0; bit < 64; bit++) {
    UINT64 Pattern = (1ULL << bit);
    for (UINTN i = 0; i < ElementCount; i++) {
      Buffer[i] = Pattern;
    }
    for (UINTN i = 0; i < ElementCount; i++) {
      if (Buffer[i] != Pattern) {
        Errors++;
        RecordRamFault (FaultInfo, (UINT64)(UINTN)&Buffer[i], Pattern, Buffer[i]);
      }
    }
  }

  // Walking 0s across 64-bit bus
  for (UINT32 bit = 0; bit < 64; bit++) {
    UINT64 Pattern = ~(1ULL << bit);
    for (UINTN i = 0; i < ElementCount; i++) {
      Buffer[i] = Pattern;
    }
    for (UINTN i = 0; i < ElementCount; i++) {
      if (Buffer[i] != Pattern) {
        Errors++;
        RecordRamFault (FaultInfo, (UINT64)(UINTN)&Buffer[i], Pattern, Buffer[i]);
      }
    }
  }

  *ErrorCount = Errors;
  return (Errors == 0);
}

/**
  Retrieves UEFI Memory Map and executes RAM BIST algorithms.
**/
EFI_STATUS
RunMemoryDiagnostics (
  IN OUT DIAG_CONTEXT *Context
  )
{
  EFI_STATUS                 Status;
  UINTN                      MemoryMapSize = 0;
  EFI_MEMORY_DESCRIPTOR      *MemoryMap = NULL;
  UINTN                      MapKey;
  UINTN                      DescriptorSize;
  UINT32                     DescriptorVersion;

  Print (L"\n=================================================================\n");
  Print (L" [STAGE 3] RAM SYSTEM SUBSYSTEM & BIST DIAGNOSTICS\n");
  Print (L"=================================================================\n");

  // Step 3.1: Query Memory Map size
  Status = gBS->GetMemoryMap (
                  &MemoryMapSize,
                  MemoryMap,
                  &MapKey,
                  &DescriptorSize,
                  &DescriptorVersion
                  );

  if (Status == EFI_BUFFER_TOO_SMALL) {
    MemoryMapSize += (8 * DescriptorSize); // add headroom
    MemoryMap = AllocatePool (MemoryMapSize);
    if (MemoryMap == NULL) {
      Context->FailCount++;
      return EFI_OUT_OF_RESOURCES;
    }

    Status = gBS->GetMemoryMap (
                    &MemoryMapSize,
                    MemoryMap,
                    &MapKey,
                    &DescriptorSize,
                    &DescriptorVersion
                    );
  }

  if (EFI_ERROR (Status)) {
    Print (L"  [FAIL] Failed to retrieve UEFI Memory Map: %r\n", Status);
    if (MemoryMap != NULL) {
      FreePool (MemoryMap);
    }
    Context->FailCount++;
    return Status;
  }

  UINT64 TotalPhysicalBytes = 0;
  UINT64 ConventionalBytes  = 0;
  UINTN  Entries = MemoryMapSize / DescriptorSize;

  for (UINTN i = 0; i < Entries; i++) {
    EFI_MEMORY_DESCRIPTOR *Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)MemoryMap + (i * DescriptorSize));
    UINT64 Size = MultU64x32 (Desc->NumberOfPages, EFI_PAGE_SIZE);
    TotalPhysicalBytes += Size;
    if (Desc->Type == EfiConventionalMemory) {
      ConventionalBytes += Size;
    }
  }

  FreePool (MemoryMap);

  Print (L"  - Total Physical RAM  : %ld MB (%ld GB)\n",
         TotalPhysicalBytes / (1024 * 1024),
         TotalPhysicalBytes / (1024 * 1024 * 1024));
  Print (L"  - Available Free RAM  : %ld MB (EfiConventionalMemory)\n",
         ConventionalBytes / (1024 * 1024));

  // Step 3.2: Allocate test buffer for BIST
  UINTN DesiredMB = (Context->RamTestSizeMB > 0) ? Context->RamTestSizeMB : 32;
  UINTN TestPages = EFI_SIZE_TO_PAGES (DesiredMB * 1024 * 1024);
  EFI_PHYSICAL_ADDRESS TestBuffer = 0;

  Status = gBS->AllocatePages (
                  AllocateAnyPages,
                  EfiBootServicesData,
                  TestPages,
                  &TestBuffer
                  );

  if (EFI_ERROR (Status)) {
    Print (L"  [WARN] Could not allocate %ldMB buffer, falling back to 8MB.\n", DesiredMB);
    TestPages = EFI_SIZE_TO_PAGES (8 * 1024 * 1024);
    Status = gBS->AllocatePages (
                    AllocateAnyPages,
                    EfiBootServicesData,
                    TestPages,
                    &TestBuffer
                    );
  }

  if (EFI_ERROR (Status)) {
    Print (L"  [FAIL] Failed to allocate RAM BIST test buffer: %r\n", Status);
    Context->FailCount++;
    return Status;
  }

  UINTN ElementCount = (TestPages * EFI_PAGE_SIZE) / sizeof (UINT64);
  volatile UINT64 *BufferPtr = (volatile UINT64 *)(UINTN)TestBuffer;
  UINT64 MarchErrors = 0;
  UINT64 WalkingErrors = 0;

  UINT64 BufferSizeMB = (TestPages * EFI_PAGE_SIZE) / (1024 * 1024);
  Print (L"  - Dedicated BIST Range: 0x%016lx - 0x%016lx (%ld MB)\n",
         (UINT64)TestBuffer,
         (UINT64)(TestBuffer + (TestPages * EFI_PAGE_SIZE)),
         BufferSizeMB);
  Print (L"  - Data Cache Policy   : Explicit ARM64 dc civac Clean/Invalidate on each March pass\n");

  ZeroMem (&Context->LastFault, sizeof (RAM_FAULT_INFO));

  Print (L"  - Executing March C- BIST (6-step Coupling & Stuck-at test)... ");
  UINT64 StartTick = ArmReadCntPct ();
  BOOLEAN MarchOk = ExecuteMarchCTest (BufferPtr, ElementCount, &MarchErrors, &Context->LastFault);
  UINT64 EndTick = ArmReadCntPct ();

  Context->MarchDurationMs = ArmGetElapsedMs (StartTick, EndTick, Context->TimerFreqHz);
  if (Context->MarchDurationMs > 0) {
    Context->MarchSpeedMBps = (BufferSizeMB * 10ULL * 1000ULL) / Context->MarchDurationMs;
  }

  if (MarchOk) {
    Print (L"[" COLOR_GREEN L"PASSED" COLOR_RESET L"] (0 bit-flips in %ld ms, ~%ld MB/s traffic)\n",
           Context->MarchDurationMs, Context->MarchSpeedMBps);
  } else {
    Print (L"[" COLOR_RED L"FAILED" COLOR_RESET L"] (%ld bit-flips detected in %ld ms!)\n",
           MarchErrors, Context->MarchDurationMs);
    if (Context->LastFault.HasFault) {
      Print (L"    [FAULT PINPOINT] Addr: 0x%016lx | Expected: 0x%016lx | Got: 0x%016lx | Bad Line: Bit %d\n",
             Context->LastFault.FaultAddress,
             Context->LastFault.Expected,
             Context->LastFault.Actual,
             Context->LastFault.FailingBit);
    }
  }

  Print (L"  - Executing Walking 1s & 0s Bus Crosstalk / Short test... ");
  StartTick = ArmReadCntPct ();
  BOOLEAN WalkingOk = ExecuteWalkingBitsTest (BufferPtr, ElementCount, &WalkingErrors, &Context->LastFault);
  EndTick = ArmReadCntPct ();

  Context->WalkingDurationMs = ArmGetElapsedMs (StartTick, EndTick, Context->TimerFreqHz);
  if (Context->WalkingDurationMs > 0) {
    Context->WalkingSpeedMBps = (BufferSizeMB * 128ULL * 1000ULL) / Context->WalkingDurationMs;
  }

  if (WalkingOk) {
    Print (L"[" COLOR_GREEN L"PASSED" COLOR_RESET L"] (0 bus errors in %ld ms, ~%ld MB/s traffic)\n",
           Context->WalkingDurationMs, Context->WalkingSpeedMBps);
  } else {
    Print (L"[" COLOR_RED L"FAILED" COLOR_RESET L"] (%ld bus errors detected in %ld ms!)\n",
           WalkingErrors, Context->WalkingDurationMs);
    if (Context->LastFault.HasFault) {
      Print (L"    [FAULT PINPOINT] Addr: 0x%016lx | Expected: 0x%016lx | Got: 0x%016lx | Bad Line: Bit %d\n",
             Context->LastFault.FaultAddress,
             Context->LastFault.Expected,
             Context->LastFault.Actual,
             Context->LastFault.FailingBit);
    }
  }

  gBS->FreePages (TestBuffer, TestPages);

  if (MarchOk && WalkingOk) {
    Print (L"  ==> Status: [" COLOR_GREEN L"PASS" COLOR_RESET L"] RAM Subsystem verified healthy.\n");
    Context->PassCount++;
    return EFI_SUCCESS;
  } else {
    Context->FailCount++;
    return EFI_DEVICE_ERROR;
  }
}
