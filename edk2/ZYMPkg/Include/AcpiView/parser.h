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

typedef
EFI_STATUS
(EFIAPI *ACPI_PARSER_FUNCTION)(
  IN VOID *table_ptr
);

ACPI_PARSER_FUNCTION
EFIAPI
GetParser(
  IN UINT8 *signature_ptr
);

#endif // ZYM_ACPI_VIEW_COMMON_H_ 