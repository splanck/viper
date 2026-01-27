//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
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
#include "../cap/handle.hpp"
#include "../cap/table.hpp"
#include "../console/serial.hpp"
#include "../fs/vfs/vfs.hpp"
#include "../mm/kheap.hpp"
#include "../mm/pmm.hpp"
#include "../mm/vma.hpp"
#include "../sched/scheduler.hpp"
#include "../sched/task.hpp"
#include "../viper/address_space.hpp"
#include "elf.hpp"

namespace loader {

namespace {

/**
 * @brief Flush instruction cache for executable segment.
 */
void flush_icache(u64 phys, usize size) {
    for (usize j = 0; j < size; j += 64) {
        u64 addr = phys + j;
        asm volatile("dc cvau, %0" ::"r"(addr));
    }
    asm volatile("dsb ish");
    for (usize j = 0; j < size; j += 64) {
        u64 addr = phys + j;
        asm volatile("ic ivau, %0" ::"r"(addr));
    }
    asm volatile("dsb ish");
    asm volatile("isb");
}

/**
 * @brief Load a single PT_LOAD segment into the address space.
 * @return Segment end address on success, 0 on failure.
 */
u64 load_segment(viper::AddressSpace *as,
                 const elf::Elf64_Phdr *phdr,
                 const u8 *file_data,
                 usize elf_size,
                 u64 base_addr,
                 u16 seg_idx) {
    u64 vaddr = base_addr + phdr->p_vaddr;
    u64 vaddr_aligned = vaddr & ~0xFFFULL;
    u64 offset_in_page = vaddr & 0xFFF;
    usize mem_size = phdr->p_memsz + offset_in_page;
    usize pages = (mem_size + 0xFFF) >> 12;

    serial::puts("[loader] Segment ");
    serial::put_dec(seg_idx);
    serial::puts(": vaddr=");
    serial::put_hex(vaddr);
    serial::puts(", filesz=");
    serial::put_dec(phdr->p_filesz);
    serial::puts(", memsz=");
    serial::put_dec(phdr->p_memsz);
    serial::puts(", pages=");
    serial::put_dec(pages);
    serial::puts("\n");

    // Verify vinit's page tables before allocation
    viper::debug_verify_vinit_tables("before alloc_map");

    u32 prot = elf::flags_to_prot(phdr->p_flags);

    if (as->alloc_map(vaddr_aligned, pages * 4096, prot) == 0) {
        serial::puts("[loader] Failed to map segment\n");
        return 0;
    }

    // Verify after alloc_map
    viper::debug_verify_vinit_tables("after alloc_map");

    u64 phys = as->translate(vaddr_aligned);
    if (phys == 0) {
        serial::puts("[loader] Failed to translate segment address\n");
        return 0;
    }


    u8 *dest = reinterpret_cast<u8 *>(pmm::phys_to_virt(phys));

    // Verify before zeroing
    viper::debug_verify_vinit_tables("before zeroing");

    for (usize j = 0; j < pages * 4096; j++)
        dest[j] = 0;

    // Verify after zeroing
    viper::debug_verify_vinit_tables("after zeroing");

    if (phdr->p_filesz > 0) {
        if (phdr->p_offset + phdr->p_filesz > elf_size) {
            serial::puts("[loader] Segment extends beyond file\n");
            return 0;
        }
        const u8 *src = file_data + phdr->p_offset;

        // Verify before copy
        viper::debug_verify_vinit_tables("before memcpy");

        for (usize j = 0; j < phdr->p_filesz; j++)
            dest[offset_in_page + j] = src[j];

        // Verify after copy
        viper::debug_verify_vinit_tables("after memcpy");
    }

    serial::puts("[loader] Segment loaded OK\n");

    if (prot & viper::prot::EXEC)
        flush_icache(phys, pages * 4096);

    return vaddr + phdr->p_memsz;
}

} // anonymous namespace

/** @copydoc loader::load_elf */
LoadResult load_elf(viper::Viper *v, const void *elf_data, usize elf_size) {
    LoadResult result = {false, 0, 0, 0};

    if (!v || !elf_data || elf_size < sizeof(elf::Elf64_Ehdr)) {
        serial::puts("[loader] Invalid parameters\n");
        return result;
    }

    const elf::Elf64_Ehdr *ehdr = static_cast<const elf::Elf64_Ehdr *>(elf_data);
    if (!elf::validate_header(ehdr)) {
        serial::puts("[loader] Invalid ELF header\n");
        return result;
    }

    serial::puts("[loader] Loading ELF: entry=");
    serial::put_hex(ehdr->e_entry);
    serial::puts(", phnum=");
    serial::put_dec(ehdr->e_phnum);
    serial::puts("\n");

    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as || !as->is_valid()) {
        serial::puts("[loader] No valid address space\n");
        return result;
    }

    u64 base_addr = (ehdr->e_type == elf::ET_DYN) ? viper::layout::USER_CODE_BASE : 0;
    u64 max_addr = 0;
    const u8 *file_data = static_cast<const u8 *>(elf_data);

    for (u16 i = 0; i < ehdr->e_phnum; i++) {
        const elf::Elf64_Phdr *phdr = elf::get_phdr(ehdr, i);
        if (!phdr || phdr->p_type != elf::PT_LOAD)
            continue;

        u64 segment_end = load_segment(as, phdr, file_data, elf_size, base_addr, i);
        if (segment_end == 0)
            return result;

        if (segment_end > max_addr)
            max_addr = segment_end;
    }

    result.success = true;
    result.entry_point = base_addr + ehdr->e_entry;
    result.base_addr = base_addr;
    result.brk = (max_addr + 0xFFF) & ~0xFFFULL;

    serial::puts("[loader] ELF loaded: entry=");
    serial::put_hex(result.entry_point);
    serial::puts(", brk=");
    serial::put_hex(result.brk);
    serial::puts("\n");

    return result;
}

/** @copydoc loader::load_elf_from_blob */
LoadResult load_elf_from_blob(viper::Viper *v, const void *data, usize size) {
    return load_elf(v, data, size);
}

/** @copydoc loader::load_elf_from_disk */
LoadResult load_elf_from_disk(viper::Viper *v, const char *path) {
    LoadResult result = {false, 0, 0, 0};

    if (!v || !path) {
        serial::puts("[loader] Invalid parameters for disk load\n");
        return result;
    }

    serial::puts("[loader] Loading ELF from disk: ");
    serial::puts(path);
    serial::puts("\n");

    // Verify before opening file
    viper::debug_verify_vinit_tables("before vfs::open");

    // Open the file
    i32 fd = fs::vfs::open(path, fs::vfs::flags::O_RDONLY);
    if (fd < 0) {
        serial::puts("[loader] Failed to open file\n");
        return result;
    }

    // Verify after opening file
    viper::debug_verify_vinit_tables("after vfs::open");

    // Get file size using stat
    fs::vfs::Stat st;
    if (fs::vfs::fstat(fd, &st) < 0) {
        serial::puts("[loader] Failed to stat file\n");
        fs::vfs::close(fd);
        return result;
    }

    usize file_size = static_cast<usize>(st.size);
    serial::puts("[loader] File size: ");
    serial::put_dec(file_size);
    serial::puts(" bytes\n");

    if (file_size < sizeof(elf::Elf64_Ehdr)) {
        serial::puts("[loader] File too small to be an ELF\n");
        fs::vfs::close(fd);
        return result;
    }

    // Verify before kmalloc
    viper::debug_verify_vinit_tables("before kmalloc");

    // Allocate buffer for file contents
    void *buf = kheap::kmalloc(file_size);
    if (!buf) {
        serial::puts("[loader] Failed to allocate buffer\n");
        fs::vfs::close(fd);
        return result;
    }

    // Debug: Show buffer address
    serial::puts("[loader] ELF buffer at ");
    serial::put_hex(reinterpret_cast<u64>(buf));
    serial::puts("\n");

    // Verify after kmalloc
    viper::debug_verify_vinit_tables("after kmalloc");

    // Read entire file
    i64 bytes_read = fs::vfs::read(fd, buf, file_size);

    // Verify after read
    viper::debug_verify_vinit_tables("after vfs::read");

    fs::vfs::close(fd);

    if (bytes_read < 0 || static_cast<usize>(bytes_read) != file_size) {
        serial::puts("[loader] Failed to read file\n");
        kheap::kfree(buf);
        return result;
    }

    // Verify before load_elf
    viper::debug_verify_vinit_tables("before load_elf");

    // Load the ELF
    result = load_elf(v, buf, file_size);

    // Free the buffer
    kheap::kfree(buf);

    return result;
}

/**
 * @brief Internal helper to set up user stack for a new process.
 *
 * @param as The address space.
 * @return Stack top address, or 0 on failure.
 */
static u64 setup_user_stack(viper::AddressSpace *as) {
    // Allocate and map stack pages
    u64 stack_base = viper::layout::USER_STACK_TOP - viper::layout::USER_STACK_SIZE;
    u64 stack_size = viper::layout::USER_STACK_SIZE;

    u64 mapped = as->alloc_map(stack_base, stack_size, viper::prot::READ | viper::prot::WRITE);
    if (mapped == 0) {
        serial::puts("[loader] Failed to map user stack\n");
        return 0;
    }

    // Zero the stack (convert physical to virtual address)
    u64 phys = as->translate(stack_base);
    if (phys != 0) {
        u8 *stack_mem = reinterpret_cast<u8 *>(pmm::phys_to_virt(phys));
        for (usize i = 0; i < stack_size; i++) {
            stack_mem[i] = 0;
        }
    }

    serial::puts("[loader] User stack mapped at ");
    serial::put_hex(stack_base);
    serial::puts(" - ");
    serial::put_hex(viper::layout::USER_STACK_TOP);
    serial::puts("\n");

    // Return stack top (stack grows down)
    return viper::layout::USER_STACK_TOP;
}

/**
 * @brief Internal helper to complete process spawn after ELF is loaded.
 */
static SpawnResult complete_spawn(viper::Viper *v,
                                  const LoadResult &load_result,
                                  const char *name) {
    SpawnResult result = {false, nullptr, 0};

    if (!load_result.success) {
        serial::puts("[loader] ELF load failed, destroying process\n");
        viper::destroy(v);
        return result;
    }

    // Get address space
    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as) {
        serial::puts("[loader] No address space for process\n");
        viper::destroy(v);
        return result;
    }

    // Set up user stack
    u64 stack_top = setup_user_stack(as);
    if (stack_top == 0) {
        viper::destroy(v);
        return result;
    }

    // Update heap tracking
    v->heap_start = load_result.brk;
    v->heap_break = load_result.brk;

    // DEBUG: Print name pointer and current TTBR0 before create_user_task
    u64 ttbr0_before;
    asm volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0_before));
    serial::puts("[loader] complete_spawn: name ptr=");
    serial::put_hex(reinterpret_cast<u64>(name));
    serial::puts(", ttbr0=");
    serial::put_hex(ttbr0_before);
    serial::puts(", new viper ttbr0=");
    serial::put_hex(v->ttbr0);
    serial::puts("\n");

    // Create user task
    task::Task *t = task::create_user_task(name, v, load_result.entry_point, stack_top);
    if (!t) {
        serial::puts("[loader] Failed to create user task\n");
        viper::destroy(v);
        return result;
    }

    // Link task to viper
    t->viper = reinterpret_cast<task::ViperProcess *>(v);
    v->task_list = t;
    v->task_count = 1;

    // Enqueue task for scheduling
    scheduler::enqueue(t);

    serial::puts("[loader] Process '");
    serial::puts(name);
    serial::puts("' spawned: pid=");
    serial::put_dec(v->id);
    serial::puts(", tid=");
    serial::put_dec(t->id);
    serial::puts(", entry=");
    serial::put_hex(load_result.entry_point);
    serial::puts("\n");

    result.success = true;
    result.viper = v;
    result.task_id = t->id;

    return result;
}

/** @copydoc loader::spawn_process */
SpawnResult spawn_process(const char *path, const char *name, viper::Viper *parent) {
    SpawnResult result = {false, nullptr, 0};

    if (!path || !name) {
        serial::puts("[loader] spawn_process: invalid parameters\n");
        return result;
    }

    serial::puts("[loader] Spawning process '");
    serial::puts(name);
    serial::puts("' from ");
    serial::puts(path);
    serial::puts("\n");

    // Verify vinit's page tables before creating new process
    viper::debug_verify_vinit_tables("before viper::create");

    // Create new process
    viper::Viper *v = viper::create(parent, name);
    if (!v) {
        serial::puts("[loader] Failed to create Viper process\n");
        return result;
    }

    // Verify after creating new process
    viper::debug_verify_vinit_tables("after viper::create");

    // Load ELF from disk
    LoadResult load_result = load_elf_from_disk(v, path);

    // Verify after loading ELF
    viper::debug_verify_vinit_tables("after load_elf_from_disk");

    return complete_spawn(v, load_result, name);
}

/** @copydoc loader::spawn_process_from_blob */
SpawnResult spawn_process_from_blob(const void *elf_data,
                                    usize elf_size,
                                    const char *name,
                                    viper::Viper *parent) {
    SpawnResult result = {false, nullptr, 0};

    if (!elf_data || elf_size == 0 || !name) {
        serial::puts("[loader] spawn_process_from_blob: invalid parameters\n");
        return result;
    }

    serial::puts("[loader] Spawning process '");
    serial::puts(name);
    serial::puts("' from blob (");
    serial::put_dec(elf_size);
    serial::puts(" bytes)\n");

    // Create new process
    viper::Viper *v = viper::create(parent, name);
    if (!v) {
        serial::puts("[loader] Failed to create Viper process\n");
        return result;
    }

    // Load ELF from memory
    LoadResult load_result = load_elf(v, elf_data, elf_size);

    return complete_spawn(v, load_result, name);
}

/** @copydoc loader::replace_process */
ReplaceResult replace_process(const char *path,
                              const cap::Handle *preserve_handles,
                              u32 preserve_count) {
    ReplaceResult result = {false, 0};

    if (!path) {
        serial::puts("[loader] replace_process: invalid path\n");
        return result;
    }

    // Get current process
    viper::Viper *v = viper::current();
    if (!v) {
        serial::puts("[loader] replace_process: no current process\n");
        return result;
    }

    serial::puts("[loader] Replacing process '");
    serial::puts(v->name);
    serial::puts("' with ");
    serial::puts(path);
    serial::puts("\n");

    // Get address space
    viper::AddressSpace *as = viper::get_address_space(v);
    if (!as) {
        serial::puts("[loader] replace_process: no address space\n");
        return result;
    }

    // Clear VMA list and unmap all user pages
    // Walk through all VMAs and unmap them
    mm::Vma *vma = v->vma_list.head();
    while (vma) {
        mm::Vma *next = vma->next;
        // Unmap the region
        as->unmap(vma->start, vma->end - vma->start);
        vma = next;
    }

    // Clear the VMA list
    v->vma_list.clear();

    // Handle capability preservation
    cap::Table *ct = v->cap_table;
    if (ct) {
        if (preserve_handles && preserve_count > 0) {
            // Build a set of handles to preserve
            // Then remove all others
            for (usize i = 0; i < ct->capacity(); i++) {
                cap::Entry *e = ct->entry_at(i);
                if (e && e->kind != cap::Kind::Invalid) {
                    cap::Handle h = cap::make_handle(static_cast<u32>(i), e->generation);

                    // Check if this handle should be preserved
                    bool preserve = false;
                    for (u32 j = 0; j < preserve_count; j++) {
                        if (preserve_handles[j] == h) {
                            preserve = true;
                            break;
                        }
                    }

                    if (!preserve) {
                        ct->remove(h);
                    }
                }
            }
        } else {
            // No preservation - remove all capabilities
            for (usize i = 0; i < ct->capacity(); i++) {
                cap::Entry *e = ct->entry_at(i);
                if (e && e->kind != cap::Kind::Invalid) {
                    cap::Handle h = cap::make_handle(static_cast<u32>(i), e->generation);
                    ct->remove(h);
                }
            }
        }
    }

    // Re-add heap and stack VMAs
    v->vma_list.add(viper::layout::USER_HEAP_BASE,
                    v->heap_max,
                    mm::vma_prot::READ | mm::vma_prot::WRITE,
                    mm::VmaType::ANONYMOUS);

    u64 stack_bottom = viper::layout::USER_STACK_TOP - viper::layout::USER_STACK_SIZE;
    v->vma_list.add(stack_bottom,
                    viper::layout::USER_STACK_TOP,
                    mm::vma_prot::READ | mm::vma_prot::WRITE,
                    mm::VmaType::STACK);

    // Load the new ELF
    LoadResult load_result = load_elf_from_disk(v, path);
    if (!load_result.success) {
        serial::puts("[loader] replace_process: ELF load failed\n");
        return result;
    }

    // Set up new user stack
    u64 stack_top = setup_user_stack(as);
    if (stack_top == 0) {
        serial::puts("[loader] replace_process: stack setup failed\n");
        return result;
    }

    // Reset heap tracking
    v->heap_start = load_result.brk;
    v->heap_break = load_result.brk;

    // Update process name from path
    const char *name_start = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/') {
            name_start = p + 1;
        }
    }
    int i = 0;
    while (name_start[i] && i < 31) {
        v->name[i] = name_start[i];
        i++;
    }
    v->name[i] = '\0';

    serial::puts("[loader] Process replaced: new entry=");
    serial::put_hex(load_result.entry_point);
    serial::puts("\n");

    result.success = true;
    result.entry_point = load_result.entry_point;

    return result;
}

} // namespace loader
