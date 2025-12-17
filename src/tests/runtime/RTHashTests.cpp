//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/runtime/RTHashTests.cpp
// Purpose: Validate Viper.Crypto.Hash runtime functions for MD5, SHA1, SHA256, CRC32.
// Key invariants: Hash outputs match known test vectors; all outputs are lowercase hex.
// Links: docs/viperlib.md

#include "rt_hash.h"
#include "rt_string.h"

#include <cassert>
#include <cstdio>
#include <cstring>

/// @brief Helper to print test result.
static void test_result(const char *name, bool passed)
{
    printf("  %s: %s\n", name, passed ? "PASS" : "FAIL");
    assert(passed);
}

//=============================================================================
// CRC32 Tests
//=============================================================================

static void test_crc32()
{
    printf("Testing Hash.CRC32:\n");

    // Empty string
    rt_string empty = rt_const_cstr("");
    int64_t crc_empty = rt_hash_crc32(empty);
    test_result("Empty string CRC32 = 0", crc_empty == 0);

    // Standard test vector: "123456789" -> 0xCBF43926
    rt_string digits = rt_const_cstr("123456789");
    int64_t crc_digits = rt_hash_crc32(digits);
    test_result("'123456789' CRC32 = 0xCBF43926", crc_digits == 0xCBF43926);

    // "Hello" test
    rt_string hello = rt_const_cstr("Hello");
    int64_t crc_hello = rt_hash_crc32(hello);
    // Known value for "Hello": 0xF7D18982
    test_result("'Hello' CRC32 = 0xF7D18982", crc_hello == 0xF7D18982);

    // Single character
    rt_string a = rt_const_cstr("a");
    int64_t crc_a = rt_hash_crc32(a);
    // Known value for "a": 0xE8B7BE43
    test_result("'a' CRC32 = 0xE8B7BE43", crc_a == 0xE8B7BE43);

    printf("\n");
}

//=============================================================================
// MD5 Tests (RFC 1321 test vectors)
//=============================================================================

static void test_md5()
{
    printf("Testing Hash.MD5:\n");

    // Empty string: d41d8cd98f00b204e9800998ecf8427e
    rt_string empty = rt_const_cstr("");
    rt_string md5_empty = rt_hash_md5(empty);
    test_result("Empty string MD5", strcmp(rt_string_cstr(md5_empty), "d41d8cd98f00b204e9800998ecf8427e") == 0);

    // "a": 0cc175b9c0f1b6a831c399e269772661
    rt_string a = rt_const_cstr("a");
    rt_string md5_a = rt_hash_md5(a);
    test_result("'a' MD5", strcmp(rt_string_cstr(md5_a), "0cc175b9c0f1b6a831c399e269772661") == 0);

    // "abc": 900150983cd24fb0d6963f7d28e17f72
    rt_string abc = rt_const_cstr("abc");
    rt_string md5_abc = rt_hash_md5(abc);
    test_result("'abc' MD5", strcmp(rt_string_cstr(md5_abc), "900150983cd24fb0d6963f7d28e17f72") == 0);

    // "message digest": f96b697d7cb7938d525a2f31aaf161d0
    rt_string msg = rt_const_cstr("message digest");
    rt_string md5_msg = rt_hash_md5(msg);
    test_result("'message digest' MD5", strcmp(rt_string_cstr(md5_msg), "f96b697d7cb7938d525a2f31aaf161d0") == 0);

    // "abcdefghijklmnopqrstuvwxyz": c3fcd3d76192e4007dfb496cca67e13b
    rt_string alpha = rt_const_cstr("abcdefghijklmnopqrstuvwxyz");
    rt_string md5_alpha = rt_hash_md5(alpha);
    test_result("'a-z' MD5", strcmp(rt_string_cstr(md5_alpha), "c3fcd3d76192e4007dfb496cca67e13b") == 0);

    printf("\n");
}

//=============================================================================
// SHA1 Tests (RFC 3174 test vectors)
//=============================================================================

static void test_sha1()
{
    printf("Testing Hash.SHA1:\n");

    // Empty string: da39a3ee5e6b4b0d3255bfef95601890afd80709
    rt_string empty = rt_const_cstr("");
    rt_string sha1_empty = rt_hash_sha1(empty);
    test_result("Empty string SHA1", strcmp(rt_string_cstr(sha1_empty), "da39a3ee5e6b4b0d3255bfef95601890afd80709") == 0);

    // "abc": a9993e364706816aba3e25717850c26c9cd0d89d
    rt_string abc = rt_const_cstr("abc");
    rt_string sha1_abc = rt_hash_sha1(abc);
    test_result("'abc' SHA1", strcmp(rt_string_cstr(sha1_abc), "a9993e364706816aba3e25717850c26c9cd0d89d") == 0);

    // "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
    // 84983e441c3bd26ebaae4aa1f95129e5e54670f1
    rt_string long_str = rt_const_cstr("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    rt_string sha1_long = rt_hash_sha1(long_str);
    test_result("448-bit string SHA1", strcmp(rt_string_cstr(sha1_long), "84983e441c3bd26ebaae4aa1f95129e5e54670f1") == 0);

    // "The quick brown fox jumps over the lazy dog"
    // 2fd4e1c67a2d28fced849ee1bb76e7391b93eb12
    rt_string fox = rt_const_cstr("The quick brown fox jumps over the lazy dog");
    rt_string sha1_fox = rt_hash_sha1(fox);
    test_result("'The quick brown fox...' SHA1", strcmp(rt_string_cstr(sha1_fox), "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12") == 0);

    printf("\n");
}

//=============================================================================
// SHA256 Tests (RFC 6234 test vectors)
//=============================================================================

static void test_sha256()
{
    printf("Testing Hash.SHA256:\n");

    // Empty string: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
    rt_string empty = rt_const_cstr("");
    rt_string sha256_empty = rt_hash_sha256(empty);
    test_result("Empty string SHA256", strcmp(rt_string_cstr(sha256_empty), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0);

    // "abc": ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
    rt_string abc = rt_const_cstr("abc");
    rt_string sha256_abc = rt_hash_sha256(abc);
    test_result("'abc' SHA256", strcmp(rt_string_cstr(sha256_abc), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);

    // "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
    // 248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1
    rt_string long_str = rt_const_cstr("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    rt_string sha256_long = rt_hash_sha256(long_str);
    test_result("448-bit string SHA256", strcmp(rt_string_cstr(sha256_long), "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1") == 0);

    // "Hello": 185f8db32271fe25f561a6fc938b2e264306ec304eda518007d1764826381969
    rt_string hello = rt_const_cstr("Hello");
    rt_string sha256_hello = rt_hash_sha256(hello);
    test_result("'Hello' SHA256", strcmp(rt_string_cstr(sha256_hello), "185f8db32271fe25f561a6fc938b2e264306ec304eda518007d1764826381969") == 0);

    printf("\n");
}

//=============================================================================
// Entry Point
//=============================================================================

int main()
{
    printf("=== RT Hash Tests ===\n\n");

    test_crc32();
    test_md5();
    test_sha1();
    test_sha256();

    printf("All Hash tests passed!\n");
    return 0;
}
