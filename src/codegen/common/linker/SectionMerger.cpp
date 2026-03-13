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

namespace viper::codegen::linker
{

bool mergeSections(const std::vector<ObjFile> &objects,
                   LinkPlatform platform,
                   LinkArch arch,
                   LinkLayout &layout,
                   std::ostream & /*err*/)
{
    // Determine page size.
    if (platform == LinkPlatform::macOS && arch == LinkArch::AArch64)
        layout.pageSize = 0x4000; // 16KB
    else
        layout.pageSize = 0x1000; // 4KB

    // Collect input sections by class.
    struct PendingChunk
    {
        size_t objIdx;
        size_t secIdx;
        SectionClass cls;
        uint32_t alignment;
    };

    std::vector<PendingChunk> pending;

    for (size_t oi = 0; oi < objects.size(); ++oi)
    {
        const auto &obj = objects[oi];
        for (size_t si = 1; si < obj.sections.size(); ++si)
        {
            const auto &sec = obj.sections[si];
            if (!sec.alloc)
                continue;
            if (sec.data.empty() && sec.relocs.empty())
                continue;

            SectionClass cls = classifySection(sec.name, sec.executable, sec.writable, sec.tls);
            pending.push_back({oi, si, cls, sec.alignment});
        }
    }

    // Sort chunks by alignment descending within each class to minimize padding.
    // Higher-alignment chunks placed first reduce wasted inter-chunk padding.
    std::stable_sort(pending.begin(),
                     pending.end(),
                     [](const PendingChunk &a, const PendingChunk &b)
                     {
                         if (a.cls != b.cls)
                             return false; // Preserve inter-class ordering.
                         return a.alignment > b.alignment;
                     });

    // Create output sections in order.
    auto addOutputSection =
        [&](SectionClass cls, const char *name, bool exec, bool write, bool tls) -> OutputSection &
    {
        layout.sections.push_back({});
        auto &out = layout.sections.back();
        out.name = name;
        out.executable = exec;
        out.writable = write;
        out.tls = tls;
        return out;
    };

    // Merge chunks into output sections.
    auto mergeClass = [&](SectionClass cls, OutputSection &out)
    {
        for (const auto &pc : pending)
        {
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
        if (p.cls == SectionClass::Text)
        {
            hasText = true;
            break;
        }
    if (hasText)
    {
        auto &text = addOutputSection(SectionClass::Text, ".text", true, false, false);
        mergeClass(SectionClass::Text, text);
    }

    // Rodata section.
    bool hasRodata = false;
    for (const auto &p : pending)
        if (p.cls == SectionClass::Rodata)
        {
            hasRodata = true;
            break;
        }
    if (hasRodata)
    {
        auto &rodata = addOutputSection(SectionClass::Rodata, ".rodata", false, false, false);
        mergeClass(SectionClass::Rodata, rodata);
    }

    // Data section.
    bool hasData = false;
    for (const auto &p : pending)
        if (p.cls == SectionClass::Data)
        {
            hasData = true;
            break;
        }
    if (hasData)
    {
        auto &data = addOutputSection(SectionClass::Data, ".data", false, true, false);
        mergeClass(SectionClass::Data, data);
    }

    // BSS section — placed before TLS to keep TLS sections contiguous.
    // Mach-O dyld requires __thread_vars and __thread_data to be adjacent
    // within the __DATA segment for correct TLS template discovery.
    bool hasBss = false;
    for (const auto &p : pending)
        if (p.cls == SectionClass::Bss)
        {
            hasBss = true;
            break;
        }
    if (hasBss)
    {
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
        for (const auto &p : pending)
        {
            if (p.cls != SectionClass::TlsData)
                continue;
            const auto &sec = objects[p.objIdx].sections[p.secIdx];
            if (sec.name.find("thread_vars") != std::string::npos)
                hasTlvDescriptors = true;
            else
                hasTlvTemplateData = true;
        }

        // TLV descriptors → .tdata (mapped to __thread_vars in MachOExeWriter).
        if (hasTlvDescriptors)
        {
            auto &tdata = addOutputSection(SectionClass::TlsData, ".tdata", false, true, true);
            for (const auto &pc : pending)
            {
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
        if (hasTlvTemplateData)
        {
            auto &tmpl =
                addOutputSection(SectionClass::TlsData, ".tdata_template", false, true, true);
            for (const auto &pc : pending)
            {
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
        if (p.cls == SectionClass::TlsBss)
        {
            hasTlsBss = true;
            break;
        }
    if (hasTlsBss)
    {
        auto &tbss = addOutputSection(SectionClass::TlsBss, ".tbss", false, true, true);
        mergeClass(SectionClass::TlsBss, tbss);
    }

    // ObjC metadata sections — each unique section name gets its own output section.
    // The ObjC runtime locates classes, selectors, protocols, etc. by section name
    // (e.g., __objc_classlist, __objc_selrefs). Merging them into generic .data
    // would make the ObjC runtime unable to discover registered classes.
    {
        std::map<std::string, std::vector<size_t>> objcGroups;
        for (size_t pi = 0; pi < pending.size(); ++pi)
        {
            if (pending[pi].cls != SectionClass::ObjC)
                continue;
            const auto &sec = objects[pending[pi].objIdx].sections[pending[pi].secIdx];
            objcGroups[sec.name].push_back(pi);
        }
        for (auto &[name, indices] : objcGroups)
        {
            const auto &firstPc = pending[indices[0]];
            const auto &firstSec = objects[firstPc.objIdx].sections[firstPc.secIdx];

            layout.sections.push_back({});
            auto &out = layout.sections.back();
            out.name = name;
            out.executable = firstSec.executable;
            out.writable = firstSec.writable;
            out.tls = false;

            for (size_t pi : indices)
            {
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

    // Assign virtual addresses.
    uint64_t baseAddr;
    switch (platform)
    {
        case LinkPlatform::macOS:
            baseAddr = 0x100000000ULL;
            break;
        case LinkPlatform::Windows:
            baseAddr = 0x140000000ULL;
            break;
        case LinkPlatform::Linux:
        default:
            baseAddr = 0x400000ULL;
            break;
    }

    uint64_t currentAddr = baseAddr;
    // Skip first page (null page / __PAGEZERO).
    currentAddr += layout.pageSize;

    // Permission class: only page-align when switching between segments
    // (executable → readonly → writable → TLS). Within a segment, sections
    // pack tightly with only their natural alignment respected.
    auto permClass = [](const OutputSection &s) -> int
    {
        if (s.executable)
            return 0;
        if (s.tls)
            return 3;
        if (s.writable)
            return 2;
        return 1; // readonly
    };

    // Sort sections by permission class before VA assignment.
    // ObjC metadata sections are appended after the standard sections above,
    // but their permission class (writable vs readonly) must be respected so
    // that all non-writable sections come before writable ones in the VA space.
    // Without this, non-writable ObjC sections (e.g., __objc_classname) get
    // VAs after writable sections, causing __TEXT/__DATA segment overlap in
    // the Mach-O executable (SIGKILL on macOS ARM64).
    std::stable_sort(layout.sections.begin(), layout.sections.end(),
                     [&permClass](const OutputSection &a, const OutputSection &b)
                     { return permClass(a) < permClass(b); });

    int prevClass = -1;
    for (auto &sec : layout.sections)
    {
        int cls = permClass(sec);
        if (cls != prevClass)
        {
            // Segment boundary — page-align.
            currentAddr = alignUp(currentAddr, layout.pageSize);
            prevClass = cls;
        }
        // Respect section's internal alignment.
        currentAddr = alignUp(currentAddr, sec.alignment);
        sec.virtualAddr = currentAddr;
        currentAddr += sec.data.size();
    }

    return true;
}

} // namespace viper::codegen::linker
