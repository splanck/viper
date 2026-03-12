//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/ObjFileReader.cpp
// Purpose: Format detection and dispatch for object file reading.
// Key invariants:
//   - Auto-detects ELF (7f 45 4c 46), Mach-O (cf fa ed fe), COFF (machine field)
// Links: codegen/common/linker/ObjFileReader.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/ObjFileReader.hpp"

#include <fstream>

namespace viper::codegen::linker
{

ObjFileFormat detectFormat(const uint8_t *data, size_t size)
{
    if (size < 4)
        return ObjFileFormat::Unknown;

    // ELF: 0x7F 'E' 'L' 'F'
    if (data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F')
        return ObjFileFormat::ELF;

    // Mach-O 64-bit: 0xFEEDFACF (little-endian: CF FA ED FE)
    if (data[0] == 0xCF && data[1] == 0xFA && data[2] == 0xED && data[3] == 0xFE)
        return ObjFileFormat::MachO;

    // COFF: check for known machine types at offset 0.
    if (size >= 20)
    {
        const uint16_t machine =
            static_cast<uint16_t>(data[0]) | (static_cast<uint16_t>(data[1]) << 8);
        if (machine == 0x8664 || machine == 0xAA64)
            return ObjFileFormat::COFF;
    }

    return ObjFileFormat::Unknown;
}

bool readObjFile(const uint8_t *data, size_t size, const std::string &name, ObjFile &obj,
                 std::ostream &err)
{
    const ObjFileFormat fmt = detectFormat(data, size);
    switch (fmt)
    {
    case ObjFileFormat::ELF:
        return readElfObj(data, size, name, obj, err);
    case ObjFileFormat::MachO:
        return readMachOObj(data, size, name, obj, err);
    case ObjFileFormat::COFF:
        return readCoffObj(data, size, name, obj, err);
    case ObjFileFormat::Unknown:
        err << "error: " << name << ": unknown object file format\n";
        return false;
    }
    return false;
}

bool readObjFile(const std::string &path, ObjFile &obj, std::ostream &err)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
    {
        err << "error: cannot open object file '" << path << "'\n";
        return false;
    }
    const auto fileSize = static_cast<size_t>(f.tellg());
    f.seekg(0);
    std::vector<uint8_t> data(fileSize);
    f.read(reinterpret_cast<char *>(data.data()), static_cast<std::streamsize>(fileSize));
    if (!f)
    {
        err << "error: failed to read object file '" << path << "'\n";
        return false;
    }
    return readObjFile(data.data(), data.size(), path, obj, err);
}

} // namespace viper::codegen::linker
