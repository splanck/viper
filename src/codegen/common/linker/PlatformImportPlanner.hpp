//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/PlatformImportPlanner.hpp
// Purpose: Declare per-platform dynamic import planners used by the native
//          linker.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "codegen/common/linker/LinkTypes.hpp"
#include "codegen/common/linker/MachOExeWriter.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/PeExeWriter.hpp"

#include <cstdint>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace viper::codegen::linker {

struct WindowsImportPlan {
    ObjFile obj;
    std::vector<DllImport> imports;
};

struct MacImportPlan {
    std::vector<DylibImport> dylibs;
    std::unordered_map<std::string, uint32_t> symOrdinals;
};

struct LinuxImportPlan {
    std::vector<std::string> neededLibs;
};

bool planMacImports(const std::unordered_set<std::string> &dynamicSyms,
                    MacImportPlan &plan,
                    std::ostream &err);

bool planLinuxImports(const std::unordered_set<std::string> &dynamicSyms,
                      LinuxImportPlan &plan,
                      std::ostream &err);

bool generateWindowsImports(LinkArch arch,
                            const std::unordered_set<std::string> &dynamicSyms,
                            bool debugRuntime,
                            WindowsImportPlan &plan,
                            std::ostream &err);

} // namespace viper::codegen::linker
