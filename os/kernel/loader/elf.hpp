#pragma once

/**
 * @file elf.hpp
 * @brief Minimal ELF64 definitions and helpers used by the kernel loader.
 *
 * @details
 * ViperOS' loader only needs a small subset of the ELF specification:
 * - The ELF64 file header (to validate the image and locate program headers).
 * - The ELF64 program header table (to load PT_LOAD segments).
 * - A small set of constants for AArch64 binaries and common segment flags.
 *
 * The structures in this header are laid out to match the on-disk ELF
 * structures; the loader treats the input image as a byte array and casts it
 * to these structures. As a result, the code assumes the ELF image is stored
 * in a naturally aligned memory buffer.
 */

#include "../include/types.hpp"

namespace elf
{

/**
 * @brief ELF64 file header.
 *
 * @details
 * This structure mirrors `Elf64_Ehdr` from the ELF specification. The loader
 * uses it to:
 * - Validate the magic/class/endianness/architecture.
 * - Determine the entry point virtual address.
 * - Locate the program header table (`e_phoff`, `e_phnum`, `e_phentsize`).
 *
 * Only fields needed by the loader are interpreted; section headers are not
 * used during load.
 */
struct Elf64_Ehdr
{
    u8 e_ident[16];  // Magic number and other info
    u16 e_type;      // Object file type
    u16 e_machine;   // Architecture
    u32 e_version;   // Object file version
    u64 e_entry;     // Entry point virtual address
    u64 e_phoff;     // Program header table file offset
    u64 e_shoff;     // Section header table file offset
    u32 e_flags;     // Processor-specific flags
    u16 e_ehsize;    // ELF header size
    u16 e_phentsize; // Program header table entry size
    u16 e_phnum;     // Program header table entry count
    u16 e_shentsize; // Section header table entry size
    u16 e_shnum;     // Section header table entry count
    u16 e_shstrndx;  // Section header string table index
};

/**
 * @brief ELF64 program header.
 *
 * @details
 * This structure mirrors `Elf64_Phdr` from the ELF specification. The loader
 * iterates program headers and loads segments with `p_type == PT_LOAD`:
 * - `p_offset` and `p_filesz` describe the segment bytes in the file.
 * - `p_vaddr` and `p_memsz` describe where/how much memory is reserved at runtime.
 * - `p_flags` determines segment permissions (read/write/execute).
 */
struct Elf64_Phdr
{
    u32 p_type;   // Segment type
    u32 p_flags;  // Segment flags
    u64 p_offset; // Segment file offset
    u64 p_vaddr;  // Segment virtual address
    u64 p_paddr;  // Segment physical address
    u64 p_filesz; // Segment size in file
    u64 p_memsz;  // Segment size in memory
    u64 p_align;  // Segment alignment
};

/** @name ELF magic number bytes */
///@{
constexpr u8 ELFMAG0 = 0x7f;
constexpr u8 ELFMAG1 = 'E';
constexpr u8 ELFMAG2 = 'L';
constexpr u8 ELFMAG3 = 'F';
///@}

/** @name Indices into `e_ident` */
///@{
constexpr int EI_MAG0 = 0;
constexpr int EI_MAG1 = 1;
constexpr int EI_MAG2 = 2;
constexpr int EI_MAG3 = 3;
constexpr int EI_CLASS = 4;
constexpr int EI_DATA = 5;
constexpr int EI_VERSION = 6;
///@}

/** @brief `e_ident[EI_CLASS]` value for ELF64. */
constexpr u8 ELFCLASS64 = 2;

/** @brief `e_ident[EI_DATA]` value for little-endian encoding. */
constexpr u8 ELFDATA2LSB = 1; // Little endian

/** @name `e_type` values used by the loader */
///@{
constexpr u16 ET_EXEC = 2; // Executable file
constexpr u16 ET_DYN = 3;  // Shared object file (PIE)
///@}

/** @brief `e_machine` value for AArch64. */
constexpr u16 EM_AARCH64 = 183;

/** @name Program header types */
///@{
constexpr u32 PT_NULL = 0;
constexpr u32 PT_LOAD = 1;
constexpr u32 PT_DYNAMIC = 2;
constexpr u32 PT_INTERP = 3;
constexpr u32 PT_NOTE = 4;
constexpr u32 PT_PHDR = 6;
constexpr u32 PT_TLS = 7;
///@}

/** @name Program header permission flags (`p_flags`) */
///@{
constexpr u32 PF_X = 1; // Execute
constexpr u32 PF_W = 2; // Write
constexpr u32 PF_R = 4; // Read
///@}

/**
 * @brief Validate that an ELF header represents a supported binary.
 *
 * @details
 * Performs a minimal set of checks sufficient for the current loader:
 * - Magic bytes match `0x7F 'E' 'L' 'F'`
 * - Class is ELF64
 * - Data encoding is little-endian
 * - File type is ET_EXEC or ET_DYN (PIE)
 * - Machine is AArch64
 *
 * This does not validate all invariants (e.g., header sizes, program header
 * bounds); callers should still perform size/bounds checks before dereferencing
 * tables in untrusted images.
 *
 * @param ehdr Pointer to the ELF header.
 * @return `true` if the header appears supported, otherwise `false`.
 */
bool validate_header(const Elf64_Ehdr *ehdr);

/**
 * @brief Get a pointer to a program header at a given index.
 *
 * @details
 * Interprets `ehdr` as the base of the ELF image in memory, then returns the
 * program header at `index` within the program header table.
 *
 * The returned pointer refers directly into the caller-provided ELF image; it
 * remains valid as long as the image buffer remains valid.
 *
 * @param ehdr Pointer to the ELF header / start of the ELF image.
 * @param index Zero-based program header index.
 * @return Pointer to the program header, or `nullptr` if `index` is out of range.
 */
const Elf64_Phdr *get_phdr(const Elf64_Ehdr *ehdr, int index);

/**
 * @brief Convert ELF segment permission flags into ViperOS protection flags.
 *
 * @details
 * Translates the standard ELF `PF_R/PF_W/PF_X` bits into the `viper::prot::*`
 * flag set used by the AddressSpace mapping layer.
 *
 * @param p_flags ELF program header flags.
 * @return Protection flags suitable for @ref viper::AddressSpace::map.
 */
u32 flags_to_prot(u32 p_flags);

} // namespace elf
