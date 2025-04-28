#include <AcpiView/parserimpl.h>

EFI_STATUS
EFIAPI
ParseXSDT(
  IN VOID *table_ptr
) {
  Print(L"ParseXSDT called\n");
  DECLARE_AND_PARSE_ACPI_HEADER(table_ptr);
  if(revision != 1) {
    Print(L"Unsupported revision: %d\n", revision);
    return EFI_UNSUPPORTED;
  }
  if((length - 36) % 8 != 0) {
    Print(L"Invalid length: %d\n", length);
    return EFI_INVALID_PARAMETER;
  }
  UINTN table_count = (length - 36) / 8;
  UINTN idx;
  Print(L"there are %d sub tables:\n", table_count);
  for(idx = 0; idx < table_count; idx++) {
    Print(L"  0x%016lx -> ", (UINTN)ReadUnaligned64((UINT64 *)(table_ptr + 36 + idx * 8)));
    SafeStrPrint((UINT8 *)(UINTN)ReadUnaligned64((UINT64 *)(table_ptr + 36 + idx * 8)), 4);
    Print(L"\n");
  }
  Print(L"====================\n");
  for(idx = 0; idx < table_count; idx++) {
    VOID *sub_table_ptr = (VOID *)(UINTN)ReadUnaligned64((UINT64 *)(table_ptr + 36 + idx * 8));
    EFI_STATUS status = RecursiveParse(sub_table_ptr);
    if(EFI_ERROR(status)) {
      Print(L"Error parsing sub-table %d\n", idx);
      return status;
    }
  }
  return EFI_SUCCESS;
}


EFI_STATUS
EFIAPI
ParseFACP(
  IN VOID *table_ptr
) { // FADT
  Print(L"ParseFACP called\n");
  DECLARE_AND_PARSE_ACPI_HEADER(table_ptr);
  UINT32 flags = ReadUnaligned32((UINT32 *)(table_ptr + 112));
  VOID* FACS_ptr = NULL;
  VOID* DSDT_ptr = NULL;
  VOID* tmp;
  if ((tmp = (VOID*)(UINTN)ReadUnaligned64((UINT64 *)(table_ptr + 132)))) {
    FACS_ptr = (VOID *)tmp;
  } else if((tmp = (VOID*)(UINTN)ReadUnaligned32((UINT32 *)(table_ptr + 36)))) {
    FACS_ptr = (VOID *)tmp;
  } else {
    BOOLEAN HW_REDUCED_ACPI = ((flags >> 20) & 1);
    if(HW_REDUCED_ACPI) {
      Print(L"No FACS found but HW_REDUCED_ACPI is set\n");
      Print(L"However Hardware-Reduced ACPI is not supported yet\n");
      return EFI_INVALID_PARAMETER;
    } else {
      Print(L"No FACS found\n");
      return EFI_INVALID_PARAMETER;
    }
  }
  if ((tmp = (VOID*)(UINTN)ReadUnaligned64((UINT64 *)(table_ptr + 140)))) {
    DSDT_ptr = (VOID *)tmp;
  } else if ((tmp = (VOID*)(UINTN)ReadUnaligned32((UINT32 *)(table_ptr + 40)))) {
    DSDT_ptr = (VOID *)tmp;
  } else {
    Print(L"No DSDT found\n");
    return EFI_INVALID_PARAMETER;
  }

  Print(L"there are %d sub tables:\n", (FACS_ptr != NULL) + (DSDT_ptr != NULL));
  if (FACS_ptr != NULL) {
    Print(L"  0x%016lx -> FACS\n", (UINTN)FACS_ptr);
  }
  if (DSDT_ptr != NULL) {
    Print(L"  0x%016lx -> DSDT\n", (UINTN)DSDT_ptr);
  }
  Print(L"====================\n");
  EFI_STATUS status = ParseFACS(FACS_ptr); // FACS is not a normal description table
  if(EFI_ERROR(status)) {
    Print(L"Error parsing FACS\n");
    return status;
  }
  status = RecursiveParse(DSDT_ptr);
  
  return status;
}

EFI_STATUS
EFIAPI
ParseFACS(
  IN VOID *table_ptr
) {
  UINT8 signature[4];
  UINT32 length;
  UINT32 hardware_signature;
  signature[0] = *(UINT8 *)(table_ptr + 0);
  signature[1] = *(UINT8 *)(table_ptr + 1);
  signature[2] = *(UINT8 *)(table_ptr + 2);
  signature[3] = *(UINT8 *)(table_ptr + 3);
  length = ReadUnaligned32((UINT32 *)(table_ptr + 4));
  hardware_signature = ReadUnaligned32((UINT32 *)(table_ptr + 8));
  Print(L"\n\n====================\n");
  Print(L"Signature: "); SafeStrPrint(signature, 4); Print(L"\n");
  Print(L"Address: 0x%016lx\n", (UINTN)table_ptr);
  Print(L"Length: %u\n", length);
  Print(L"Hardware Signature: 0x%08x\n", hardware_signature);
  Print(L"====================\n");
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
ParseDSDT(
  IN VOID *table_ptr
) {
  Print(L"ParseDSDT called\n");
  DECLARE_AND_PARSE_ACPI_HEADER(table_ptr);
  Print(L"====================\n");
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
ParseAPIC(
  IN VOID *table_ptr
) {
  Print(L"ParseAPIC called\n");
  DECLARE_AND_PARSE_ACPI_HEADER(table_ptr);
  Print(L"====================\n");
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
ParseHPET(
  IN VOID *table_ptr
) {
  Print(L"ParseHPET called\n");
  DECLARE_AND_PARSE_ACPI_HEADER(table_ptr);
  Print(L"====================\n");
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
ParseMCFG(
  IN VOID *table_ptr
) {
  Print(L"ParseMCFG called\n");
  DECLARE_AND_PARSE_ACPI_HEADER(table_ptr);
  Print(L"====================\n");
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
ParseWAET(
  IN VOID *table_ptr
) {
  Print(L"ParseWAET called\n");
  DECLARE_AND_PARSE_ACPI_HEADER(table_ptr);
  Print(L"====================\n");
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
ParseBGRT(
  IN VOID *table_ptr
) {
  Print(L"ParseBGRT called\n");
  DECLARE_AND_PARSE_ACPI_HEADER(table_ptr);
  Print(L"====================\n");
  return EFI_SUCCESS;
}