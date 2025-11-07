// File: tests/unit/PathNormalizeTests.cpp
// Purpose: Verify normalized paths yield correct basename.
// Key invariants: Backslashes become slashes; '..' segments collapsed;
// basename extracted from normalized path.
// Ownership: Standalone executable.
// Links: docs/codemap.md
#include "support/source_manager.hpp"
#include "viper/vm/debug/Debug.hpp"
#include <cassert>
#include <string>

int main()
{
    using il::vm::DebugCtrl;
    using il::support::SourceManager;
    std::string norm = DebugCtrl::normalizePath("a/b/../c\\file.bas");
    assert(norm == "a/c/file.bas");
    size_t pos = norm.find_last_of('/');
    std::string base = (pos == std::string::npos) ? norm : norm.substr(pos + 1);
    assert(base == "file.bas");
    SourceManager sm;
    const std::string mixedCaseWindowsPath = "C:/Temp/Dir/FILE.bas";
    const uint32_t smId = sm.addFile(mixedCaseWindowsPath);
    assert(smId != 0);

    const std::string smNormalized(sm.getPath(smId));
    const std::string debugNormalized = DebugCtrl::normalizePath(mixedCaseWindowsPath);
    assert(debugNormalized == smNormalized);

#ifdef _WIN32
    assert(debugNormalized == "c:/temp/dir/file.bas");
#endif

    return 0;
}
