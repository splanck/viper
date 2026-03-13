//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/StringDedup.cpp
// Purpose: Implements cross-module string deduplication for the native linker.
//          Operates between deadStrip() and mergeSections() in the link pipeline.
// Key invariants:
//   - Only scans LOCAL symbols at exact offsets with NUL-terminated content
//   - Non-string rodata (floats, alignment padding) is skipped
//   - All duplicates share one synthetic global name (__dedup_str_N)
//   - Canonical copy is first occurrence; others become aliases
// Links: codegen/common/linker/StringDedup.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/StringDedup.hpp"

#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"

#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace viper::codegen::linker
{

size_t deduplicateStrings(std::vector<ObjFile> &allObjects,
                          std::unordered_map<std::string, GlobalSymEntry> &globalSyms)
{
    // Location of a string occurrence: (object index, symbol index).
    struct SymLoc
    {
        size_t objIdx;
        size_t symIdx;
    };

    // Step 1: Scan all LOCAL symbols in rodata sections and extract NUL-terminated content.
    // Key = string content (including NUL terminator), Value = list of occurrences.
    std::unordered_map<std::string, std::vector<SymLoc>> contentMap;

    for (size_t oi = 0; oi < allObjects.size(); ++oi)
    {
        auto &obj = allObjects[oi];
        for (size_t si = 1; si < obj.symbols.size(); ++si)
        {
            const auto &sym = obj.symbols[si];

            // Only LOCAL symbols with a valid section reference.
            if (sym.binding != ObjSymbol::Local)
                continue;
            if (sym.sectionIndex == 0 || sym.sectionIndex >= obj.sections.size())
                continue;

            const auto &sec = obj.sections[sym.sectionIndex];

            // Only non-executable, non-writable, allocatable sections (rodata).
            if (sec.executable || sec.writable || !sec.alloc)
                continue;
            if (sec.data.empty())
                continue;

            // Extract NUL-terminated content starting at sym.offset.
            if (sym.offset >= sec.data.size())
                continue;

            const uint8_t *start = sec.data.data() + sym.offset;
            size_t maxLen = sec.data.size() - sym.offset;

            // Find NUL terminator within section bounds.
            const void *nulPos = std::memchr(start, '\0', maxLen);
            if (!nulPos)
                continue; // No NUL found — not a string.

            size_t strLen = static_cast<size_t>(static_cast<const uint8_t *>(nulPos) - start) +
                            1; // Include NUL.

            // Skip degenerate "strings" that are just a NUL byte. Non-string data
            // (bitmap fonts, lookup tables) often starts with 0x00 and would be
            // misidentified as an empty string, causing incorrect merging.
            if (strLen <= 1)
                continue;

            // Use the raw bytes (including NUL) as the content key.
            std::string content(reinterpret_cast<const char *>(start), strLen);

            contentMap[content].push_back({oi, si});
        }
    }

    // Step 2: For each group with 2+ occurrences, promote to shared global symbol.
    size_t dedupCounter = 0;
    size_t eliminated = 0;

    for (auto &[content, locs] : contentMap)
    {
        if (locs.size() < 2)
            continue;

        // Generate synthetic global name.
        std::string synthName = "__dedup_str_" + std::to_string(dedupCounter++);

        // First occurrence is canonical.
        const auto &canonical = locs[0];
        auto &canonObj = allObjects[canonical.objIdx];
        auto &canonSym = canonObj.symbols[canonical.symIdx];

        // Register canonical in globalSyms.
        GlobalSymEntry entry;
        entry.name = synthName;
        entry.binding = GlobalSymEntry::Global;
        entry.objIndex = canonical.objIdx;
        entry.secIndex = canonSym.sectionIndex;
        entry.offset = canonSym.offset;
        globalSyms[synthName] = entry;

        // Rename all occurrences (including canonical) to the synthetic name.
        for (const auto &loc : locs)
        {
            auto &sym = allObjects[loc.objIdx].symbols[loc.symIdx];
            sym.name = synthName;
            sym.binding = ObjSymbol::Global;
        }

        eliminated += locs.size() - 1;
    }

    return eliminated;
}

} // namespace viper::codegen::linker
