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
#include <Protocol/LoadedImage.h>
#include <IndustryStandard/Acpi.h>
#include <AcpiView/utils.h>
#include <AcpiView/parser.h>

BOOLEAN hack_acpi_mode;

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  Print (L"ZYM's ACPI Table Viewer\n");

  hack_acpi_mode = FALSE;
  
  // Get LoadedImage protocol to access command line arguments
  EFI_LOADED_IMAGE_PROTOCOL *LoadedImage;
  EFI_STATUS Status;
  Status = gBS->HandleProtocol (
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&LoadedImage
                  );
  if (!EFI_ERROR (Status)) {
    if (LoadedImage->LoadOptionsSize > 0 && LoadedImage->LoadOptions != NULL) {
      CHAR16 *CmdLine = (CHAR16 *)LoadedImage->LoadOptions;
      UINTN CmdLineLen = LoadedImage->LoadOptionsSize / sizeof (CHAR16);
      Print (L"Command line arguments: %.*s\n", CmdLineLen, CmdLine);
      
      if (StrStr(CmdLine, L"hack_acpi_mode") != NULL) {
        hack_acpi_mode = TRUE;
      }
    } else {
      Print (L"No command line arguments provided\n");
    }
  }

  if (hack_acpi_mode) {
    Print(L"hack_acpi_mode\n");
    // return EFI_SUCCESS;
  }
  
  UINT8 *rsdp_ptr;
  BOOLEAN rsdp_found = FALSE;
  UINTN idx;
  EFI_CONFIGURATION_TABLE *rsdp_efi_config_table = NULL;
  for(idx = 0; idx < SystemTable->NumberOfTableEntries; idx++) {
    if(CompareGuid(&gEfiAcpiTableGuid, &SystemTable->ConfigurationTable[idx].VendorGuid)) {
      rsdp_efi_config_table = &SystemTable->ConfigurationTable[idx];
      rsdp_found = TRUE;
      break;
    }
  }
  if(!rsdp_found) {
    Print(L"RSDP not found\n");
    return EFI_SUCCESS;
  }
  rsdp_ptr = (UINT8 *)rsdp_efi_config_table->VendorTable;
  Print(L"RSDP found at %p\n", rsdp_ptr);
  UINT8 *rsdt_addr;
  UINT8 *xsdt_addr;
  rsdt_addr = (UINT8 *)(UINTN)(ReadUnaligned32((UINT32 *)(rsdp_ptr + 16)));
  xsdt_addr = (UINT8 *)(UINTN)(ReadUnaligned64((UINT64 *)(rsdp_ptr + 24)));
  Print(L"RSDT at %p\n", rsdt_addr);
  Print(L"XSDT at %p\n", xsdt_addr);
  UINT8 rsdp_revision = *(rsdp_ptr + 15);
  if(rsdp_revision < 2) {
    Print(L"unsupported RSDP revision\n");
    return EFI_UNSUPPORTED;
  }
  return RecursiveParse(xsdt_addr);
}