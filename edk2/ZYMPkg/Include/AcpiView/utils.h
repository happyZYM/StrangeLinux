#ifndef ZYM_ACPIVIEW_UTILS_H_
#define ZYM_ACPIVIEW_UTILS_H_
#include <Library/UefiLib.h>
VOID
EFIAPI
SafeStrPrint(
  IN UINT8 *str_ptr,
  IN UINTN max_len
);
#endif // ZYM_ACPIVIEW_UTILS_H_
