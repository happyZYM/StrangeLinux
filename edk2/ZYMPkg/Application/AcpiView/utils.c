#include <AcpiView/utils.h>

VOID
EFIAPI
SafeStrPrint(
  IN UINT8 *str_ptr,
  IN UINTN max_len
) {
  UINTN idx;
  for(idx = 0; idx < max_len; idx++) {
    if(str_ptr[idx] == '\0') {
      break;
    }
    Print(L"%c", str_ptr[idx]);
  }
}