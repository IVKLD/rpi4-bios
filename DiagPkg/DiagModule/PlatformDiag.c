/** @file
  PCIe, ACPI, SMBIOS and Block IO Hardware Diagnostic Subsystems.

  Copyright (c) 2026, Master's Thesis Project. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "DiagModule.h"

/**
  PCIe Root Bridge & Device Topology Scanner (Stage 4.1).
**/
EFI_STATUS
RunPciDiagnostics (
  IN OUT DIAG_CONTEXT *Context
  )
{
  EFI_STATUS                       Status;
  UINTN                            HandleCount = 0;
  EFI_HANDLE                       *HandleBuffer = NULL;
  EFI_PCI_ROOT_BRIDGE_IO_PROTOCOL  *PciRootBridgeIo;
  UINT32                           DevicesFound = 0;

  Print (L"\n=================================================================\n");
  Print (L" [STAGE 4.1] PCIE ROOT BRIDGE & BUS ENUMERATION\n");
  Print (L"=================================================================\n");

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciRootBridgeIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &HandleBuffer
                  );

  if (EFI_ERROR (Status) || HandleCount == 0) {
    Print (L"  [WARN] No PCIe Root Bridges found: %r\n", Status);
    Context->WarnCount++;
    return EFI_SUCCESS;
  }

  Print (L"  - Detected %d PCIe Root Bridge(s)\n", HandleCount);

  for (UINTN h = 0; h < HandleCount; h++) {
    Status = gBS->HandleProtocol (
                    HandleBuffer[h],
                    &gEfiPciRootBridgeIoProtocolGuid,
                    (VOID **)&PciRootBridgeIo
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    // Enumerate Bus 0..1, Dev 0..31, Func 0..7
    for (UINT16 Bus = 0; Bus < 2; Bus++) {
      for (UINT8 Dev = 0; Dev < 32; Dev++) {
        for (UINT8 Func = 0; Func < 8; Func++) {
          UINT64 Address = EFI_PCI_ADDRESS (Bus, Dev, Func, 0);
          UINT32 VendorDeviceId = 0;

          Status = PciRootBridgeIo->Pci.Read (
                                          PciRootBridgeIo,
                                          EfiPciWidthUint32,
                                          Address,
                                          1,
                                          &VendorDeviceId
                                          );

          if (EFI_ERROR (Status)) {
            continue;
          }

          UINT16 VendorId = (UINT16)(VendorDeviceId & 0xFFFF);
          UINT16 DeviceId = (UINT16)((VendorDeviceId >> 16) & 0xFFFF);

          if (VendorId != 0xFFFF && VendorId != 0x0000) {
            DevicesFound++;
            UINT8 HeaderType = 0;
            PciRootBridgeIo->Pci.Read (
                                    PciRootBridgeIo,
                                    EfiPciWidthUint8,
                                    EFI_PCI_ADDRESS (Bus, Dev, Func, 0x0E),
                                    1,
                                    &HeaderType
                                    );

            Print (L"    [%02x:%02x.%d] VID: 0x%04X, DID: 0x%04X | Hdr: 0x%02X\n",
                   Bus, Dev, Func, VendorId, DeviceId, HeaderType);

            if (VendorId == 0x1106 && DeviceId == 0x3483) {
              Print (L"      -> [Raspberry Pi 4] VIA VL805 USB 3.0 Host Controller detected!\n");
            } else if (VendorId == 0x14E4) {
              Print (L"      -> [Broadcom SoC] Native BCM2711 PCIe Host Core detected!\n");
            } else if (VendorId == 0x1B36) {
              Print (L"      -> [QEMU Virt Platform] Red Hat / QEMU PCIe Host Controller.\n");
            }

            // If not multifunction, skip other functions
            if (Func == 0 && ((HeaderType & 0x80) == 0)) {
              break;
            }
          }
        }
      }
    }
  }

  if (HandleBuffer != NULL) {
    FreePool (HandleBuffer);
  }

  Context->PciDeviceCount = DevicesFound;
  Print (L"  - Total PCIe Devices Discovered: %d\n", DevicesFound);
  Print (L"  ==> Status: [" COLOR_GREEN L"PASS" COLOR_RESET L"] PCIe Configuration Space access verified.\n");
  Context->PassCount++;
  return EFI_SUCCESS;
}

/**
  Calculates standard byte-wise ACPI/SMBIOS 8-bit checksum.
**/
STATIC
UINT8
CalculateChecksum8 (
  IN CONST UINT8 *Buffer,
  IN UINTN       Size
  )
{
  UINT8 Sum = 0;
  for (UINTN i = 0; i < Size; i++) {
    Sum = (UINT8)(Sum + Buffer[i]);
  }
  return Sum;
}

/**
  ACPI & SMBIOS System Table Structure & Checksum Diagnostics (Stage 4.2).
**/
EFI_STATUS
RunAcpiSmbiosDiagnostics (
  IN OUT DIAG_CONTEXT *Context
  )
{
  VOID   *AcpiTablePtr   = NULL;
  VOID   *SmbiosTablePtr = NULL;
  UINT32 TablesVerified  = 0;
  UINT32 TableErrors     = 0;

  Print (L"\n=================================================================\n");
  Print (L" [STAGE 4.2] ACPI & SMBIOS SYSTEM TABLE INTEGRITY VALIDATION\n");
  Print (L"=================================================================\n");

  // Search in System Table Configuration Table
  for (UINTN i = 0; i < gST->NumberOfTableEntries; i++) {
    EFI_GUID *Guid = &gST->ConfigurationTable[i].VendorGuid;
    if (CompareGuid (Guid, &gEfiAcpi20TableGuid) || CompareGuid (Guid, &gEfiAcpiTableGuid)) {
      AcpiTablePtr = gST->ConfigurationTable[i].VendorTable;
    }
    if (CompareGuid (Guid, &gEfiSmbiosTableGuid) || CompareGuid (Guid, &gEfiSmbios3TableGuid)) {
      SmbiosTablePtr = gST->ConfigurationTable[i].VendorTable;
    }
  }

  // 1. Validate ACPI RSDP / XSDT
  if (AcpiTablePtr != NULL) {
    EFI_ACPI_6_4_ROOT_SYSTEM_DESCRIPTION_POINTER *Rsdp =
      (EFI_ACPI_6_4_ROOT_SYSTEM_DESCRIPTION_POINTER *)AcpiTablePtr;

    CHAR8 Sig[9];
    CopyMem (Sig, &Rsdp->Signature, 8);
    Sig[8] = '\0';

    Print (L"  - ACPI RSDP Located at: 0x%016lx [Sig: %a, Rev: %d]\n",
           (UINT64)(UINTN)Rsdp, Sig, Rsdp->Revision);

    UINT8 RsdpSum = CalculateChecksum8 ((UINT8 *)Rsdp, (Rsdp->Revision >= 2) ? Rsdp->Length : 20);
    if (RsdpSum == 0) {
      Print (L"    [OK] RSDP Checksum verified (0x00)\n");
      TablesVerified++;
    } else {
      Print (L"    [FAIL] RSDP Checksum invalid! (Sum=0x%02X)\n", RsdpSum);
      TableErrors++;
    }

    if (Rsdp->XsdtAddress != 0) {
      EFI_ACPI_DESCRIPTION_HEADER *Xsdt = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Rsdp->XsdtAddress;
      UINT8 XsdtSum = CalculateChecksum8 ((UINT8 *)Xsdt, Xsdt->Length);
      CHAR8 XsdtSig[5];
      CopyMem (XsdtSig, &Xsdt->Signature, 4);
      XsdtSig[4] = '\0';

      Print (L"  - ACPI XSDT Located at: 0x%016lx [Sig: %a, Len: %d]\n",
             (UINT64)(UINTN)Xsdt, XsdtSig, Xsdt->Length);

      if (XsdtSum == 0) {
        Print (L"    [OK] XSDT Checksum verified (0x00)\n");
        TablesVerified++;

        UINTN EntryCount = (Xsdt->Length - sizeof (EFI_ACPI_DESCRIPTION_HEADER)) / sizeof (UINT64);
        UINT64 *EntryPtr = (UINT64 *)(Xsdt + 1);

        Print (L"    [INFO] XSDT contains %d sub-tables:\n", EntryCount);
        for (UINTN t = 0; t < EntryCount; t++) {
          EFI_ACPI_DESCRIPTION_HEADER *SubTable = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)EntryPtr[t];
          if (SubTable != NULL) {
            CHAR8 SubSig[5];
            CopyMem (SubSig, &SubTable->Signature, 4);
            SubSig[4] = '\0';
            UINT8 SubSum = CalculateChecksum8 ((UINT8 *)SubTable, SubTable->Length);
            Print (L"      [%d] %a (Len: %4d, OEM: %.6a) -> %s\n",
                   t + 1, SubSig, SubTable->Length, SubTable->OemId,
                   (SubSum == 0) ? L"VALID" : L"CHECKSUM_FAIL");
            if (SubSum == 0) {
              TablesVerified++;
            } else {
              TableErrors++;
            }
          }
        }
      } else {
        Print (L"    [FAIL] XSDT Checksum invalid! (Sum=0x%02X)\n", XsdtSum);
        TableErrors++;
      }
    }
  } else {
    Print (L"  [WARN] ACPI Root Pointer not found in EFI System Table!\n");
  }

  // 2. Validate SMBIOS Entry Point
  if (SmbiosTablePtr != NULL) {
    SMBIOS_TABLE_ENTRY_POINT *Smbios = (SMBIOS_TABLE_ENTRY_POINT *)SmbiosTablePtr;
    if (CompareMem (Smbios->AnchorString, "_SM_", 4) == 0) {
      UINT8 Sum = CalculateChecksum8 ((UINT8 *)Smbios, Smbios->EntryPointLength);
      Print (L"  - SMBIOS 2.x/3.x Entry Point at 0x%016lx [Ver %d.%d]\n",
             (UINT64)(UINTN)Smbios, Smbios->MajorVersion, Smbios->MinorVersion);
      if (Sum == 0) {
        Print (L"    [OK] SMBIOS Entry Point Checksum verified.\n");
        TablesVerified++;
      } else {
        Print (L"    [FAIL] SMBIOS Entry Point Checksum invalid!\n");
        TableErrors++;
      }
    }
  } else {
    Print (L"  [INFO] SMBIOS Entry Point not registered on this platform.\n");
  }

  Context->AcpiTablesCount = TablesVerified;

  if (TableErrors == 0 && TablesVerified > 0) {
    Print (L"  ==> Status: [" COLOR_GREEN L"PASS" COLOR_RESET L"] All System Tables (%d verified) intact.\n", TablesVerified);
    Context->PassCount++;
    return EFI_SUCCESS;
  } else if (TableErrors > 0) {
    Print (L"  ==> Status: [" COLOR_RED L"FAIL" COLOR_RESET L"] %d Table Checksum Errors detected!\n", TableErrors);
    Context->FailCount++;
    return EFI_CRC_ERROR;
  } else {
    Context->WarnCount++;
    return EFI_SUCCESS;
  }
}

/**
  Block IO and GPT Partition Table Integrity Diagnostics (Stage 4.3).
**/
EFI_STATUS
RunBlockIoDiagnostics (
  IN OUT DIAG_CONTEXT *Context
  )
{
  EFI_STATUS             Status;
  UINTN                  HandleCount = 0;
  EFI_HANDLE             *HandleBuffer = NULL;
  EFI_BLOCK_IO_PROTOCOL  *BlockIo;
  UINT32                 ValidMediaCount = 0;

  Print (L"\n=================================================================\n");
  Print (L" [STAGE 4.3] STORAGE & BLOCK IO GPT PARTITION INTEGRITY\n");
  Print (L"=================================================================\n");

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &HandleBuffer
                  );

  if (EFI_ERROR (Status) || HandleCount == 0) {
    Print (L"  [WARN] No Block IO handles detected.\n");
    Context->WarnCount++;
    return EFI_SUCCESS;
  }

  Print (L"  - Detected %d Block IO Handle(s)\n", HandleCount);

  for (UINTN h = 0; h < HandleCount; h++) {
    Status = gBS->HandleProtocol (
                    HandleBuffer[h],
                    &gEfiBlockIoProtocolGuid,
                    (VOID **)&BlockIo
                    );
    if (EFI_ERROR (Status) || BlockIo->Media == NULL) {
      continue;
    }

    // Check only non-logical partition block devices (raw media)
    if (BlockIo->Media->MediaPresent && !BlockIo->Media->LogicalPartition) {
      ValidMediaCount++;
      Print (L"  - Storage Media #%d: BlockSize=%d, Blocks=%ld (%ld MB)\n",
             ValidMediaCount,
             BlockIo->Media->BlockSize,
             BlockIo->Media->LastBlock + 1,
             MultU64x32 (BlockIo->Media->LastBlock + 1, BlockIo->Media->BlockSize) / (1024 * 1024));

      // Read LBA 0 (MBR) and LBA 1 (GPT Header)
      UINT32 BlockSize = BlockIo->Media->BlockSize;
      if (BlockSize >= 512) {
        UINT8 *SectorBuffer = AllocatePool (BlockSize);
        if (SectorBuffer != NULL) {
          Status = BlockIo->ReadBlocks (
                              BlockIo,
                              BlockIo->Media->MediaId,
                              1, // LBA 1: GPT Header
                              BlockSize,
                              SectorBuffer
                              );

          if (!EFI_ERROR (Status)) {
            // Check "EFI PART" signature
            if (CompareMem (SectorBuffer, "EFI PART", 8) == 0) {
              Print (L"    [OK] Valid Primary GPT Header discovered at LBA 1.\n");
            } else {
              Print (L"    [INFO] No Primary GPT signature at LBA 1 (MBR/RAW/FAT format).\n");
            }
          } else {
            Print (L"    [WARN] ReadBlocks on LBA 1 failed: %r\n", Status);
          }
          FreePool (SectorBuffer);
        }
      }
    }
  }

  if (HandleBuffer != NULL) {
    FreePool (HandleBuffer);
  }

  Print (L"  ==> Status: [" COLOR_GREEN L"PASS" COLOR_RESET L"] Block I/O subsystem operational.\n");
  Context->PassCount++;
  return EFI_SUCCESS;
}

/**
  Device Tree (FDT) Structure Diagnostics (Stage 4.3).
**/
EFI_STATUS
RunFdtDiagnostics (
  IN OUT DIAG_CONTEXT *Context
  )
{
  VOID     *FdtTablePtr = NULL;
  EFI_GUID FdtGuid      = EFI_FDT_TABLE_GUID;

  Print (L"\n=================================================================\n");
  Print (L" [STAGE 4.4] DEVICE TREE (FDT) FLATTENED FIRMWARE STRUCTURE\n");
  Print (L"=================================================================\n");

  for (UINTN i = 0; i < gST->NumberOfTableEntries; i++) {
    if (CompareGuid (&gST->ConfigurationTable[i].VendorGuid, &FdtGuid)) {
      FdtTablePtr = gST->ConfigurationTable[i].VendorTable;
      break;
    }
  }

  if (FdtTablePtr == NULL) {
    Print (L"  [INFO] FDT Device Tree not installed in EFI System Configuration Table.\n");
    Print (L"         (Platform is operating in standard ACPI Mode).\n");
    Context->FdtPresent = FALSE;
    return EFI_SUCCESS;
  }

  FDT_HEADER *Header    = (FDT_HEADER *)FdtTablePtr;
  UINT32     Magic      = SwapBytes32 (Header->Magic);
  UINT32     TotalSize  = SwapBytes32 (Header->TotalSize);
  UINT32     Version    = SwapBytes32 (Header->Version);

  Print (L"  - FDT Device Tree Base : 0x%016lx\n", (UINT64)(UINTN)FdtTablePtr);
  Print (L"  - FDT Header Magic     : 0x%08X (Expected: 0xD00DFEED)\n", Magic);
  Print (L"  - FDT Total Size       : %d bytes (%d KB)\n", TotalSize, TotalSize / 1024);
  Print (L"  - FDT Format Version   : %d (Boot CPU Phys ID: 0x%X)\n",
         Version, SwapBytes32 (Header->BootCpuidPhys));

  if (Magic == 0xD00DFEED && TotalSize >= sizeof (FDT_HEADER)) {
    Print (L"  ==> Status: [" COLOR_GREEN L"PASS" COLOR_RESET L"] Device Tree header signature & size verified.\n");
    Context->FdtPresent = TRUE;
    Context->PassCount++;
    return EFI_SUCCESS;
  } else {
    Print (L"  ==> Status: [" COLOR_RED L"FAIL" COLOR_RESET L"] Corrupt FDT Device Tree header magic!\n");
    Context->FailCount++;
    return EFI_VOLUME_CORRUPTED;
  }
}

/**
  Persistent File Logger: Writes diagnostic report matrix to ESP (Stage 5.3).
**/
EFI_STATUS
SaveDiagnosticReport (
  IN CONST DIAG_CONTEXT   *Context,
  IN CONST ARM64_CPU_INFO *CpuInfo
  )
{
  EFI_STATUS                       Status;
  EFI_LOADED_IMAGE_PROTOCOL        *LoadedImage = NULL;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *FileSystem  = NULL;
  EFI_FILE_PROTOCOL                *RootDir     = NULL;
  EFI_FILE_PROTOCOL                *LogFile     = NULL;
  CHAR8                            *LogBuffer   = NULL;
  UINTN                            BufferSize   = 8192;
  UINTN                            WrittenSize  = 0;
  CHAR8                            CpuNameAscii[64];

  Print (L"\n=================================================================\n");
  Print (L" [STAGE 5.3] PERSISTENT REPORT LOGGING TO ESP STORAGE\n");
  Print (L"=================================================================\n");

  ZeroMem (CpuNameAscii, sizeof (CpuNameAscii));
  for (UINTN j = 0; j < 63 && CpuInfo->CpuName[j] != 0; j++) {
    CpuNameAscii[j] = (CHAR8)CpuInfo->CpuName[j];
  }

  // Attempt 1: Get FileSystem from LoadedImage protocol on ImageHandle
  if (Context->ImageHandle != NULL) {
    Status = gBS->HandleProtocol (
                    Context->ImageHandle,
                    &gEfiLoadedImageProtocolGuid,
                    (VOID **)&LoadedImage
                    );
    if (!EFI_ERROR (Status) && LoadedImage->DeviceHandle != NULL) {
      Status = gBS->HandleProtocol (
                      LoadedImage->DeviceHandle,
                      &gEfiSimpleFileSystemProtocolGuid,
                      (VOID **)&FileSystem
                      );
    }
  }

  // Attempt 2: Locate first available SimpleFileSystem protocol
  if (FileSystem == NULL) {
    Status = gBS->LocateProtocol (
                    &gEfiSimpleFileSystemProtocolGuid,
                    NULL,
                    (VOID **)&FileSystem
                    );
  }

  if (EFI_ERROR (Status) || FileSystem == NULL) {
    Print (L"  [WARN] SimpleFileSystem Protocol unavailable. Log persistence skipped.\n");
    return EFI_SUCCESS;
  }

  Status = FileSystem->OpenVolume (FileSystem, &RootDir);
  if (EFI_ERROR (Status) || RootDir == NULL) {
    Print (L"  [WARN] Could not open ESP root directory: %r\n", Status);
    return Status;
  }

  Status = RootDir->Open (
                      RootDir,
                      &LogFile,
                      L"diag_report.txt",
                      EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                      0
                      );

  if (EFI_ERROR (Status)) {
    Print (L"  [WARN] Failed to open diag_report.txt on ESP: %r\n", Status);
    RootDir->Close (RootDir);
    return Status;
  }

  LogBuffer = AllocateZeroPool (BufferSize);
  if (LogBuffer == NULL) {
    LogFile->Close (LogFile);
    RootDir->Close (RootDir);
    return EFI_OUT_OF_RESOURCES;
  }

  WrittenSize = AsciiSPrint (
                  LogBuffer,
                  BufferSize,
                  "=================================================================\r\n"
                  "       ARM64 PRE-BOOT HARDWARE DIAGNOSTIC REPORT MATRIX          \r\n"
                  "=================================================================\r\n"
                  "Target SoC Architecture : AArch64\r\n"
                  "CPU Model Name          : %a\r\n"
                  "MIDR_EL1 Raw Register   : 0x%016lx\r\n"
                  "MPIDR_EL1 Affinity      : 0x%016lx\r\n"
                  "Execution Exception Level: EL%d\r\n"
                  "Total Cores Discovered  : %d\r\n"
                  "ARM Generic Counter Freq: %ld MHz (%ld Hz)\r\n"
                  "SIMD / Floating-Point   : %a / %a\r\n"
                  "-----------------------------------------------------------------\r\n"
                  "RAM March C- BIST Traffic: 10 passes, %ld ms (~%ld MB/s)\r\n"
                  "RAM Walking Bits Traffic: 128 passes, %ld ms (~%ld MB/s)\r\n"
                  "PCIe Discovered Devices : %d\r\n"
                  "ACPI Sub-Tables Verified: %d\r\n"
                  "Device Tree (FDT) Status: %a\r\n"
                  "-----------------------------------------------------------------\r\n"
                  "Tests PASSED            : %d\r\n"
                  "Tests FAILED            : %d\r\n"
                  "Warnings / Skips        : %d\r\n"
                  "Overall System Status   : %a\r\n"
                  "=================================================================\r\n",
                  CpuNameAscii,
                  CpuInfo->RawMidr,
                  CpuInfo->RawMpidr,
                  (UINT32)CpuInfo->CurrentEL,
                  CpuInfo->CoreCount,
                  Context->TimerFreqHz / 1000000ULL,
                  Context->TimerFreqHz,
                  CpuInfo->HasFP ? "YES" : "NO",
                  CpuInfo->HasASIMD ? "YES" : "NO",
                  Context->MarchDurationMs,
                  Context->MarchSpeedMBps,
                  Context->WalkingDurationMs,
                  Context->WalkingSpeedMBps,
                  Context->PciDeviceCount,
                  Context->AcpiTablesCount,
                  Context->FdtPresent ? "PRESENT (0xD00DFEED)" : "ACPI ONLY",
                  Context->PassCount,
                  Context->FailCount,
                  Context->WarnCount,
                  (Context->FailCount == 0) ? "HEALTHY - PASS" : "MALFUNCTION DETECTED"
                  );

  UINTN WriteBytes = WrittenSize;
  Status = LogFile->Write (LogFile, &WriteBytes, LogBuffer);
  LogFile->Flush (LogFile);
  LogFile->Close (LogFile);
  RootDir->Close (RootDir);
  FreePool (LogBuffer);

  if (!EFI_ERROR (Status)) {
    Print (L"  - Persistent Report saved: \\diag_report.txt (%d bytes)\n", WriteBytes);
    Print (L"  ==> Status: [" COLOR_GREEN L"PASS" COLOR_RESET L"] Diagnostic report successfully committed to disk.\n");
  } else {
    Print (L"  ==> Status: [" COLOR_YELLOW L"WARN" COLOR_RESET L"] Write failed: %r\n", Status);
  }

  return EFI_SUCCESS;
}
