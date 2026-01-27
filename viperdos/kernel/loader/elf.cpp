//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file elf.cpp
 * @brief Implementation of minimal ELF parsing helpers.
 *
 * @details
 * Provides small, freestanding routines used by the kernel loader to validate
 * and interpret ELF64 images. These helpers intentionally avoid libc
 * dependencies and only implement what the loader requires.
 */

#include "elf.hpp"
#include "../viper/address_space.hpp"

namespace elf {

/** @copydoc elf::validate_header */
bool validate_header(const Elf64_Ehdr *ehdr) {
    // Check magic number
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 || ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 || ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        return false;
    }

    // Check class (64-bit)
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        return false;
    }

    // Check endianness (little endian)
    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        return false;
    }

    // Check type (executable or PIE)
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        return false;
    }

    // Check machine (AArch64)
    if (ehdr->e_machine != EM_AARCH64) {
        return false;
    }

    return true;
}

/** @copydoc elf::get_phdr */
const Elf64_Phdr *get_phdr(const Elf64_Ehdr *ehdr, int index) {
    if (index < 0 || index >= ehdr->e_phnum) {
        return nullptr;
    }

    const u8 *base = reinterpret_cast<const u8 *>(ehdr);
    return reinterpret_cast<const Elf64_Phdr *>(base + ehdr->e_phoff + index * ehdr->e_phentsize);
}

/** @copydoc elf::flags_to_prot */
u32 flags_to_prot(u32 p_flags) {
    u32 prot = 0;
    if (p_flags & PF_R)
        prot |= viper::prot::READ;
    if (p_flags & PF_W)
        prot |= viper::prot::WRITE;
    if (p_flags & PF_X)
        prot |= viper::prot::EXEC;
    return prot;
}

} // namespace elf
