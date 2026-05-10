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
#include "codegen/common/linker/RelocConstants.hpp"

#include <cstring>
#include <string>
#include <vector>

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
    if (offset > size || sizeof(T) > size - offset)
        return nullptr;
    return reinterpret_cast<const T *>(data + offset);
}

static bool checkedRange(size_t off, size_t len, size_t size) {
    return off <= size && len <= size - off;
}

static std::string trimNul(const char *s, size_t maxLen) {
    size_t len = 0;
    while (len < maxLen && s[len] != '\0')
        ++len;
    return std::string(s, len);
}

static std::string readString(const uint8_t *data, size_t size, size_t off, size_t len, uint32_t pos) {
    if (!checkedRange(off, len, size) || pos >= len)
        return "";
    const uint8_t *begin = data + off + pos;
    const uint8_t *end = data + off + len;
    const void *nul = std::memchr(begin, '\0', static_cast<size_t>(end - begin));
    if (!nul)
        return "";
    return std::string(reinterpret_cast<const char *>(begin),
                       static_cast<const char *>(nul));
}

static int64_t signExtend(uint64_t value, unsigned bits) {
    const uint64_t signBit = uint64_t{1} << (bits - 1);
    const uint64_t mask = (uint64_t{1} << bits) - 1;
    value &= mask;
    return static_cast<int64_t>((value ^ signBit) - signBit);
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
    uint32_t relocLength,
    bool isArm64) {
    if (isArm64) {
        if (!checkedRange(offset, 4, sectionSize))
            return 0;
        const uint32_t insn = readLE32(sectionData + offset);
        switch (relocType) {
            case macho_a64::kBranch26:
                return signExtend(insn & 0x03FFFFFFu, 26) << 2;
            case macho_a64::kPage21:
            case macho_a64::kGotLoadPage21:
            case macho_a64::kTlvpLoadPage21: {
                const uint32_t immlo = (insn >> 29) & 0x3u;
                const uint32_t immhi = (insn >> 5) & 0x7FFFFu;
                return signExtend((immhi << 2) | immlo, 21) << 12;
            }
            case macho_a64::kPageOff12:
            case macho_a64::kGotLoadPageOff12:
            case macho_a64::kTlvpLoadPageOff12: {
                uint32_t pageOff = (insn >> 10) & 0xFFFu;
                if ((insn & 0x3B000000) == 0x39000000) {
                    uint32_t shift = insn >> 30;
                    if ((insn & 0x04800000) == 0x04800000)
                        shift = 4;
                    pageOff <<= shift;
                }
                return static_cast<int64_t>(pageOff);
            }
            default:
                break;
        }
        return 0;
    } else {
        const size_t fieldSize = size_t{1} << relocLength;
        if (checkedRange(offset, fieldSize, sectionSize)) {
            if (fieldSize == 1) {
                int8_t val = 0;
                std::memcpy(&val, sectionData + offset, 1);
                return val;
            }
            if (fieldSize == 2) {
                int16_t val = 0;
                std::memcpy(&val, sectionData + offset, 2);
                return val;
            }
            if (fieldSize == 4) {
                int32_t val = 0;
                std::memcpy(&val, sectionData + offset, 4);
                return val;
            }
            int64_t val = 0;
            std::memcpy(&val, sectionData + offset, 8);
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
    obj.symbols.assign(1, ObjSymbol{});

    const bool isArm64 = (hdr->cputype == macho::CPU_TYPE_ARM64);
    if (hdr->filetype != macho::MH_OBJECT ||
        (hdr->cputype != macho::CPU_TYPE_ARM64 && hdr->cputype != macho::CPU_TYPE_X86_64)) {
        err << "error: " << name << ": unsupported Mach-O object format\n";
        return false;
    }
    obj.machine = isArm64 ? 183 : 62; // Map to ELF machine constants for uniformity.

    if (!checkedRange(sizeof(macho::mach_header_64), hdr->sizeofcmds, size)) {
        err << "error: " << name << ": Mach-O load commands are out of bounds\n";
        return false;
    }

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
        if (!lc) {
            err << "error: " << name << ": truncated Mach-O load command\n";
            return false;
        }
        if (lc->cmdsize < sizeof(macho::load_command) || !checkedRange(tmpOff, lc->cmdsize, size)) {
            err << "error: " << name << ": malformed Mach-O load command\n";
            return false;
        }

        if (lc->cmd == macho::LC_SEGMENT_64) {
            const auto *seg = readAt<macho::segment_command_64>(data, size, tmpOff);
            if (!seg) {
                err << "error: " << name << ": truncated Mach-O segment command\n";
                return false;
            }
            const size_t secTableOff = tmpOff + sizeof(macho::segment_command_64);
            const size_t secTableSize =
                static_cast<size_t>(seg->nsects) * sizeof(macho::section_64);
            if (!checkedRange(secTableOff, secTableSize, size) ||
                secTableOff + secTableSize > tmpOff + lc->cmdsize) {
                err << "error: " << name << ": Mach-O section table is out of bounds\n";
                return false;
            }

            size_t secOff = secTableOff;
            for (uint32_t s = 0; s < seg->nsects; ++s) {
                const auto *sec = readAt<macho::section_64>(data, size, secOff);
                if (!sec) {
                    err << "error: " << name << ": truncated Mach-O section header\n";
                    return false;
                }

                std::string segName = trimNul(sec->segname, 16);
                std::string secName = trimNul(sec->sectname, 16);
                const bool isDebugSection =
                    (segName == "__DWARF" || (sec->flags & macho::S_ATTR_DEBUG) != 0);

                ObjSection os;
                os.name = segName + "," + secName;
                os.alignment = (sec->align < 30) ? (1u << sec->align) : 1;
                os.executable = (sec->flags & macho::S_ATTR_PURE_INSTRUCTIONS) != 0;
                os.alloc = !isDebugSection;

                // Infer writability from Mach-O segment name and section type.
                // .o files don't have segment permission bits, but each section header
                // carries the intended segment name (__TEXT vs __DATA).
                const uint32_t secType = sec->flags & 0xFF;
                os.writable = !isDebugSection &&
                              ((segName == "__DATA") || (secType == macho::S_ZEROFILL) ||
                               (secType == macho::S_THREAD_LOCAL_REGULAR) ||
                               (secType == macho::S_THREAD_LOCAL_ZEROFILL) ||
                               (secType == macho::S_THREAD_LOCAL_VARIABLES) ||
                               (secType == macho::S_NON_LAZY_SYMBOL_POINTERS) ||
                               (secType == macho::S_LAZY_SYMBOL_POINTERS));

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
                os.zeroFill = isZerofill;

                if (sec->size > static_cast<uint64_t>(SIZE_MAX)) {
                    err << "error: " << name << ": Mach-O section is too large\n";
                    return false;
                }
                if (isZerofill && sec->size > 0) {
                    os.data.resize(static_cast<size_t>(sec->size), 0);
                } else if (sec->size > 0 && checkedRange(sec->offset, static_cast<size_t>(sec->size), size)) {
                    os.data.assign(data + sec->offset, data + sec->offset + sec->size);
                } else if (sec->size > 0) {
                    err << "error: " << name << ": Mach-O section '" << os.name
                        << "' contents are out of bounds\n";
                    return false;
                }

                // Parse section relocations.
                // ARM64_RELOC_ADDEND (type 10) is a paired relocation:
                // its r_symbolnum carries the addend for the NEXT relocation.
                int64_t pendingAddend = 0;
                bool hasPendingAddend = false;
                if (sec->nreloc > 0 &&
                    !checkedRange(sec->reloff,
                                  static_cast<size_t>(sec->nreloc) *
                                      sizeof(macho::relocation_info),
                                  size)) {
                    err << "error: " << name << ": Mach-O relocation table is out of bounds\n";
                    return false;
                }
                for (uint32_t r = 0; r < sec->nreloc; ++r) {
                    const auto *ri = readAt<macho::relocation_info>(
                        data, size, sec->reloff + r * sizeof(macho::relocation_info));

                    // Scattered relocations carry a different payload layout. Do not
                    // silently drop them; unresolved fixups would corrupt the link.
                    if (ri->r_address & 0x80000000) {
                        err << "error: " << name
                            << ": Mach-O scattered relocations are not supported\n";
                        return false;
                    }

                    const uint32_t info = ri->r_info;
                    const uint32_t symbolNum = info & 0x00FFFFFF;
                    const bool isExtern = ((info >> 27) & 1) != 0;
                    const uint32_t relLength = (info >> 25) & 0x3;
                    const uint32_t relType = (info >> 28) & 0xF;

                    // ARM64_RELOC_ADDEND (type 10): stores addend for next relocation.
                    if (isArm64 && relType == 10) {
                        pendingAddend = signExtend(symbolNum, 24);
                        hasPendingAddend = true;
                        continue;
                    }

                    ObjReloc rel;
                    rel.offset = static_cast<size_t>(ri->r_address);
                    rel.symIndex = symbolNum; // nlist index or section ordinal.
                    rel.type = relType;
                    rel.sectionRelative = !isExtern;

                    if (hasPendingAddend) {
                        rel.addend = pendingAddend;
                        hasPendingAddend = false;
                    } else {
                        // Extract addend from instruction bytes.
                        rel.addend = extractMachOAddend(
                            os.data.data(),
                            os.data.size(),
                            rel.offset,
                            rel.type,
                            relLength,
                            isArm64);
                    }

                    os.relocs.push_back(rel);
                }
                if (hasPendingAddend) {
                    err << "error: " << name << ": dangling ARM64_RELOC_ADDEND in Mach-O section "
                        << os.name << "\n";
                    return false;
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
        if (!checkedRange(symtabLcOff, 24, size)) {
            err << "error: " << name << ": Mach-O LC_SYMTAB is out of bounds\n";
            return false;
        }

        const uint32_t symoff = readLE32(data + symtabLcOff + 8);
        const uint32_t nsyms = readLE32(data + symtabLcOff + 12);
        const uint32_t stroff = readLE32(data + symtabLcOff + 16);
        const uint32_t strsize = readLE32(data + symtabLcOff + 20);

        if (!checkedRange(symoff, static_cast<size_t>(nsyms) * sizeof(macho::nlist_64), size) ||
            !checkedRange(stroff, strsize, size)) {
            err << "error: " << name << ": Mach-O symbol table is out of bounds\n";
            return false;
        }

        if (nsyms > kMaxObjSymbols) {
            err << "error: " << name << ": symbol count " << nsyms << " exceeds limit\n";
            return false;
        }

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
            os.name = readString(data, size, stroff, strsize, nl->n_strx);
            // Strip leading underscore (Mach-O convention).
            if (!os.name.empty() && os.name[0] == '_')
                os.name.erase(0, 1);

            // Parse binding from n_type.
            const uint8_t nType = nl->n_type & 0x0E; // N_TYPE mask.
            const bool isExtern = (nl->n_type & 0x01) != 0;

            if (nType == 0x00) // N_UNDF
            {
                os.binding = ObjSymbol::Undefined;
                // Check for weak imports.
                if (nl->n_desc & 0x0040) // N_WEAK_REF
                    os.weakExternal = true;
            } else if (nType == 0x02) // N_ABS
            {
                os.binding = isExtern ? ObjSymbol::Global : ObjSymbol::Local;
                os.absolute = true;
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
            if (nl->n_sect > 0 && nl->n_sect < secAddrs.size() && os.sectionIndex > 0) {
                const uint64_t base = secAddrs[nl->n_sect];
                if (nl->n_value < base) {
                    err << "error: " << name << ": Mach-O symbol '" << os.name
                        << "' value is before its section base\n";
                    return false;
                }
                const uint64_t relValue = nl->n_value - base;
                if (relValue > static_cast<uint64_t>(SIZE_MAX)) {
                    err << "error: " << name << ": Mach-O symbol '" << os.name
                        << "' offset exceeds addressable size\n";
                    return false;
                }
                os.offset = static_cast<size_t>(relValue);
            } else
                os.offset = static_cast<size_t>(nl->n_value);

            symMap[i] = static_cast<uint32_t>(obj.symbols.size());
            obj.symbols.push_back(std::move(os));
        }

        // Fix up relocation symbol indices from nlist indices to ObjFile indices.
        std::vector<uint32_t> sectionSymMap(machoSecMap.size(), 0);
        auto sectionSymbolFor = [&](uint32_t machoSectionOrdinal) -> uint32_t {
            if (machoSectionOrdinal >= machoSecMap.size())
                return 0;
            const uint32_t objSecIdx = machoSecMap[machoSectionOrdinal];
            if (objSecIdx == 0)
                return 0;
            uint32_t &cached = sectionSymMap[machoSectionOrdinal];
            if (cached != 0)
                return cached;
            ObjSymbol sym;
            sym.name = "$sect." + std::to_string(machoSectionOrdinal);
            sym.binding = ObjSymbol::Local;
            sym.sectionIndex = objSecIdx;
            sym.offset = 0;
            cached = static_cast<uint32_t>(obj.symbols.size());
            obj.symbols.push_back(std::move(sym));
            return cached;
        };
        for (auto &sec : obj.sections) {
            for (auto &rel : sec.relocs) {
                if (rel.sectionRelative) {
                    const uint32_t mapped = sectionSymbolFor(rel.symIndex);
                    if (mapped == 0) {
                        err << "error: " << name
                            << ": relocation references unmapped Mach-O section "
                            << rel.symIndex << "\n";
                        return false;
                    }
                    rel.symIndex = mapped;
                    rel.sectionRelative = false;
                } else if (rel.symIndex < symMap.size()) {
                    rel.symIndex = symMap[rel.symIndex];
                } else {
                    err << "error: " << name << ": relocation references invalid symbol index "
                        << rel.symIndex << "\n";
                    return false;
                }
            }
        }
    } else {
        for (const auto &sec : obj.sections) {
            if (!sec.relocs.empty()) {
                err << "error: " << name
                    << ": Mach-O relocations require LC_SYMTAB for symbol mapping\n";
                return false;
            }
        }
    }

    return true;
}

} // namespace viper::codegen::linker
