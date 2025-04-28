#ifndef ZYM_ACPIVIEW_PARSERIMPL_H_
#define ZYM_ACPIVIEW_PARSERIMPL_H_

#include <AcpiView/utils.h>
#include <AcpiView/parser.h>

EFI_STATUS
EFIAPI
ParseXSDT(
  IN VOID *table_ptr
);

EFI_STATUS
EFIAPI
ParseFACP(
  IN VOID *table_ptr
);

EFI_STATUS
EFIAPI
ParseFACS(
  IN VOID *table_ptr
);

EFI_STATUS
EFIAPI
ParseDSDT(
  IN VOID *table_ptr
);

EFI_STATUS
EFIAPI
ParseAPIC(
  IN VOID *table_ptr
);

EFI_STATUS
EFIAPI
ParseHPET(
  IN VOID *table_ptr
);

EFI_STATUS
EFIAPI
ParseMCFG(
  IN VOID *table_ptr
);

EFI_STATUS
EFIAPI
ParseWAET(
  IN VOID *table_ptr
);

EFI_STATUS
EFIAPI
ParseBGRT(
  IN VOID *table_ptr
);

#endif // ZYM_ACPIVIEW_PARSERIMPL_H_
