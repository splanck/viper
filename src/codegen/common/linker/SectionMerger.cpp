//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/SectionMerger.cpp
// Purpose: Merges input sections by class and assigns virtual addresses.
// Key invariants:
//   - Order: text → rodata → data → tls_data → bss → tls_bss
//   - Each segment starts on a page boundary
//   - Chunks within a section respect their alignment requirements
// Links: codegen/common/linker/SectionMerger.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/SectionMerger.hpp"

#include "codegen/common/linker/AlignUtil.hpp"

#include <algorithm>
#include <map>

namespace viper::codegen::linker {

namespace {

bool isWindowsCrtSubsection(const std::string &name) {
    return name.rfind(".CRT$", 0) == 0;
}

bool isWindowsTlsSubsection(const std::string &name) {
    return name.rfind(".tls", 0) == 0;
}

uint64_t imageBaseForPlatform(LinkPlatform platform) {
    switch (platform) {
        case LinkPlatform::macOS:
            return 0x100000000ULL;
        case LinkPlatform::Windows:
            return 0x140000000ULL;
        case LinkPlatform::Linux:
        default:
            return 0x400000ULL;
    }
}

int permClass(const OutputSection &s) {
    if (!s.alloc)
        return 4; // Non-alloc sections (debug) sort last.
    if (s.executable)
        return 0;
    if (s.tls)
        return 3;
    if (s.writable)
        return 2;
    return 1; // readonly
}

} // namespace

void assignSectionVirtualAddresses(LinkLayout &layout, LinkPlatform platform) {
    uint64_t currentAddr = imageBaseForPlatform(platform) + layout.pageSize;
    int prevClass = -1;
    for (auto &sec : layout.sections) {
        if (!sec.alloc)
            continue;
        const int cls = permClass(sec);
        if (platform == LinkPlatform::Windows) {
            currentAddr = alignUp(currentAddr, layout.pageSize);
            prevClass = cls;
        } else if (cls != prevClass) {
            currentAddr = alignUp(currentAddr, layout.pageSize);
            prevClass = cls;
        }
        currentAddr = alignUp(currentAddr, sec.alignment);
        sec.virtualAddr = currentAddr;
        currentAddr += sec.data.size();
    }
}

bool mergeSections(const std::vector<ObjFile> &objects,
                   LinkPlatform platform,
                   LinkArch arch,
                   LinkLayout &layout,
                   std::ostream & /*err*/) {
    // Determine page size.
    if (platform == LinkPlatform::macOS && arch == LinkArch::AArch64)
        layout.pageSize = 0x4000; // 16KB
    else
        layout.pageSize = 0x1000; // 4KB

    // Collect input sections by class.
    struct PendingChunk {
        size_t objIdx;
        size_t secIdx;
        SectionClass cls;
        std::string name;
        uint32_t alignment;
    };

    std::vector<PendingChunk> pending;

    for (size_t oi = 0; oi < objects.size(); ++oi) {
        const auto &obj = objects[oi];
        for (size_t si = 1; si < obj.sections.size(); ++si) {
            const auto &sec = obj.sections[si];
            if (!sec.alloc)
                continue;
            if (sec.data.empty() && sec.relocs.empty())
                continue;

            SectionClass cls = classifySection(sec.name, sec.executable, sec.writable, sec.tls);
            pending.push_back({oi, si, cls, sec.name, sec.alignment});
        }
    }

    // Sort chunks within each class.
    // Default: higher-alignment chunks first to minimize inter-chunk padding.
    // Windows exception: COFF subsection families such as .CRT$X* and .tls$*
    // must preserve lexicographic subsection order so the CRT startup ranges
    // and TLS template remain valid after merging.
    std::stable_sort(
        pending.begin(), pending.end(), [platform](const PendingChunk &a, const PendingChunk &b) {
            if (a.cls != b.cls)
                return false; // Preserve inter-class ordering.

            if (platform == LinkPlatform::Windows) {
                const bool aCrt = isWindowsCrtSubsection(a.name);
                const bool bCrt = isWindowsCrtSubsection(b.name);
                if (aCrt != bCrt)
                    return aCrt;
                if (aCrt && bCrt) {
                    if (a.name != b.name)
                        return a.name < b.name;
                    return a.alignment > b.alignment;
                }

                const bool aTls = isWindowsTlsSubsection(a.name);
                const bool bTls = isWindowsTlsSubsection(b.name);
                if (aTls && bTls) {
                    if (a.name != b.name)
                        return a.name < b.name;
                    return a.alignment > b.alignment;
                }
            }

            return a.alignment > b.alignment;
        });

    // Create output sections in order.
    auto addOutputSection = [&](SectionClass cls, const char *name, bool exec, bool write, bool tls)
        -> OutputSection & {
        layout.sections.push_back({});
        auto &out = layout.sections.back();
        out.name = name;
        out.executable = exec;
        out.writable = write;
        out.tls = tls;
        return out;
    };

    // Merge chunks into output sections.
    auto mergeClass = [&](SectionClass cls, OutputSection &out) {
        for (const auto &pc : pending) {
            if (pc.cls != cls)
                continue;

            const auto &sec = objects[pc.objIdx].sections[pc.secIdx];
            // Align within output.
            size_t align = std::max(pc.alignment, 1u);
            if (align > out.alignment)
                out.alignment = align;

            size_t padded = alignUp(out.data.size(), align);
            if (padded > out.data.size())
                out.data.resize(padded, 0);

            InputChunk chunk;
            chunk.inputObjIndex = pc.objIdx;
            chunk.inputSecIndex = pc.secIdx;
            chunk.outputOffset = out.data.size();
            chunk.size = sec.data.size();
            out.chunks.push_back(chunk);

            out.data.insert(out.data.end(), sec.data.begin(), sec.data.end());
        }
    };

    // Text section.
    bool hasText = false;
    for (const auto &p : pending)
        if (p.cls == SectionClass::Text) {
            hasText = true;
            break;
        }
    if (hasText) {
        auto &text = addOutputSection(SectionClass::Text, ".text", true, false, false);
        mergeClass(SectionClass::Text, text);
    }

    // Rodata section.
    bool hasRodata = false;
    for (const auto &p : pending)
        if (p.cls == SectionClass::Rodata) {
            hasRodata = true;
            break;
        }
    if (hasRodata) {
        auto &rodata = addOutputSection(SectionClass::Rodata, ".rodata", false, false, false);
        mergeClass(SectionClass::Rodata, rodata);
    }

    // Data section.
    bool hasData = false;
    for (const auto &p : pending)
        if (p.cls == SectionClass::Data) {
            hasData = true;
            break;
        }
    if (hasData) {
        auto &data = addOutputSection(SectionClass::Data, ".data", false, true, false);
        mergeClass(SectionClass::Data, data);
    }

    // BSS section — placed before TLS to keep TLS sections contiguous.
    // Mach-O dyld requires __thread_vars and __thread_data to be adjacent
    // within the __DATA segment for correct TLS template discovery.
    bool hasBss = false;
    for (const auto &p : pending)
        if (p.cls == SectionClass::Bss) {
            hasBss = true;
            break;
        }
    if (hasBss) {
        auto &bss = addOutputSection(SectionClass::Bss, ".bss", false, true, false);
        mergeClass(SectionClass::Bss, bss);
    }

    // TLS data — on Mach-O, TLV descriptors (__thread_vars) and template data
    // (__thread_data) must be in separate output sections. dyld validates that
    // __thread_vars size is a multiple of 24 bytes (one descriptor = {thunk(8),
    // key(8), offset(8)}). Mixing template data into the same section produces
    // an invalid size and a dyld abort.
    {
        bool hasTlvDescriptors = false;
        bool hasTlvTemplateData = false;
        for (const auto &p : pending) {
            if (p.cls != SectionClass::TlsData)
                continue;
            const auto &sec = objects[p.objIdx].sections[p.secIdx];
            if (sec.name.find("thread_vars") != std::string::npos)
                hasTlvDescriptors = true;
            else
                hasTlvTemplateData = true;
        }

        // TLV descriptors → .tdata (mapped to __thread_vars in MachOExeWriter).
        if (hasTlvDescriptors) {
            auto &tdata = addOutputSection(SectionClass::TlsData, ".tdata", false, true, true);
            for (const auto &pc : pending) {
                if (pc.cls != SectionClass::TlsData)
                    continue;
                const auto &sec = objects[pc.objIdx].sections[pc.secIdx];
                if (sec.name.find("thread_vars") == std::string::npos)
                    continue;

                size_t align = std::max(pc.alignment, 1u);
                if (align > tdata.alignment)
                    tdata.alignment = align;
                size_t padded = alignUp(tdata.data.size(), align);
                if (padded > tdata.data.size())
                    tdata.data.resize(padded, 0);

                InputChunk chunk;
                chunk.inputObjIndex = pc.objIdx;
                chunk.inputSecIndex = pc.secIdx;
                chunk.outputOffset = tdata.data.size();
                chunk.size = sec.data.size();
                tdata.chunks.push_back(chunk);
                tdata.data.insert(tdata.data.end(), sec.data.begin(), sec.data.end());
            }
        }

        // TLS template data → .tdata_template (mapped to __thread_data in
        // MachOExeWriter). On ELF/PE, all TLS data is template data (no TLV
        // descriptors), so this path handles those platforms correctly.
        if (hasTlvTemplateData) {
            auto &tmpl =
                addOutputSection(SectionClass::TlsData, ".tdata_template", false, true, true);
            for (const auto &pc : pending) {
                if (pc.cls != SectionClass::TlsData)
                    continue;
                const auto &sec = objects[pc.objIdx].sections[pc.secIdx];
                if (sec.name.find("thread_vars") != std::string::npos)
                    continue;

                size_t align = std::max(pc.alignment, 1u);
                if (align > tmpl.alignment)
                    tmpl.alignment = align;
                size_t padded = alignUp(tmpl.data.size(), align);
                if (padded > tmpl.data.size())
                    tmpl.data.resize(padded, 0);

                InputChunk chunk;
                chunk.inputObjIndex = pc.objIdx;
                chunk.inputSecIndex = pc.secIdx;
                chunk.outputOffset = tmpl.data.size();
                chunk.size = sec.data.size();
                tmpl.chunks.push_back(chunk);
                tmpl.data.insert(tmpl.data.end(), sec.data.begin(), sec.data.end());
            }
        }
    }

    // TLS BSS (zero-initialized thread-local data).
    bool hasTlsBss = false;
    for (const auto &p : pending)
        if (p.cls == SectionClass::TlsBss) {
            hasTlsBss = true;
            break;
        }
    if (hasTlsBss) {
        auto &tbss = addOutputSection(SectionClass::TlsBss, ".tbss", false, true, true);
        mergeClass(SectionClass::TlsBss, tbss);
    }

    // Preserved-name sections — each unique section name gets its own output
    // section. This keeps ObjC metadata discoverable by name and preserves
    // Windows unwind tables (.pdata/.xdata) for the PE writer.
    {
        std::map<std::string, std::vector<size_t>> objcGroups;
        for (size_t pi = 0; pi < pending.size(); ++pi) {
            if (pending[pi].cls != SectionClass::ObjC)
                continue;
            const auto &sec = objects[pending[pi].objIdx].sections[pending[pi].secIdx];
            objcGroups[sec.name].push_back(pi);
        }
        for (auto &[name, indices] : objcGroups) {
            const auto &firstPc = pending[indices[0]];
            const auto &firstSec = objects[firstPc.objIdx].sections[firstPc.secIdx];

            layout.sections.push_back({});
            auto &out = layout.sections.back();
            out.name = name;
            out.executable = firstSec.executable;
            out.writable = firstSec.writable;
            out.tls = false;

            for (size_t pi : indices) {
                const auto &pc = pending[pi];
                const auto &sec = objects[pc.objIdx].sections[pc.secIdx];
                size_t align = std::max(pc.alignment, 1u);
                if (align > out.alignment)
                    out.alignment = align;
                size_t padded = alignUp(out.data.size(), align);
                if (padded > out.data.size())
                    out.data.resize(padded, 0);

                InputChunk chunk;
                chunk.inputObjIndex = pc.objIdx;
                chunk.inputSecIndex = pc.secIdx;
                chunk.outputOffset = out.data.size();
                chunk.size = sec.data.size();
                out.chunks.push_back(chunk);
                out.data.insert(out.data.end(), sec.data.begin(), sec.data.end());
            }
        }
    }

    // Collect non-alloc sections (e.g., .debug_line) into separate output sections.
    // These have no virtual address and are not mapped into the process address space.
    // Debuggers read them directly from the file.
    {
        std::map<std::string, std::vector<std::pair<size_t, size_t>>> debugGroups;
        for (size_t oi = 0; oi < objects.size(); ++oi) {
            const auto &obj = objects[oi];
            for (size_t si = 1; si < obj.sections.size(); ++si) {
                const auto &sec = obj.sections[si];
                if (sec.alloc)
                    continue;
                if (sec.data.empty())
                    continue;
                debugGroups[sec.name].push_back({oi, si});
            }
        }
        for (auto &[name, pairs] : debugGroups) {
            layout.sections.push_back({});
            auto &out = layout.sections.back();
            out.name = name;
            out.alloc = false;

            for (auto [oi, si] : pairs) {
                const auto &sec = objects[oi].sections[si];
                size_t align = std::max(sec.alignment, 1u);
                if (align > out.alignment)
                    out.alignment = align;
                size_t padded = alignUp(out.data.size(), align);
                if (padded > out.data.size())
                    out.data.resize(padded, 0);

                InputChunk chunk;
                chunk.inputObjIndex = oi;
                chunk.inputSecIndex = si;
                chunk.outputOffset = out.data.size();
                chunk.size = sec.data.size();
                out.chunks.push_back(chunk);
                out.data.insert(out.data.end(), sec.data.begin(), sec.data.end());
            }
        }
    }

    // Assign virtual addresses.
    // Sort sections by permission class before VA assignment.
    // ObjC metadata sections are appended after the standard sections above,
    // but their permission class (writable vs readonly) must be respected so
    // that all non-writable sections come before writable ones in the VA space.
    // Without this, non-writable ObjC sections (e.g., __objc_classname) get
    // VAs after writable sections, causing __TEXT/__DATA segment overlap in
    // the Mach-O executable (SIGKILL on macOS ARM64).
    std::stable_sort(layout.sections.begin(),
                     layout.sections.end(),
                     [](const OutputSection &a, const OutputSection &b) {
                         return permClass(a) < permClass(b);
                     });

    assignSectionVirtualAddresses(layout, platform);

    return true;
}

} // namespace viper::codegen::linker
