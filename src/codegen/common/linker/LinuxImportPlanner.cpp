//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/LinuxImportPlanner.cpp
// Purpose: Linux ELF dynamic import classification for the native linker.
//          Maps each undefined dynamic symbol to one of a fixed set of libraries
//          (libc, libm, libdl, libpthread, libX11, libasound) and produces the
//          deduplicated DT_NEEDED list for the ELF executable writer.
// Key invariants:
//   - The lookup table is fully baked-in; no filesystem access.
//   - DT_NEEDED order follows a stable ABI-conventional sequence: libc.so.6 is
//     always first (when present) so unresolved C runtime symbols satisfy.
//   - Returning false leaves @p plan in a partially-populated state; callers
//     should treat this as a fatal link error.
// Ownership/Lifetime: stateless — caller owns the populated LinuxImportPlan.
// Links: codegen/common/linker/PlatformImportPlanner.hpp,
//        codegen/common/linker/ElfExeWriter.hpp
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/PlatformImportPlanner.hpp"

#include <cstdint>
#include <unordered_set>

namespace viper::codegen::linker {
namespace {

enum class LinuxNeededLib : uint8_t {
    LibC,
    LibM,
    LibDL,
    LibPthread,
    LibStdCpp,
    LibGccS,
    LibX11,
    LibASound,
};

const char *linuxNeededLibName(LinuxNeededLib lib) {
    switch (lib) {
        case LinuxNeededLib::LibC:
            return "libc.so.6";
        case LinuxNeededLib::LibM:
            return "libm.so.6";
        case LinuxNeededLib::LibDL:
            return "libdl.so.2";
        case LinuxNeededLib::LibPthread:
            return "libpthread.so.0";
        case LinuxNeededLib::LibStdCpp:
            return "libstdc++.so.6";
        case LinuxNeededLib::LibGccS:
            return "libgcc_s.so.1";
        case LinuxNeededLib::LibX11:
            return "libX11.so.6";
        case LinuxNeededLib::LibASound:
            return "libasound.so.2";
    }
    return "libc.so.6";
}

bool isLinuxMathSymbol(const std::string &name) {
    static const std::unordered_set<std::string> kMath = {
        "acos",  "acosf",  "asin",      "asinf",      "atan",   "atan2", "atan2f", "atanf",
        "cbrt",  "cbrtf",  "ceil",      "ceilf",      "copysign","copysignf","cos", "cosf",
        "cosh",  "exp",
        "expf",  "fabs",   "fabsf",     "floor",      "floorf", "fmax",  "fmaxf",  "fmin",
        "fminf", "fmod",   "fmodf",     "hypot",      "ldexp",  "log",   "log10",  "log2",
        "logf",  "nan",    "pow",       "powf",       "rint",   "round", "roundf", "sin",
        "sinf",  "sinh",   "sqrt",      "sqrtf",      "tan",    "tanf",  "tanh",   "trunc",
        "truncf",
        "atan2l","ceill",  "cosl",      "floorl",     "fmaxl",  "fminl", "fmodl",  "sinl",
        "sqrtl",
    };
    return kMath.count(name) != 0;
}

bool isLinuxDlSymbol(const std::string &name) {
    return name == "dlopen" || name == "dlsym" || name == "dlclose" || name == "dlerror";
}

bool isLinuxCppRuntimeSymbol(const std::string &name) {
    static const std::unordered_set<std::string> kExact = {
        "__cxa_begin_catch",
        "__cxa_end_catch",
        "__cxa_rethrow",
        "__gxx_personality_v0",
    };
    if (kExact.count(name) != 0)
        return true;

    static const char *const kPrefixes[] = {
        "_ZNSt",
        "_ZNKSt",
        "_ZTINSt",
        "_ZTSNSt",
        "_ZTVNSt",
        "_ZSt",
        "_ZTISt",
        "_ZTSSt",
        "_ZTVSt",
        "_Zda",
        "_Zdl",
        "_Zna",
        "_Znw",
    };
    for (const char *prefix : kPrefixes) {
        if (name.rfind(prefix, 0) == 0)
            return true;
    }
    return false;
}

LinuxNeededLib classifyLinuxImportLibrary(const std::string &name) {
    if (name.rfind("snd_", 0) == 0)
        return LinuxNeededLib::LibASound;
    if (name.rfind("X", 0) == 0)
        return LinuxNeededLib::LibX11;
    if (isLinuxDlSymbol(name))
        return LinuxNeededLib::LibDL;
    if (name.rfind("pthread_", 0) == 0)
        return LinuxNeededLib::LibPthread;
    if (name.rfind("_Unwind_", 0) == 0)
        return LinuxNeededLib::LibGccS;
    if (isLinuxCppRuntimeSymbol(name))
        return LinuxNeededLib::LibStdCpp;
    if (isLinuxMathSymbol(name))
        return LinuxNeededLib::LibM;
    return LinuxNeededLib::LibC;
}

} // namespace

bool planLinuxImports(const std::unordered_set<std::string> &dynamicSyms,
                      LinuxImportPlan &plan,
                      std::ostream & /*err*/) {
    std::unordered_set<LinuxNeededLib> libs = {LinuxNeededLib::LibC};
    for (const auto &sym : dynamicSyms)
        libs.insert(classifyLinuxImportLibrary(sym));

    static constexpr LinuxNeededLib kOrder[] = {
        LinuxNeededLib::LibC,
        LinuxNeededLib::LibM,
        LinuxNeededLib::LibDL,
        LinuxNeededLib::LibPthread,
        LinuxNeededLib::LibStdCpp,
        LinuxNeededLib::LibGccS,
        LinuxNeededLib::LibX11,
        LinuxNeededLib::LibASound,
    };
    for (LinuxNeededLib lib : kOrder) {
        if (libs.count(lib) != 0)
            plan.neededLibs.push_back(linuxNeededLibName(lib));
    }
    return true;
}

} // namespace viper::codegen::linker
