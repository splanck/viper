//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/linker/test_section_merger.cpp
// Purpose: Unit tests for the native linker's section merger — verifies
//          correct section ordering, alignment, virtual address assignment,
//          and empty section handling.
// Key invariants:
//   - Section order: text → rodata → data → bss → tls_data → tls_bss
//   - Segments page-aligned at permission boundaries
//   - Chunks respect their alignment within output sections
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/linker/SectionMerger.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/SectionMerger.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

using namespace viper::codegen::linker;

static int gFail = 0;

static void check(bool cond, const char *msg, int line)
{
    if (!cond)
    {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// Helper: create an ObjFile with given sections.
static ObjFile makeObj(const std::string &name,
                       ObjFileFormat fmt,
                       const std::vector<ObjSection> &secs)
{
    ObjFile obj;
    obj.name = name;
    obj.format = fmt;
    // Section 0: null.
    obj.sections.push_back({});
    for (const auto &s : secs)
        obj.sections.push_back(s);
    return obj;
}

static ObjSection makeSection(const std::string &name,
                              size_t size,
                              bool exec,
                              bool write,
                              bool tls = false,
                              uint32_t align = 1)
{
    ObjSection sec;
    sec.name = name;
    sec.data.resize(size, 0xCC);
    sec.executable = exec;
    sec.writable = write;
    sec.tls = tls;
    sec.alloc = true;
    sec.alignment = align;
    return sec;
}

int main()
{
    // --- Basic section ordering: text before rodata before data ---
    {
        auto obj = makeObj("test.o",
                           ObjFileFormat::ELF,
                           {makeSection(".rodata", 16, false, false),
                            makeSection(".text", 32, true, false),
                            makeSection(".data", 8, false, true)});

        std::vector<ObjFile> objs = {obj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);
        CHECK(err.str().empty());

        // Should have 3 sections: text, rodata, data.
        CHECK(layout.sections.size() == 3);
        CHECK(layout.sections[0].name == ".text");
        CHECK(layout.sections[1].name == ".rodata");
        CHECK(layout.sections[2].name == ".data");

        // Text data should be 32 bytes.
        CHECK(layout.sections[0].data.size() == 32);
        CHECK(layout.sections[1].data.size() == 16);
        CHECK(layout.sections[2].data.size() == 8);
    }

    // --- Virtual addresses are page-aligned at segment boundaries ---
    {
        auto obj = makeObj(
            "test.o",
            ObjFileFormat::ELF,
            {makeSection(".text", 100, true, false), makeSection(".data", 50, false, true)});

        std::vector<ObjFile> objs = {obj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);
        CHECK(layout.sections.size() == 2);

        // Linux base = 0x400000, skip first page → text starts at 0x401000.
        CHECK(layout.sections[0].virtualAddr == 0x401000);

        // Data in a different permission class → page-aligned.
        CHECK((layout.sections[1].virtualAddr % layout.pageSize) == 0);
        CHECK(layout.sections[1].virtualAddr > layout.sections[0].virtualAddr);
    }

    // --- macOS arm64 uses 16KB pages ---
    {
        auto obj = makeObj(
            "test.o",
            ObjFileFormat::MachO,
            {makeSection("__text", 100, true, false), makeSection("__data", 50, false, true)});

        std::vector<ObjFile> objs = {obj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::macOS, LinkArch::AArch64, layout, err);
        CHECK(ok);
        CHECK(layout.pageSize == 0x4000);

        // Data section page-aligned at 16KB boundary.
        CHECK((layout.sections[1].virtualAddr % 0x4000) == 0);
    }

    // --- Empty sections are skipped ---
    {
        auto emptyText = makeSection(".text", 0, true, false);
        emptyText.data.clear(); // Explicitly empty.
        auto obj = makeObj(
            "test.o", ObjFileFormat::ELF, {emptyText, makeSection(".data", 8, false, true)});

        std::vector<ObjFile> objs = {obj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);
        // Only .data should appear (empty .text skipped).
        CHECK(layout.sections.size() == 1);
        CHECK(layout.sections[0].name == ".data");
    }

    // --- Chunk alignment is respected (sorted: high alignment first) ---
    {
        // Two rodata chunks: first 3 bytes (align 1), second 4 bytes (align 16).
        // After alignment sort, the 16-byte chunk comes first, reducing padding.
        auto obj1 =
            makeObj("a.o", ObjFileFormat::ELF, {makeSection(".rodata", 3, false, false, false, 1)});
        auto obj2 = makeObj(
            "b.o", ObjFileFormat::ELF, {makeSection(".rodata", 4, false, false, false, 16)});

        std::vector<ObjFile> objs = {obj1, obj2};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);

        // Should have one merged rodata section.
        CHECK(layout.sections.size() == 1);
        CHECK(layout.sections[0].name == ".rodata");

        // After sort: b.o (align=16) first at offset 0, then a.o (align=1) at offset 4.
        // High-alignment chunk placed first eliminates inter-chunk padding.
        CHECK(layout.sections[0].chunks.size() == 2);
        CHECK((layout.sections[0].chunks[0].outputOffset % 16) == 0); // b.o at aligned offset
        CHECK(layout.sections[0].chunks[0].size == 4);                // b.o data

        // Total size = 4 + 3 = 7 (no padding needed — the optimization works).
        CHECK(layout.sections[0].data.size() == 7);
    }

    // --- Multiple objects merge into same section ---
    {
        auto obj1 = makeObj("a.o", ObjFileFormat::ELF, {makeSection(".text", 10, true, false)});
        auto obj2 = makeObj("b.o", ObjFileFormat::ELF, {makeSection(".text", 20, true, false)});

        std::vector<ObjFile> objs = {obj1, obj2};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);
        CHECK(layout.sections.size() == 1);
        CHECK(layout.sections[0].data.size() == 30);
        CHECK(layout.sections[0].chunks.size() == 2);
    }

    // --- TLS sections are ordered correctly ---
    {
        auto obj = makeObj(
            "test.o",
            ObjFileFormat::ELF,
            {makeSection(".tdata", 8, false, true, true), makeSection(".text", 16, true, false)});

        std::vector<ObjFile> objs = {obj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);

        // Should have text, then tdata.
        CHECK(layout.sections.size() == 2);
        CHECK(layout.sections[0].name == ".text");
        CHECK(layout.sections[1].name == ".tdata");
        CHECK(layout.sections[1].tls);
    }

    // --- Result ---
    if (gFail == 0)
    {
        std::cout << "All SectionMerger tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " SectionMerger test(s) FAILED.\n";
    return EXIT_FAILURE;
}
