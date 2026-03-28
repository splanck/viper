//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/MachOReader.cpp
// Purpose: Mach-O 64-bit relocatable object file reader.
// Key invariants:
//   - Magic 0xFEEDFACF (little-endian 64-bit)
//   - Symbol names are stripped of leading '_' (Mach-O convention)
//   - Relocations have NO explicit addend — extracted from instruction bytes
//   - Sections parsed from LC_SEGMENT_64, symbols from LC_SYMTAB
// Links: codegen/common/linker/ObjFileReader.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/ObjFileReader.hpp"

#include <cstring>

namespace viper::codegen::linker {

namespace macho {
static constexpr uint32_t MH_MAGIC_64 = 0xFEEDFACF;
static constexpr uint32_t MH_OBJECT = 1;

static constexpr uint32_t CPU_TYPE_X86_64 = 0x01000007;
static constexpr uint32_t CPU_TYPE_ARM64 = 0x0100000C;

static constexpr uint32_t LC_SEGMENT_64 = 0x19;
static constexpr uint32_t LC_SYMTAB = 0x02;

// Section type constants (low 8 bits of flags).
static constexpr uint32_t S_REGULAR = 0x00;
static constexpr uint32_t S_ZEROFILL = 0x01;
static constexpr uint32_t S_NON_LAZY_SYMBOL_POINTERS = 0x06;
static constexpr uint32_t S_LAZY_SYMBOL_POINTERS = 0x07;
static constexpr uint32_t S_THREAD_LOCAL_REGULAR = 0x11;
static constexpr uint32_t S_THREAD_LOCAL_ZEROFILL = 0x12;
static constexpr uint32_t S_THREAD_LOCAL_VARIABLES = 0x13;

// Section attribute flags (high bits).
static constexpr uint32_t S_ATTR_PURE_INSTRUCTIONS = 0x80000000;
static constexpr uint32_t S_ATTR_DEBUG = 0x02000000;

struct mach_header_64 {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
};

struct load_command {
    uint32_t cmd;
    uint32_t cmdsize;
};

struct segment_command_64 {
    uint32_t cmd;
    uint32_t cmdsize;
    char segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct section_64 {
    char sectname[16];
    char segname[16];
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
};

struct nlist_64 {
    uint32_t n_strx;
    uint8_t n_type;
    uint8_t n_sect;
    uint16_t n_desc;
    uint64_t n_value;
};

struct relocation_info {
    int32_t r_address;
    uint32_t r_info; // symbolnum:24 | pcrel:1 | length:2 | extern:1 | type:4
};

} // namespace macho

template <typename T> static const T *readAt(const uint8_t *data, size_t size, size_t offset) {
    if (offset + sizeof(T) > size)
        return nullptr;
    return reinterpret_cast<const T *>(data + offset);
}

static std::string trimNul(const char *s, size_t maxLen) {
    size_t len = 0;
    while (len < maxLen && s[len] != '\0')
        ++len;
    return std::string(s, len);
}

/// Read little-endian 32-bit from raw bytes.
static uint32_t readLE32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

/// Extract relocation addend from instruction bytes (Mach-O has no explicit addend).
static int64_t extractMachOAddend(const uint8_t *sectionData,
                                  size_t sectionSize,
                                  size_t offset,
                                  uint32_t relocType,
                                  bool isArm64) {
    if (isArm64) {
        // ARM64 relocation addends are encoded in instruction fields.
        // For BRANCH26: extract imm26 field (bits [25:0]) × 4.
        // For PAGE21/PAGEOFF12: typically 0 for compiler-generated code.
        // We'll extract what we can; most are 0 for our use case.
        return 0;
    } else {
        // x86_64: addend is typically in the 4 bytes at the relocation site.
        if (offset + 4 <= sectionSize) {
            int32_t val;
            std::memcpy(&val, sectionData + offset, 4);
            return val;
        }
        return 0;
    }
}

bool readMachOObj(
    const uint8_t *data, size_t size, const std::string &name, ObjFile &obj, std::ostream &err) {
    const auto *hdr = readAt<macho::mach_header_64>(data, size, 0);
    if (!hdr || hdr->magic != macho::MH_MAGIC_64) {
        err << "error: " << name << ": invalid Mach-O magic\n";
        return false;
    }

    obj.format = ObjFileFormat::MachO;
    obj.is64bit = true;
    obj.isLittleEndian = true;
    obj.name = name;

    const bool isArm64 = (hdr->cputype == macho::CPU_TYPE_ARM64);
    obj.machine = isArm64 ? 183 : 62; // Map to ELF machine constants for uniformity.

    // Parse load commands.
    size_t lcOff = sizeof(macho::mach_header_64);
    const macho::load_command *symtabLc = nullptr;
    size_t symtabLcOff = 0;

    // First pass: collect sections and find LC_SYMTAB.
    obj.sections.resize(1); // Null section at index 0.
    obj.sections[0].name = "";
    // Track section base addresses for converting n_value to section-relative offsets.
    // Mach-O n_value is an absolute address within the .o; we need (n_value - secAddr).
    std::vector<uint64_t> secAddrs;
    secAddrs.push_back(0); // Null section.
    // Map Mach-O 1-based section index → ObjFile section index.
    // Skipped sections (debug, etc.) map to 0 (unmapped).
    std::vector<uint32_t> machoSecMap;
    machoSecMap.push_back(0); // Null section (index 0).
    uint32_t machoSecIdx = 1;

    size_t tmpOff = lcOff;
    for (uint32_t c = 0; c < hdr->ncmds; ++c) {
        const auto *lc = readAt<macho::load_command>(data, size, tmpOff);
        if (!lc)
            break;

        if (lc->cmd == macho::LC_SEGMENT_64) {
            const auto *seg = readAt<macho::segment_command_64>(data, size, tmpOff);
            if (!seg)
                break;

            size_t secOff = tmpOff + sizeof(macho::segment_command_64);
            for (uint32_t s = 0; s < seg->nsects; ++s) {
                const auto *sec = readAt<macho::section_64>(data, size, secOff);
                if (!sec)
                    break;

                std::string segName = trimNul(sec->segname, 16);
                std::string secName = trimNul(sec->sectname, 16);

                // Skip debug and metadata sections that don't belong in the output.
                if (segName == "__DWARF" || (sec->flags & macho::S_ATTR_DEBUG) != 0 ||
                    secName == "__compact_unwind" || secName == "__eh_frame") {
                    machoSecMap.push_back(0); // Unmapped.
                    ++machoSecIdx;
                    secOff += sizeof(macho::section_64);
                    continue;
                }

                ObjSection os;
                os.name = segName + "," + secName;
                os.alignment = (sec->align < 30) ? (1u << sec->align) : 1;
                os.executable = (sec->flags & macho::S_ATTR_PURE_INSTRUCTIONS) != 0;

                // Infer writability from Mach-O segment name and section type.
                // .o files don't have segment permission bits, but each section header
                // carries the intended segment name (__TEXT vs __DATA).
                const uint32_t secType = sec->flags & 0xFF;
                os.writable = (segName == "__DATA") || (secType == macho::S_ZEROFILL) ||
                              (secType == macho::S_THREAD_LOCAL_REGULAR) ||
                              (secType == macho::S_THREAD_LOCAL_ZEROFILL) ||
                              (secType == macho::S_THREAD_LOCAL_VARIABLES) ||
                              (secType == macho::S_NON_LAZY_SYMBOL_POINTERS) ||
                              (secType == macho::S_LAZY_SYMBOL_POINTERS);

                os.tls = (secType == macho::S_THREAD_LOCAL_REGULAR ||
                          secType == macho::S_THREAD_LOCAL_ZEROFILL ||
                          secType == macho::S_THREAD_LOCAL_VARIABLES);

                // S_CSTRING_LITERALS (0x02) sections contain only NUL-terminated
                // C strings and are safe for cross-module string deduplication.
                os.isCStringSection = (secType == 0x02);

                // Zerofill sections (S_ZEROFILL, S_THREAD_LOCAL_ZEROFILL) have
                // offset=0 in .o files — they carry no file data. Reading from
                // offset 0 would incorrectly copy the Mach-O header bytes.
                const bool isZerofill =
                    (secType == macho::S_ZEROFILL) || (secType == macho::S_THREAD_LOCAL_ZEROFILL);

                if (isZerofill && sec->size > 0) {
                    os.data.resize(static_cast<size_t>(sec->size), 0);
                } else if (sec->size > 0 && sec->offset + sec->size <= size) {
                    os.data.assign(data + sec->offset, data + sec->offset + sec->size);
                } else if (sec->size > 0) {
                    os.data.resize(static_cast<size_t>(sec->size), 0);
                }

                // Parse section relocations.
                // ARM64_RELOC_ADDEND (type 10) is a paired relocation:
                // its r_symbolnum carries the addend for the NEXT relocation.
                int64_t pendingAddend = 0;
                bool hasPendingAddend = false;
                for (uint32_t r = 0; r < sec->nreloc; ++r) {
                    const auto *ri = readAt<macho::relocation_info>(
                        data, size, sec->reloff + r * sizeof(macho::relocation_info));
                    if (!ri)
                        break;

                    // Skip scattered relocations (r_address bit 31 set).
                    if (ri->r_address & 0x80000000)
                        continue;

                    const uint32_t info = ri->r_info;
                    const uint32_t relType = (info >> 28) & 0xF;

                    // ARM64_RELOC_ADDEND (type 10): stores addend for next relocation.
                    if (isArm64 && relType == 10) {
                        pendingAddend = static_cast<int64_t>(info & 0x00FFFFFF);
                        hasPendingAddend = true;
                        continue;
                    }

                    ObjReloc rel;
                    rel.offset = static_cast<size_t>(ri->r_address);
                    rel.symIndex = info & 0x00FFFFFF; // symbolnum (24 bits).
                    rel.type = relType;

                    if (hasPendingAddend) {
                        rel.addend = pendingAddend;
                        hasPendingAddend = false;
                    } else {
                        // Extract addend from instruction bytes.
                        rel.addend = extractMachOAddend(
                            os.data.data(), os.data.size(), rel.offset, rel.type, isArm64);
                    }

                    os.relocs.push_back(rel);
                }

                machoSecMap.push_back(static_cast<uint32_t>(obj.sections.size()));
                secAddrs.push_back(sec->addr);
                obj.sections.push_back(std::move(os));
                ++machoSecIdx;
                secOff += sizeof(macho::section_64);
            }
        } else if (lc->cmd == macho::LC_SYMTAB) {
            symtabLcOff = tmpOff;
            symtabLc = lc;
        }

        tmpOff += lc->cmdsize;
    }

    // Validate section count.
    if (obj.sections.size() > kMaxObjSections) {
        err << "error: " << name << ": section count " << obj.sections.size() << " exceeds limit\n";
        return false;
    }

    // Parse symbols from LC_SYMTAB.
    if (symtabLcOff != 0) {
        // LC_SYMTAB layout: cmd(4) + cmdsize(4) + symoff(4) + nsyms(4) + stroff(4) + strsize(4)
        if (symtabLcOff + 24 > size)
            return true;

        const uint32_t symoff = readLE32(data + symtabLcOff + 8);
        const uint32_t nsyms = readLE32(data + symtabLcOff + 12);
        const uint32_t stroff = readLE32(data + symtabLcOff + 16);
        const uint32_t strsize = readLE32(data + symtabLcOff + 20);

        if (nsyms > kMaxObjSymbols) {
            err << "error: " << name << ": symbol count " << nsyms << " exceeds limit\n";
            return false;
        }

        obj.symbols.resize(1); // Null symbol at index 0.
        obj.symbols[0] = ObjSymbol{};

        // Build symbol index map: Mach-O nlist index → ObjFile symbol index.
        // Mach-O relocations reference nlist indices directly.
        std::vector<uint32_t> symMap(nsyms, 0);

        for (uint32_t i = 0; i < nsyms; ++i) {
            const auto *nl =
                readAt<macho::nlist_64>(data, size, symoff + i * sizeof(macho::nlist_64));
            if (!nl)
                break;

            ObjSymbol os;

            // Read name from string table.
            if (nl->n_strx < strsize) {
                os.name = reinterpret_cast<const char *>(data + stroff + nl->n_strx);
                // Strip leading underscore (Mach-O convention).
                if (!os.name.empty() && os.name[0] == '_')
                    os.name.erase(0, 1);
            }

            // Parse binding from n_type.
            const uint8_t nType = nl->n_type & 0x0E; // N_TYPE mask.
            const bool isExtern = (nl->n_type & 0x01) != 0;

            if (nType == 0x00) // N_UNDF
            {
                os.binding = ObjSymbol::Undefined;
                // Check for weak imports.
                if (nl->n_desc & 0x0040) // N_WEAK_REF
                    os.binding = ObjSymbol::Weak;
            } else if (nType == 0x0E) // N_SECT
            {
                if (isExtern) {
                    if (nl->n_desc & 0x0080) // N_WEAK_DEF
                        os.binding = ObjSymbol::Weak;
                    else
                        os.binding = ObjSymbol::Global;
                } else
                    os.binding = ObjSymbol::Local;
            } else {
                os.binding = ObjSymbol::Local;
            }

            // Map Mach-O 1-based section number to our section index.
            // machoSecMap translates Mach-O indices (which include skipped debug
            // sections) to ObjFile section indices.
            if (nl->n_sect > 0 && nl->n_sect < machoSecMap.size())
                os.sectionIndex = machoSecMap[nl->n_sect];
            // Convert Mach-O absolute n_value to section-relative offset.
            // n_value is an address in the .o's virtual space; subtract section base.
            if (os.sectionIndex > 0 && os.sectionIndex < secAddrs.size())
                os.offset = static_cast<size_t>(nl->n_value - secAddrs[os.sectionIndex]);
            else
                os.offset = static_cast<size_t>(nl->n_value);

            symMap[i] = static_cast<uint32_t>(obj.symbols.size());
            obj.symbols.push_back(std::move(os));
        }

        // Fix up relocation symbol indices from nlist indices to ObjFile indices.
        for (auto &sec : obj.sections) {
            for (auto &rel : sec.relocs) {
                if (rel.symIndex < symMap.size())
                    rel.symIndex = symMap[rel.symIndex];
            }
        }
    }

    return true;
}

} // namespace viper::codegen::linker
