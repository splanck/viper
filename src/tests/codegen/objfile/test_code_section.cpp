//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/objfile/test_code_section.cpp
// Purpose: Unit tests for CodeSection — verifies byte emission, alignment,
//          patching, relocation tracking, and symbol management.
// Key invariants:
//   - Byte emission is little-endian
//   - Alignment pads with zeros
//   - patch32LE overwrites at the correct offset
//   - Relocations record correct offset, kind, and addend
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/objfile/CodeSection.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/objfile/CodeSection.hpp"

#include <cstdlib>
#include <iostream>

using namespace viper::codegen::objfile;

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

int main()
{
    // --- Initial state ---
    {
        CodeSection cs;
        CHECK(cs.currentOffset() == 0);
        CHECK(cs.bytes().empty());
        CHECK(cs.relocations().empty());
        CHECK(cs.empty());
    }

    // --- emit8 ---
    {
        CodeSection cs;
        cs.emit8(0xC3); // RET
        CHECK(cs.currentOffset() == 1);
        CHECK(cs.bytes()[0] == 0xC3);
        CHECK(!cs.empty());
    }

    // --- emit16LE ---
    {
        CodeSection cs;
        cs.emit16LE(0xABCD);
        CHECK(cs.currentOffset() == 2);
        CHECK(cs.bytes()[0] == 0xCD); // little-endian: low byte first
        CHECK(cs.bytes()[1] == 0xAB);
    }

    // --- emit32LE ---
    {
        CodeSection cs;
        cs.emit32LE(0x12345678);
        CHECK(cs.currentOffset() == 4);
        CHECK(cs.bytes()[0] == 0x78);
        CHECK(cs.bytes()[1] == 0x56);
        CHECK(cs.bytes()[2] == 0x34);
        CHECK(cs.bytes()[3] == 0x12);
    }

    // --- emit64LE ---
    {
        CodeSection cs;
        cs.emit64LE(0x0102030405060708ULL);
        CHECK(cs.currentOffset() == 8);
        CHECK(cs.bytes()[0] == 0x08);
        CHECK(cs.bytes()[7] == 0x01);
    }

    // --- emitBytes ---
    {
        CodeSection cs;
        const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
        cs.emitBytes(data, sizeof(data));
        CHECK(cs.currentOffset() == 4);
        CHECK(cs.bytes()[0] == 0xDE);
        CHECK(cs.bytes()[3] == 0xEF);
    }

    // --- emitZeros ---
    {
        CodeSection cs;
        cs.emit8(0xFF);
        cs.emitZeros(3);
        CHECK(cs.currentOffset() == 4);
        CHECK(cs.bytes()[0] == 0xFF);
        CHECK(cs.bytes()[1] == 0x00);
        CHECK(cs.bytes()[2] == 0x00);
        CHECK(cs.bytes()[3] == 0x00);
    }

    // --- alignTo ---
    {
        CodeSection cs;
        cs.emit8(0x01);
        cs.emit8(0x02);
        cs.emit8(0x03); // 3 bytes
        cs.alignTo(8);  // should pad 5 zero bytes to reach 8
        CHECK(cs.currentOffset() == 8);
        CHECK(cs.bytes()[3] == 0x00);
        CHECK(cs.bytes()[7] == 0x00);
    }

    // --- alignTo (already aligned) ---
    {
        CodeSection cs;
        cs.emit32LE(0);
        cs.emit32LE(0); // 8 bytes
        cs.alignTo(8);  // no padding needed
        CHECK(cs.currentOffset() == 8);
    }

    // --- patch32LE ---
    {
        CodeSection cs;
        cs.emit32LE(0x00000000); // placeholder
        cs.emit32LE(0xDEADDEAD); // some other data
        cs.patch32LE(0, 0xCAFEBABE);
        CHECK(cs.bytes()[0] == 0xBE);
        CHECK(cs.bytes()[1] == 0xBA);
        CHECK(cs.bytes()[2] == 0xFE);
        CHECK(cs.bytes()[3] == 0xCA);
        // Verify other data untouched
        CHECK(cs.bytes()[4] == 0xAD);
    }

    // --- patch8 ---
    {
        CodeSection cs;
        cs.emit8(0x00);
        cs.emit8(0xFF);
        cs.patch8(0, 0x42);
        CHECK(cs.bytes()[0] == 0x42);
        CHECK(cs.bytes()[1] == 0xFF);
    }

    // --- Relocation tracking ---
    {
        CodeSection cs;
        uint32_t sym = cs.declareExternal("rt_print_i64");
        cs.emit8(0xE8); // CALL opcode
        cs.addRelocation(RelocKind::Branch32, sym, -4);
        cs.emit32LE(0x00000000); // placeholder displacement

        CHECK(cs.relocations().size() == 1);
        const auto &r = cs.relocations()[0];
        CHECK(r.offset == 1); // after the opcode byte
        CHECK(r.kind == RelocKind::Branch32);
        CHECK(r.symbolIndex == sym);
        CHECK(r.addend == -4);
    }

    // --- addRelocationAt ---
    {
        CodeSection cs;
        uint32_t sym = cs.declareExternal("rt_alloc");
        cs.emit8(0x00);
        cs.emit8(0x00);
        cs.emit8(0x00);
        cs.addRelocationAt(1, RelocKind::PCRel32, sym, -4);

        CHECK(cs.relocations().size() == 1);
        CHECK(cs.relocations()[0].offset == 1);
    }

    // --- Symbol management ---
    {
        CodeSection cs;
        uint32_t main_idx =
            cs.defineSymbol("main", SymbolBinding::Global, SymbolSection::Text);
        cs.emit8(0x55); // push rbp

        uint32_t ext_idx = cs.declareExternal("rt_init");
        uint32_t ext_idx2 = cs.findOrDeclareSymbol("rt_init"); // should find existing

        CHECK(main_idx != 0); // not the null entry
        CHECK(cs.symbols().at(main_idx).name == "main");
        CHECK(cs.symbols().at(main_idx).binding == SymbolBinding::Global);
        CHECK(cs.symbols().at(main_idx).section == SymbolSection::Text);
        CHECK(cs.symbols().at(main_idx).offset == 0);

        CHECK(ext_idx != 0);
        CHECK(cs.symbols().at(ext_idx).binding == SymbolBinding::External);
        CHECK(ext_idx2 == ext_idx); // found same symbol
    }

    // --- Multiple relocations ---
    {
        CodeSection cs;
        uint32_t sym1 = cs.declareExternal("func_a");
        uint32_t sym2 = cs.declareExternal("func_b");

        cs.emit8(0xE8);
        cs.addRelocation(RelocKind::Branch32, sym1, -4);
        cs.emit32LE(0);

        cs.emit8(0xE8);
        cs.addRelocation(RelocKind::Branch32, sym2, -4);
        cs.emit32LE(0);

        CHECK(cs.relocations().size() == 2);
        CHECK(cs.relocations()[0].symbolIndex == sym1);
        CHECK(cs.relocations()[0].offset == 1);
        CHECK(cs.relocations()[1].symbolIndex == sym2);
        CHECK(cs.relocations()[1].offset == 6); // 5 bytes for first call + 1 opcode byte
    }

    // --- AArch64-style relocation ---
    {
        CodeSection cs;
        uint32_t sym = cs.declareExternal("rt_alloc");
        cs.addRelocation(RelocKind::A64Call26, sym, 0);
        cs.emit32LE(0x94000000); // BL placeholder

        CHECK(cs.relocations().size() == 1);
        CHECK(cs.relocations()[0].kind == RelocKind::A64Call26);
        CHECK(cs.relocations()[0].addend == 0);
    }

    // --- Result ---
    if (gFail == 0)
    {
        std::cout << "All CodeSection tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " CodeSection test(s) FAILED.\n";
    return EXIT_FAILURE;
}
