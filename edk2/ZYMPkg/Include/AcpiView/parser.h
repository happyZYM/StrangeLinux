/** @file
  Common header file for ACPI Table Viewer components.

  Copyright (c) 2025, Your Organization. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef ZYM_ACPIVIEW_COMMON_H_
#define ZYM_ACPIVIEW_COMMON_H_

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/AcpiSystemDescriptionTable.h>
#include <IndustryStandard/Acpi.h>

#include <AcpiView/utils.h>
#include <AcpiView/parserimpl.h>

/**
  宏：用于声明和解析ACPI表头部分
  使用方法：DECLARE_AND_PARSE_ACPI_HEADER(table_ptr);
  
  @param[in]  TablePtr  指向ACPI表的指针
**/
#define DECLARE_AND_PARSE_ACPI_HEADER(TablePtr) \
  UINT8 signature[4]; \
  UINT32 length; \
  UINT8 revision; \
  UINT8 checksum; \
  UINT8 oem_id[6]; \
  UINT8 oem_table_id[8]; \
  UINT32 oem_revision; \
  UINT32 creator_id; \
  UINT32 creator_revision; \
  ParseHeader(TablePtr, signature, &length, &revision, &checksum, oem_id, oem_table_id, &oem_revision, &creator_id, &creator_revision)

VOID
EFIAPI
ParseHeader(
  IN VOID *table_ptr,
  UINT8 *signature_ptr, // 4 bytes ascii string
  UINT32 *length_ptr,
  UINT8 *revision_ptr,
  UINT8 *checksum_ptr,
  UINT8 *oem_id_ptr, // 6 bytes string
  UINT8 *oem_table_id_ptr, // 8 bytes string
  UINT32 *oem_revision_ptr,
  UINT32 *creator_id_ptr,
  UINT32 *creator_revision_ptr
);

EFI_STATUS
EFIAPI
RecursiveParse(
  IN VOID *table_ptr
);

/**
  函数指针类型，用于ACPI表解析函数
**/
typedef
EFI_STATUS
(EFIAPI *ACPI_PARSER_FUNCTION)(
  IN VOID *table_ptr
);

/**
  根据ACPI表签名获取对应的解析函数

  @param[in]  signature_ptr   指向ACPI表签名的指针（4字节ASCII字符串）

  @retval     返回对应签名的解析函数，如果未找到匹配的解析器则返回NULL
**/
ACPI_PARSER_FUNCTION
EFIAPI
GetParser(
  IN UINT8 *signature_ptr
);

#endif // ZYM_ACPI_VIEW_COMMON_H_ 