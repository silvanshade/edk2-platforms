/** @file
The library call to pass the device tree to DXE via HOB.

Copyright (c) 2021, Hewlett Packard Enterprise Development LP. All rights reserved.<BR>

SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiPei.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Include/Library/PrePiLib.h>
#include <libfdt.h>
#include <Guid/FdtHob.h>

/**
  Build memory map I/O range resource HOB using the
  base address and size.

  @param  MemoryBase     Memory map I/O base.
  @param  MemorySize     Memory map I/O size.

**/
STATIC
VOID
AddIoMemoryBaseSizeHob (
  EFI_PHYSICAL_ADDRESS  MemoryBase,
  UINT64                MemorySize
  )
{
  /* Align to EFI_PAGE_SIZE */
  MemorySize = ALIGN_VALUE (MemorySize, EFI_PAGE_SIZE);
  BuildResourceDescriptorHob (
    EFI_RESOURCE_MEMORY_MAPPED_IO,
    EFI_RESOURCE_ATTRIBUTE_PRESENT     |
    EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
    EFI_RESOURCE_ATTRIBUTE_UNCACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_TESTED,
    MemoryBase,
    MemorySize
    );
}

/**
  Populate IO resources from FDT that not added to GCD by its
  driver in the DXE phase.

  @param  FdtBase       Fdt base address
  @param  Compatible    Compatible string

**/
STATIC
VOID
PopulateIoResources (
  VOID         *FdtBase,
  CONST CHAR8  *Compatible
  )
{
  UINT64  *Reg;
  INT32   Node, LenP;

  Node = fdt_node_offset_by_compatible (FdtBase, -1, Compatible);
  while (Node != -FDT_ERR_NOTFOUND) {
    Reg = (UINT64 *)fdt_getprop (FdtBase, Node, "reg", &LenP);
    if (Reg) {
      AddIoMemoryBaseSizeHob (SwapBytes64 (Reg[0]), SwapBytes64 (Reg[1]));
      DEBUG ((
        DEBUG_INFO,
        "%a(): MemoryBase=0x%lx\tMemorySize=0x%lx\n",
        __func__,
        SwapBytes64 (Reg[0]),
        SwapBytes64 (Reg[1])
        ));
      //
      // PCIe node may have two regions for reg ("reg" and "cfg")
      //
      if (LenP > (2 * sizeof (UINT64))) {
        AddIoMemoryBaseSizeHob (SwapBytes64 (Reg[2]), SwapBytes64 (Reg[3]));
        DEBUG ((
          DEBUG_INFO,
          "%a(): MemoryBase=0x%lx\tMemorySize=0x%lx\n",
          __func__,
          SwapBytes64 (Reg[2]),
          SwapBytes64 (Reg[3])
          ));
      }
    }

    Node = fdt_node_offset_by_compatible (FdtBase, Node, Compatible);
  }
}

/**
  @param DeviceTreeAddress       Pointer to FDT.
  @retval EFI_SUCCESS            The address of FDT is passed in HOB.
          EFI_UNSUPPORTED        Can't locate FDT.
**/
EFI_STATUS
EFIAPI
PlatformPeimInitialization (
  IN  VOID  *DeviceTreeAddress
  )
{
  VOID                        *Base;
  VOID                        *NewBase;
  UINTN                       FdtSize;
  UINTN                       FdtPages;
  UINT64                      *FdtHobData;

  if (DeviceTreeAddress == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Invalid FDT pointer\n", __func__));
    return EFI_UNSUPPORTED;
  }

  DEBUG ((DEBUG_INFO, "%a: Build FDT HOB - FDT at address: 0x%x \n", __func__, DeviceTreeAddress));
  Base = DeviceTreeAddress;
  if (fdt_check_header (Base) != 0) {
    DEBUG ((DEBUG_ERROR, "%a: Corrupted DTB\n", __func__));
    return EFI_UNSUPPORTED;
  }

  FdtSize  = fdt_totalsize (Base);
  FdtPages = EFI_SIZE_TO_PAGES (FdtSize);
  NewBase  = AllocatePages (FdtPages);
  if (NewBase == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Could not allocate memory for DTB\n", __func__));
    return EFI_UNSUPPORTED;
  }

  fdt_open_into (Base, NewBase, EFI_PAGES_TO_SIZE (FdtPages));

  FdtHobData = BuildGuidHob (&gFdtHobGuid, sizeof *FdtHobData);
  if (FdtHobData == NULL) {
    DEBUG ((DEBUG_ERROR, "%a: Could not build FDT Hob\n", __func__));
    return EFI_UNSUPPORTED;
  }

  *FdtHobData = (UINTN)NewBase;

  BuildFvHob (PcdGet32 (PcdRiscVDxeFvBase), PcdGet32 (PcdRiscVDxeFvSize));

  //
  // Add PCI resource
  //
  PopulateIoResources (Base, "sophgo,cdns-pcie-host");

  //
  // 3GB - 4GB memory space is reserved for PCIe 32-bit inbound access.
  //
  AddIoMemoryBaseSizeHob (0xC0000000, 0x40000000);

  //
  // Add SDHI resource
  //
  PopulateIoResources (Base, "bitmain,bm-sd");

  return EFI_SUCCESS;
}
