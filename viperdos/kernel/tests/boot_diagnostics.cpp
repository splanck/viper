//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//

/**
 * @file boot_diagnostics.cpp
 * @brief Boot-time diagnostic functions for verbose debugging output.
 */

#include "boot_diagnostics.hpp"
#include "../cap/handle.hpp"
#include "../console/serial.hpp"
#include "../drivers/virtio/blk.hpp"
#include "../fs/cache.hpp"
#include "../fs/vfs/vfs.hpp"
#include "../fs/viperfs/viperfs.hpp"
#include "../kobj/blob.hpp"
#include "../kobj/channel.hpp"
#include "../mm/pmm.hpp"
#include "../viper/address_space.hpp"

namespace boot_diag {

void test_block_device() {
    serial::puts("[kernel] Block device ready: ");
    serial::put_dec(virtio::blk_device()->size_bytes() / (1024 * 1024));
    serial::puts(" MB\n");

    u8 sector_buf[512];

    // Test read
    serial::puts("[kernel] Testing block read (sector 0)...\n");
    if (virtio::blk_device()->read_sectors(0, 1, sector_buf) == 0)
        serial::puts("[kernel] Read sector 0 OK!\n");
    else
        serial::puts("[kernel] Read sector 0 FAILED\n");

    // Test write and read back
    serial::puts("[kernel] Testing block write (sector 1)...\n");
    for (int i = 0; i < 512; i++)
        sector_buf[i] = static_cast<u8>(i & 0xFF);
    sector_buf[0] = 'V';
    sector_buf[1] = 'i';
    sector_buf[2] = 'p';
    sector_buf[3] = 'e';
    sector_buf[4] = 'r';

    if (virtio::blk_device()->write_sectors(1, 1, sector_buf) == 0) {
        serial::puts("[kernel] Write sector 1 OK\n");
        u8 read_buf[512] = {};
        if (virtio::blk_device()->read_sectors(1, 1, read_buf) == 0 && read_buf[0] == 'V' &&
            read_buf[1] == 'i') {
            serial::puts("[kernel] Read-back verified: ");
            for (int i = 0; i < 5; i++)
                serial::putc(read_buf[i]);
            serial::puts("\n");
        }
    } else {
        serial::puts("[kernel] Write sector 1 FAILED\n");
    }
}

void test_block_cache() {
    serial::puts("[kernel] Initializing block cache...\n");
    fs::cache_init();

    serial::puts("[kernel] Testing block cache...\n");
    fs::CacheBlock *blk0 = fs::cache().get(0);
    if (blk0) {
        serial::puts("[kernel] Cache block 0 OK, first bytes: ");
        for (int i = 0; i < 4; i++) {
            serial::put_hex(blk0->data[i]);
            serial::puts(" ");
        }
        serial::puts("\n");

        fs::CacheBlock *blk0_again = fs::cache().get(0);
        if (blk0_again == blk0)
            serial::puts("[kernel] Cache hit OK (same block returned)\n");
        fs::cache().release(blk0_again);
        fs::cache().release(blk0);

        serial::puts("[kernel] Cache stats: hits=");
        serial::put_dec(fs::cache().hits());
        serial::puts(", misses=");
        serial::put_dec(fs::cache().misses());
        serial::puts("\n");
    }
}

void test_viperfs_root(fs::viperfs::Inode *root) {
    serial::puts("[kernel] Root inode: size=");
    serial::put_dec(root->size);
    serial::puts(", mode=");
    serial::put_hex(root->mode);
    serial::puts("\n");

    serial::puts("[kernel] Directory contents:\n");
    fs::viperfs::viperfs().readdir(
        root,
        0,
        [](const char *name, usize name_len, u64 ino, u8 type, void *) {
            serial::puts("  ");
            for (usize i = 0; i < name_len; i++)
                serial::putc(name[i]);
            serial::puts(" (inode ");
            serial::put_dec(ino);
            serial::puts(", type ");
            serial::put_dec(type);
            serial::puts(")\n");
        },
        nullptr);

    // Look for hello.txt
    u64 hello_ino = fs::viperfs::viperfs().lookup(root, "hello.txt", 9);
    if (hello_ino != 0) {
        serial::puts("[kernel] Found hello.txt at inode ");
        serial::put_dec(hello_ino);
        serial::puts("\n");

        fs::viperfs::Inode *hello = fs::viperfs::viperfs().read_inode(hello_ino);
        if (hello) {
            char buf[256] = {};
            i64 bytes = fs::viperfs::viperfs().read_data(hello, 0, buf, sizeof(buf) - 1);
            if (bytes > 0) {
                serial::puts("[kernel] hello.txt contents: ");
                serial::puts(buf);
            }
            fs::viperfs::viperfs().release_inode(hello);
        }
    } else {
        serial::puts("[kernel] hello.txt not found\n");
    }
}

void test_viperfs_write(fs::viperfs::Inode *root) {
    serial::puts("[kernel] Testing file creation...\n");
    u64 test_ino = fs::viperfs::viperfs().create_file(root, "test.txt", 8);
    if (test_ino == 0)
        return;

    serial::puts("[kernel] Created test.txt at inode ");
    serial::put_dec(test_ino);
    serial::puts("\n");

    fs::viperfs::Inode *test_file = fs::viperfs::viperfs().read_inode(test_ino);
    if (test_file) {
        const char *test_data = "Written by ViperDOS kernel!";
        i64 written = fs::viperfs::viperfs().write_data(test_file, 0, test_data, 27);
        serial::puts("[kernel] Wrote ");
        serial::put_dec(written);
        serial::puts(" bytes\n");

        fs::viperfs::viperfs().write_inode(test_file);

        char verify[64] = {};
        i64 read_back = fs::viperfs::viperfs().read_data(test_file, 0, verify, sizeof(verify) - 1);
        if (read_back > 0) {
            serial::puts("[kernel] Read back: ");
            serial::puts(verify);
            serial::puts("\n");
        }
        fs::viperfs::viperfs().release_inode(test_file);
    }

    serial::puts("[kernel] Updated directory contents:\n");
    fs::viperfs::viperfs().readdir(
        root,
        0,
        [](const char *name, usize name_len, u64 ino, u8, void *) {
            serial::puts("  ");
            for (usize i = 0; i < name_len; i++)
                serial::putc(name[i]);
            serial::puts(" (inode ");
            serial::put_dec(ino);
            serial::puts(")\n");
        },
        nullptr);

    fs::viperfs::viperfs().sync();
    serial::puts("[kernel] Filesystem synced\n");
}

void test_vfs_operations() {
    serial::puts("[kernel] Testing VFS operations...\n");

    i32 fd = fs::vfs::open("/c/hello.prg", fs::vfs::flags::O_RDONLY);
    if (fd >= 0) {
        serial::puts("[kernel] Opened /c/hello.prg as fd ");
        serial::put_dec(fd);
        serial::puts("\n");

        char buf[8] = {};
        i64 bytes = fs::vfs::read(fd, buf, 4);
        if (bytes > 0) {
            serial::puts("[kernel] Read ELF header: ");
            for (int i = 0; i < 4; i++) {
                serial::put_hex(static_cast<u8>(buf[i]));
                serial::puts(" ");
            }
            serial::puts("\n");
        }
        fs::vfs::close(fd);
        serial::puts("[kernel] Closed fd\n");
    } else {
        serial::puts("[kernel] VFS open /c/hello.prg failed\n");
    }

    fd = fs::vfs::open("/t/t/vfs_test.txt", fs::vfs::flags::O_RDWR | fs::vfs::flags::O_CREAT);
    if (fd >= 0) {
        serial::puts("[kernel] Created /t/vfs_test.txt as fd ");
        serial::put_dec(fd);
        serial::puts("\n");

        const char *data = "Created via VFS!";
        i64 written = fs::vfs::write(fd, data, 16);
        serial::puts("[kernel] VFS wrote ");
        serial::put_dec(written);
        serial::puts(" bytes\n");

        fs::vfs::lseek(fd, 0, fs::vfs::seek::SET);
        char buf[32] = {};
        i64 rd = fs::vfs::read(fd, buf, sizeof(buf) - 1);
        if (rd > 0) {
            serial::puts("[kernel] VFS read back: ");
            serial::puts(buf);
            serial::puts("\n");
        }
        fs::vfs::close(fd);
    }

    fs::viperfs::viperfs().sync();
}

void test_cap_table(cap::Table *ct) {
    serial::puts("[kernel] Testing capability table...\n");

    int dummy_object = 42;
    cap::Handle h1 = ct->insert(&dummy_object, cap::Kind::Blob, cap::CAP_RW);
    if (h1 == cap::HANDLE_INVALID)
        return;

    serial::puts("[kernel] Inserted handle ");
    serial::put_hex(h1);
    serial::puts(" (index=");
    serial::put_dec(cap::handle_index(h1));
    serial::puts(", gen=");
    serial::put_dec(cap::handle_gen(h1));
    serial::puts(")\n");

    cap::Entry *e = ct->get(h1);
    if (e && e->object == &dummy_object)
        serial::puts("[kernel] Handle lookup OK\n");

    cap::Handle h2 = ct->derive(h1, cap::CAP_READ);
    if (h2 == cap::HANDLE_INVALID)
        serial::puts("[kernel] Derive failed (expected - no CAP_DERIVE)\n");

    cap::Handle h3 = ct->insert(
        &dummy_object, cap::Kind::Blob, static_cast<cap::Rights>(cap::CAP_RW | cap::CAP_DERIVE));
    cap::Handle h4 = ct->derive(h3, cap::CAP_READ);
    if (h4 != cap::HANDLE_INVALID) {
        serial::puts("[kernel] Derived handle ");
        serial::put_hex(h4);
        serial::puts(" with CAP_READ only\n");
    }

    ct->remove(h1);
    if (!ct->get(h1))
        serial::puts("[kernel] Handle correctly invalidated after remove\n");

    serial::puts("[kernel] Capability table: ");
    serial::put_dec(ct->count());
    serial::puts("/");
    serial::put_dec(ct->capacity());
    serial::puts(" slots used\n");

    serial::puts("[kernel] Testing KObj blob...\n");
    kobj::Blob *blob = kobj::Blob::create(4096);
    if (blob) {
        cap::Handle blob_h = ct->insert(blob, cap::Kind::Blob, cap::CAP_RW);
        if (blob_h != cap::HANDLE_INVALID) {
            serial::puts("[kernel] Blob handle: ");
            serial::put_hex(blob_h);
            serial::puts(", size=");
            serial::put_dec(blob->size());
            serial::puts(", phys=");
            serial::put_hex(blob->phys());
            serial::puts("\n");

            u32 *data = static_cast<u32 *>(blob->data());
            data[0] = 0xDEADBEEF;
            serial::puts("[kernel] Wrote 0xDEADBEEF to blob\n");
        }

        kobj::Channel *kch = kobj::Channel::create();
        if (kch) {
            cap::Handle ch_h = ct->insert(kch, cap::Kind::Channel, cap::CAP_RW);
            serial::puts("[kernel] KObj channel handle: ");
            serial::put_hex(ch_h);
            serial::puts(", channel_id=");
            serial::put_dec(kch->id());
            serial::puts("\n");
        }
    }
}

void test_sbrk(viper::Viper *vp) {
    serial::puts("[kernel] Testing sbrk...\n");

    u64 initial_break = vp->heap_break;
    serial::puts("[kernel]   Initial heap break: ");
    serial::put_hex(initial_break);
    serial::puts("\n");

    i64 result = viper::do_sbrk(vp, 0);
    if (result == static_cast<i64>(initial_break))
        serial::puts("[kernel]   sbrk(0) returned correct break\n");
    else
        serial::puts("[kernel]   ERROR: sbrk(0) returned wrong value\n");

    result = viper::do_sbrk(vp, 4096);
    if (result == static_cast<i64>(initial_break)) {
        serial::puts("[kernel]   sbrk(4096) returned old break\n");
        serial::puts("[kernel]   New heap break: ");
        serial::put_hex(vp->heap_break);
        serial::puts("\n");

        viper::AddressSpace *as = viper::get_address_space(vp);
        if (as) {
            u64 phys = as->translate(initial_break);
            if (phys != 0) {
                serial::puts("[kernel]   Heap page mapped to phys: ");
                serial::put_hex(phys);
                serial::puts("\n");

                u32 *ptr = static_cast<u32 *>(pmm::phys_to_virt(phys));
                ptr[0] = 0xCAFEBABE;
                if (ptr[0] == 0xCAFEBABE)
                    serial::puts("[kernel]   Heap memory R/W test PASSED\n");
                else
                    serial::puts("[kernel]   ERROR: Heap memory R/W test FAILED\n");
            } else {
                serial::puts("[kernel]   ERROR: Heap page not mapped!\n");
            }
        }
    } else {
        serial::puts("[kernel]   ERROR: sbrk(4096) failed with ");
        serial::put_dec(result);
        serial::puts("\n");
    }

    result = viper::do_sbrk(vp, 8192);
    if (result > 0) {
        serial::puts("[kernel]   sbrk(8192) succeeded, new break: ");
        serial::put_hex(vp->heap_break);
        serial::puts("\n");
    }

    serial::puts("[kernel] sbrk test complete\n");
}

void test_address_space(viper::Viper *vp) {
    viper::AddressSpace *as = viper::get_address_space(vp);
    if (!as || !as->is_valid())
        return;

    u64 test_vaddr = viper::layout::USER_HEAP_BASE;
    u64 test_page = as->alloc_map(test_vaddr, 4096, viper::prot::RW);
    if (test_page) {
        serial::puts("[kernel] Mapped test page at ");
        serial::put_hex(test_vaddr);
        serial::puts("\n");

        u64 phys = as->translate(test_vaddr);
        serial::puts("[kernel] Translates to physical ");
        serial::put_hex(phys);
        serial::puts("\n");

        as->unmap(test_vaddr, 4096);
        serial::puts("[kernel] Unmapped test page\n");
    }
}

} // namespace boot_diag
