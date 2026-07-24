//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_windows_installer_cleanup_policy.cpp
// Purpose: Verify the detached Windows installer cleanup path policy.
//
// Key invariants:
//   - Only unambiguous absolute paths below a drive or UNC share are accepted.
//   - Device aliases, traversal, alternate streams, and root deletion fail closed.
//
// Ownership/Lifetime: Standalone dependency-free unit test executable.
//
// Links: src/tools/windows_installer/WindowsInstallerCleanupPolicy.cpp
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include "tools/windows_installer/WindowsInstallerCleanupPolicy.hpp"

#include <array>
#include <string_view>

using zanna::installer::cleanup::isSafeAbsolutePath;
using zanna::installer::cleanup::pathsEqual;

TEST(WindowsInstallerCleanupPolicy, AcceptsQualifiedChildPaths) {
    static constexpr std::array<std::wstring_view, 6> kAccepted = {
        LR"(C:\Users\Example\AppData\Local\Zanna\cache.exe)",
        LR"(d:/ProgramData/Zanna/Packages/1.0/zanna.exe)",
        LR"(\\?\C:\Users\Example\AppData\Local\Zanna\cache.exe)",
        LR"(\\server\share\Zanna\cache.exe)",
        LR"(\\?\UNC\server\share\Zanna\cache.exe)",
        LR"(C:\Zanna Cleanup\package-1\cleanup.exe)",
    };
    for (const std::wstring_view path : kAccepted)
        EXPECT_TRUE(isSafeAbsolutePath(path));
}

TEST(WindowsInstallerCleanupPolicy, RejectsRootsNamespacesAndTraversal) {
    static constexpr std::array<std::wstring_view, 16> kRejected = {
        LR"(C:\)",
        LR"(\\server\share)",
        LR"(\\server\share\)",
        LR"(\\?\C:\)",
        LR"(\\?\UNC\server\share\)",
        LR"(relative\cache.exe)",
        LR"(\rooted\cache.exe)",
        LR"(C:drive-relative.exe)",
        LR"(\\.\C:\cache.exe)",
        LR"(\\?\GLOBALROOT\Device\HarddiskVolume1\cache.exe)",
        LR"(\\?\Volume{00000000-0000-0000-0000-000000000000}\cache.exe)",
        LR"(C:\Zanna\..\victim.exe)",
        LR"(C:\Zanna\.\victim.exe)",
        LR"(C:\Zanna\\victim.exe)",
        LR"(\\server\\share\victim.exe)",
        LR"(\\server\share\\victim.exe)",
    };
    for (const std::wstring_view path : kRejected)
        EXPECT_FALSE(isSafeAbsolutePath(path));
}

TEST(WindowsInstallerCleanupPolicy, RejectsAmbiguousWin32Components) {
    static constexpr std::array<std::wstring_view, 17> kRejected = {
        LR"(C:\Zanna\CON)",
        LR"(C:\Zanna\con.txt)",
        LR"(C:\Zanna\PRN.log)",
        LR"(C:\Zanna\AUX)",
        LR"(C:\Zanna\NUL.bin)",
        LR"(C:\Zanna\CLOCK$)",
        LR"(C:\Zanna\COM1.txt)",
        LR"(C:\Zanna\LPT9)",
        L"C:\\Zanna\\COM\u00b9.log",
        LR"(C:\Zanna\trailing.)",
        LR"(C:\Zanna\trailing )",
        LR"(C:\Zanna\file.exe:stream)",
        LR"(C:\Zanna\bad?.exe)",
        LR"(C:\Zanna\bad*.exe)",
        LR"(C:\Zanna\bad|name.exe)",
        L"C:\\Zanna\\bad\x1fname.exe",
        LR"(C:\Zanna\)",
    };
    for (const std::wstring_view path : kRejected)
        EXPECT_FALSE(isSafeAbsolutePath(path));
}

TEST(WindowsInstallerCleanupPolicy, ComparesCaseAndSeparatorsOrdinally) {
    EXPECT_TRUE(pathsEqual(LR"(C:\Zanna\Cache.EXE)", LR"(c:/zanna/cache.exe)"));
    EXPECT_TRUE(pathsEqual(LR"(\\Server\Share\Zanna)", LR"(//server/share/zanna)"));
    EXPECT_FALSE(pathsEqual(LR"(C:\Zanna\Cache.exe)", LR"(C:\Zanna\Cache2.exe)"));
}

int main(int argc, char **argv) {
    zanna_test::init(&argc, argv);
    return zanna_test::run_all_tests();
}
