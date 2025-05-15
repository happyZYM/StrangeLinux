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

  if (hack_acpi_mode) {
    // we change creator revision as it seems harmless
    WriteUnaligned32((UINT32 *)(table_ptr + 32), 0x12345678);
    UINT8 sum = 0;
    UINTN idx;
    for(idx = 0; idx < *length_ptr; idx++) {
      sum += ((UINT8 *)table_ptr)[idx];
    }
    UINT8 check_sum_delta = 0x100 - sum;
    *(UINT8 *)(table_ptr + 9) += check_sum_delta;

    *creator_revision_ptr = 0x12345678;
    *checksum_ptr = *(UINT8 *)(table_ptr + 9);
  }
}

EFI_STATUS
EFIAPI
RecursiveParse(
  IN VOID *table_ptr
) {
  DECLARE_AND_PARSE_ACPI_HEADER(table_ptr);
  Print(L"\n\n====================\n");
  Print(L"Signature: "); SafeStrPrint(signature, 4); Print(L"\n");
  Print(L"Address: 0x%016lx\n", (UINTN)table_ptr);
  Print(L"Length: %u\n", length);
  Print(L"Revision: %d\n", revision);
  Print(L"Checksum: 0x%02x\n", checksum);
  Print(L"OEM ID: "); SafeStrPrint(oem_id, 6); Print(L"\n");
  UINT8 partial_checksum = 0;
  UINTN idx;
  for(idx = 0; idx < length; idx++) {
    partial_checksum += ((UINT8 *)table_ptr)[idx];
  }
  if(partial_checksum != 0) {
    Print(L"Checksum mismatch\n");
    return EFI_INVALID_PARAMETER;
  } else {
    Print(L"Checksum verified\n");
  }
  Print(L"--------------------\n");

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
  PARSER_ENTRY(FACP);
  PARSER_ENTRY(FACS);
  PARSER_ENTRY(DSDT);
  PARSER_ENTRY(APIC);
  PARSER_ENTRY(HPET);
  PARSER_ENTRY(MCFG);
  PARSER_ENTRY(WAET);
  PARSER_ENTRY(BGRT);

  #undef PARSER_ENTRY
  
  return NULL;
}