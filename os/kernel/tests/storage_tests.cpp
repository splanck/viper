/**
 * @file storage_tests.cpp
 * @brief Tests for Assign system, VFS, and ViperFS.
 *
 * @details
 * This file contains tests that verify:
 * - Assign name resolution and management
 * - VFS file operations (open, read, write, close, seek)
 * - Directory operations (mkdir, getdents, rmdir)
 * - Path resolution
 */

#include "../assign/assign.hpp"
#include "../console/serial.hpp"
#include "../fs/vfs/vfs.hpp"
#include "../fs/viperfs/viperfs.hpp"
#include "tests.hpp"

namespace tests
{

// Test result tracking
static int tests_passed = 0;
static int tests_failed = 0;

static void test_pass(const char *name)
{
    serial::puts("[TEST] ");
    serial::puts(name);
    serial::puts(" PASSED\n");
    tests_passed++;
}

static void test_fail(const char *name, const char *reason)
{
    serial::puts("[TEST] ");
    serial::puts(name);
    serial::puts(" FAILED: ");
    serial::puts(reason);
    serial::puts("\n");
    tests_failed++;
}

// ============================================================================
// Assign System Tests
// ============================================================================

static void test_assign_sys_exists()
{
    const char *name = "assign_sys_exists";
    if (viper::assign::exists("SYS"))
    {
        test_pass(name);
    }
    else
    {
        test_fail(name, "SYS assign not found");
    }
}

static void test_assign_d0_exists()
{
    const char *name = "assign_d0_exists";
    if (viper::assign::exists("D0"))
    {
        test_pass(name);
    }
    else
    {
        test_fail(name, "D0 assign not found");
    }
}

static void test_assign_case_insensitive()
{
    const char *name = "assign_case_insensitive";
    // SYS should match sys, Sys, SYS, etc.
    u64 inode1 = viper::assign::get_inode("SYS");
    u64 inode2 = viper::assign::get_inode("sys");
    u64 inode3 = viper::assign::get_inode("Sys");

    if (inode1 != 0 && inode1 == inode2 && inode2 == inode3)
    {
        test_pass(name);
    }
    else
    {
        test_fail(name, "case-insensitive lookup failed");
    }
}

static void test_assign_is_system()
{
    const char *name = "assign_is_system";
    if (viper::assign::is_system("SYS") && viper::assign::is_system("D0"))
    {
        test_pass(name);
    }
    else
    {
        test_fail(name, "system assigns not marked as system");
    }
}

static void test_assign_nonexistent()
{
    const char *name = "assign_nonexistent";
    if (!viper::assign::exists("NONEXISTENT123"))
    {
        test_pass(name);
    }
    else
    {
        test_fail(name, "nonexistent assign reported as existing");
    }
}

static void test_assign_create_remove()
{
    const char *name = "assign_create_remove";

    // Create a new assign
    auto err = viper::assign::set("TEST", 1, viper::assign::ASSIGN_NONE);
    if (err != viper::assign::AssignError::OK)
    {
        test_fail(name, "failed to create assign");
        return;
    }

    if (!viper::assign::exists("TEST"))
    {
        test_fail(name, "assign not found after creation");
        return;
    }

    // Remove it
    err = viper::assign::remove("TEST");
    if (err != viper::assign::AssignError::OK)
    {
        test_fail(name, "failed to remove assign");
        return;
    }

    if (viper::assign::exists("TEST"))
    {
        test_fail(name, "assign still exists after removal");
        return;
    }

    test_pass(name);
}

static void test_assign_system_readonly()
{
    const char *name = "assign_system_readonly";

    // Attempt to remove SYS (should fail)
    auto err = viper::assign::remove("SYS");
    if (err == viper::assign::AssignError::ReadOnly)
    {
        test_pass(name);
    }
    else
    {
        test_fail(name, "system assign was not protected");
    }
}

static void test_assign_parse_path()
{
    const char *name = "assign_parse_path";
    char assign_name[32];
    const char *remainder = nullptr;

    if (viper::assign::parse_assign("SYS:test/file.txt", assign_name, &remainder))
    {
        // Check assign name is "SYS"
        bool name_ok = (assign_name[0] == 'S' && assign_name[1] == 'Y' && assign_name[2] == 'S' &&
                        assign_name[3] == '\0');
        // Check remainder is "test/file.txt"
        bool rem_ok = (remainder != nullptr && remainder[0] == 't');

        if (name_ok && rem_ok)
        {
            test_pass(name);
        }
        else
        {
            test_fail(name, "parsed values incorrect");
        }
    }
    else
    {
        test_fail(name, "parse_assign returned false");
    }
}

static void test_assign_list()
{
    const char *name = "assign_list";
    viper::assign::AssignInfo info[16];
    int count = viper::assign::list(info, 16);

    if (count >= 2)
    { // At least SYS and D0
        test_pass(name);
    }
    else
    {
        test_fail(name, "list returned fewer than 2 assigns");
    }
}

// ============================================================================
// VFS File Operation Tests
// ============================================================================

static void test_vfs_open_close()
{
    const char *name = "vfs_open_close";

    // Try to open the root directory
    i32 fd = fs::vfs::open("/", fs::vfs::flags::O_RDONLY);
    if (fd < 0)
    {
        test_fail(name, "failed to open root directory");
        return;
    }

    i32 result = fs::vfs::close(fd);
    if (result < 0)
    {
        test_fail(name, "failed to close fd");
        return;
    }

    test_pass(name);
}

static void test_vfs_invalid_fd()
{
    const char *name = "vfs_invalid_fd";

    // Operations on invalid fd should fail
    char buf[32];
    i64 result = fs::vfs::read(999, buf, sizeof(buf));
    if (result < 0)
    {
        test_pass(name);
    }
    else
    {
        test_fail(name, "read on invalid fd succeeded");
    }
}

static void test_vfs_getdents()
{
    const char *name = "vfs_getdents";

    i32 fd = fs::vfs::open("/", fs::vfs::flags::O_RDONLY);
    if (fd < 0)
    {
        test_fail(name, "failed to open root directory");
        return;
    }

    char buf[512];
    i64 bytes = fs::vfs::getdents(fd, buf, sizeof(buf));

    fs::vfs::close(fd);

    if (bytes > 0)
    {
        test_pass(name);
    }
    else
    {
        test_fail(name, "getdents returned no entries");
    }
}

static void test_vfs_file_create_write_read()
{
    const char *name = "vfs_file_create_write_read";

    // Create a test file
    i32 fd = fs::vfs::open("/testfile.txt", fs::vfs::flags::O_RDWR | fs::vfs::flags::O_CREAT);
    if (fd < 0)
    {
        test_fail(name, "failed to create file");
        return;
    }

    // Write data
    const char *test_data = "Hello, ViperOS!";
    i64 written = fs::vfs::write(fd, test_data, 15);
    if (written != 15)
    {
        fs::vfs::close(fd);
        test_fail(name, "write returned wrong count");
        return;
    }

    // Seek back to start
    i64 pos = fs::vfs::lseek(fd, 0, fs::vfs::seek::SET);
    if (pos != 0)
    {
        fs::vfs::close(fd);
        test_fail(name, "seek failed");
        return;
    }

    // Read it back
    char buf[32] = {0};
    i64 readbytes = fs::vfs::read(fd, buf, sizeof(buf));
    fs::vfs::close(fd);

    if (readbytes >= 15)
    {
        // Compare data
        bool match = true;
        for (int i = 0; i < 15; i++)
        {
            if (buf[i] != test_data[i])
            {
                match = false;
                break;
            }
        }
        if (match)
        {
            test_pass(name);
        }
        else
        {
            test_fail(name, "data mismatch");
        }
    }
    else
    {
        test_fail(name, "read returned wrong count");
    }

    // Cleanup
    fs::vfs::unlink("/testfile.txt");
}

static void test_vfs_mkdir_rmdir()
{
    const char *name = "vfs_mkdir_rmdir";

    // Create directory
    i32 result = fs::vfs::mkdir("/testdir");
    if (result < 0)
    {
        test_fail(name, "mkdir failed");
        return;
    }

    // Verify it exists by opening
    i32 fd = fs::vfs::open("/testdir", fs::vfs::flags::O_RDONLY);
    if (fd < 0)
    {
        test_fail(name, "directory not found after mkdir");
        return;
    }
    fs::vfs::close(fd);

    // Remove it
    result = fs::vfs::rmdir("/testdir");
    if (result < 0)
    {
        test_fail(name, "rmdir failed");
        return;
    }

    // Verify it's gone
    fd = fs::vfs::open("/testdir", fs::vfs::flags::O_RDONLY);
    if (fd >= 0)
    {
        fs::vfs::close(fd);
        test_fail(name, "directory still exists after rmdir");
        return;
    }

    test_pass(name);
}

static void test_vfs_seek_operations()
{
    const char *name = "vfs_seek_operations";

    // Create a test file with known content
    i32 fd = fs::vfs::open("/seektest.txt", fs::vfs::flags::O_RDWR | fs::vfs::flags::O_CREAT);
    if (fd < 0)
    {
        test_fail(name, "failed to create file");
        return;
    }

    // Write 100 bytes
    char data[100];
    for (int i = 0; i < 100; i++)
        data[i] = static_cast<char>(i);
    fs::vfs::write(fd, data, 100);

    // Test SEEK_SET
    i64 pos = fs::vfs::lseek(fd, 50, fs::vfs::seek::SET);
    if (pos != 50)
    {
        fs::vfs::close(fd);
        fs::vfs::unlink("/seektest.txt");
        test_fail(name, "SEEK_SET failed");
        return;
    }

    // Test SEEK_CUR
    pos = fs::vfs::lseek(fd, 10, fs::vfs::seek::CUR);
    if (pos != 60)
    {
        fs::vfs::close(fd);
        fs::vfs::unlink("/seektest.txt");
        test_fail(name, "SEEK_CUR failed");
        return;
    }

    // Test SEEK_END
    pos = fs::vfs::lseek(fd, -10, fs::vfs::seek::END);
    if (pos != 90)
    {
        fs::vfs::close(fd);
        fs::vfs::unlink("/seektest.txt");
        test_fail(name, "SEEK_END failed");
        return;
    }

    fs::vfs::close(fd);
    fs::vfs::unlink("/seektest.txt");
    test_pass(name);
}

static void test_vfs_stat()
{
    const char *name = "vfs_stat";

    fs::vfs::Stat st;
    i32 result = fs::vfs::stat("/", &st);

    if (result == 0 && st.ino != 0)
    {
        test_pass(name);
    }
    else
    {
        test_fail(name, "stat on root failed");
    }
}

// ============================================================================
// Main Test Runner
// ============================================================================

void run_storage_tests()
{
    serial::puts("\n");
    serial::puts("========================================\n");
    serial::puts("  ViperOS Storage Subsystem Tests\n");
    serial::puts("========================================\n\n");

    tests_passed = 0;
    tests_failed = 0;

    // Assign tests
    serial::puts("[SUITE] Assign System Tests\n");
    test_assign_sys_exists();
    test_assign_d0_exists();
    test_assign_case_insensitive();
    test_assign_is_system();
    test_assign_nonexistent();
    test_assign_create_remove();
    test_assign_system_readonly();
    test_assign_parse_path();
    test_assign_list();

    // VFS tests
    serial::puts("\n[SUITE] VFS Tests\n");
    test_vfs_open_close();
    test_vfs_invalid_fd();
    test_vfs_getdents();
    test_vfs_file_create_write_read();
    test_vfs_mkdir_rmdir();
    test_vfs_seek_operations();
    test_vfs_stat();

    // Summary
    serial::puts("\n========================================\n");
    serial::puts("  Storage Tests Complete\n");
    serial::puts("  Passed: ");
    serial::put_dec(tests_passed);
    serial::puts("\n  Failed: ");
    serial::put_dec(tests_failed);
    serial::puts("\n========================================\n");

    if (tests_failed == 0)
    {
        serial::puts("[RESULT] ALL STORAGE TESTS PASSED\n");
    }
    else
    {
        serial::puts("[RESULT] SOME STORAGE TESTS FAILED\n");
    }
}

} // namespace tests
