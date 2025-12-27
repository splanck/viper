/**
 * @file hello.cpp
 * @brief Test program for verifying malloc/sbrk functionality.
 *
 * @details
 * This user-space program tests the heap allocation system by:
 * 1. Allocating memory with malloc
 * 2. Writing to and reading from allocated memory
 * 3. Freeing memory
 * 4. Testing multiple allocations
 */

#include "../syscall.hpp"

// Simple sbrk wrapper for direct testing
static void *test_sbrk(long increment)
{
    sys::SyscallResult r = sys::syscall1(0x0A, static_cast<u64>(increment));
    if (r.error < 0)
    {
        return reinterpret_cast<void *>(-1);
    }
    return reinterpret_cast<void *>(r.val0);
}

// Simple block header for our test malloc
struct BlockHeader
{
    u64 size;
    BlockHeader *next;
    bool free;
};

static BlockHeader *free_list = nullptr;

static void *test_malloc(u64 size)
{
    if (size == 0)
        return nullptr;

    // Align size to 16 bytes
    size = (size + 15) & ~15ULL;

    // Check free list first
    BlockHeader *prev = nullptr;
    BlockHeader *curr = free_list;
    while (curr)
    {
        if (curr->free && curr->size >= size)
        {
            curr->free = false;
            return reinterpret_cast<void *>(curr + 1);
        }
        prev = curr;
        curr = curr->next;
    }

    // Allocate new block from heap
    u64 total = sizeof(BlockHeader) + size;
    void *ptr = test_sbrk(static_cast<long>(total));
    if (ptr == reinterpret_cast<void *>(-1))
    {
        return nullptr;
    }

    BlockHeader *block = static_cast<BlockHeader *>(ptr);
    block->size = size;
    block->next = nullptr;
    block->free = false;

    // Add to list
    if (prev)
    {
        prev->next = block;
    }
    else
    {
        free_list = block;
    }

    return reinterpret_cast<void *>(block + 1);
}

static void test_free(void *ptr)
{
    if (!ptr)
        return;

    BlockHeader *block = static_cast<BlockHeader *>(ptr) - 1;
    block->free = true;
}

/**
 * @brief Print a string to the console.
 */
static void puts(const char *s)
{
    while (*s)
    {
        sys::putchar(*s++);
    }
}

/**
 * @brief Print an integer in decimal.
 */
static void put_num(i64 n)
{
    if (n < 0)
    {
        sys::putchar('-');
        n = -n;
    }
    if (n >= 10)
    {
        put_num(n / 10);
    }
    sys::putchar('0' + (n % 10));
}

/**
 * @brief Print a hex value.
 */
static void put_hex(u64 n)
{
    puts("0x");
    bool started = false;
    for (int i = 60; i >= 0; i -= 4)
    {
        int digit = (n >> i) & 0xF;
        if (digit != 0 || started || i == 0)
        {
            started = true;
            if (digit < 10)
                sys::putchar('0' + digit);
            else
                sys::putchar('a' + digit - 10);
        }
    }
}

/**
 * @brief Program entry point.
 *
 * Tests malloc functionality.
 */
extern "C" void _start()
{
    puts("[malloc_test] Starting malloc/sbrk test...\n");

    // Test 1: Simple sbrk to get current break
    puts("[malloc_test] Test 1: sbrk(0) - get current break\n");
    void *brk = test_sbrk(0);
    puts("[malloc_test]   Current break: ");
    put_hex(reinterpret_cast<u64>(brk));
    puts("\n");

    // Test 2: Single malloc
    puts("[malloc_test] Test 2: malloc(64)\n");
    char *ptr1 = static_cast<char *>(test_malloc(64));
    if (ptr1 == nullptr)
    {
        puts("[malloc_test]   FAILED: malloc returned NULL\n");
        sys::exit(1);
    }
    puts("[malloc_test]   Allocated at: ");
    put_hex(reinterpret_cast<u64>(ptr1));
    puts("\n");

    // Test 3: Write to allocated memory
    puts("[malloc_test] Test 3: Write to allocated memory\n");
    for (int i = 0; i < 64; i++)
    {
        ptr1[i] = static_cast<char>(i);
    }
    puts("[malloc_test]   Write successful\n");

    // Test 4: Read back from memory
    puts("[malloc_test] Test 4: Read from allocated memory\n");
    bool read_ok = true;
    for (int i = 0; i < 64; i++)
    {
        if (ptr1[i] != static_cast<char>(i))
        {
            read_ok = false;
            break;
        }
    }
    if (read_ok)
    {
        puts("[malloc_test]   Read verification successful\n");
    }
    else
    {
        puts("[malloc_test]   FAILED: Data mismatch\n");
        sys::exit(2);
    }

    // Test 5: Multiple allocations
    puts("[malloc_test] Test 5: Multiple allocations\n");
    char *ptrs[5];
    for (int i = 0; i < 5; i++)
    {
        ptrs[i] = static_cast<char *>(test_malloc(128));
        if (ptrs[i] == nullptr)
        {
            puts("[malloc_test]   FAILED: malloc returned NULL\n");
            sys::exit(3);
        }
        puts("[malloc_test]   Allocation ");
        put_num(i);
        puts(" at ");
        put_hex(reinterpret_cast<u64>(ptrs[i]));
        puts("\n");
    }

    // Test 6: Write to all allocations
    puts("[malloc_test] Test 6: Write to all allocations\n");
    for (int i = 0; i < 5; i++)
    {
        for (int j = 0; j < 128; j++)
        {
            ptrs[i][j] = static_cast<char>(i * 10 + j);
        }
    }
    puts("[malloc_test]   Write successful\n");

    // Test 7: Verify all allocations
    puts("[malloc_test] Test 7: Verify all allocations\n");
    bool verify_ok = true;
    for (int i = 0; i < 5; i++)
    {
        for (int j = 0; j < 128; j++)
        {
            if (ptrs[i][j] != static_cast<char>(i * 10 + j))
            {
                verify_ok = false;
                break;
            }
        }
        if (!verify_ok)
            break;
    }
    if (verify_ok)
    {
        puts("[malloc_test]   Verification successful\n");
    }
    else
    {
        puts("[malloc_test]   FAILED: Data verification failed\n");
        sys::exit(4);
    }

    // Test 8: Free and reuse
    puts("[malloc_test] Test 8: Free first allocation\n");
    test_free(ptr1);
    puts("[malloc_test]   Freed ptr1\n");

    // Test 9: Large allocation (1KB)
    puts("[malloc_test] Test 9: Large allocation (1KB)\n");
    char *large = static_cast<char *>(test_malloc(1024));
    if (large == nullptr)
    {
        puts("[malloc_test]   FAILED: Large malloc returned NULL\n");
        sys::exit(5);
    }
    puts("[malloc_test]   Large allocation at: ");
    put_hex(reinterpret_cast<u64>(large));
    puts("\n");

    // Write and verify large allocation
    for (int i = 0; i < 1024; i++)
    {
        large[i] = static_cast<char>(i & 0xFF);
    }
    for (int i = 0; i < 1024; i++)
    {
        if (large[i] != static_cast<char>(i & 0xFF))
        {
            puts("[malloc_test]   FAILED: Large allocation verification failed\n");
            sys::exit(6);
        }
    }
    puts("[malloc_test]   Large allocation verified\n");

    // Check final break
    void *final_brk = test_sbrk(0);
    puts("[malloc_test] Final heap break: ");
    put_hex(reinterpret_cast<u64>(final_brk));
    puts("\n");

    puts("[malloc_test] All tests PASSED!\n");
    sys::exit(0);
}
