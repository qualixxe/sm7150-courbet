/** @file
  Storage Report - prints the block devices and their partitions at boot.

  Why this exists
  ---------------
  The UEFI Shell has exactly the commands needed for this, dblk, dh and map, but
  the phone has no keyboard attached, so nothing can be typed into it. The Shell
  is useless here for the one job it was wanted for.

  So the information is put on the screen without any input at all: this driver
  runs at ReadyToBoot, when the graphical output is already up, prints what it
  found, and holds the screen for a while so it can be read or photographed
  before the menu draws over it.

  What it prints
  --------------
  Every BlockIo handle, its media size, and every partition behind it with its
  name taken from the GPT. That last part is the point. The File Manager cannot
  answer the storage question at all, because the firmware mounts FAT32 and this
  phone's partitions are ext4 and f2fs, so a working controller would show
  nothing either. A partition name needs no filesystem, which is exactly what is
  missing everywhere else.

  Why a driver and not a menu entry
  ---------------------------------
  A menu entry needs a UI section, which needs a FILE block in the FDF, and a
  FILE block makes GenFds look for the .efi in the workspace while the module is
  really in the build output. It fails with "not found in packages path" however
  the path is written, because the build output path carries the toolchain and
  architecture in it. A plain INF line resolves itself.

  On the partition structure
  --------------------------
  EFI_PARTITION_INFO_PROTOCOL is declared here rather than included. The
  Protocol/Partition.h header does not exist in this edk2 revision, and adding a
  header to a submodule that is pinned by SHA is not worth doing for one
  structure. The layout is fixed by PI 1.8 and has not changed.
**/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>
#include <Library/MemoryAllocationLib.h>
#include <Protocol/BlockIo.h>

//
// How long the report stays on the screen. Long enough to read off the phone,
// short enough not to look like a hang.
//
#define REPORT_HOLD_SECONDS 20

//
// EFI_PARTITION_INFO_PROTOCOL, PI 1.8 section 10.5.
//
typedef struct {
  UINT32    Revision;
  UINT32    Type;                 ///< 0 = MBR, 1 = GPT
  EFI_GUID  PartitionType;
  EFI_GUID  UniquePartitionGUID;
  UINT64    StartingLBANumber;
  UINT64    EndingLBANumber;
  CHAR16    *PartitionName;
} EFI_PARTITION_INFO_PROTOCOL;

//
// The GUID variable has to exist as well as the value. Declaring only a macro
// leaves every use of it undefined, which is what happened on the first attempt.
// Named without the usual gEfi prefix so it cannot clash if a header that does
// define it ever appears.
//
EFI_GUID  gStorageReportPartitionInfoGuid =
  { 0x8cf2f62c, 0xbc9b, 0x4821, { 0x9d, 0x14, 0x98, 0x9a, 0x78, 0xbe, 0x2e, 0xc5 } };

/**
  Print one block device and, if it has any, its partitions.

  @param[in] Handle  The block device handle.
  @param[in] Index   Its position in the handle buffer, so the reader can refer
                     to it.
**/
STATIC
VOID
PrintBlockDevice (
  IN EFI_HANDLE  Handle,
  IN UINTN       Index
  )
{
  EFI_BLOCK_IO_PROTOCOL        *BlockIo;
  EFI_HANDLE                  *PartHandles;
  UINTN                       NoParts;
  EFI_STATUS                  Status;
  UINTN                       PartIndex;
  UINT32                      BlockSize;

  Status = gBS->OpenProtocol (
                  Handle,
                  &gEfiBlockIoProtocolGuid,
                  (VOID **)&BlockIo,
                  EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL,
                  NULL,
                  NULL
                  );
  if (EFI_ERROR (Status) || (BlockIo == NULL) || (BlockIo->Media == NULL)) {
    return;
  }
  BlockSize = BlockIo->Media->BlockSize;

  Print (L"  [%2d] %lu MB  (%lu byte blocks)\r\n",
         (UINT32)Index,
         (UINT32)((BlockIo->Media->LastBlock + 1) * BlockSize / (1024 * 1024)),
         (UINT32)BlockSize
         );

  //
  // A partition handle is not required to carry its own openable protocol
  // instances, so the children are found by LocateHandleBuffer against the
  // parent's interface rather than by opening anything on them.
  //
  PartHandles = NULL;
  NoParts     = 0;
  Status      = gBS->LocateHandleBuffer (
                         ByProtocol,
                         &gStorageReportPartitionInfoGuid,
                         (VOID *)Handle,
                         &NoParts,
                         &PartHandles
                         );
  if (EFI_ERROR (Status) || (NoParts == 0) || (PartHandles == NULL)) {
    gBS->CloseProtocol (Handle, &gEfiBlockIoProtocolGuid, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
    return;
  }

  for (PartIndex = 0; PartIndex < NoParts; PartIndex++) {
    EFI_PARTITION_INFO_PROTOCOL  *ThisPart;

    Status = gBS->HandleProtocol (
                     PartHandles[PartIndex],
                     &gStorageReportPartitionInfoGuid,
                     (VOID **)&ThisPart
                     );
    if (EFI_ERROR (Status) || (ThisPart == NULL)) {
      continue;
    }

    //
    // PartitionName is null when the entry carries no name, which happens for
    // an MBR partition and for a zeroed GPT slot. Those are printed anyway, so
    // the list of sizes stays complete either way.
    //
    Print (L"        %-20s %6lu MB   LBA %lu..%lu\r\n",
           (ThisPart->PartitionName != NULL) ? ThisPart->PartitionName : L"<no name>",
           (UINT32)((ThisPart->EndingLBANumber - ThisPart->StartingLBANumber + 1) * BlockSize / (1024 * 1024)),
           (UINT32)ThisPart->StartingLBANumber,
           (UINT32)ThisPart->EndingLBANumber
           );
  }

  FreePool (PartHandles);
  gBS->CloseProtocol (Handle, &gEfiBlockIoProtocolGuid, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
}

/**
  Put the whole report on the screen.

  No arguments beyond the event's, and it returns nothing: this is called from an
  event notification and must not affect the boot result.
**/
STATIC
VOID
EFIAPI
StorageReportReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_HANDLE  *Buffer;
  UINTN       NoHandles;
  EFI_STATUS  Status;
  UINTN       Index;
  EFI_HANDLE  *SecondBuffer;
  UINTN       SecondCount;

  Print (L"\r\n");
  Print (L"========================================================\r\n");
  Print (L"  STORAGE REPORT - no keyboard needed\r\n");
  Print (L"========================================================\r\n");

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &NoHandles,
                  &Buffer
                  );
  if (EFI_ERROR (Status) || (NoHandles == 0) || (Buffer == NULL)) {
    Print (L"\r\nNo BlockIo handle exists at all.\r\n");
    Print (L"The storage driver never registered anything.\r\n");
  } else {
    Print (L"\r\n%u block device(s):\r\n", (UINT32)NoHandles);
    for (Index = 0; Index < NoHandles; Index++) {
      PrintBlockDevice (Buffer[Index], Index);
    }

    //
    // Look a second time. A larger list means drivers connected while this was
    // printing, so the first pass was simply too early to mean anything, and
    // that is worth saying rather than leaving a wrong answer on screen.
    //
    SecondBuffer = NULL;
    SecondCount  = 0;
    Status       = gBS->LocateHandleBuffer (
                            ByProtocol,
                            &gEfiBlockIoProtocolGuid,
                            NULL,
                            &SecondCount,
                            &SecondBuffer
                            );
    if (!EFI_ERROR (Status) && (SecondCount > NoHandles)) {
      Print (L"\r\nRe-checked: %u now exist, more drivers connected.\r\n", (UINT32)SecondCount);
      for (Index = 0; Index < SecondCount; Index++) {
        PrintBlockDevice (SecondBuffer[Index], Index);
      }
      FreePool (SecondBuffer);
    }
    FreePool (Buffer);
  }

  Print (L"\r\n--------------------------------------------------------\r\n");
  Print (L"  A partition of tens of GB means the controller enumerated\r\n");
  Print (L"  it and storage works. UEFI still cannot mount it: only Fat\r\n");
  Print (L"  is in this build and these partitions are ext4 and f2fs.\r\n");
  Print (L"\r\nMenu in %d seconds.\r\n", REPORT_HOLD_SECONDS);

  gBS->Stall (REPORT_HOLD_SECONDS * 1000000);
}

/**
  Driver entry. Registers for ReadyToBoot and returns.

  @param[in] ImageHandle  Image handle.
  @param[in] SystemTable  System table.

  @retval EFI_SUCCESS     The report is scheduled.
  @retval EFI_NOT_READY   The event could not be created.
**/
EFI_STATUS
EFIAPI
StorageReportEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_EVENT  ReadyToBoot;
  EFI_STATUS Status;

  ReadyToBoot = NULL;

  //
  // Registering rather than printing here matters. At this point in DXE the
  // graphics output is not up yet and none of the drivers have connected, so
  // anything printed now would go nowhere and describe nothing.
  //
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  StorageReportReadyToBoot,
                  NULL,
                  &ReadyToBoot
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->RegisterProtocolNotify (
                  &gEfiEventReadyToBootGuid,
                  ReadyToBoot,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (ReadyToBoot);
  }

  return Status;
}