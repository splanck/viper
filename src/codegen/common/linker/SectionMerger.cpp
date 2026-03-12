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

#include <algorithm>
#include <map>

namespace viper::codegen::linker
{

static size_t alignUp(size_t val, size_t align)
{
    if (align == 0)
        return val;
    return (val + align - 1) & ~(align - 1);
}

bool mergeSections(const std::vector<ObjFile> &objects, LinkPlatform platform, LinkArch arch,
                   LinkLayout &layout, std::ostream & /*err*/)
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

    // Create output sections in order.
    auto addOutputSection = [&](SectionClass cls, const char *name, bool exec, bool write,
                                bool tls) -> OutputSection &
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

    // TLS data (TLV descriptors on Mach-O, .tdata on ELF).
    bool hasTlsData = false;
    for (const auto &p : pending)
        if (p.cls == SectionClass::TlsData)
        {
            hasTlsData = true;
            break;
        }
    if (hasTlsData)
    {
        auto &tdata = addOutputSection(SectionClass::TlsData, ".tdata", false, true, true);
        mergeClass(SectionClass::TlsData, tdata);
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

    for (auto &sec : layout.sections)
    {
        // Page-align each section.
        currentAddr = alignUp(currentAddr, layout.pageSize);
        // Also respect section's internal alignment.
        currentAddr = alignUp(currentAddr, sec.alignment);
        sec.virtualAddr = currentAddr;
        currentAddr += sec.data.size();
    }

    return true;
}

} // namespace viper::codegen::linker
