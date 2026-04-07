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

namespace viper::codegen::linker {

size_t deduplicateStrings(std::vector<ObjFile> &allObjects,
                          std::unordered_map<std::string, GlobalSymEntry> &globalSyms) {
    struct SymLoc {
        size_t objIdx;
        size_t symIdx;
        uint32_t secIdx;
        size_t strLen;
        std::string content;
        bool keepBytes = true;
    };

    auto makeSectionKey = [](size_t objIdx, uint32_t secIdx) -> uint64_t {
        return (static_cast<uint64_t>(objIdx) << 32) | static_cast<uint64_t>(secIdx);
    };

    std::unordered_map<std::string, std::vector<size_t>> contentMap;
    std::unordered_map<uint64_t, std::vector<size_t>> sectionMap;
    std::vector<SymLoc> locations;

    for (size_t oi = 0; oi < allObjects.size(); ++oi) {
        auto &obj = allObjects[oi];
        for (size_t si = 1; si < obj.symbols.size(); ++si) {
            const auto &sym = obj.symbols[si];

            // Only LOCAL symbols with a valid section reference.
            if (sym.binding != ObjSymbol::Local)
                continue;
            if (sym.sectionIndex == 0 || sym.sectionIndex >= obj.sections.size())
                continue;

            const auto &sec = obj.sections[sym.sectionIndex];

            // Only sections that are known to contain NUL-terminated C strings.
            // Scanning generic rodata/const sections (e.g., Mach-O __const) is
            // unsafe: binary data like integer arrays may start with a byte
            // followed by NUL, which would be misidentified as a short string
            // and merged with an unrelated real string, corrupting references.
            if (!sec.isCStringSection)
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
            const size_t locIdx = locations.size();
            locations.push_back({oi, si, sym.sectionIndex, strLen, content, true});
            contentMap[content].push_back(locIdx);
            sectionMap[makeSectionKey(oi, sym.sectionIndex)].push_back(locIdx);
        }
    }

    // Step 2: For each group with 2+ occurrences, promote to shared global symbol.
    size_t dedupCounter = 0;
    size_t eliminated = 0;

    for (auto &[content, locs] : contentMap) {
        if (locs.size() < 2)
            continue;

        // Generate synthetic global name.
        std::string synthName = "__dedup_str_" + std::to_string(dedupCounter++);

        // First occurrence is canonical.
        const auto &canonical = locations[locs[0]];
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
        for (size_t i = 0; i < locs.size(); ++i) {
            auto &loc = locations[locs[i]];
            auto &sym = allObjects[loc.objIdx].symbols[loc.symIdx];
            sym.name = synthName;
            sym.binding = ObjSymbol::Global;
            if (i != 0) {
                loc.keepBytes = false;
                sym.sectionIndex = 0;
                sym.offset = 0;
            }
        }

        eliminated += locs.size() - 1;
    }

    // Step 3: Compact cstring sections when every byte belongs to a symbolized
    // string. This turns dedup from symbol aliasing into actual size reduction.
    for (auto &[sectionKey, locIndices] : sectionMap) {
        if (locIndices.empty())
            continue;

        const SymLoc &firstLoc = locations[locIndices.front()];
        auto &obj = allObjects[firstLoc.objIdx];
        auto &sec = obj.sections[firstLoc.secIdx];
        if (!sec.relocs.empty())
            continue;

        std::vector<size_t> sortedByOffset = locIndices;
        std::sort(sortedByOffset.begin(), sortedByOffset.end(), [&](size_t aIdx, size_t bIdx) {
            const auto &symA = obj.symbols[locations[aIdx].symIdx];
            const auto &symB = obj.symbols[locations[bIdx].symIdx];
            return symA.offset < symB.offset;
        });

        size_t cursor = 0;
        bool fullyCovered = true;
        for (size_t locIdx : sortedByOffset) {
            const auto &loc = locations[locIdx];
            const auto &sym = obj.symbols[loc.symIdx];
            if (sym.offset != cursor) {
                fullyCovered = false;
                break;
            }
            cursor += loc.strLen;
        }
        if (!fullyCovered || cursor != sec.data.size())
            continue;

        std::vector<uint8_t> newData;
        newData.reserve(sec.data.size());
        for (size_t locIdx : sortedByOffset) {
            auto &loc = locations[locIdx];
            auto &sym = obj.symbols[loc.symIdx];
            if (!loc.keepBytes)
                continue;

            const size_t newOffset = newData.size();
            newData.insert(newData.end(), loc.content.begin(), loc.content.end());
            sym.sectionIndex = firstLoc.secIdx;
            sym.offset = newOffset;

            auto git = globalSyms.find(sym.name);
            if (git != globalSyms.end() && git->second.objIndex == loc.objIdx &&
                git->second.secIndex == firstLoc.secIdx) {
                git->second.offset = newOffset;
            }
        }
        sec.data = std::move(newData);
    }

    return eliminated;
}

} // namespace viper::codegen::linker
