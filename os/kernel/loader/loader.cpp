/**
 * @file loader.cpp
 * @brief ELF loader implementation.
 *
 * @details
 * Implements the high-level image loading routines declared in `loader.hpp`.
 * The implementation performs a straightforward PT_LOAD segment mapping and
 * copy into the target process address space, then returns an entry point and
 * initial break suitable for starting the program.
 *
 * The code is designed for a freestanding kernel environment and avoids libc
 * dependencies. It also assumes the kernel can write to newly allocated
 * user-mapped physical pages via an identity mapping during bring-up.
 */

#include "loader.hpp"
#include "../arch/aarch64/mmu.hpp"
#include "../console/serial.hpp"
#include "../fs/vfs/vfs.hpp"
#include "../mm/kheap.hpp"
#include "../mm/pmm.hpp"
#include "../viper/address_space.hpp"
#include "elf.hpp"

namespace loader
{

/** @copydoc loader::load_elf */
LoadResult load_elf(viper::Viper *v, const void *elf_data, usize elf_size)
{
    LoadResult result = {false, 0, 0, 0};

    if (!v || !elf_data || elf_size < sizeof(elf::Elf64_Ehdr))
    {
        serial::puts("[loader] Invalid parameters\n");
        return result;
    }

    const elf::Elf64_Ehdr *ehdr = static_cast<const elf::Elf64_Ehdr *>(elf_data);

    // Validate ELF header
    if (!elf::validate_header(ehdr))
    {
        serial::puts("[loader] Invalid ELF header\n");
        return result;
    }

    serial::puts("[loader] Loading ELF: entry=");
    serial::put_hex(ehdr->e_entry);
    serial::puts(", phnum=");
    serial::put_dec(ehdr->e_phnum);
    serial::puts("\n");

    // Get address space
    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as || !as->is_valid())
    {
        serial::puts("[loader] No valid address space\n");
        return result;
    }

    // For PIE binaries, we need a base address
    // Use USER_CODE_BASE as the load base
    u64 base_addr = 0;
    if (ehdr->e_type == elf::ET_DYN)
    {
        base_addr = viper::layout::USER_CODE_BASE;
    }

    u64 max_addr = 0;
    const u8 *file_data = static_cast<const u8 *>(elf_data);

    // Load each PT_LOAD segment
    for (u16 i = 0; i < ehdr->e_phnum; i++)
    {
        const elf::Elf64_Phdr *phdr = elf::get_phdr(ehdr, i);
        if (!phdr || phdr->p_type != elf::PT_LOAD)
        {
            continue;
        }

        // Calculate virtual address (with potential base offset for PIE)
        u64 vaddr = base_addr + phdr->p_vaddr;
        u64 vaddr_aligned = vaddr & ~0xFFFULL; // Page-align down
        u64 offset_in_page = vaddr & 0xFFF;

        // Calculate size needed (including offset and rounding up)
        usize mem_size = phdr->p_memsz + offset_in_page;
        usize pages = (mem_size + 0xFFF) >> 12;

        serial::puts("[loader] Segment ");
        serial::put_dec(i);
        serial::puts(": vaddr=");
        serial::put_hex(vaddr);
        serial::puts(", filesz=");
        serial::put_dec(phdr->p_filesz);
        serial::puts(", memsz=");
        serial::put_dec(phdr->p_memsz);
        serial::puts(", pages=");
        serial::put_dec(pages);
        serial::puts("\n");

        // Convert flags to protection
        u32 prot = elf::flags_to_prot(phdr->p_flags);

        // Allocate and map pages
        u64 mapped = as->alloc_map(vaddr_aligned, pages * 4096, prot);
        if (mapped == 0)
        {
            serial::puts("[loader] Failed to map segment\n");
            return result;
        }

        // Get physical address for copying
        u64 phys = as->translate(vaddr_aligned);
        if (phys == 0)
        {
            serial::puts("[loader] Failed to translate segment address\n");
            return result;
        }

        // Zero the entire region first (for BSS)
        u8 *dest = reinterpret_cast<u8 *>(phys);
        for (usize j = 0; j < pages * 4096; j++)
        {
            dest[j] = 0;
        }

        // Copy file data if any
        if (phdr->p_filesz > 0)
        {
            if (phdr->p_offset + phdr->p_filesz > elf_size)
            {
                serial::puts("[loader] Segment extends beyond file\n");
                return result;
            }

            const u8 *src = file_data + phdr->p_offset;
            for (usize j = 0; j < phdr->p_filesz; j++)
            {
                dest[offset_in_page + j] = src[j];
            }
        }

        serial::puts("[loader] Segment loaded OK\n");

        // Clean data cache and invalidate instruction cache for code segments
        if (prot & viper::prot::EXEC)
        {
            for (usize j = 0; j < pages * 4096; j += 64)
            {
                u64 addr = phys + j;
                asm volatile("dc cvau, %0" ::"r"(addr));
            }
            asm volatile("dsb ish");
            for (usize j = 0; j < pages * 4096; j += 64)
            {
                u64 addr = phys + j;
                asm volatile("ic ivau, %0" ::"r"(addr));
            }
            asm volatile("dsb ish");
            asm volatile("isb");
        }

        // Track max address for brk
        u64 segment_end = vaddr + phdr->p_memsz;
        if (segment_end > max_addr)
        {
            max_addr = segment_end;
        }
    }

    // Success!
    result.success = true;
    result.entry_point = base_addr + ehdr->e_entry;
    result.base_addr = base_addr;
    result.brk = (max_addr + 0xFFF) & ~0xFFFULL; // Page-align up

    serial::puts("[loader] ELF loaded: entry=");
    serial::put_hex(result.entry_point);
    serial::puts(", brk=");
    serial::put_hex(result.brk);
    serial::puts("\n");

    return result;
}

/** @copydoc loader::load_elf_from_blob */
LoadResult load_elf_from_blob(viper::Viper *v, const void *data, usize size)
{
    return load_elf(v, data, size);
}

/** @copydoc loader::load_elf_from_disk */
LoadResult load_elf_from_disk(viper::Viper *v, const char *path)
{
    LoadResult result = {false, 0, 0, 0};

    if (!v || !path)
    {
        serial::puts("[loader] Invalid parameters for disk load\n");
        return result;
    }

    serial::puts("[loader] Loading ELF from disk: ");
    serial::puts(path);
    serial::puts("\n");

    // Open the file
    i32 fd = fs::vfs::open(path, fs::vfs::flags::O_RDONLY);
    if (fd < 0)
    {
        serial::puts("[loader] Failed to open file\n");
        return result;
    }

    // Get file size using stat
    fs::vfs::Stat st;
    if (fs::vfs::fstat(fd, &st) < 0)
    {
        serial::puts("[loader] Failed to stat file\n");
        fs::vfs::close(fd);
        return result;
    }

    usize file_size = static_cast<usize>(st.size);
    serial::puts("[loader] File size: ");
    serial::put_dec(file_size);
    serial::puts(" bytes\n");

    if (file_size < sizeof(elf::Elf64_Ehdr))
    {
        serial::puts("[loader] File too small to be an ELF\n");
        fs::vfs::close(fd);
        return result;
    }

    // Allocate buffer for file contents
    void *buf = kheap::kmalloc(file_size);
    if (!buf)
    {
        serial::puts("[loader] Failed to allocate buffer\n");
        fs::vfs::close(fd);
        return result;
    }

    // Read entire file
    i64 bytes_read = fs::vfs::read(fd, buf, file_size);
    fs::vfs::close(fd);

    if (bytes_read < 0 || static_cast<usize>(bytes_read) != file_size)
    {
        serial::puts("[loader] Failed to read file\n");
        kheap::kfree(buf);
        return result;
    }

    // Load the ELF
    result = load_elf(v, buf, file_size);

    // Free the buffer
    kheap::kfree(buf);

    return result;
}

} // namespace loader
