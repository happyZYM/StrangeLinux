## @file
#  Dsc include file for Dynamic Tables Framework.
#
#  Copyright (c) 2017 - 2022, Arm Limited. All rights reserved.<BR>
#  Copyright (C) 2024 Advanced Micro Devices, Inc. All rights reserved.<BR>
#  Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved.<BR>
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#
##

[Defines]

[BuildOptions]
  RELEASE_*_*_CC_FLAGS     = -DMDEPKG_NDEBUG

[LibraryClasses.common]
  AcpiHelperLib|DynamicTablesPkg/Library/Common/AcpiHelperLib/AcpiHelperLib.inf
  AmlLib|DynamicTablesPkg/Library/Common/AmlLib/AmlLib.inf
  SsdtPcieSupportLib|DynamicTablesPkg/Library/Common/SsdtPcieSupportLib/SsdtPcieSupportLib.inf
  SsdtSerialPortFixupLib|DynamicTablesPkg/Library/Common/SsdtSerialPortFixupLib/SsdtSerialPortFixupLib.inf
  TableHelperLib|DynamicTablesPkg/Library/Common/TableHelperLib/TableHelperLib.inf
  SmbiosStringTableLib|DynamicTablesPkg/Library/Common/SmbiosStringTableLib/SmbiosStringTableLib.inf

[LibraryClasses.AARCH64]
  DynamicTablesScmiInfoLib|DynamicTablesPkg/Library/DynamicTablesScmiInfoLib/DynamicTablesScmiInfoLib.inf

[Components.common]
  #
  # Generators (Common)
  #
  DynamicTablesPkg/Library/Acpi/Common/AcpiDbg2Lib/AcpiDbg2Lib.inf
  DynamicTablesPkg/Library/Acpi/Common/AcpiFadtLib/AcpiFadtLib.inf
  DynamicTablesPkg/Library/Acpi/Common/AcpiMcfgLib/AcpiMcfgLib.inf
  DynamicTablesPkg/Library/Acpi/Common/AcpiPcctLib/AcpiPcctLib.inf
  DynamicTablesPkg/Library/Acpi/Common/AcpiPpttLib/AcpiPpttLib.inf
  DynamicTablesPkg/Library/Acpi/Common/AcpiRawLib/AcpiRawLib.inf
  DynamicTablesPkg/Library/Acpi/Common/AcpiSpcrLib/AcpiSpcrLib.inf
  DynamicTablesPkg/Library/Acpi/Common/AcpiSratLib/AcpiSratLib.inf
  DynamicTablesPkg/Library/Acpi/Common/AcpiTpm2Lib/AcpiTpm2Lib.inf

  # AML Fixup (Common)
  DynamicTablesPkg/Library/Acpi/Common/AcpiSsdtSerialPortLib/SsdtSerialPortLib.inf

  # AML Codegen (Common)
  DynamicTablesPkg/Library/Acpi/Common/AcpiSsdtCpuTopologyLib/SsdtCpuTopologyLib.inf
  DynamicTablesPkg/Library/Acpi/Common/AcpiSsdtPcieLib/SsdtPcieLib.inf

  #
  # Dynamic Tables Manager Dxe
  #
  DynamicTablesPkg/Drivers/DynamicTableManagerDxe/DynamicTableManagerDxe.inf

[Components.IA32, Components.X64]
  #
  # Dynamic Table Factory Dxe
  #
  DynamicTablesPkg/Drivers/DynamicTableFactoryDxe/DynamicTableFactoryDxe.inf {
    <LibraryClasses>
      # Generators
      #   Common
      NULL|DynamicTablesPkg/Library/Acpi/Common/AcpiFadtLib/AcpiFadtLib.inf
  }

[Components.ARM, Components.AARCH64]
  #
  # Generators (Arm specific)
  #
  DynamicTablesPkg/Library/Acpi/Arm/AcpiGtdtLibArm/AcpiGtdtLibArm.inf
  DynamicTablesPkg/Library/Acpi/Arm/AcpiIortLibArm/AcpiIortLibArm.inf
  DynamicTablesPkg/Library/Acpi/Arm/AcpiMadtLibArm/AcpiMadtLibArm.inf

  # AML Fixup (Arm specific)
  DynamicTablesPkg/Library/Acpi/Arm/AcpiSsdtCmn600LibArm/SsdtCmn600LibArm.inf

  #
  # Dynamic Table Factory Dxe
  #
  DynamicTablesPkg/Drivers/DynamicTableFactoryDxe/DynamicTableFactoryDxe.inf {
    <LibraryClasses>
      # Generators
      #   Common
      NULL|DynamicTablesPkg/Library/Acpi/Common/AcpiDbg2Lib/AcpiDbg2Lib.inf
      NULL|DynamicTablesPkg/Library/Acpi/Common/AcpiFadtLib/AcpiFadtLib.inf
      NULL|DynamicTablesPkg/Library/Acpi/Common/AcpiMcfgLib/AcpiMcfgLib.inf
      NULL|DynamicTablesPkg/Library/Acpi/Common/AcpiPcctLib/AcpiPcctLib.inf
      NULL|DynamicTablesPkg/Library/Acpi/Common/AcpiPpttLib/AcpiPpttLib.inf
      NULL|DynamicTablesPkg/Library/Acpi/Common/AcpiRawLib/AcpiRawLib.inf
      NULL|DynamicTablesPkg/Library/Acpi/Common/AcpiSpcrLib/AcpiSpcrLib.inf
      NULL|DynamicTablesPkg/Library/Acpi/Common/AcpiSratLib/AcpiSratLib.inf
      NULL|DynamicTablesPkg/Library/Acpi/Common/AcpiTpm2Lib/AcpiTpm2Lib.inf
      #   Arm specific
      NULL|DynamicTablesPkg/Library/Acpi/Arm/AcpiGtdtLibArm/AcpiGtdtLibArm.inf
      NULL|DynamicTablesPkg/Library/Acpi/Arm/AcpiIortLibArm/AcpiIortLibArm.inf
      NULL|DynamicTablesPkg/Library/Acpi/Arm/AcpiMadtLibArm/AcpiMadtLibArm.inf

      # AML Fixup
      #  Common
      NULL|DynamicTablesPkg/Library/Acpi/Common/AcpiSsdtSerialPortLib/SsdtSerialPortLib.inf
      #  Arm specific
      NULL|DynamicTablesPkg/Library/Acpi/Arm/AcpiSsdtCmn600LibArm/SsdtCmn600LibArm.inf

      # AML Codegen
      #  Common
      NULL|DynamicTablesPkg/Library/Acpi/Common/AcpiSsdtCpuTopologyLib/SsdtCpuTopologyLib.inf
      NULL|DynamicTablesPkg/Library/Acpi/Common/AcpiSsdtPcieLib/SsdtPcieLib.inf
  }

