//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/ICF.cpp
// Purpose: Implements Identical Code Folding for the native linker.
//          Operates between deduplicateStrings() and dead-symbol cleanup in the
//          link pipeline.
// Key invariants:
//   - Only .text.* sections with one Global symbol at offset 0 are candidates
//   - Identity includes both bytes AND relocation signatures (type, target, addend)
//   - Address-taken functions are excluded from folding
//   - Non-canonical sections have both data AND relocs cleared
// Links: codegen/common/linker/ICF.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/ICF.hpp"

#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace viper::codegen::linker {

namespace {

/// FNV-1a hash over a byte range.
uint64_t fnv1a(const uint8_t *data, size_t len) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; ++i) {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    return h;
}

/// Mix an integer into an FNV-1a hash.
uint64_t fnv1aMix(uint64_t h, uint64_t val) {
    for (int i = 0; i < 8; ++i) {
        h ^= static_cast<uint8_t>(val >> (i * 8));
        h *= 1099511628211ULL;
    }
    return h;
}

/// Normalized relocation signature for identity comparison.
struct RelocSig {
    size_t offset;
    uint32_t type;
    std::string targetName;
    int64_t addend;

    bool operator==(const RelocSig &o) const {
        return offset == o.offset && type == o.type && targetName == o.targetName &&
               addend == o.addend;
    }

    bool operator<(const RelocSig &o) const {
        if (offset != o.offset)
            return offset < o.offset;
        if (type != o.type)
            return type < o.type;
        if (targetName != o.targetName)
            return targetName < o.targetName;
        return addend < o.addend;
    }
};

/// ICF candidate: a per-function .text section.
struct Candidate {
    size_t objIdx;
    size_t secIdx;
    std::string funcSymName; ///< The Global symbol this section defines.
    uint64_t hash;           ///< Combined hash of bytes + reloc sigs.
    std::vector<RelocSig> sigs;
};

/// Build sorted relocation signatures for a section.
std::vector<RelocSig> buildRelocSigs(const ObjFile &obj, const ObjSection &sec) {
    std::vector<RelocSig> sigs;
    sigs.reserve(sec.relocs.size());
    for (const auto &rel : sec.relocs) {
        RelocSig sig;
        sig.offset = rel.offset;
        sig.type = rel.type;
        sig.addend = rel.addend;
        // Resolve symbol index to name.
        if (rel.symIndex < obj.symbols.size())
            sig.targetName = obj.symbols[rel.symIndex].name;
        sigs.push_back(std::move(sig));
    }
    std::sort(sigs.begin(), sigs.end());
    return sigs;
}

/// Compute a combined hash of section bytes and relocation signatures.
uint64_t hashCandidate(const ObjSection &sec, const std::vector<RelocSig> &sigs) {
    uint64_t h = fnv1a(sec.data.data(), sec.data.size());
    for (const auto &sig : sigs) {
        h = fnv1aMix(h, sig.offset);
        h = fnv1aMix(h, sig.type);
        for (char c : sig.targetName) {
            h ^= static_cast<uint8_t>(c);
            h *= 1099511628211ULL;
        }
        h = fnv1aMix(h, static_cast<uint64_t>(sig.addend));
    }
    return h;
}

/// Check if two candidates are truly identical (not just hash-equal).
bool candidatesIdentical(const std::vector<ObjFile> &objects,
                         const Candidate &a,
                         const Candidate &b) {
    const auto &secA = objects[a.objIdx].sections[a.secIdx];
    const auto &secB = objects[b.objIdx].sections[b.secIdx];

    // Compare bytes.
    if (secA.data.size() != secB.data.size())
        return false;
    if (secA.data != secB.data)
        return false;

    // Compare relocation signatures.
    if (a.sigs.size() != b.sigs.size())
        return false;
    for (size_t i = 0; i < a.sigs.size(); ++i) {
        if (!(a.sigs[i] == b.sigs[i]))
            return false;
    }
    return true;
}

} // namespace

size_t foldIdenticalCode(std::vector<ObjFile> &allObjects,
                         std::unordered_map<std::string, GlobalSymEntry> &globalSyms) {
    // Step 1: Identify address-taken functions.
    // A function is address-taken if any Abs64/Abs32 data relocation references it.
    // Relocation type classification: we check the raw type values. On x86-64,
    // R_X86_64_64=1 (Abs64) and R_X86_64_32=10 (Abs32). On AArch64,
    // R_AARCH64_ABS64=257 and R_AARCH64_ABS32=258. We check all data/rodata
    // sections for relocations pointing to .text symbols.
    std::unordered_set<std::string> addressTaken;
    for (size_t oi = 0; oi < allObjects.size(); ++oi) {
        const auto &obj = allObjects[oi];
        for (size_t si = 1; si < obj.sections.size(); ++si) {
            const auto &sec = obj.sections[si];
            // Only scan data/rodata sections (non-executable, allocatable).
            if (sec.executable || sec.data.empty())
                continue;
            for (const auto &rel : sec.relocs) {
                if (rel.symIndex >= obj.symbols.size())
                    continue;
                const auto &targetSym = obj.symbols[rel.symIndex];
                if (targetSym.sectionIndex == 0)
                    continue;
                if (targetSym.sectionIndex >= obj.sections.size())
                    continue;
                // If the target is in an executable section, this is an address-taken ref.
                if (obj.sections[targetSym.sectionIndex].executable)
                    addressTaken.insert(targetSym.name);
            }
        }
    }

    // Step 2: Scan for ICF candidates — per-function .text.* sections.
    std::vector<Candidate> candidates;
    for (size_t oi = 0; oi < allObjects.size(); ++oi) {
        const auto &obj = allObjects[oi];
        for (size_t si = 1; si < obj.sections.size(); ++si) {
            const auto &sec = obj.sections[si];
            if (!sec.executable || sec.data.empty())
                continue;

            // Must be a per-function section (.text.funcname).
            // Generic ".text" sections with multiple functions are excluded.
            if (sec.name == ".text" || sec.name == "__text" || sec.name == "__TEXT,__text")
                continue;

            // Find the Global symbol at offset 0.
            std::string funcName;
            bool hasGlobalAtZero = false;
            for (size_t sym_i = 1; sym_i < obj.symbols.size(); ++sym_i) {
                const auto &sym = obj.symbols[sym_i];
                if (sym.sectionIndex == si && sym.binding == ObjSymbol::Global && sym.offset == 0) {
                    funcName = sym.name;
                    hasGlobalAtZero = true;
                    break;
                }
            }
            if (!hasGlobalAtZero)
                continue;

            // Skip address-taken functions.
            if (addressTaken.count(funcName))
                continue;

            // Build candidate.
            Candidate cand;
            cand.objIdx = oi;
            cand.secIdx = si;
            cand.funcSymName = funcName;
            cand.sigs = buildRelocSigs(obj, sec);
            cand.hash = hashCandidate(sec, cand.sigs);
            candidates.push_back(std::move(cand));
        }
    }

    if (candidates.empty())
        return 0;

    // Step 3: Group candidates by hash.
    std::unordered_map<uint64_t, std::vector<size_t>> hashGroups;
    for (size_t i = 0; i < candidates.size(); ++i)
        hashGroups[candidates[i].hash].push_back(i);

    // Step 4: Within each hash group, find identical clusters and fold.
    size_t folded = 0;
    for (auto &[hash, indices] : hashGroups) {
        if (indices.size() < 2)
            continue;

        // Track which candidates have already been folded.
        std::vector<bool> processed(indices.size(), false);

        for (size_t i = 0; i < indices.size(); ++i) {
            if (processed[i])
                continue;

            // This is the canonical copy. Find all identical matches.
            const auto &canonical = candidates[indices[i]];
            std::vector<size_t> cluster; // indices into `indices`
            cluster.push_back(i);

            for (size_t j = i + 1; j < indices.size(); ++j) {
                if (processed[j])
                    continue;
                if (candidatesIdentical(allObjects, canonical, candidates[indices[j]])) {
                    cluster.push_back(j);
                    processed[j] = true;
                }
            }

            if (cluster.size() < 2)
                continue;

            // Fold: redirect all non-canonical symbols to canonical's section.
            const auto &canonCand = candidates[indices[cluster[0]]];
            for (size_t k = 1; k < cluster.size(); ++k) {
                const auto &foldCand = candidates[indices[cluster[k]]];

                // Redirect globalSyms entry for the folded function.
                auto it = globalSyms.find(foldCand.funcSymName);
                if (it != globalSyms.end()) {
                    it->second.objIndex = canonCand.objIdx;
                    it->second.secIndex = static_cast<uint32_t>(canonCand.secIdx);
                    it->second.offset = 0;
                }

                // Clear both data AND relocs on the folded section.
                auto &foldSec = allObjects[foldCand.objIdx].sections[foldCand.secIdx];
                foldSec.data.clear();
                foldSec.relocs.clear();

                ++folded;
            }
        }
    }

    return folded;
}

} // namespace viper::codegen::linker
