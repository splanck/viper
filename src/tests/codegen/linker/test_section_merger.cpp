//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/linker/test_section_merger.cpp
// Purpose: Unit tests for the native linker's section merger — verifies
//          correct section ordering, alignment, virtual address assignment,
//          and empty section handling.
// Key invariants:
//   - Section order: text → rodata → data → tls_data → tls_bss → bss
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
#include <limits>
#include <sstream>

using namespace zanna::codegen::linker;

static int gFail = 0;

static void check(bool cond, const char *msg, int line) {
    if (!cond) {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// Helper: create an ObjFile with given sections.
static ObjFile makeObj(const std::string &name,
                       ObjFileFormat fmt,
                       const std::vector<ObjSection> &secs) {
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
                              uint32_t align = 1,
                              bool zeroFill = false) {
    ObjSection sec;
    sec.name = name;
    sec.data.resize(size, 0xCC);
    sec.executable = exec;
    sec.writable = write;
    sec.tls = tls;
    sec.zeroFill = zeroFill;
    sec.alloc = true;
    sec.alignment = align;
    return sec;
}

int main() {
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

    // --- Class ordering is primary even when rodata has higher alignment ---
    {
        auto obj1 = makeObj(
            "ro.o", ObjFileFormat::ELF, {makeSection(".rodata", 4, false, false, false, 64)});
        auto obj2 =
            makeObj("tx.o", ObjFileFormat::ELF, {makeSection(".text", 4, true, false, false, 1)});

        std::vector<ObjFile> objs = {obj1, obj2};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);
        CHECK(layout.sections.size() == 2);
        CHECK(layout.sections[0].name == ".text");
        CHECK(layout.sections[1].name == ".rodata");
    }

    // --- Invalid input alignment is diagnosed instead of rounded silently ---
    {
        auto obj = makeObj(
            "bad-align.o", ObjFileFormat::ELF, {makeSection(".text", 4, true, false, false, 3)});

        std::vector<ObjFile> objs = {obj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(!ok);
        CHECK(err.str().find("alignment") != std::string::npos);
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

        // Should have text, then ELF TLS template data.
        CHECK(layout.sections.size() == 2);
        CHECK(layout.sections[0].name == ".text");
        CHECK(layout.sections[1].name == ".tdata");
        CHECK(layout.sections[1].tls);
    }

    // --- Windows unwind sections keep their original names ---
    {
        auto obj = makeObj("test.obj",
                           ObjFileFormat::COFF,
                           {makeSection(".xdata", 8, false, false),
                            makeSection(".pdata", 12, false, false),
                            makeSection(".text", 16, true, false)});

        std::vector<ObjFile> objs = {obj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::Windows, LinkArch::X86_64, layout, err);
        CHECK(ok);
        CHECK(layout.sections.size() == 3);
        CHECK(layout.sections[0].name == ".text");
        bool sawXdata = false;
        bool sawPdata = false;
        for (size_t i = 1; i < layout.sections.size(); ++i) {
            sawXdata = sawXdata || layout.sections[i].name == ".xdata";
            sawPdata = sawPdata || layout.sections[i].name == ".pdata";
        }
        CHECK(sawXdata);
        CHECK(sawPdata);
    }

    // --- Windows PE output sections are page-aligned ---
    {
        auto obj = makeObj("test.obj",
                           ObjFileFormat::COFF,
                           {makeSection(".text", 16, true, false),
                            makeSection(".rdata", 24, false, false),
                            makeSection(".pdata", 12, false, false),
                            makeSection(".xdata", 8, false, false),
                            makeSection(".data", 32, false, true)});

        std::vector<ObjFile> objs = {obj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::Windows, LinkArch::X86_64, layout, err);
        CHECK(ok);
        for (const auto &sec : layout.sections) {
            if (!sec.alloc)
                continue;
            CHECK((sec.virtualAddr % layout.pageSize) == 0);
        }
    }

    // --- Windows .CRT$ subsections preserve lexicographic order ---
    {
        auto crtA = makeSection(".CRT$XIAA", 8, false, false, false, 32);
        auto crtB = makeSection(".CRT$XIAC", 8, false, false, false, 8);
        auto ro = makeSection(".rdata", 8, false, false, false, 64);
        auto obj = makeObj("crt.obj", ObjFileFormat::COFF, {crtA, ro, crtB});

        std::vector<ObjFile> objs = {obj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::Windows, LinkArch::X86_64, layout, err);
        CHECK(ok);
        CHECK(layout.sections.size() == 1);
        CHECK(layout.sections[0].name == ".rodata");
        CHECK(layout.sections[0].chunks.size() == 3);
        CHECK(layout.sections[0].chunks[0].inputSecIndex == 1); // .CRT$XIAA
        CHECK(layout.sections[0].chunks[1].inputSecIndex == 3); // .CRT$XIAC
        CHECK(layout.sections[0].chunks[2].inputSecIndex == 2); // .rdata
    }

    // --- Windows .tls subsections preserve lexicographic order ---
    {
        auto tlsZ = makeSection(".tls$ZZZ", 8, false, true, true, 32);
        auto tlsA = makeSection(".tls$", 8, false, true, true, 8);
        auto obj = makeObj("tls.obj", ObjFileFormat::COFF, {tlsZ, tlsA});

        std::vector<ObjFile> objs = {obj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::Windows, LinkArch::X86_64, layout, err);
        CHECK(ok);
        CHECK(layout.sections.size() == 1);
        CHECK(layout.sections[0].name == ".tdata");
        CHECK(layout.sections[0].chunks.size() == 2);
        CHECK(layout.sections[0].chunks[0].inputSecIndex == 2); // .tls$
        CHECK(layout.sections[0].chunks[1].inputSecIndex == 1); // .tls$ZZZ
    }

    // --- Zero-fill sections preserve BSS/TBSS classification ---
    {
        auto obj = makeObj("bss.o",
                           ObjFileFormat::ELF,
                           {makeSection(".data", 8, false, true),
                            makeSection(".bss", 16, false, true, false, 8, true),
                            makeSection(".tbss", 12, false, true, true, 8, true)});

        std::vector<ObjFile> objs = {obj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);
        CHECK(err.str().empty());
        CHECK(layout.sections.size() == 3);
        CHECK(layout.sections[0].name == ".data");
        CHECK(layout.sections[1].name == ".tbss");
        CHECK(layout.sections[1].zeroFill);
        CHECK(layout.sections[1].writable);
        CHECK(layout.sections[1].tls);
        CHECK(layout.sections[2].name == ".bss");
        CHECK(layout.sections[2].zeroFill);
        CHECK(!layout.sections[2].tls);
    }

    // --- ELF init/fini arrays use constructor priority instead of alignment order ---
    {
        auto obj = makeObj("ctors.o",
                           ObjFileFormat::ELF,
                           {makeSection(".init_array.65535", 8, false, true, false, 64),
                            makeSection(".init_array.101", 8, false, true, false, 1),
                            makeSection(".fini_array.200", 8, false, true, false, 32),
                            makeSection(".preinit_array.50", 8, false, true, false, 1),
                            makeSection(".init_array.42949672960", 8, false, true, false, 128)});

        std::vector<ObjFile> objs = {obj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);
        CHECK(layout.sections.size() == 1);
        CHECK(layout.sections[0].name == ".data");
        CHECK(layout.sections[0].chunks.size() == 5);
        CHECK(layout.sections[0].chunks[0].inputSecIndex == 4); // .preinit_array.50
        CHECK(layout.sections[0].chunks[1].inputSecIndex == 2); // .init_array.101
        CHECK(layout.sections[0].chunks[2].inputSecIndex == 1); // .init_array.65535
        CHECK(layout.sections[0].chunks[3].inputSecIndex == 3); // .fini_array.200
        CHECK(layout.sections[0].chunks[4].inputSecIndex == 5); // overflowed priority ignored
    }

    // --- ELF read-only metadata sections keep their original output names ---
    {
        auto obj = makeObj("metadata.o",
                           ObjFileFormat::ELF,
                           {makeSection(".eh_frame", 8, false, false, false, 8),
                            makeSection(".note.zanna", 12, false, false, false, 4),
                            makeSection(".rodata", 16, false, false, false, 16)});

        std::vector<ObjFile> objs = {obj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);
        bool sawRodata = false;
        bool sawEhFrame = false;
        bool sawNote = false;
        for (const auto &sec : layout.sections) {
            sawRodata = sawRodata || sec.name == ".rodata";
            sawEhFrame = sawEhFrame || sec.name == ".eh_frame";
            sawNote = sawNote || sec.name == ".note.zanna";
        }
        CHECK(sawRodata);
        CHECK(sawEhFrame);
        CHECK(sawNote);
    }

    // --- Mach-O mod-init functions keep their loader-visible name and source order ---
    {
        auto obj = makeObj("modinit.o",
                           ObjFileFormat::MachO,
                           {makeSection("__DATA,__mod_init_func", 8, false, true, false, 1),
                            makeSection("__DATA,__mod_init_func", 8, false, true, false, 64)});

        std::vector<ObjFile> objs = {obj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::macOS, LinkArch::AArch64, layout, err);
        CHECK(ok);
        CHECK(layout.sections.size() == 1);
        CHECK(layout.sections[0].name == "__DATA,__mod_init_func");
        CHECK(layout.sections[0].dataSegment);
        CHECK(layout.sections[0].chunks.size() == 2);
        CHECK(layout.sections[0].chunks[0].inputSecIndex == 1);
        CHECK(layout.sections[0].chunks[1].inputSecIndex == 2);
    }

    // --- Empty alloc sections with live symbols are preserved ---
    {
        ObjSection emptyText = makeSection(".text", 0, true, false);
        emptyText.data.clear();
        auto obj = makeObj("empty_live.o", ObjFileFormat::ELF, {emptyText});
        obj.symbols.push_back({});
        ObjSymbol marker;
        marker.name = "empty_marker";
        marker.binding = ObjSymbol::Global;
        marker.sectionIndex = 1;
        marker.offset = 0;
        obj.symbols.push_back(marker);

        std::vector<ObjFile> objs = {obj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);
        CHECK(layout.sections.size() == 1);
        CHECK(layout.sections[0].name == ".text");
        CHECK(layout.sections[0].data.empty());
        CHECK(layout.sections[0].chunks.size() == 1);
        CHECK(layout.sections[0].chunks[0].inputSecIndex == 1);
        CHECK(layout.sections[0].chunks[0].size == 0);
    }

    // --- Zero-fill sections keep logical memory size without materialized bytes ---
    {
        ObjSection bss = makeSection(".bss", 0, false, true, false, 16, true);
        bss.data.clear();
        bss.memSize = 64;
        auto obj = makeObj("bss.o", ObjFileFormat::ELF, {bss});
        obj.symbols.push_back({});
        ObjSymbol sym;
        sym.name = "bss_sym";
        sym.binding = ObjSymbol::Global;
        sym.sectionIndex = 1;
        sym.offset = 32;
        obj.symbols.push_back(sym);

        std::vector<ObjFile> objs = {obj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::Linux, LinkArch::X86_64, layout, err);
        CHECK(ok);
        CHECK(layout.sections.size() == 1);
        CHECK(layout.sections[0].name == ".bss");
        CHECK(layout.sections[0].zeroFill);
        CHECK(layout.sections[0].data.empty());
        CHECK(outputSectionMemSize(layout.sections[0]) == 64);
        CHECK(layout.sections[0].chunks.size() == 1);
        CHECK(layout.sections[0].chunks[0].size == 64);
    }

    // --- Mach-O const data stays in a data segment without becoming writable ---
    {
        ObjSection constData = makeSection("__DATA_CONST,__const", 8, false, false, false, 8);
        constData.dataSegment = true;
        auto obj = makeObj("constdata.o", ObjFileFormat::MachO, {constData});

        std::vector<ObjFile> objs = {obj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::macOS, LinkArch::AArch64, layout, err);
        CHECK(ok);
        CHECK(layout.sections.size() == 1);
        CHECK(layout.sections[0].name == "__DATA_CONST,__const");
        CHECK(!layout.sections[0].writable);
        CHECK(layout.sections[0].dataSegment);
    }

    // --- ObjC metadata sections merge despite harmless reader/synthetic flag differences ---
    {
        ObjSection inputSelrefs = makeSection("__DATA,__objc_selrefs", 8, false, false, false, 8);
        inputSelrefs.dataSegment = true;
        ObjSection generatedSelrefs =
            makeSection("__DATA,__objc_selrefs", 8, false, true, false, 8);
        generatedSelrefs.dataSegment = false;

        auto inputObj = makeObj("input.o", ObjFileFormat::MachO, {inputSelrefs});
        auto generatedObj = makeObj("objc-stubs.o", ObjFileFormat::ELF, {generatedSelrefs});

        std::vector<ObjFile> objs = {inputObj, generatedObj};
        LinkLayout layout;
        std::ostringstream err;

        bool ok = mergeSections(objs, LinkPlatform::macOS, LinkArch::AArch64, layout, err);
        CHECK(ok);
        CHECK(err.str().empty());
        CHECK(layout.sections.size() == 1);
        CHECK(layout.sections[0].name == "__DATA,__objc_selrefs");
        CHECK(layout.sections[0].data.size() == 16);
        CHECK(layout.sections[0].writable);
        CHECK(layout.sections[0].dataSegment);
        CHECK(layout.sections[0].chunks.size() == 2);
    }

    // --- Virtual address overflow is diagnosed ---
    {
        LinkLayout layout;
        layout.pageSize = std::numeric_limits<uint64_t>::max();
        OutputSection text;
        text.name = ".text";
        text.alloc = true;
        text.executable = true;
        text.data.resize(1, 0);
        layout.sections.push_back(std::move(text));

        std::ostringstream err;
        CHECK(!assignSectionVirtualAddresses(layout, LinkPlatform::Linux, err));
        CHECK(err.str().find("image base plus page size") != std::string::npos);
    }

    // --- Read-only data names containing ".text" are not executable text ---
    {
        CHECK(classifySection(".rodata.textual_metadata", false, false, false, false) ==
              SectionClass::Rodata);
        CHECK(classifySection(".text.hot", false, false, false, false) == SectionClass::Text);
    }

    // --- Result ---
    if (gFail == 0) {
        std::cout << "All SectionMerger tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " SectionMerger test(s) FAILED.\n";
    return EXIT_FAILURE;
}
