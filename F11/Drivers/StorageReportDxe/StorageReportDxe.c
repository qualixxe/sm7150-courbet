/** @file
  Storage Report - prints the block devices and their partitions at boot, and
  saves the same text as \STORAGE.TXT on every FAT volume.

  Why this exists
  ---------------
  The UEFI Shell has exactly the commands needed for this, dblk, dh and map, but
  the phone has no keyboard attached, so nothing can be typed into it. The Shell
  is useless here for the one job it was wanted for.

  So the information is put on the screen without any input at all: this driver
  runs at ReadyToBoot, when the graphical output is already up, prints what it
  found, and holds the screen for a while so it can be read or photographed
  before the menu draws over it.

  The file is the channel that actually survives. A photo needs someone holding
  the phone at the right second; \STORAGE.TXT can be pulled over adb in recovery
  long after the boot moved on, even when Windows never started at all.

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
#include <Library/PrintLib.h>
#include <Library/BaseLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/SimpleFileSystem.h>

//
// How long the report stays on the screen. Long enough to read off the phone,
// short enough not to look like a hang.
//
#define REPORT_HOLD_SECONDS 20

//
// Enough for a full GPT (courbet has some fifty entries) times several block
// devices, with room to spare. Static so the report cannot fail on a pool
// allocation at the exact moment the machine is being diagnosed for failing.
//
#define REPORT_CAP 32768

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
  { 0x8cf2f62c, 0xbc9b, 0x4821, { 0x80, 0x8d, 0xec, 0x9e, 0xc4, 0x21, 0xa1, 0xa0 } };

STATIC CHAR8  mReport[REPORT_CAP];
STATIC UINTN  mReportLen = 0;

/**
  Emit one line to the screen and to the saved copy.

  The copy is ASCII: it is read back on a Linux host with od and grep, which have
  no interest in UTF-16. Anything outside printable ASCII becomes '?' rather than
  being dropped, so a column never silently shifts.
**/
STATIC
VOID
RepPrint (
  IN CONST CHAR16  *Format,
  ...
  )
{
  CHAR16   Wide[512];
  VA_LIST  Args;
  UINTN    Index;
  CHAR16   Ch;

  VA_START (Args, Format);
  UnicodeVSPrint (Wide, sizeof (Wide), Format, Args);
  VA_END (Args);

  Print (L"%s", Wide);

  for (Index = 0; Wide[Index] != L'\0'; Index++) {
    Ch = Wide[Index];
    if (Ch == L'\r') {
      continue;
    }
    if (mReportLen + 1 >= REPORT_CAP) {
      break;
    }
    // Keep the newline: without it the whole report is one line on the host.
    mReport[mReportLen++] = (CHAR8)((Ch == L'\n' || (Ch >= 0x20 && Ch < 0x7F)) ? Ch : '?');
  }
  mReport[mReportLen] = '\0';
}

/**
  Print one block device.

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
  EFI_BLOCK_IO_PROTOCOL  *BlockIo;
  EFI_STATUS             Status;

  Status = gBS->OpenProtocol (
                  Handle,
                  &gEfiBlockIoProtocolGuid,
                  (VOID **)&BlockIo,
                  NULL,
                  NULL,
                  EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL
                  );
  if (EFI_ERROR (Status) || (BlockIo == NULL) || (BlockIo->Media == NULL)) {
    RepPrint (L"  [%2u] cannot open BlockIo (%r)\r\n", (UINT32)Index, Status);
    return;
  }

  RepPrint (L"  [%2u] %lu MB  (%u byte blocks)  mediaid=%u lastblock=%lu logical=%u\r\n",
            (UINT32)Index,
            (UINT64)((BlockIo->Media->LastBlock + 1) * BlockIo->Media->BlockSize / (1024 * 1024)),
            BlockIo->Media->BlockSize,
            BlockIo->Media->MediaId,
            (UINT64)BlockIo->Media->LastBlock,
            (UINT32)BlockIo->Media->LogicalPartition
            );

  gBS->CloseProtocol (Handle, &gEfiBlockIoProtocolGuid, NULL, NULL);
}

/**
  Print every partition once, as its own section.

  LocateHandleBuffer is global: it hands back the same partition set no matter
  which device the caller is looking at. Enumerating under each whole-disk handle
  therefore repeated the whole table once per UFS LUN -- six times here -- and ran
  the report past its cap, truncating the part that matters.
**/
STATIC
VOID
PrintPartitions (
  VOID
  )
{
  EFI_HANDLE                   *PartHandles;
  UINTN                        NoParts;
  UINTN                        PartIndex;
  EFI_STATUS                   Status;
  EFI_PARTITION_INFO_PROTOCOL  *ThisPart;
  EFI_BLOCK_IO_PROTOCOL        *BlockIo;
  UINT32                       BlockSize;

  PartHandles = NULL;
  NoParts     = 0;
  Status      = gBS->LocateHandleBuffer (
                       ByProtocol,
                       &gStorageReportPartitionInfoGuid,
                       NULL,
                       &NoParts,
                       &PartHandles
                       );
  if (EFI_ERROR (Status) || (NoParts == 0) || (PartHandles == NULL)) {
    RepPrint (L"\r\nNo PartitionInfo handle exists at all (%r).\r\n", Status);
    return;
  }

  RepPrint (L"\r\n%u partition(s), in handle order:\r\n", (UINT32)NoParts);

  for (PartIndex = 0; PartIndex < NoParts; PartIndex++) {
    Status = gBS->HandleProtocol (
                     PartHandles[PartIndex],
                     &gStorageReportPartitionInfoGuid,
                     (VOID **)&ThisPart
                     );
    if (EFI_ERROR (Status) || (ThisPart == NULL)) {
      continue;
    }

    //
    // The LBAs are in units of the parent's block size, which the partition's own
    // BlockIo carries. Reading it there keeps the MB column right on a 4Kn LUN.
    //
    BlockSize = 512;
    if (!EFI_ERROR (gBS->HandleProtocol (
                          PartHandles[PartIndex],
                          &gEfiBlockIoProtocolGuid,
                          (VOID **)&BlockIo
                          )) &&
        (BlockIo != NULL) && (BlockIo->Media != NULL))
    {
      BlockSize = BlockIo->Media->BlockSize;
    }

    //
    // PartitionName is null when the entry carries no name, which happens for
    // an MBR partition and for a zeroed GPT slot. Those are printed anyway, so
    // the list of sizes stays complete either way.
    //
    RepPrint (L"  [%3u] %-20s %6lu MB   LBA %lu..%lu\r\n",
              (UINT32)PartIndex,
              (ThisPart->PartitionName != NULL) ? ThisPart->PartitionName : L"<no name>",
              (UINT64)((ThisPart->EndingLBANumber - ThisPart->StartingLBANumber + 1) * BlockSize / (1024 * 1024)),
              (UINT64)ThisPart->StartingLBANumber,
              (UINT64)ThisPart->EndingLBANumber
              );
  }

  FreePool (PartHandles);
}

/**
  Save the report on every FAT volume, before the OS loader runs.
**/
STATIC
VOID
SaveReport (
  VOID
  )
{
  EFI_HANDLE                       *Handles;
  UINTN                            Count;
  UINTN                            Index;
  EFI_STATUS                       Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;
  EFI_FILE_PROTOCOL                *Root;
  EFI_FILE_PROTOCOL                *File;
  UINTN                            WriteLen;

  Handles = NULL;
  Count   = 0;
  Status  = gBS->LocateHandleBuffer (
                   ByProtocol,
                   &gEfiSimpleFileSystemProtocolGuid,
                   NULL,
                   &Count,
                   &Handles
                   );
  if (EFI_ERROR (Status) || (Handles == NULL) || (Count == 0)) {
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    if (EFI_ERROR (gBS->HandleProtocol (Handles[Index], &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs))) {
      continue;
    }
    if (EFI_ERROR (Fs->OpenVolume (Fs, &Root))) {
      continue;
    }

    //
    // Truncate any previous run's file, so a stale report can never be mistaken
    // for this boot's.
    //
    File = NULL;
    if (!EFI_ERROR (Root->Open (Root, &File, L"STORAGE.TXT", EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0)) && (File != NULL)) {
      File->Delete (File);
    }
    File = NULL;
    if (!EFI_ERROR (Root->Open (Root, &File, L"STORAGE.TXT", EFI_FILE_MODE_CREATE | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_READ, 0)) && (File != NULL)) {
      WriteLen = mReportLen;
      File->Write (File, &WriteLen, mReport);
      File->Close (File);
    }
    Root->Close (Root);
  }

  gBS->FreePool (Handles);
}

/**
  Put the whole report on the screen and on disk.

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
  EFI_HANDLE  *FsHandles;
  UINTN       FsCount;

  mReportLen = 0;
  mReport[0] = '\0';

  RepPrint (L"========================================================\r\n");
  RepPrint (L"  COURBET STORAGE REPORT\r\n");
  RepPrint (L"========================================================\r\n");

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &NoHandles,
                  &Buffer
                  );
  if (EFI_ERROR (Status) || (NoHandles == 0) || (Buffer == NULL)) {
    RepPrint (L"\r\nNo BlockIo handle exists at all (%r).\r\n", Status);
    RepPrint (L"The storage driver never registered anything.\r\n");
  } else {
    RepPrint (L"\r\n%u block device(s):\r\n", (UINT32)NoHandles);
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
      RepPrint (L"\r\nRe-checked: %u now exist, more drivers connected.\r\n", (UINT32)SecondCount);
      for (Index = 0; Index < SecondCount; Index++) {
        PrintBlockDevice (SecondBuffer[Index], Index);
      }
      FreePool (SecondBuffer);
    }
    FreePool (Buffer);
  }

  PrintPartitions ();

  //
  // Which volumes the firmware itself mounted. This answers "can anything read
  // the installer media" independently of whatever Windows does afterwards.
  //
  FsHandles = NULL;
  FsCount   = 0;
  Status    = gBS->LocateHandleBuffer (
                     ByProtocol,
                     &gEfiSimpleFileSystemProtocolGuid,
                     NULL,
                     &FsCount,
                     &FsHandles
                     );
  RepPrint (L"\r\nSimpleFileSystem volumes: %u (%r)\r\n",
            EFI_ERROR (Status) ? 0 : (UINT32)FsCount, Status);
  if (FsHandles != NULL) {
    FreePool (FsHandles);
  }

  RepPrint (L"========================================================\r\n");

  SaveReport ();

  gBS->Stall (REPORT_HOLD_SECONDS * 1000000);
}

/**
  Driver entry. Registers for ReadyToBoot and returns.

  @param[in] ImageHandle  Image handle.
  @param[in] SystemTable  System table.

  @retval EFI_SUCCESS     The report is scheduled.
  @retval other           The event could not be created.
**/
EFI_STATUS
EFIAPI
StorageReportEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_EVENT  ReadyToBoot;

  ReadyToBoot = NULL;

  //
  // Registering rather than printing here matters. At this point in DXE the
  // graphics output is not up yet and none of the drivers have connected, so
  // anything printed now would go nowhere and describe nothing.
  //
  // ReadyToBoot is an event GROUP, not a protocol: BDS signals it through
  // CoreSignalEventGroup, and no protocol with that GUID is ever installed. So
  // CreateEvent + RegisterProtocolNotify returns EFI_SUCCESS and then never
  // fires -- which is exactly what happened, twice, with no error to show for
  // it. EfiCreateEventReadyToBootEx is the CreateEventEx form that joins the
  // group, and is what the one callback here that does run (LogFlushToFile in
  // I2cClockDxe) uses.
  //
  return EfiCreateEventReadyToBootEx (
           TPL_CALLBACK,
           StorageReportReadyToBoot,
           NULL,
           &ReadyToBoot
           );
}