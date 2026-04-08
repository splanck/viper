//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/objfile/test_debug_line.cpp
// Purpose: Unit tests for DebugLineTable DWARF v5 .debug_line encoding.
// Key invariants:
//   - Encoded bytes form a valid DWARF v5 .debug_line section
//   - Unit length field matches total size minus 4
//   - File table entries are NUL-terminated, 1-based indexed
//   - Special opcodes encode compact (addr, line) deltas
//   - End-of-sequence marker terminates the line program
// Ownership/Lifetime: Standalone test binary.
// Links: codegen/common/objfile/DebugLineTable.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/objfile/DebugLineTable.hpp"

#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace viper::codegen;

static int gFail = 0;

static void check(bool cond, const char *msg, int line) {
    if (!cond) {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

/// Read a little-endian uint16 from a byte buffer.
static uint16_t readLE16(const uint8_t *p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

/// Read a little-endian uint32 from a byte buffer.
static uint32_t readLE32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/// Decode a ULEB128 value from a byte buffer, advancing the pointer.
static uint64_t readULEB128(const uint8_t *&p) {
    uint64_t result = 0;
    unsigned shift = 0;
    while (true) {
        uint8_t byte = *p++;
        result |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0)
            break;
        shift += 7;
    }
    return result;
}

/// Decode a SLEB128 value from a byte buffer, advancing the pointer.
static int64_t readSLEB128(const uint8_t *&p) {
    int64_t result = 0;
    unsigned shift = 0;
    uint8_t byte;
    do {
        byte = *p++;
        result |= static_cast<int64_t>(byte & 0x7F) << shift;
        shift += 7;
    } while (byte & 0x80);
    if (shift < 64 && (byte & 0x40))
        result |= -(static_cast<int64_t>(1) << shift);
    return result;
}

int main() {
    // --- Test 1: Empty table produces non-empty output (header + end_sequence) ---
    {
        DebugLineTable table;
        table.addFile("<source>");
        auto bytes = table.encodeDwarf5(8);

        // Should produce at least a header. The unit_length field (bytes 0-3)
        // should equal total size minus 4.
        CHECK(bytes.size() >= 4);
        uint32_t unitLen = readLE32(bytes.data());
        CHECK(unitLen == bytes.size() - 4);

        // Version should be 5.
        CHECK(readLE16(bytes.data() + 4) == 5);
        // Address size = 8.
        CHECK(bytes[6] == 8);
        // Segment selector size = 0.
        CHECK(bytes[7] == 0);
    }

    // --- Test 2: Unit length is correctly computed ---
    {
        DebugLineTable table;
        table.addFile("main.vpr");
        table.addEntry(0x100, 1, 10, 1);
        table.addEntry(0x108, 1, 11, 5);
        auto bytes = table.encodeDwarf5(8);

        uint32_t unitLen = readLE32(bytes.data());
        CHECK(unitLen == bytes.size() - 4);
    }

    // --- Test 3: File table contains registered file names ---
    {
        DebugLineTable table;
        uint32_t idx1 = table.addFile("hello.vpr");
        uint32_t idx2 = table.addFile("world.vpr");
        CHECK(idx1 == 1);
        CHECK(idx2 == 2);

        // Duplicate file returns existing index.
        uint32_t idx3 = table.addFile("hello.vpr");
        CHECK(idx3 == 1);

        auto bytes = table.encodeDwarf5(8);

        // Search for file names in the encoded bytes.
        // They should appear as NUL-terminated strings in the file table.
        bool foundHello = false, foundWorld = false;
        for (size_t i = 0; i + 9 < bytes.size(); ++i) {
            if (std::memcmp(bytes.data() + i, "hello.vpr", 9) == 0 && bytes[i + 9] == 0)
                foundHello = true;
            if (std::memcmp(bytes.data() + i, "world.vpr", 9) == 0 && bytes[i + 9] == 0)
                foundWorld = true;
        }
        CHECK(foundHello);
        CHECK(foundWorld);
    }

    // --- Test 4: Line program contains DW_LNE_set_address for first entry ---
    {
        DebugLineTable table;
        table.addFile("test.vpr");
        table.addEntry(0x1000, 1, 1, 1);
        auto bytes = table.encodeDwarf5(8);

        // After the header, the first opcode should be extended opcode 0x00 (escape),
        // followed by ULEB128 length, then DW_LNE_set_address (2).
        // Find the extended opcode pattern: 0x00, length, 0x02, <8 bytes address>
        bool foundSetAddr = false;
        for (size_t i = 0; i + 11 < bytes.size(); ++i) {
            if (bytes[i] == 0x00) {
                const uint8_t *p = bytes.data() + i + 1;
                uint64_t extLen = readULEB128(p);
                if (extLen == 9 && *p == 0x02) // DW_LNE_set_address, 8-byte addr + 1 opcode byte
                {
                    foundSetAddr = true;
                    break;
                }
            }
        }
        CHECK(foundSetAddr);
    }

    // --- Test 5: Line program ends with DW_LNE_end_sequence ---
    {
        DebugLineTable table;
        table.addFile("test.vpr");
        table.addEntry(0x0, 1, 1, 0);
        auto bytes = table.encodeDwarf5(8);

        // End sequence is extended opcode: 0x00, ULEB128(1), 0x01 (DW_LNE_end_sequence).
        size_t sz = bytes.size();
        CHECK(sz >= 3);
        // The last 3 bytes should be the end_sequence: 0x00, 0x01, 0x01
        CHECK(bytes[sz - 3] == 0x00);
        CHECK(bytes[sz - 2] == 0x01);
        CHECK(bytes[sz - 1] == 0x01);
    }

    // --- Test 6: Multiple entries with line advances ---
    {
        DebugLineTable table;
        table.addFile("multi.vpr");
        table.addEntry(0x0, 1, 1, 0);
        table.addEntry(0x4, 1, 2, 0);
        table.addEntry(0x8, 1, 5, 0);
        table.addEntry(0x10, 1, 100, 0);
        auto bytes = table.encodeDwarf5(8);

        // Just verify it produces valid output with correct unit_length.
        uint32_t unitLen = readLE32(bytes.data());
        CHECK(unitLen == bytes.size() - 4);
        // Should be larger than single-entry case due to additional opcodes.
        CHECK(bytes.size() > 30);
    }

    // --- Test 7: 32-bit address size ---
    {
        DebugLineTable table;
        table.addFile("test32.vpr");
        table.addEntry(0x100, 1, 1, 0);
        auto bytes = table.encodeDwarf5(4);

        // Address size field should be 4.
        CHECK(bytes[6] == 4);
        // Unit length still valid.
        uint32_t unitLen = readLE32(bytes.data());
        CHECK(unitLen == bytes.size() - 4);
    }

    // --- Test 8: File index changes emit DW_LNS_set_file ---
    {
        DebugLineTable table;
        table.addFile("a.vpr");
        table.addFile("b.vpr");
        table.addEntry(0x0, 1, 1, 0);
        table.addEntry(0x4, 2, 1, 0);
        auto bytes = table.encodeDwarf5(8);

        // Search for DW_LNS_set_file (opcode 4) followed by ULEB128(2).
        bool foundSetFile = false;
        for (size_t i = 0; i + 1 < bytes.size(); ++i) {
            if (bytes[i] == 4 && bytes[i + 1] == 2) // DW_LNS_set_file, file=2
            {
                foundSetFile = true;
                break;
            }
        }
        CHECK(foundSetFile);
    }

    // --- Test 9: Column changes emit DW_LNS_set_column ---
    {
        DebugLineTable table;
        table.addFile("col.vpr");
        table.addEntry(0x0, 1, 1, 0);
        table.addEntry(0x4, 1, 2, 10);
        auto bytes = table.encodeDwarf5(8);

        // Search for DW_LNS_set_column (opcode 5) followed by ULEB128(10).
        bool foundSetCol = false;
        for (size_t i = 0; i + 1 < bytes.size(); ++i) {
            if (bytes[i] == 5 && bytes[i + 1] == 10) // DW_LNS_set_column, col=10
            {
                foundSetCol = true;
                break;
            }
        }
        CHECK(foundSetCol);
    }

    // --- Test 10: Empty entries produce minimal output ---
    {
        DebugLineTable table;
        table.addFile("empty.vpr");
        CHECK(table.empty());
        auto bytes = table.encodeDwarf5(8);
        // No entries, but should still have header + end_sequence.
        uint32_t unitLen = readLE32(bytes.data());
        CHECK(unitLen == bytes.size() - 4);
    }

    // --- Test 11: SLEB128 round-trip (negative values) ---
    {
        // Verify SLEB128 encoding by checking that the DebugLineTable can
        // handle large negative line deltas (e.g., line 100 → line 1).
        DebugLineTable table;
        table.addFile("neg.vpr");
        table.addEntry(0x0, 1, 100, 0);
        table.addEntry(0x4, 1, 1, 0); // line delta = -99
        auto bytes = table.encodeDwarf5(8);

        // Should contain DW_LNS_advance_line (opcode 3) followed by SLEB128(-99).
        bool foundAdvanceLine = false;
        for (size_t i = 0; i + 2 < bytes.size(); ++i) {
            if (bytes[i] == 3) // DW_LNS_advance_line
            {
                const uint8_t *p = bytes.data() + i + 1;
                int64_t val = readSLEB128(p);
                if (val == -99) {
                    foundAdvanceLine = true;
                    break;
                }
            }
        }
        CHECK(foundAdvanceLine);
    }

    // --- Test 12: Duplicate logical file slots survive append() ---
    {
        DebugLineTable perFunc;
        perFunc.addFileSlot("<source>");
        perFunc.addFileSlot("<source>");
        perFunc.addEntry(0x0, 2, 7, 0);

        DebugLineTable module;
        module.append(perFunc, 0x40);
        auto bytes = module.encodeDwarf5(8);

        uint32_t unitLen = readLE32(bytes.data());
        CHECK(unitLen == bytes.size() - 4);

        bool foundSetFile = false;
        for (size_t i = 0; i + 1 < bytes.size(); ++i) {
            if (bytes[i] == 4 && bytes[i + 1] == 2) {
                foundSetFile = true;
                break;
            }
        }
        CHECK(foundSetFile);
    }

    if (gFail == 0)
        std::cout << "All debug_line tests passed.\n";
    else
        std::cerr << gFail << " debug_line test(s) FAILED.\n";

    return gFail ? EXIT_FAILURE : EXIT_SUCCESS;
}
