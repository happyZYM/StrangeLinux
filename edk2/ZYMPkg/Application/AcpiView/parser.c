/** @file
  ACPI Table Parser implementation.

  Copyright (c) 2025, Your Organization. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <AcpiView/parser.h>

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
) {
  signature_ptr[0] = *(UINT8 *)(table_ptr + 0);
  signature_ptr[1] = *(UINT8 *)(table_ptr + 1);
  signature_ptr[2] = *(UINT8 *)(table_ptr + 2);
  signature_ptr[3] = *(UINT8 *)(table_ptr + 3);
  *length_ptr = ReadUnaligned32((UINT32 *)(table_ptr + 4));
  *revision_ptr = *(UINT8 *)(table_ptr + 8);
  *checksum_ptr = *(UINT8 *)(table_ptr + 9);
  oem_id_ptr[0] = *(UINT8 *)(table_ptr + 10);
  oem_id_ptr[1] = *(UINT8 *)(table_ptr + 11);
  oem_id_ptr[2] = *(UINT8 *)(table_ptr + 12);
  oem_id_ptr[3] = *(UINT8 *)(table_ptr + 13);
  oem_id_ptr[4] = *(UINT8 *)(table_ptr + 14);
  oem_id_ptr[5] = *(UINT8 *)(table_ptr + 15);
  oem_table_id_ptr[0] = *(UINT8 *)(table_ptr + 16);
  oem_table_id_ptr[1] = *(UINT8 *)(table_ptr + 17);
  oem_table_id_ptr[2] = *(UINT8 *)(table_ptr + 18);
  oem_table_id_ptr[3] = *(UINT8 *)(table_ptr + 19);
  oem_table_id_ptr[4] = *(UINT8 *)(table_ptr + 20);
  oem_table_id_ptr[5] = *(UINT8 *)(table_ptr + 21);
  oem_table_id_ptr[6] = *(UINT8 *)(table_ptr + 22);
  oem_table_id_ptr[7] = *(UINT8 *)(table_ptr + 23);
  *oem_revision_ptr = ReadUnaligned32((UINT32 *)(table_ptr + 24));
  *creator_id_ptr = ReadUnaligned32((UINT32 *)(table_ptr + 28));
  *creator_revision_ptr = ReadUnaligned32((UINT32 *)(table_ptr + 32));
}

EFI_STATUS
EFIAPI
RecursiveParse(
  IN VOID *table_ptr
) {
  UINT8 signature[4];
  UINT32 length;
  UINT8 revision;
  UINT8 checksum;
  UINT8 oem_id[6];
  UINT8 oem_table_id[8];
  UINT32 oem_revision;
  UINT32 creator_id;
  UINT32 creator_revision;
  ParseHeader(table_ptr, signature, &length, &revision, &checksum, oem_id, oem_table_id, &oem_revision, &creator_id, &creator_revision);
  Print(L"Signature: "); SafeStrPrint(signature, 4); Print(L"\n");
  Print(L"Length: %d\n", length);
  Print(L"Revision: %d\n", revision);
  Print(L"Checksum: %d\n", checksum);
  Print(L"OEM ID: "); SafeStrPrint(oem_id, 6); Print(L"\n");
  Print(L"OEM Table ID: "); SafeStrPrint(oem_table_id, 8); Print(L"\n");
  Print(L"OEM Revision: %d\n", oem_revision);
  Print(L"Creator ID: %d\n", creator_id);
  Print(L"Creator Revision: %d\n", creator_revision);
  ACPI_PARSER_FUNCTION parser = GetParser(signature);
  if(parser == NULL) {
    Print(L"No parser found for signature: "); SafeStrPrint(signature, 4); Print(L"\n");
    return EFI_UNSUPPORTED;
  }
  return parser(table_ptr);
}

ACPI_PARSER_FUNCTION
EFIAPI
GetParser(
  IN UINT8 *signature_ptr
) {
  #define PARSER_ENTRY(sig) \
    if(AsciiStrnCmp((const CHAR8 *)signature_ptr, #sig, 4) == 0) { \
      return Parse##sig; \
    }

  PARSER_ENTRY(XSDT);

  #undef PARSER_ENTRY
  
  return NULL;
}