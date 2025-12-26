/**
 * @file efi.h
 * @brief Minimal UEFI type and protocol definitions used by `vboot`.
 *
 * @details
 * The ViperOS UEFI bootloader (`vboot`) is a freestanding UEFI application.
 * To keep the bootloader self-contained, it re-defines a small subset of UEFI
 * types, constants, and protocol structures rather than depending on a full
 * UEFI SDK.
 *
 * This header is intentionally incomplete:
 * - Only the pieces required by `vboot` are declared.
 * - Many fields are represented as `void*` placeholders where `vboot` does not
 *   currently use the corresponding functionality.
 *
 * The definitions are based on the UEFI Specification (2.9) but may be trimmed
 * and simplified for bring-up. If you extend `vboot`, prefer adding only the
 * specific protocol structures and GUIDs that are needed.
 */

#ifndef VBOOT_EFI_H
#define VBOOT_EFI_H

#include <stddef.h>
#include <stdint.h>

/** @name Basic EFI types
 *  @brief UEFI fixed-width integer and handle types.
 *
 *  @details
 *  UEFI defines its own type aliases and calling convention macros. These
 *  aliases match the widths expected by the firmware on AArch64.
 *  @{
 */
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int8_t INT8;
typedef int16_t INT16;
typedef int32_t INT32;
typedef int64_t INT64;
typedef uint8_t BOOLEAN;
typedef uint16_t CHAR16;
typedef void VOID;
typedef uint64_t UINTN;
typedef int64_t INTN;
typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef void *EFI_EVENT;
typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint64_t EFI_VIRTUAL_ADDRESS;
typedef uint64_t EFI_LBA;
typedef UINTN EFI_TPL;
/** @} */

/** @name Boolean values and helpers
 *  @brief Convenience definitions used throughout the UEFI interfaces.
 *  @{
 */
#define TRUE 1
#define FALSE 0
#ifndef NULL
#define NULL ((void *)0)
#endif
/** @} */

/**
 * @brief UEFI calling convention annotation.
 *
 * @details
 * On some platforms this macro expands to compiler attributes (e.g., MS ABI).
 * For the AArch64 UEFI environment targeted by `vboot`, the default ABI is
 * typically sufficient, so this is currently an empty macro.
 */
#define EFIAPI

/** @name EFI_STATUS and status codes
 *  @brief Standard UEFI status code conventions.
 *
 *  @details
 *  `EFI_STATUS` is an unsigned integer type. Error codes are identified by the
 *  high bit being set (`EFI_ERROR_MASK`).
 *  @{
 */
#define EFI_SUCCESS 0
#define EFI_ERROR_MASK 0x8000000000000000ULL
#define EFI_ERROR(x) ((x) & EFI_ERROR_MASK)
#define EFI_LOAD_ERROR (EFI_ERROR_MASK | 1)
#define EFI_INVALID_PARAMETER (EFI_ERROR_MASK | 2)
#define EFI_UNSUPPORTED (EFI_ERROR_MASK | 3)
#define EFI_BAD_BUFFER_SIZE (EFI_ERROR_MASK | 4)
#define EFI_BUFFER_TOO_SMALL (EFI_ERROR_MASK | 5)
#define EFI_NOT_READY (EFI_ERROR_MASK | 6)
#define EFI_DEVICE_ERROR (EFI_ERROR_MASK | 7)
#define EFI_OUT_OF_RESOURCES (EFI_ERROR_MASK | 9)
#define EFI_NOT_FOUND (EFI_ERROR_MASK | 14)

/** @} */

/**
 * @brief UEFI memory type enumeration (subset).
 *
 * @details
 * UEFI classifies memory regions in the system memory map. The bootloader uses
 * these values when interpreting the firmware-provided map and deciding which
 * regions are usable RAM vs reserved/firmware memory.
 */
typedef enum
{
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

/**
 * @brief Allocation policy used by `AllocatePages`.
 *
 * @details
 * These values select how UEFI chooses the physical address for a page
 * allocation request.
 */
typedef enum
{
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

/**
 * @brief One entry in the UEFI memory map.
 *
 * @details
 * The bootloader queries the memory map using `GetMemoryMap` which returns an
 * array of these descriptors. Each descriptor describes a contiguous region of
 * memory with a type and attributes.
 */
typedef struct
{
    UINT32 Type;
    EFI_PHYSICAL_ADDRESS PhysicalStart;
    EFI_VIRTUAL_ADDRESS VirtualStart;
    UINT64 NumberOfPages;
    UINT64 Attribute;
} EFI_MEMORY_DESCRIPTOR;

/**
 * @brief UEFI GUID type.
 *
 * @details
 * Protocols and other UEFI objects are identified by GUIDs. Bootloaders pass
 * these GUIDs to services like `LocateProtocol`.
 */
typedef struct
{
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8 Data4[8];
} EFI_GUID;

/**
 * @brief Common header embedded in most UEFI tables.
 *
 * @details
 * Used by the system table and boot services tables. Includes signature,
 * revision, header size, and CRC.
 */
typedef struct
{
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

/** @name Forward declarations
 *  @brief Opaque protocol/table types referenced by function pointers.
 *  @{
 */
struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct _EFI_BOOT_SERVICES;
struct _EFI_RUNTIME_SERVICES;
/** @} */

/** @name Simple Text Output Protocol
 *  @brief Console text output used for early bootloader diagnostics.
 *
 *  @details
 *  The bootloader primarily uses `OutputString` and `ClearScreen`.
 *  @{
 */

/**
 * @brief Function pointer type for `OutputString`.
 *
 * @details
 * Writes a NUL-terminated UTF-16 string to the output console.
 *
 * @param This Protocol instance.
 * @param String UTF-16 string to print.
 * @return EFI status code.
 */
typedef EFI_STATUS(EFIAPI *EFI_TEXT_STRING)(struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
                                            CHAR16 *String);

/**
 * @brief Function pointer type for `ClearScreen`.
 *
 * @param This Protocol instance.
 * @return EFI status code.
 */
typedef EFI_STATUS(EFIAPI *EFI_TEXT_CLEAR_SCREEN)(struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This);

/**
 * @brief Minimal subset of the UEFI Simple Text Output Protocol.
 *
 * @details
 * This structure contains function pointers and fields defined by UEFI.
 * `vboot` treats most entries as opaque and only relies on the subset it
 * actively calls.
 */
typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL
{
    void *Reset;
    EFI_TEXT_STRING OutputString;
    void *TestString;
    void *QueryMode;
    void *SetMode;
    void *SetAttribute;
    EFI_TEXT_CLEAR_SCREEN ClearScreen;
    void *SetCursorPosition;
    void *EnableCursor;
    void *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

/** @} */

/** @name Boot Services (subset)
 *  @brief Boot-time services provided by UEFI firmware.
 *
 *  @details
 *  UEFI exposes a large set of services via the `EFI_BOOT_SERVICES` table.
 *  This header models only the small subset used by the bootloader:
 *  - Memory allocation (`AllocatePages`, `AllocatePool`, etc.)
 *  - Memory map retrieval (`GetMemoryMap`)
 *  - Protocol location (`LocateProtocol`)
 *  - Exiting boot services (`ExitBootServices`)
 *  @{
 */

/** @brief Allocate page-aligned physical memory. */
typedef EFI_STATUS(EFIAPI *EFI_ALLOCATE_PAGES)(EFI_ALLOCATE_TYPE Type,
                                               EFI_MEMORY_TYPE MemoryType,
                                               UINTN Pages,
                                               EFI_PHYSICAL_ADDRESS *Memory);

/** @brief Free pages previously allocated with `AllocatePages`. */
typedef EFI_STATUS(EFIAPI *EFI_FREE_PAGES)(EFI_PHYSICAL_ADDRESS Memory, UINTN Pages);

/** @brief Retrieve the current UEFI memory map. */
typedef EFI_STATUS(EFIAPI *EFI_GET_MEMORY_MAP)(UINTN *MemoryMapSize,
                                               EFI_MEMORY_DESCRIPTOR *MemoryMap,
                                               UINTN *MapKey,
                                               UINTN *DescriptorSize,
                                               UINT32 *DescriptorVersion);

/** @brief Allocate pool memory (byte-granular). */
typedef EFI_STATUS(EFIAPI *EFI_ALLOCATE_POOL)(EFI_MEMORY_TYPE PoolType, UINTN Size, VOID **Buffer);

/** @brief Free pool memory allocated by `AllocatePool`. */
typedef EFI_STATUS(EFIAPI *EFI_FREE_POOL)(VOID *Buffer);

/** @brief Set a buffer to a byte value (firmware-provided memset). */
typedef EFI_STATUS(EFIAPI *EFI_SET_MEM)(VOID *Buffer, UINTN Size, UINT8 Value);

/** @brief Locate a protocol implementation by GUID. */
typedef EFI_STATUS(EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *Protocol,
                                                VOID *Registration,
                                                VOID **Interface);

/** @brief Exit boot services and transition ownership of memory map to OS. */
typedef EFI_STATUS(EFIAPI *EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE ImageHandle, UINTN MapKey);

/**
 * @brief UEFI Boot Services table (partial).
 *
 * @details
 * The table contains many more fields than modeled here. `vboot` only depends
 * on a small subset, so unused fields are left as `void*` placeholders to keep
 * the structure layout roughly aligned for the functions we do use.
 */
typedef struct _EFI_BOOT_SERVICES
{
    EFI_TABLE_HEADER Hdr;

    // Task Priority Services
    void *RaiseTPL;
    void *RestoreTPL;

    // Memory Services
    EFI_ALLOCATE_PAGES AllocatePages;
    EFI_FREE_PAGES FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    EFI_FREE_POOL FreePool;

    // Event & Timer Services
    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;

    // Protocol Handler Services
    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    void *HandleProtocol;
    void *Reserved;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;

    // Image Services
    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;

    // Misc Services
    void *GetNextMonotonicCount;
    void *Stall;
    void *SetWatchdogTimer;

    // Driver Support Services
    void *ConnectController;
    void *DisconnectController;

    // Open/Close Protocol Services
    void *OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;

    // Library Services
    void *ProtocolsPerHandle;
    void *LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
    void *InstallMultipleProtocolInterfaces;
    void *UninstallMultipleProtocolInterfaces;

    // CRC Services
    void *CalculateCrc32;

    // Misc Services
    void *CopyMem;
    EFI_SET_MEM SetMem;
    void *CreateEventEx;
} EFI_BOOT_SERVICES;

/** @} */

/**
 * @brief UEFI System Table.
 *
 * @details
 * The firmware passes a pointer to this table to the UEFI application entry
 * point. It provides access to:
 * - Console input/output protocols.
 * - The Boot Services and Runtime Services tables.
 * - Firmware vendor/revision metadata.
 *
 * `vboot` stores the pointer in a global (`gST`) for convenience.
 */
typedef struct
{
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    struct _EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    struct _EFI_RUNTIME_SERVICES *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    void *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/** @name Graphics Output Protocol (GOP)
 *  @brief UEFI protocol used to access the framebuffer.
 *
 *  @details
 *  `vboot` uses GOP to query and (optionally) set a graphics mode and to obtain
 *  a linear framebuffer address and dimensions to pass to the kernel.
 *  @{
 */

/** @brief Color component bitmask used when `PixelFormat == PixelBitMask`. */
typedef struct
{
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

/** @brief Pixel format identifiers used by GOP. */
typedef enum
{
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

/** @brief GOP mode information describing the current display mode. */
typedef struct
{
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

/** @brief GOP protocol mode state (selected mode + framebuffer address/size). */
typedef struct
{
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

struct _EFI_GRAPHICS_OUTPUT_PROTOCOL;

/** @brief Query information about a graphics mode. */
typedef EFI_STATUS(EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE)(
    struct _EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    UINT32 ModeNumber,
    UINTN *SizeOfInfo,
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);

/** @brief Set the current graphics mode. */
typedef EFI_STATUS(EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE)(
    struct _EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 ModeNumber);

/**
 * @brief UEFI Graphics Output Protocol (minimal).
 *
 * @details
 * Provides function pointers to query/set modes and a pointer to the current
 * mode information, including framebuffer base and size.
 */
typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE QueryMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE SetMode;
    void *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/** @brief GUID identifying the Graphics Output Protocol. */
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID                                                          \
    {0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}}

/** @} */

/** @name Simple File System Protocol (SFS)
 *  @brief UEFI protocol used to open the boot volume and load files.
 *
 *  @details
 *  `vboot` uses SFS to open the firmware-provided filesystem that contains the
 *  bootloader and (eventually) the kernel image.
 *  @{
 */
struct _EFI_FILE_PROTOCOL;

/** @brief Open a file relative to an existing directory handle. */
typedef EFI_STATUS(EFIAPI *EFI_FILE_OPEN)(struct _EFI_FILE_PROTOCOL *This,
                                          struct _EFI_FILE_PROTOCOL **NewHandle,
                                          CHAR16 *FileName,
                                          UINT64 OpenMode,
                                          UINT64 Attributes);

/** @brief Close an open file handle. */
typedef EFI_STATUS(EFIAPI *EFI_FILE_CLOSE)(struct _EFI_FILE_PROTOCOL *This);

/** @brief Read bytes from an open file. */
typedef EFI_STATUS(EFIAPI *EFI_FILE_READ)(struct _EFI_FILE_PROTOCOL *This,
                                          UINTN *BufferSize,
                                          VOID *Buffer);

/** @brief Set the current file position. */
typedef EFI_STATUS(EFIAPI *EFI_FILE_SET_POSITION)(struct _EFI_FILE_PROTOCOL *This, UINT64 Position);

/**
 * @brief UEFI file protocol interface (minimal).
 *
 * @details
 * This structure is used both for directories (volumes) and files. `vboot`
 * models only the fields required for opening and reading files.
 */
typedef struct _EFI_FILE_PROTOCOL
{
    UINT64 Revision;
    EFI_FILE_OPEN Open;
    EFI_FILE_CLOSE Close;
    void *Delete;
    EFI_FILE_READ Read;
    void *Write;
    void *GetPosition;
    EFI_FILE_SET_POSITION SetPosition;
    void *GetInfo;
    void *SetInfo;
    void *Flush;
} EFI_FILE_PROTOCOL;

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

/** @brief Open the root directory of a filesystem volume. */
typedef EFI_STATUS(EFIAPI *EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME)(
    struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Root);

/**
 * @brief Simple File System Protocol (minimal).
 *
 * @details
 * Used to open the volume root. From there, `EFI_FILE_PROTOCOL` handles are
 * used to traverse directories and read files.
 */
typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL
{
    UINT64 Revision;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME OpenVolume;
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

/** @brief GUID identifying the Simple File System Protocol. */
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID                                                       \
    {0x0964e5b22, 0x6459, 0x11d2, {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}

/** @} */

/** @name Loaded Image Protocol
 *  @brief UEFI protocol describing the currently loaded image.
 *
 *  @details
 *  Bootloaders use this protocol to determine their own load address, device
 *  handle, and to locate the filesystem that contains the executable.
 *  @{
 */

/** @brief Loaded image protocol structure (minimal). */
typedef struct
{
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    void *FilePath;
    void *Reserved;
    UINT32 LoadOptionsSize;
    void *LoadOptions;
    void *ImageBase;
    UINT64 ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    void *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/** @brief GUID identifying the Loaded Image Protocol. */
#define EFI_LOADED_IMAGE_PROTOCOL_GUID                                                             \
    {0x5B1B31A1, 0x9562, 0x11d2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}}

/** @} */

/** @name File open modes
 *  @brief Flags passed to `EFI_FILE_OPEN`.
 *  @{
 */
#define EFI_FILE_MODE_READ 0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE 0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL
/** @} */

/** @name Global UEFI pointers
 *  @brief Convenience globals set by the UEFI entry point.
 *
 *  @details
 *  `vboot/main.c` stores the UEFI-provided pointers here so helper functions
 *  can access boot services and console output without threading them through
 *  every call.
 *  @{
 */
extern EFI_SYSTEM_TABLE *gST;
extern EFI_BOOT_SERVICES *gBS;
extern EFI_HANDLE gImageHandle;
/** @} */

#endif // VBOOT_EFI_H
