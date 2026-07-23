//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/test_windows_installer_lifecycle_contract.cpp
// Purpose: Protect fail-closed contracts in the native Windows installer lifecycle.
//
// Key invariants:
//   - Registry, COM, filesystem, and known-folder outputs are validated before use.
//   - Transaction metadata is bounded, durable, and parsed as an exact schema.
//   - Windows path ownership decisions use locale-independent ordinal comparison.
//
// Ownership/Lifetime:
//   - The test owns one in-memory copy of the lifecycle implementation source.
//
// Links: src/tools/windows_installer/WindowsInstallerLifecycle.cpp
//
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#ifndef ZANNA_SOURCE_DIR
#define ZANNA_SOURCE_DIR "."
#endif

namespace {

int testsRun = 0;
int testsPassed = 0;

void expect(bool condition, std::string_view message) {
    ++testsRun;
    if (condition) {
        ++testsPassed;
    } else {
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool appearsInOrder(std::string_view source,
                    std::string_view anchor,
                    std::string_view first,
                    std::string_view second) {
    const size_t anchorPosition = source.find(anchor);
    const size_t firstPosition = anchorPosition == std::string_view::npos
                                     ? anchorPosition
                                     : source.find(first, anchorPosition);
    const size_t secondPosition = firstPosition == std::string_view::npos
                                      ? firstPosition
                                      : source.find(second, firstPosition);
    return secondPosition != std::string_view::npos;
}

std::string readLifecycleSource() {
    const std::filesystem::path path = std::filesystem::path(ZANNA_SOURCE_DIR) / "src" / "tools" /
                                       "windows_installer" / "WindowsInstallerLifecycle.cpp";
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

} // namespace

int main() {
    const std::string source = readLifecycleSource();
    expect(!source.empty(), "Windows installer lifecycle source is readable");
    if (source.empty())
        return 1;

    expect(source.find("CompareStringOrdinal") != std::string::npos &&
               source.find("std::towlower") == std::string::npos,
           "Windows path decisions use locale-independent ordinal comparison");
    expect(source.find("Windows registry operation returned no key") != std::string::npos,
           "Successful registry opens still require a non-null key");
    expect(source.find("refusing to write an invalid Windows registry string") != std::string::npos,
           "Registry string writes reject invalid types, NULs, and size overflow");
    expect(source.find("cannot resolve a protected Windows known folder") != std::string::npos &&
               source.find("cannot resolve the protected Windows directory") != std::string::npos,
           "Destination protection fails closed when Windows roots cannot be resolved");
    expect(source.find("cannot verify an installation path ancestor") != std::string::npos,
           "Unexpected ancestor attribute errors cannot bypass reparse-point checks");

    expect(source.find("kMaximumTextFileBytes") != std::string::npos &&
               source.find("metadata text file grew while being read") != std::string::npos,
           "Transaction text reads are bounded exact snapshots");
    expect(appearsInOrder(
               source, "void writeBytesAtomic", "FlushFileBuffers(output.get())", "MoveFileExW"),
           "Atomic transaction writes flush their payload before publication");
    expect(appearsInOrder(
               source, "void writeBytesAtomic", "catch (...)", "DeleteFileW(temporary.c_str())"),
           "Failed transaction staging removes its temporary file");
    expect(source.find("installer transaction journal is malformed") != std::string::npos &&
               source.find("value.find(L\"state=") == std::string::npos,
           "Recovery journals require an exact schema instead of substring matching");
    expect(source.find("journal is missing after the old tree ") != std::string::npos &&
               source.find("moved; transaction retained") != std::string::npos,
           "A missing recovery journal cannot discard a preserved old tree");

    expect(appearsInOrder(source,
                          "std::wstring updatePath",
                          "readPathValue(environment.get())",
                          "original.present ? original.type : REG_EXPAND_SZ"),
           "PATH updates use a validated snapshot and preserve its registry type");
    expect(source.find("ordinalEqualsIgnoreCase(key, removeKey)") != std::string::npos &&
               source.find("ordinalEqualsIgnoreCase(key, addKey)") != std::string::npos,
           "PATH entry ownership is locale independent");

    expect(source.find("|| !link") != std::string::npos &&
               source.find("|| !persist") != std::string::npos,
           "Shell-link COM calls reject success-with-null outputs");
    expect(source.find("terminatedWideView(target)") != std::string::npos &&
               source.find("terminatedWideView(arguments)") != std::string::npos,
           "Shell-link getters must return bounded NUL-terminated strings");
    expect(source.find("isProtectedShortcutRoot(parent)") != std::string::npos,
           "Shortcut cleanup cannot remove Desktop or Start Menu roots");
    expect(source.find("refusing to remove a non-file Zanna shortcut path") != std::string::npos,
           "Shortcut cleanup rejects directories and reparse points");

    std::cout << testsPassed << "/" << testsRun << " tests passed\n";
    return testsPassed == testsRun ? 0 : 1;
}
