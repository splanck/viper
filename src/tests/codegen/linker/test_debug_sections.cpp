//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/linker/test_debug_sections.cpp
// Purpose: Unit tests for non-alloc debug section handling in the linker:
//          SectionMerger collection, VA assignment, and exe writer output.
// Key invariants:
//   - Non-alloc sections are collected into separate OutputSections
//   - Non-alloc sections have virtualAddr=0 (no VA assignment)
//   - Non-alloc sections sort after all alloc sections
//   - classifySections() excludes non-alloc sections
//   - ELF exe writer emits non-alloc section headers with no SHF_ALLOC
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/linker/SectionMerger.hpp,
//        codegen/common/linker/ExeWriterUtil.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/ExeWriterUtil.hpp"
#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/SectionMerger.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

using namespace viper::codegen::linker;

static int gFail = 0;

static void check(bool cond, const char *msg, int line) {
    if (!cond) {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// Helper: create an ObjFile with given sections.
static ObjFile makeObj(const std::string &name, const std::vector<ObjSection> &secs) {
    ObjFile obj;
    obj.name = name;
    obj.format = ObjFileFormat::ELF;
    // Section 0: null.
    obj.sections.push_back({});
    for (const auto &s : secs)
        obj.sections.push_back(s);
    return obj;
}

static ObjSection makeAllocSection(
    const std::string &name, size_t size, bool exec, bool write, uint32_t align = 1) {
    ObjSection sec;
    sec.name = name;
    sec.data.resize(size, 0xCC);
    sec.executable = exec;
    sec.writable = write;
    sec.alloc = true;
    sec.alignment = align;
    return sec;
}

static ObjSection makeDebugSection(const std::string &name, size_t size, uint32_t align = 1) {
    ObjSection sec;
    sec.name = name;
    sec.data.resize(size, 0xDB); // Debug bytes.
    sec.executable = false;
    sec.writable = false;
    sec.alloc = false;
    sec.alignment = align;
    return sec;
}

int main() {
    // --- Test 1: Debug sections are collected into non-alloc OutputSections ---
    {
        std::vector<ObjFile> objects;
        objects.push_back(makeObj("a.o",
                                  {
                                      makeAllocSection(".text", 64, true, false),
                                      makeDebugSection(".debug_line", 100),
                                  }));

        LinkLayout layout;
        std::ostringstream err;
        bool ok = mergeSections(objects, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);

        // Should have text + debug_line.
        bool foundText = false, foundDebug = false;
        for (const auto &sec : layout.sections) {
            if (sec.name == ".text") {
                foundText = true;
                CHECK(sec.alloc);
            }
            if (sec.name == ".debug_line") {
                foundDebug = true;
                CHECK(!sec.alloc);
                CHECK(sec.data.size() == 100);
            }
        }
        CHECK(foundText);
        CHECK(foundDebug);
    }

    // --- Test 2: Non-alloc sections have virtualAddr=0 ---
    {
        std::vector<ObjFile> objects;
        objects.push_back(makeObj("b.o",
                                  {
                                      makeAllocSection(".text", 64, true, false),
                                      makeAllocSection(".rodata", 32, false, false),
                                      makeDebugSection(".debug_line", 50),
                                  }));

        LinkLayout layout;
        std::ostringstream err;
        bool ok = mergeSections(objects, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);

        for (const auto &sec : layout.sections) {
            if (!sec.alloc) {
                CHECK(sec.virtualAddr == 0);
            } else {
                CHECK(sec.virtualAddr != 0);
            }
        }
    }

    // --- Test 3: Non-alloc sections sort after alloc sections ---
    {
        std::vector<ObjFile> objects;
        objects.push_back(makeObj("c.o",
                                  {
                                      makeDebugSection(".debug_line", 50),
                                      makeAllocSection(".text", 64, true, false),
                                      makeAllocSection(".data", 32, false, true),
                                  }));

        LinkLayout layout;
        std::ostringstream err;
        bool ok = mergeSections(objects, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);

        // All alloc sections should come before non-alloc.
        bool seenNonAlloc = false;
        for (const auto &sec : layout.sections) {
            if (!sec.alloc)
                seenNonAlloc = true;
            else
                CHECK(!seenNonAlloc); // Alloc section after non-alloc is wrong.
        }
        CHECK(seenNonAlloc);
    }

    // --- Test 4: Multiple debug sections from multiple objects are merged by name ---
    {
        std::vector<ObjFile> objects;
        objects.push_back(makeObj("d1.o",
                                  {
                                      makeAllocSection(".text", 32, true, false),
                                      makeDebugSection(".debug_line", 40),
                                  }));
        objects.push_back(makeObj("d2.o",
                                  {
                                      makeAllocSection(".text", 32, true, false),
                                      makeDebugSection(".debug_line", 60),
                                  }));

        LinkLayout layout;
        std::ostringstream err;
        bool ok = mergeSections(objects, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);

        // Should have one merged .debug_line output section.
        int debugCount = 0;
        for (const auto &sec : layout.sections) {
            if (sec.name == ".debug_line") {
                debugCount++;
                CHECK(!sec.alloc);
                CHECK(sec.data.size() >= 100); // 40 + 60, possibly with alignment padding.
                CHECK(sec.chunks.size() == 2);
            }
        }
        CHECK(debugCount == 1);
    }

    // --- Test 5: classifySections() excludes non-alloc sections ---
    {
        LinkLayout layout;
        // Manually build layout with alloc and non-alloc sections.
        OutputSection textSec;
        textSec.name = ".text";
        textSec.data.resize(64, 0xCC);
        textSec.executable = true;
        textSec.alloc = true;
        textSec.virtualAddr = 0x401000;
        layout.sections.push_back(textSec);

        OutputSection debugSec;
        debugSec.name = ".debug_line";
        debugSec.data.resize(50, 0xDB);
        debugSec.alloc = false;
        layout.sections.push_back(debugSec);

        std::vector<size_t> textIndices, dataIndices;
        classifySections(layout, textIndices, dataIndices);

        // Should have text but no debug section in either list.
        CHECK(textIndices.size() == 1);
        CHECK(dataIndices.empty());
        CHECK(textIndices[0] == 0); // Only the alloc section.
    }

    // --- Test 6: Empty debug sections are not collected ---
    {
        std::vector<ObjFile> objects;
        objects.push_back(makeObj("e.o",
                                  {
                                      makeAllocSection(".text", 64, true, false),
                                  }));
        // Add an object with an empty debug section.
        {
            ObjSection emptySec;
            emptySec.name = ".debug_line";
            emptySec.alloc = false;
            // data is empty
            objects.push_back(makeObj("f.o", {emptySec}));
        }

        LinkLayout layout;
        std::ostringstream err;
        bool ok = mergeSections(objects, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);

        // No debug output section should exist.
        for (const auto &sec : layout.sections)
            CHECK(sec.name != ".debug_line");
    }

    // --- Test 7: Debug sections don't affect alloc section VAs ---
    {
        // Build two layouts: one with debug sections and one without.
        // Alloc section VAs should be identical.
        auto buildLayout = [](bool withDebug) {
            std::vector<ObjFile> objects;
            std::vector<ObjSection> secs = {
                makeAllocSection(".text", 64, true, false),
                makeAllocSection(".rodata", 32, false, false),
                makeAllocSection(".data", 16, false, true),
            };
            if (withDebug)
                secs.push_back(makeDebugSection(".debug_line", 200));
            objects.push_back(makeObj("g.o", secs));

            LinkLayout layout;
            std::ostringstream err;
            mergeSections(objects, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
            return layout;
        };

        LinkLayout withDebug = buildLayout(true);
        LinkLayout withoutDebug = buildLayout(false);

        // Compare alloc section VAs.
        size_t allocCountWith = 0, allocCountWithout = 0;
        for (const auto &sec : withDebug.sections)
            if (sec.alloc)
                allocCountWith++;
        for (const auto &sec : withoutDebug.sections)
            if (sec.alloc)
                allocCountWithout++;

        CHECK(allocCountWith == allocCountWithout);

        // VAs should match for alloc sections in both layouts.
        size_t ai = 0;
        for (const auto &sec : withDebug.sections) {
            if (!sec.alloc)
                continue;
            // Find matching section in withoutDebug.
            for (const auto &sec2 : withoutDebug.sections) {
                if (sec2.name == sec.name && sec2.alloc) {
                    CHECK(sec.virtualAddr == sec2.virtualAddr);
                    break;
                }
            }
            ++ai;
        }
    }

    // --- Test 8: Debug section data integrity through merge ---
    {
        std::vector<ObjFile> objects;
        ObjSection debugSec;
        debugSec.name = ".debug_line";
        debugSec.alloc = false;
        debugSec.alignment = 1;
        // Fill with recognizable pattern.
        for (int i = 0; i < 64; ++i)
            debugSec.data.push_back(static_cast<uint8_t>(i));
        objects.push_back(makeObj("h.o",
                                  {
                                      makeAllocSection(".text", 32, true, false),
                                      debugSec,
                                  }));

        LinkLayout layout;
        std::ostringstream err;
        bool ok = mergeSections(objects, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);

        for (const auto &sec : layout.sections) {
            if (sec.name != ".debug_line")
                continue;
            CHECK(sec.data.size() == 64);
            for (int i = 0; i < 64; ++i)
                CHECK(sec.data[i] == static_cast<uint8_t>(i));
        }
    }

    // --- Test 9: macOS arm64 page size with debug sections ---
    {
        std::vector<ObjFile> objects;
        objects.push_back(makeObj("i.o",
                                  {
                                      makeAllocSection(".text", 64, true, false),
                                      makeDebugSection(".debug_line", 50),
                                  }));

        LinkLayout layout;
        std::ostringstream err;
        bool ok = mergeSections(objects, LinkPlatform::macOS, LinkArch::AArch64, layout, err);
        CHECK(ok);
        CHECK(layout.pageSize == 0x4000);

        for (const auto &sec : layout.sections) {
            if (!sec.alloc) {
                CHECK(sec.virtualAddr == 0);
            }
        }
    }

    if (gFail == 0)
        std::cout << "All debug_sections tests passed.\n";
    else
        std::cerr << gFail << " debug_sections test(s) FAILED.\n";

    return gFail ? EXIT_FAILURE : EXIT_SUCCESS;
}
