//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/linker/test_linker_integration.cpp
// Purpose: End-to-end integration tests for the native linker pipeline.
//          Exercises the complete flow: ObjFiles → resolveSymbols →
//          mergeSections → applyRelocations → writeElfExe → verify output.
// Key invariants:
//   - Pipeline stages compose correctly (VAs from merge used by reloc)
//   - Cross-object symbol resolution produces correct addresses
//   - Multi-section layout preserved through to final binary
//   - Entry point resolved and propagated to ELF header
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/linker/SymbolResolver.hpp
//        codegen/common/linker/SectionMerger.hpp
//        codegen/common/linker/RelocApplier.hpp
//        codegen/common/linker/ElfExeWriter.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/ElfExeWriter.hpp"
#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/RelocApplier.hpp"
#include "codegen/common/linker/SectionMerger.hpp"
#include "codegen/common/linker/SymbolResolver.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

// ─── ELF structures for parsing output ───────────────────────────────────

struct Elf64_Ehdr
{
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64_Phdr
{
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

struct Elf64_Shdr
{
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};

static constexpr uint32_t PT_LOAD = 1;
static constexpr uint32_t PF_X = 1;
static constexpr uint32_t PF_W = 2;
static constexpr uint32_t PF_R = 4;

// ─── Helpers ─────────────────────────────────────────────────────────────

static std::vector<uint8_t> readFile(const std::string &path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        return {};
    auto sz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char *>(data.data()), sz);
    return data;
}

static std::string tmpPath(const std::string &name)
{
    auto dir = std::filesystem::temp_directory_path() / "viper_linker_integ";
    std::filesystem::create_directories(dir);
    return (dir / name).string();
}

static void cleanupTmp()
{
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "viper_linker_integ", ec);
}

static uint32_t readLE32(const uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/// Create a simple ObjFile with named sections and symbols.
static ObjFile makeSimpleObj(const std::string &name)
{
    ObjFile obj;
    obj.name = name;
    obj.format = ObjFileFormat::ELF;
    obj.is64bit = true;
    obj.isLittleEndian = true;
    obj.machine = 62;           // EM_X86_64
    obj.sections.push_back({}); // Null section
    obj.symbols.push_back({});  // Null symbol
    return obj;
}

/// Add a section to an ObjFile and return its index.
static size_t addSection(ObjFile &obj,
                         const std::string &name,
                         const std::vector<uint8_t> &data,
                         bool exec,
                         bool write,
                         uint32_t align = 4)
{
    ObjSection sec;
    sec.name = name;
    sec.data = data;
    sec.executable = exec;
    sec.writable = write;
    sec.alloc = true;
    sec.alignment = align;
    size_t idx = obj.sections.size();
    obj.sections.push_back(sec);
    return idx;
}

/// Add a global symbol to an ObjFile and return its index.
static size_t addGlobalSym(ObjFile &obj, const std::string &name, uint32_t secIdx, size_t offset)
{
    ObjSymbol sym;
    sym.name = name;
    sym.binding = ObjSymbol::Global;
    sym.sectionIndex = secIdx;
    sym.offset = offset;
    size_t idx = obj.symbols.size();
    obj.symbols.push_back(sym);
    return idx;
}

/// Add an undefined symbol to an ObjFile and return its index.
static size_t addUndefSym(ObjFile &obj, const std::string &name)
{
    ObjSymbol sym;
    sym.name = name;
    sym.binding = ObjSymbol::Undefined;
    size_t idx = obj.symbols.size();
    obj.symbols.push_back(sym);
    return idx;
}

/// Add a relocation to a section.
static void addReloc(
    ObjFile &obj, size_t secIdx, size_t offset, uint32_t type, uint32_t symIdx, int64_t addend)
{
    ObjReloc rel;
    rel.offset = offset;
    rel.type = type;
    rel.symIndex = symIdx;
    rel.addend = addend;
    obj.sections[secIdx].relocs.push_back(rel);
}

// ─── Pipeline runner ─────────────────────────────────────────────────────

struct PipelineResult
{
    bool ok;
    std::string errors;
    LinkLayout layout;
    std::vector<ObjFile> allObjects;
    std::unordered_set<std::string> dynamicSyms;
};

/// Run the full linker pipeline: resolve → merge → relocate.
static PipelineResult runPipeline(std::vector<ObjFile> objects,
                                  LinkPlatform platform = LinkPlatform::Linux,
                                  LinkArch arch = LinkArch::X86_64)
{
    PipelineResult result;
    std::ostringstream err;

    // Step 1: Resolve symbols (no archives for unit tests).
    std::unordered_map<std::string, GlobalSymEntry> globalSyms;
    std::vector<Archive> archives; // Empty — no archives.
    result.ok =
        resolveSymbols(objects, archives, globalSyms, result.allObjects, result.dynamicSyms, err);
    if (!result.ok)
    {
        result.errors = err.str();
        return result;
    }

    // Step 2: Merge sections and assign VAs.
    result.ok = mergeSections(result.allObjects, platform, arch, result.layout, err);
    if (!result.ok)
    {
        result.errors = err.str();
        return result;
    }

    // Copy resolved symbol table into layout (NativeLinker does this).
    result.layout.globalSyms = globalSyms;

    // Resolve symbol addresses from the layout.
    for (auto &[name, entry] : result.layout.globalSyms)
    {
        if (entry.binding == GlobalSymEntry::Undefined || entry.binding == GlobalSymEntry::Dynamic)
            continue;
        // Find the output section containing this symbol's input chunk.
        for (const auto &outSec : result.layout.sections)
        {
            for (const auto &chunk : outSec.chunks)
            {
                if (chunk.inputObjIndex == entry.objIndex && chunk.inputSecIndex == entry.secIndex)
                {
                    entry.resolvedAddr = outSec.virtualAddr + chunk.outputOffset + entry.offset;
                    break;
                }
            }
        }
    }

    // Set entry point.
    auto mainIt = result.layout.globalSyms.find("main");
    if (mainIt != result.layout.globalSyms.end())
        result.layout.entryAddr = mainIt->second.resolvedAddr;

    // Step 3: Apply relocations.
    result.ok =
        applyRelocations(result.allObjects, result.layout, result.dynamicSyms, platform, arch, err);
    if (!result.ok)
    {
        result.errors = err.str();
        return result;
    }

    result.errors = err.str();
    return result;
}

// ─── Tests ───────────────────────────────────────────────────────────────

/// Test 1: Two objects linked together — cross-object symbol resolution + relocation.
/// Object A: main calls helper (PC-relative relocation).
/// Object B: defines helper.
/// Pipeline: resolve → merge → relocate → write ELF → verify.
static void testTwoObjectLink()
{
    // Object A: main function (8 bytes) with a CALL to "helper".
    // x86-64 CALL is E8 xx xx xx xx (5 bytes); we put the relocation at offset 1.
    auto objA = makeSimpleObj("main.o");
    std::vector<uint8_t> mainCode = {0xE8, 0x00, 0x00, 0x00, 0x00, 0xC3, 0x90, 0x90};
    auto mainTextIdx = addSection(objA, ".text", mainCode, true, false);
    auto mainSymIdx = addGlobalSym(objA, "main", static_cast<uint32_t>(mainTextIdx), 0);
    auto helperRefIdx = addUndefSym(objA, "helper");
    // R_X86_64_PC32 = type 2, addend = -4 (standard for CALL).
    addReloc(objA, mainTextIdx, 1, 2, static_cast<uint32_t>(helperRefIdx), -4);

    // Object B: helper function (4 bytes).
    auto objB = makeSimpleObj("helper.o");
    std::vector<uint8_t> helperCode = {0x48, 0x89, 0xF8, 0xC3}; // mov rax,rdi; ret
    auto helperTextIdx = addSection(objB, ".text", helperCode, true, false);
    addGlobalSym(objB, "helper", static_cast<uint32_t>(helperTextIdx), 0);

    // Run pipeline.
    auto result = runPipeline({objA, objB});
    CHECK(result.ok);

    // Verify symbols resolved.
    auto mainIt = result.layout.globalSyms.find("main");
    CHECK(mainIt != result.layout.globalSyms.end());
    auto helperIt = result.layout.globalSyms.find("helper");
    CHECK(helperIt != result.layout.globalSyms.end());

    // Both symbols should have non-zero resolved addresses.
    CHECK(mainIt->second.resolvedAddr != 0);
    CHECK(helperIt->second.resolvedAddr != 0);

    // helper's address should be after main (both in .text).
    CHECK(helperIt->second.resolvedAddr > mainIt->second.resolvedAddr);

    // Verify the relocation was applied: the CALL target at offset 1 in .text
    // should be patched to the PC-relative offset to helper.
    // Find the .text output section.
    const OutputSection *textSec = nullptr;
    for (const auto &sec : result.layout.sections)
    {
        if (sec.executable && !sec.data.empty())
        {
            textSec = &sec;
            break;
        }
    }
    CHECK(textSec != nullptr);
    if (textSec)
    {
        // The relocation at offset 1 in the first chunk should be patched.
        // PCRel32: value = S + A - P = helper_addr + (-4) - (main_addr + 1)
        uint64_t S = helperIt->second.resolvedAddr;
        uint64_t P = mainIt->second.resolvedAddr + 1; // Reloc at offset 1 from main start.
        int32_t expected = static_cast<int32_t>(S + (-4) - P);
        int32_t actual = static_cast<int32_t>(readLE32(textSec->data.data() + 1));
        CHECK(actual == expected);

        // First byte should still be E8 (CALL opcode, not relocated).
        CHECK(textSec->data[0] == 0xE8);

        // Last bytes of main should be untouched.
        CHECK(textSec->data[5] == 0xC3); // RET
    }

    // Write to ELF and verify.
    auto path = tmpPath("two_obj.elf");
    std::ostringstream err;
    bool writeOk = writeElfExe(path, result.layout, LinkArch::X86_64, err);
    CHECK(writeOk);

    auto elfData = readFile(path);
    CHECK(elfData.size() >= sizeof(Elf64_Ehdr));

    Elf64_Ehdr ehdr;
    std::memcpy(&ehdr, elfData.data(), sizeof(ehdr));
    CHECK(ehdr.e_entry == mainIt->second.resolvedAddr);
    CHECK(ehdr.e_type == 2); // ET_EXEC
}

/// Test 2: Multi-section link — .text + .rodata + .data across two objects.
static void testMultiSectionLink()
{
    // Object A: .text (code) + .rodata (string constant).
    auto objA = makeSimpleObj("a.o");
    std::vector<uint8_t> codeA = {0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0xC3};
    addSection(objA, ".text", codeA, true, false);
    addGlobalSym(objA, "main", 1, 0);
    std::vector<uint8_t> rodataA = {'H', 'e', 'l', 'l', 'o', '\0'};
    addSection(objA, ".rodata", rodataA, false, false);
    addGlobalSym(objA, "greeting", 2, 0);

    // Object B: .data (writable global).
    auto objB = makeSimpleObj("b.o");
    std::vector<uint8_t> textB = {0xC3, 0x90, 0x90, 0x90};
    addSection(objB, ".text", textB, true, false);
    addGlobalSym(objB, "init", 1, 0);
    std::vector<uint8_t> dataB = {0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00};
    addSection(objB, ".data", dataB, false, true);
    addGlobalSym(objB, "counter", 2, 0);

    auto result = runPipeline({objA, objB});
    CHECK(result.ok);

    // Verify sections were created with correct properties.
    bool hasText = false, hasRodata = false, hasData = false;
    for (const auto &sec : result.layout.sections)
    {
        if (sec.executable && !sec.data.empty())
        {
            hasText = true;
            // .text should contain code from both objects.
            CHECK(sec.data.size() >= codeA.size() + textB.size());
        }
        else if (!sec.writable && !sec.executable && !sec.data.empty())
        {
            hasRodata = true;
            // .rodata should contain the greeting string.
            CHECK(sec.data.size() >= rodataA.size());
        }
        else if (sec.writable && !sec.data.empty())
        {
            hasData = true;
            // .data should contain the counter.
            CHECK(sec.data.size() >= dataB.size());
        }
    }
    CHECK(hasText);
    CHECK(hasRodata);
    CHECK(hasData);

    // Write to ELF.
    auto path = tmpPath("multi_sec_link.elf");
    std::ostringstream err;
    bool writeOk = writeElfExe(path, result.layout, LinkArch::X86_64, err);
    CHECK(writeOk);

    // Verify ELF has multiple PT_LOAD segments.
    auto elfData = readFile(path);
    Elf64_Ehdr ehdr;
    std::memcpy(&ehdr, elfData.data(), sizeof(ehdr));

    size_t loadCount = 0;
    bool hasRxLoad = false, hasRoLoad = false, hasRwLoad = false;
    std::vector<Elf64_Phdr> phdrs(ehdr.e_phnum);
    std::memcpy(phdrs.data(), elfData.data() + ehdr.e_phoff, ehdr.e_phnum * sizeof(Elf64_Phdr));
    for (const auto &ph : phdrs)
    {
        if (ph.p_type == PT_LOAD)
        {
            ++loadCount;
            if ((ph.p_flags & PF_X) && (ph.p_flags & PF_R))
                hasRxLoad = true;
            if (!(ph.p_flags & PF_X) && !(ph.p_flags & PF_W) && (ph.p_flags & PF_R))
                hasRoLoad = true;
            if ((ph.p_flags & PF_W) && (ph.p_flags & PF_R))
                hasRwLoad = true;
        }
    }
    CHECK(loadCount >= 3); // text + rodata + data
    CHECK(hasRxLoad);      // R+X segment exists
    CHECK(hasRoLoad);      // R-only segment exists
    CHECK(hasRwLoad);      // R+W segment exists
}

/// Test 3: Symbol resolution — strong overrides weak.
static void testWeakSymbolResolution()
{
    // Object A: defines "handler" as weak.
    auto objA = makeSimpleObj("weak.o");
    std::vector<uint8_t> weakCode = {0xC3, 0x90, 0x90, 0x90}; // ret; nop; nop; nop
    addSection(objA, ".text", weakCode, true, false);
    // Add weak symbol manually.
    {
        ObjSymbol sym;
        sym.name = "handler";
        sym.binding = ObjSymbol::Weak;
        sym.sectionIndex = 1;
        sym.offset = 0;
        objA.symbols.push_back(sym);
    }
    addGlobalSym(objA, "main", 1, 0);

    // Object B: defines "handler" as strong (with different code).
    auto objB = makeSimpleObj("strong.o");
    std::vector<uint8_t> strongCode = {0x48, 0x31, 0xC0, 0xC3}; // xor rax,rax; ret
    addSection(objB, ".text", strongCode, true, false);
    addGlobalSym(objB, "handler", 1, 0);

    auto result = runPipeline({objA, objB});
    CHECK(result.ok);

    // The strong definition should win.
    auto it = result.layout.globalSyms.find("handler");
    CHECK(it != result.layout.globalSyms.end());
    CHECK(it->second.binding == GlobalSymEntry::Global);
    CHECK(it->second.objIndex == 1); // From object B (strong).
}

/// Test 4: Multiply-defined strong symbol — should produce error.
static void testMultiplyDefinedError()
{
    auto objA = makeSimpleObj("a.o");
    addSection(objA, ".text", {0xC3, 0x90, 0x90, 0x90}, true, false);
    addGlobalSym(objA, "main", 1, 0);

    auto objB = makeSimpleObj("b.o");
    addSection(objB, ".text", {0xC3, 0x90, 0x90, 0x90}, true, false);
    addGlobalSym(objB, "main", 1, 0); // Duplicate strong "main".

    auto result = runPipeline({objA, objB});
    CHECK(!result.ok);
    CHECK(result.errors.find("multiply defined") != std::string::npos);
}

/// Test 5: Section data integrity — bytes survive the full pipeline.
static void testDataIntegrity()
{
    auto obj = makeSimpleObj("data.o");

    // .text with specific byte pattern.
    std::vector<uint8_t> code(32);
    for (size_t i = 0; i < code.size(); ++i)
        code[i] = static_cast<uint8_t>(i * 7 + 3); // Deterministic pattern.
    addSection(obj, ".text", code, true, false);
    addGlobalSym(obj, "main", 1, 0);

    // .rodata with a string.
    std::vector<uint8_t> rodata = {'V', 'i', 'p', 'e', 'r', '\0'};
    addSection(obj, ".rodata", rodata, false, false);

    auto result = runPipeline({obj});
    CHECK(result.ok);

    // Verify .text data survived pipeline (no relocations to modify it).
    for (const auto &sec : result.layout.sections)
    {
        if (sec.executable)
        {
            CHECK(sec.data.size() >= code.size());
            for (size_t i = 0; i < code.size(); ++i)
                CHECK(sec.data[i] == code[i]);
        }
        else if (!sec.writable && !sec.data.empty())
        {
            CHECK(sec.data.size() >= rodata.size());
            for (size_t i = 0; i < rodata.size(); ++i)
                CHECK(sec.data[i] == rodata[i]);
        }
    }

    // Write and read back ELF — verify data in file matches.
    auto path = tmpPath("data_integrity.elf");
    std::ostringstream err;
    bool writeOk = writeElfExe(path, result.layout, LinkArch::X86_64, err);
    CHECK(writeOk);

    auto elfData = readFile(path);
    Elf64_Ehdr ehdr;
    std::memcpy(&ehdr, elfData.data(), sizeof(ehdr));

    std::vector<Elf64_Phdr> phdrs(ehdr.e_phnum);
    std::memcpy(phdrs.data(), elfData.data() + ehdr.e_phoff, ehdr.e_phnum * sizeof(Elf64_Phdr));

    // Find the executable PT_LOAD and verify its bytes.
    for (const auto &ph : phdrs)
    {
        if (ph.p_type == PT_LOAD && (ph.p_flags & PF_X))
        {
            CHECK(elfData.size() >= ph.p_offset + code.size());
            for (size_t i = 0; i < code.size(); ++i)
                CHECK(elfData[ph.p_offset + i] == code[i]);
        }
    }
}

/// Test 6: Page-aligned VA assignment — sections on page boundaries.
static void testPageAlignedVAs()
{
    auto objA = makeSimpleObj("a.o");
    addSection(objA, ".text", std::vector<uint8_t>(100, 0x90), true, false);
    addGlobalSym(objA, "main", 1, 0);

    auto objB = makeSimpleObj("b.o");
    addSection(objB, ".data", std::vector<uint8_t>(64, 0xAA), false, true);

    auto result = runPipeline({objA, objB});
    CHECK(result.ok);

    // Verify sections have page-aligned virtual addresses.
    for (const auto &sec : result.layout.sections)
    {
        if (!sec.data.empty())
        {
            CHECK(sec.virtualAddr % result.layout.pageSize == 0);
        }
    }
}

/// Test 7: Entry point ends up in ELF header correctly.
static void testEntryPointInElf()
{
    auto obj = makeSimpleObj("entry.o");
    // main starts at offset 4 within .text (skip 4 bytes of preamble).
    std::vector<uint8_t> code = {0x90, 0x90, 0x90, 0x90, 0xC3, 0x90, 0x90, 0x90};
    addSection(obj, ".text", code, true, false);
    addGlobalSym(obj, "main", 1, 4); // main is at offset 4.

    auto result = runPipeline({obj});
    CHECK(result.ok);

    auto mainIt = result.layout.globalSyms.find("main");
    CHECK(mainIt != result.layout.globalSyms.end());

    // Entry address should be text VA + 4.
    auto path = tmpPath("entry_point.elf");
    std::ostringstream err;
    writeElfExe(path, result.layout, LinkArch::X86_64, err);

    auto elfData = readFile(path);
    Elf64_Ehdr ehdr;
    std::memcpy(&ehdr, elfData.data(), sizeof(ehdr));
    CHECK(ehdr.e_entry == mainIt->second.resolvedAddr);
    CHECK(ehdr.e_entry != 0);
}

// ─── Main ────────────────────────────────────────────────────────────────

int main()
{
    testTwoObjectLink();
    testMultiSectionLink();
    testWeakSymbolResolution();
    testMultiplyDefinedError();
    testDataIntegrity();
    testPageAlignedVAs();
    testEntryPointInElf();

    cleanupTmp();

    if (gFail > 0)
    {
        std::cerr << gFail << " check(s) FAILED\n";
        return 1;
    }
    std::cout << "All linker integration tests passed.\n";
    return 0;
}
