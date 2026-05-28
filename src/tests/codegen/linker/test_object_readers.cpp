//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/linker/test_object_readers.cpp
// Purpose: Regression tests for ELF/Mach-O/COFF object reader hardening.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/RelocConstants.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace viper::codegen::linker;

static int gFail = 0;

static void check(bool cond, const char *msg, int line) {
    if (!cond) {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

static void appendLE16(std::vector<uint8_t> &buf, uint16_t value) {
    buf.push_back(static_cast<uint8_t>(value));
    buf.push_back(static_cast<uint8_t>(value >> 8));
}

static void appendLE32(std::vector<uint8_t> &buf, uint32_t value) {
    buf.push_back(static_cast<uint8_t>(value));
    buf.push_back(static_cast<uint8_t>(value >> 8));
    buf.push_back(static_cast<uint8_t>(value >> 16));
    buf.push_back(static_cast<uint8_t>(value >> 24));
}

static void appendLE64(std::vector<uint8_t> &buf, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
        buf.push_back(static_cast<uint8_t>(value >> (i * 8)));
}

static void appendName16(std::vector<uint8_t> &buf, const char *name) {
    const size_t len = std::strlen(name);
    for (size_t i = 0; i < 16; ++i)
        buf.push_back(i < len ? static_cast<uint8_t>(name[i]) : 0);
}

static void patchLE32(std::vector<uint8_t> &buf, size_t off, uint32_t value) {
    buf[off + 0] = static_cast<uint8_t>(value);
    buf[off + 1] = static_cast<uint8_t>(value >> 8);
    buf[off + 2] = static_cast<uint8_t>(value >> 16);
    buf[off + 3] = static_cast<uint8_t>(value >> 24);
}

static void patchLE64(std::vector<uint8_t> &buf, size_t off, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
        buf[off + i] = static_cast<uint8_t>(value >> (i * 8));
}

static void appendElfHeader(std::vector<uint8_t> &buf,
                            uint16_t shnum,
                            uint16_t shstrndx,
                            uint64_t shoff) {
    buf.insert(buf.end(), {0x7F, 'E', 'L', 'F', 2, 1, 1, 0});
    buf.resize(16, 0);
    appendLE16(buf, 1);  // ET_REL
    appendLE16(buf, 62); // EM_X86_64
    appendLE32(buf, 1);
    appendLE64(buf, 0);
    appendLE64(buf, 0);
    appendLE64(buf, shoff);
    appendLE32(buf, 0);
    appendLE16(buf, 64);
    appendLE16(buf, 0);
    appendLE16(buf, 0);
    appendLE16(buf, 64);
    appendLE16(buf, shnum);
    appendLE16(buf, shstrndx);
}

static void appendElfShdr(std::vector<uint8_t> &buf,
                          uint32_t type,
                          uint64_t flags,
                          uint64_t off,
                          uint64_t size,
                          uint32_t link,
                          uint32_t info,
                          uint64_t align,
                          uint64_t entsize) {
    appendLE32(buf, 0);
    appendLE32(buf, type);
    appendLE64(buf, flags);
    appendLE64(buf, 0);
    appendLE64(buf, off);
    appendLE64(buf, size);
    appendLE32(buf, link);
    appendLE32(buf, info);
    appendLE64(buf, align);
    appendLE64(buf, entsize);
}

static void appendElfSym(std::vector<uint8_t> &buf,
                         uint32_t name,
                         uint8_t info,
                         uint16_t shndx,
                         uint64_t value,
                         uint64_t size) {
    appendLE32(buf, name);
    buf.push_back(info);
    buf.push_back(0);
    appendLE16(buf, shndx);
    appendLE64(buf, value);
    appendLE64(buf, size);
}

static std::vector<uint8_t> makeElfZeroExtendedSections() {
    std::vector<uint8_t> obj;
    appendElfHeader(obj, 0, 0, 64);
    appendElfShdr(obj, 0, 0, 0, 0, 0, 0, 0, 0);
    return obj;
}

static std::vector<uint8_t> makeElfRelocWithSecondSymtab() {
    std::vector<uint8_t> obj;
    appendElfHeader(obj, 7, 0, 0);

    const size_t textOff = obj.size();
    appendLE32(obj, 0);
    const size_t str1Off = obj.size();
    obj.insert(obj.end(), {'\0', 'w', 'r', 'o', 'n', 'g', '\0'});
    const size_t str2Off = obj.size();
    obj.insert(obj.end(), {'\0', 'r', 'i', 'g', 'h', 't', '\0'});
    const size_t sym1Off = obj.size();
    obj.resize(obj.size() + 24, 0);
    appendElfSym(obj, 1, 0x10, 0, 0, 0);
    const size_t sym2Off = obj.size();
    obj.resize(obj.size() + 24, 0);
    appendElfSym(obj, 1, 0x10, 0, 0, 0);
    const size_t relaOff = obj.size();
    appendLE64(obj, 0);
    appendLE64(obj, (uint64_t{1} << 32) | elf_x64::kPC32);
    appendLE64(obj, 0);

    const size_t shoff = obj.size();
    patchLE64(obj, 40, shoff);
    appendElfShdr(obj, 0, 0, 0, 0, 0, 0, 0, 0);
    appendElfShdr(obj, 1, 0x6, textOff, 4, 0, 0, 4, 0); // .text
    appendElfShdr(obj, 3, 0, str1Off, 7, 0, 0, 1, 0);   // strtab 1
    appendElfShdr(obj, 2, 0, sym1Off, 48, 2, 1, 8, 24); // symtab 1
    appendElfShdr(obj, 3, 0, str2Off, 7, 0, 0, 1, 0);   // strtab 2
    appendElfShdr(obj, 2, 0, sym2Off, 48, 4, 1, 8, 24); // symtab 2
    appendElfShdr(obj, 4, 0, relaOff, 24, 5, 1, 8, 24); // rela.text -> symtab 2
    return obj;
}

static std::vector<uint8_t> makeCoffRelocTargetsAux() {
    constexpr uint32_t kTextFlags = 0x00000020 | 0x20000000 | 0x40000000;
    std::vector<uint8_t> obj;
    appendLE16(obj, 0x8664);
    appendLE16(obj, 1);
    appendLE32(obj, 0);
    appendLE32(obj, 20 + 40 + 4 + 10);
    appendLE32(obj, 2);
    appendLE16(obj, 0);
    appendLE16(obj, 0);
    obj.insert(obj.end(), {'.', 't', 'e', 'x', 't', 0, 0, 0});
    appendLE32(obj, 4);
    appendLE32(obj, 0);
    appendLE32(obj, 4);
    appendLE32(obj, 20 + 40);
    appendLE32(obj, 20 + 40 + 4);
    appendLE32(obj, 0);
    appendLE16(obj, 1);
    appendLE16(obj, 0);
    appendLE32(obj, kTextFlags);
    appendLE32(obj, 0);
    appendLE32(obj, 0); // relocation offset
    appendLE32(obj, 1); // aux symbol index, invalid relocation target
    appendLE16(obj, coff_x64::kRel32);
    obj.insert(obj.end(), {'f', 'u', 'n', 'c', 0, 0, 0, 0});
    appendLE32(obj, 0);
    appendLE16(obj, 1);
    appendLE16(obj, 0x20);
    obj.push_back(2);
    obj.push_back(1);
    obj.resize(obj.size() + 18, 0);
    appendLE32(obj, 4);
    return obj;
}

static std::vector<uint8_t> makeCoffWeakMissingAux() {
    std::vector<uint8_t> obj;
    appendLE16(obj, 0x8664);
    appendLE16(obj, 0);
    appendLE32(obj, 0);
    appendLE32(obj, 20);
    appendLE32(obj, 1);
    appendLE16(obj, 0);
    appendLE16(obj, 0);
    obj.insert(obj.end(), {'w', 'e', 'a', 'k', 0, 0, 0, 0});
    appendLE32(obj, 0);
    appendLE16(obj, 0);
    appendLE16(obj, 0);
    obj.push_back(105);
    obj.push_back(0);
    appendLE32(obj, 4);
    return obj;
}

static std::vector<uint8_t> makeCoffSymbolPastSection() {
    constexpr uint32_t kTextFlags = 0x00000020 | 0x20000000 | 0x40000000;
    std::vector<uint8_t> obj;
    appendLE16(obj, 0x8664);
    appendLE16(obj, 1);
    appendLE32(obj, 0);
    appendLE32(obj, 20 + 40 + 4);
    appendLE32(obj, 1);
    appendLE16(obj, 0);
    appendLE16(obj, 0);
    obj.insert(obj.end(), {'.', 't', 'e', 'x', 't', 0, 0, 0});
    appendLE32(obj, 4);
    appendLE32(obj, 0);
    appendLE32(obj, 4);
    appendLE32(obj, 20 + 40);
    appendLE32(obj, 0);
    appendLE32(obj, 0);
    appendLE16(obj, 0);
    appendLE16(obj, 0);
    appendLE32(obj, kTextFlags);
    appendLE32(obj, 0xC3);
    obj.insert(obj.end(), {'f', 'u', 'n', 'c', 0, 0, 0, 0});
    appendLE32(obj, 8);
    appendLE16(obj, 1);
    appendLE16(obj, 0x20);
    obj.push_back(2);
    obj.push_back(0);
    appendLE32(obj, 4);
    return obj;
}

static void appendMachHeader(std::vector<uint8_t> &obj,
                             uint32_t cputype,
                             uint32_t ncmds,
                             uint32_t sizeofcmds,
                             uint32_t flags) {
    appendLE32(obj, 0xFEEDFACF);
    appendLE32(obj, cputype);
    appendLE32(obj, 0);
    appendLE32(obj, 1);
    appendLE32(obj, ncmds);
    appendLE32(obj, sizeofcmds);
    appendLE32(obj, flags);
    appendLE32(obj, 0);
}

static void appendMachSymtab(
    std::vector<uint8_t> &obj, uint32_t symoff, uint32_t nsyms, uint32_t stroff, uint32_t strsize) {
    appendLE32(obj, 0x02);
    appendLE32(obj, 24);
    appendLE32(obj, symoff);
    appendLE32(obj, nsyms);
    appendLE32(obj, stroff);
    appendLE32(obj, strsize);
}

static std::vector<uint8_t> makeMachOArm64UnsignedReloc() {
    constexpr uint32_t segmentSize = 72 + 80;
    constexpr uint32_t sizeofcmds = segmentSize + 24;
    constexpr uint32_t dataOff = 32 + sizeofcmds;
    constexpr uint32_t relocOff = dataOff + 8;
    constexpr uint32_t symOff = relocOff + 8;
    constexpr uint32_t strOff = symOff + 16;

    std::vector<uint8_t> obj;
    appendMachHeader(obj, 0x0100000C, 2, sizeofcmds, 0);
    appendLE32(obj, 0x19);
    appendLE32(obj, segmentSize);
    appendName16(obj, "");
    appendLE64(obj, 0);
    appendLE64(obj, 8);
    appendLE64(obj, dataOff);
    appendLE64(obj, 8);
    appendLE32(obj, 0);
    appendLE32(obj, 0);
    appendLE32(obj, 1);
    appendLE32(obj, 0);
    appendName16(obj, "__data");
    appendName16(obj, "__DATA");
    appendLE64(obj, 0);
    appendLE64(obj, 8);
    appendLE32(obj, dataOff);
    appendLE32(obj, 3);
    appendLE32(obj, relocOff);
    appendLE32(obj, 1);
    appendLE32(obj, 0);
    appendLE32(obj, 0);
    appendLE32(obj, 0);
    appendLE32(obj, 0);
    appendMachSymtab(obj, symOff, 1, strOff, 9);
    appendLE64(obj, 0x8877665544332211ULL);
    appendLE32(obj, 0);
    appendLE32(obj, (3u << 25) | (1u << 27)); // extern, length=quad, ARM64_RELOC_UNSIGNED
    appendLE32(obj, 1);
    obj.push_back(0x01);
    obj.push_back(0);
    appendLE16(obj, 0);
    appendLE64(obj, 0);
    obj.insert(obj.end(), {'\0', '_', 't', 'a', 'r', 'g', 'e', 't', '\0'});
    return obj;
}

static std::vector<uint8_t> makeMachOLocalSubsections() {
    constexpr uint32_t segmentSize = 72 + 80;
    constexpr uint32_t sizeofcmds = segmentSize + 24;
    constexpr uint32_t dataOff = 32 + sizeofcmds;
    constexpr uint32_t symOff = dataOff + 8;
    constexpr uint32_t strOff = symOff + 32;

    std::vector<uint8_t> obj;
    appendMachHeader(obj, 0x0100000C, 2, sizeofcmds, 0x2000);
    appendLE32(obj, 0x19);
    appendLE32(obj, segmentSize);
    appendName16(obj, "__TEXT");
    appendLE64(obj, 0);
    appendLE64(obj, 8);
    appendLE64(obj, dataOff);
    appendLE64(obj, 8);
    appendLE32(obj, 0);
    appendLE32(obj, 0);
    appendLE32(obj, 1);
    appendLE32(obj, 0);
    appendName16(obj, "__text");
    appendName16(obj, "__TEXT");
    appendLE64(obj, 0);
    appendLE64(obj, 8);
    appendLE32(obj, dataOff);
    appendLE32(obj, 2);
    appendLE32(obj, 0);
    appendLE32(obj, 0);
    appendLE32(obj, 0x80000000);
    appendLE32(obj, 0);
    appendLE32(obj, 0);
    appendLE32(obj, 0);
    appendMachSymtab(obj, symOff, 2, strOff, 13);
    obj.insert(obj.end(), {0xC0, 0x03, 0x5F, 0xD6, 0xC0, 0x03, 0x5F, 0xD6});
    appendLE32(obj, 1);
    obj.push_back(0x0E);
    obj.push_back(1);
    appendLE16(obj, 0);
    appendLE64(obj, 0);
    appendLE32(obj, 8);
    obj.push_back(0x0E);
    obj.push_back(1);
    appendLE16(obj, 0);
    appendLE64(obj, 4);
    obj.insert(obj.end(), {'\0', 'L', 'f', 'i', 'r', 's', 't', '\0', 'L', 's', 'e', 'c', '\0'});
    return obj;
}

int main() {
    {
        ObjFile obj;
        std::ostringstream err;
        auto bytes = makeElfZeroExtendedSections();
        CHECK(readObjFile(bytes.data(), bytes.size(), "zero-sections.o", obj, err));
        CHECK(obj.sections.size() == 1);
    }

    {
        ObjFile obj;
        std::ostringstream err;
        auto bytes = makeElfRelocWithSecondSymtab();
        CHECK(readObjFile(bytes.data(), bytes.size(), "linked-symtab.o", obj, err));
        CHECK(obj.sections.size() == 2);
        CHECK(obj.sections[1].relocs.size() == 1);
        CHECK(obj.symbols[obj.sections[1].relocs[0].symIndex].name == "right");
    }

    {
        ObjFile obj;
        std::ostringstream err;
        auto bytes = makeCoffRelocTargetsAux();
        CHECK(!readObjFile(bytes.data(), bytes.size(), "aux-reloc.obj", obj, err));
        CHECK(err.str().find("auxiliary symbol index") != std::string::npos);
    }

    {
        ObjFile obj;
        std::ostringstream err;
        auto bytes = makeCoffWeakMissingAux();
        CHECK(!readObjFile(bytes.data(), bytes.size(), "weak-missing-aux.obj", obj, err));
        CHECK(err.str().find("missing auxiliary") != std::string::npos);
    }

    {
        ObjFile obj;
        std::ostringstream err;
        auto bytes = makeCoffSymbolPastSection();
        CHECK(!readObjFile(bytes.data(), bytes.size(), "bad-symbol.obj", obj, err));
        CHECK(err.str().find("outside section") != std::string::npos);
    }

    {
        ObjFile obj;
        std::ostringstream err;
        auto bytes = makeMachOArm64UnsignedReloc();
        CHECK(readObjFile(bytes.data(), bytes.size(), "arm64-data.o", obj, err));
        CHECK(obj.sections.size() == 2);
        CHECK(obj.sections[1].relocs.size() == 1);
        CHECK(obj.sections[1].relocs[0].addend == static_cast<int64_t>(0x8877665544332211ULL));
    }

    {
        ObjFile obj;
        std::ostringstream err;
        auto bytes = makeMachOLocalSubsections();
        CHECK(readObjFile(bytes.data(), bytes.size(), "subsections.o", obj, err));
        CHECK(obj.sections.size() == 3);
        CHECK(obj.sections[1].data.size() == 4);
        CHECK(obj.sections[2].data.size() == 4);
    }

    if (gFail == 0) {
        std::cout << "All object reader tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " object reader test(s) FAILED.\n";
    return EXIT_FAILURE;
}
