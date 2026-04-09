//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: codegen/common/linker/NativeLinker.cpp
// Purpose: Top-level native linker implementation.
// Key invariants:
//   - Pipeline: parse .o → parse archives → resolve symbols → generate stubs →
//     merge sections → branch trampolines → apply relocations → write executable
//   - For macOS: generates GOT entries and stub trampolines for dynamic symbols,
//     uses non-lazy binding (dyld fills GOT at load time)
//   - Falls back gracefully with clear error messages
// Links: codegen/common/linker/NativeLinker.hpp
// Cross-platform touchpoints: shared link pipeline orchestration, per-platform
//                             import planners, executable writer selection.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/linker/NativeLinker.hpp"

#include "codegen/common/linker/ArchiveReader.hpp"
#include "codegen/common/linker/BranchTrampoline.hpp"
#include "codegen/common/linker/DeadStripPass.hpp"
#include "codegen/common/linker/DynStubGen.hpp"
#include "codegen/common/linker/ElfExeWriter.hpp"
#include "codegen/common/linker/ICF.hpp"
#include "codegen/common/linker/MachOExeWriter.hpp"
#include "codegen/common/linker/NameMangling.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/PlatformImportPlanner.hpp"
#include "codegen/common/linker/PeExeWriter.hpp"
#include "codegen/common/linker/RelocApplier.hpp"
#include "codegen/common/linker/RelocConstants.hpp"
#include "codegen/common/linker/SectionMerger.hpp"
#include "codegen/common/linker/StringDedup.hpp"
#include "codegen/common/linker/SymbolResolver.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace viper::codegen::linker {

namespace {

void registerSyntheticSymbols(const ObjFile &obj,
                              size_t objIdx,
                              std::unordered_map<std::string, GlobalSymEntry> &globalSyms) {
    for (size_t i = 1; i < obj.symbols.size(); ++i) {
        const auto &sym = obj.symbols[i];
        if (sym.binding == ObjSymbol::Local || sym.binding == ObjSymbol::Undefined ||
            sym.name.empty())
            continue;

        GlobalSymEntry e;
        e.name = sym.name;
        e.binding = GlobalSymEntry::Global;
        e.objIndex = objIdx;
        e.secIndex = sym.sectionIndex;
        e.offset = sym.offset;
        globalSyms[sym.name] = std::move(e);
    }
}

void removeDynamicSymbol(const char *name, std::unordered_set<std::string> &dynamicSyms) {
    dynamicSyms.erase(name);
}

ObjFile makeUndefinedRootObject(const ObjFile &userObj, const std::string &symbolName) {
    ObjFile root;
    root.name = "<entry-root>";
    root.format = userObj.format;
    root.is64bit = userObj.is64bit;
    root.isLittleEndian = userObj.isLittleEndian;
    root.machine = userObj.machine;
    root.sections.push_back(ObjSection{});
    root.symbols.push_back(ObjSymbol{});

    ObjSymbol sym;
    sym.name = symbolName;
    sym.binding = ObjSymbol::Undefined;
    root.symbols.push_back(std::move(sym));
    return root;
}

bool usesDebugWindowsRuntime(const std::vector<std::string> &archivePaths) {
    for (const auto &path : archivePaths) {
        std::string lower = path;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (lower.find("\\debug\\") != std::string::npos ||
            lower.find("/debug/") != std::string::npos ||
            lower.rfind("msvcrtd.lib") != std::string::npos ||
            lower.rfind("ucrtd.lib") != std::string::npos ||
            lower.rfind("vcruntimed.lib") != std::string::npos)
            return true;
    }
    return false;
}

const char *platformName(LinkPlatform platform) {
    switch (platform) {
        case LinkPlatform::Linux:
            return "Linux";
        case LinkPlatform::macOS:
            return "macOS";
        case LinkPlatform::Windows:
            return "Windows";
    }
    return "unknown";
}

const char *archName(LinkArch arch) {
    switch (arch) {
        case LinkArch::X86_64:
            return "x86_64";
        case LinkArch::AArch64:
            return "AArch64";
    }
    return "unknown";
}


void appendArm64Insn(std::vector<uint8_t> &data, uint32_t insn) {
    data.push_back(static_cast<uint8_t>(insn & 0xFF));
    data.push_back(static_cast<uint8_t>((insn >> 8) & 0xFF));
    data.push_back(static_cast<uint8_t>((insn >> 16) & 0xFF));
    data.push_back(static_cast<uint8_t>((insn >> 24) & 0xFF));
}

ObjFile generateWindowsX64Helpers(const std::unordered_set<std::string> &dynamicSyms,
                                  bool haveVmTrapDefault,
                                  bool needTlsIndex) {
    ObjFile obj;
    obj.name = "<win64-helpers>";
    obj.format = ObjFileFormat::COFF;
    obj.is64bit = true;
    obj.isLittleEndian = true;
    obj.machine = 0x8664;
    obj.sections.push_back(ObjSection{});
    obj.symbols.push_back(ObjSymbol{});

    ObjSection textSec;
    textSec.name = ".text";
    textSec.executable = true;
    textSec.alloc = true;
    textSec.alignment = 16;

    ObjSection dataSec;
    dataSec.name = ".data";
    dataSec.writable = true;
    dataSec.alloc = true;
    dataSec.alignment = 8;

    auto needsHelper = [&](const std::string &name) {
        return dynamicSyms.count(name) || dynamicSyms.count("__imp_" + name);
    };

    auto addRetFn = [&](const std::string &name, std::initializer_list<uint8_t> bytes) {
        const size_t off = textSec.data.size();
        textSec.data.insert(textSec.data.end(), bytes.begin(), bytes.end());
        ObjSymbol sym;
        sym.name = name;
        sym.binding = ObjSymbol::Global;
        sym.sectionIndex = 1;
        sym.offset = off;
        obj.symbols.push_back(std::move(sym));
        return static_cast<uint32_t>(obj.symbols.size() - 1);
    };

    auto addData = [&](const std::string &name, const std::vector<uint8_t> &bytes, uint32_t align) {
        while ((dataSec.data.size() % align) != 0)
            dataSec.data.push_back(0);
        const size_t off = dataSec.data.size();
        dataSec.data.insert(dataSec.data.end(), bytes.begin(), bytes.end());
        ObjSymbol sym;
        sym.name = name;
        sym.binding = ObjSymbol::Global;
        sym.sectionIndex = 2;
        sym.offset = off;
        obj.symbols.push_back(std::move(sym));
        return static_cast<uint32_t>(obj.symbols.size() - 1);
    };

    auto addAbs64DataRef = [&](const std::string &name, uint32_t align, uint32_t targetSymIdx) {
        while ((dataSec.data.size() % align) != 0)
            dataSec.data.push_back(0);
        const size_t off = dataSec.data.size();
        dataSec.data.resize(off + 8, 0);
        ObjSymbol sym;
        sym.name = name;
        sym.binding = ObjSymbol::Global;
        sym.sectionIndex = 2;
        sym.offset = off;
        obj.symbols.push_back(std::move(sym));

        ObjReloc reloc;
        reloc.offset = off;
        reloc.type = coff_x64::kAddr64;
        reloc.symIndex = targetSymIdx;
        reloc.addend = 0;
        dataSec.relocs.push_back(reloc);
        return static_cast<uint32_t>(obj.symbols.size() - 1);
    };

    auto addImportAlias = [&](const std::string &name, uint32_t targetSymIdx) {
        if (dynamicSyms.count("__imp_" + name))
            addAbs64DataRef("__imp_" + name, 8, targetSymIdx);
    };

    if (needsHelper("_fltused")) {
        const uint32_t idx = addData("_fltused", {1, 0, 0, 0}, 4);
        addImportAlias("_fltused", idx);
    }
    if (needsHelper("__security_cookie")) {
        const uint32_t idx =
            addData("__security_cookie", {0x32, 0xA2, 0xDF, 0x2D, 0x99, 0x2B, 0x00, 0x00}, 8);
        addImportAlias("__security_cookie", idx);
    }
    if (needsHelper("__security_cookie_complement")) {
        const uint32_t idx = addData(
            "__security_cookie_complement", {0xCD, 0x5D, 0x20, 0xD2, 0x66, 0xD4, 0xFF, 0xFF}, 8);
        addImportAlias("__security_cookie_complement", idx);
    }
    if (needTlsIndex || needsHelper("_tls_index")) {
        const uint32_t idx = addData("_tls_index", {0, 0, 0, 0}, 4);
        addImportAlias("_tls_index", idx);
    }
    if (needsHelper("_is_c_termination_complete")) {
        const uint32_t idx = addData("_is_c_termination_complete", {0, 0, 0, 0}, 4);
        addImportAlias("_is_c_termination_complete", idx);
    }
    if (needsHelper("?_OptionsStorage@?1??__local_stdio_printf_options@@9@9")) {
        const uint32_t idx = addData(
            "?_OptionsStorage@?1??__local_stdio_printf_options@@9@9", {0, 0, 0, 0, 0, 0, 0, 0}, 8);
        addImportAlias("?_OptionsStorage@?1??__local_stdio_printf_options@@9@9", idx);
    }
    if (needsHelper("?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9")) {
        const uint32_t idx = addData(
            "?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9", {0, 0, 0, 0, 0, 0, 0, 0}, 8);
        addImportAlias("?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9", idx);
    }

    if (needsHelper("__security_check_cookie")) {
        const uint32_t idx = addRetFn("__security_check_cookie", {0xC3});
        addImportAlias("__security_check_cookie", idx);
    }
    if (needsHelper("__security_init_cookie")) {
        const uint32_t idx = addRetFn("__security_init_cookie", {0xC3});
        addImportAlias("__security_init_cookie", idx);
    }
    if (needsHelper("__GSHandlerCheck")) {
        const uint32_t idx = addRetFn("__GSHandlerCheck", {0xC3});
        addImportAlias("__GSHandlerCheck", idx);
    }
    if (needsHelper("_RTC_CheckStackVars")) {
        const uint32_t idx = addRetFn("_RTC_CheckStackVars", {0xC3});
        addImportAlias("_RTC_CheckStackVars", idx);
    }
    if (needsHelper("_RTC_InitBase")) {
        const uint32_t idx = addRetFn("_RTC_InitBase", {0x31, 0xC0, 0xC3});
        addImportAlias("_RTC_InitBase", idx);
    }
    if (needsHelper("_RTC_Shutdown")) {
        const uint32_t idx = addRetFn("_RTC_Shutdown", {0xC3});
        addImportAlias("_RTC_Shutdown", idx);
    }
    if (needsHelper("_RTC_UninitUse")) {
        const uint32_t idx = addRetFn("_RTC_UninitUse", {0xC3});
        addImportAlias("_RTC_UninitUse", idx);
    }
    if (needsHelper("IID_ID3D11Texture2D")) {
        const uint32_t idx = addData("IID_ID3D11Texture2D",
                                     {0xF2, 0xAA, 0x15, 0x6F, 0x08, 0xD2, 0x89, 0x4E,
                                      0x9A, 0xB4, 0x48, 0x95, 0x35, 0xD3, 0x4F, 0x9C},
                                     4);
        addImportAlias("IID_ID3D11Texture2D", idx);
    }
    if (needsHelper("__report_rangecheckfailure")) {
        const uint32_t idx = addRetFn("__report_rangecheckfailure", {0xCC, 0xC3});
        addImportAlias("__report_rangecheckfailure", idx);
    }
    if (needsHelper("__chkstk")) {
        std::vector<uint8_t> chkstk = {
            0x49, 0x89, 0xC2,                         // mov r10, rax
            0x49, 0x89, 0xE3,                         // mov r11, rsp
            0x49, 0x81, 0xFA, 0x00, 0x10, 0x00, 0x00, // cmp r10, 0x1000
            0x72, 0x1B,                               // jb tail
            0x49, 0x81, 0xEB, 0x00, 0x10, 0x00, 0x00, // sub r11, 0x1000
            0x41, 0xF6, 0x03, 0x00,                   // test byte ptr [r11], 0
            0x49, 0x81, 0xEA, 0x00, 0x10, 0x00, 0x00, // sub r10, 0x1000
            0x49, 0x81, 0xFA, 0x00, 0x10, 0x00, 0x00, // cmp r10, 0x1000
            0x73, 0xE5,                               // jae probe_loop
            0x4D, 0x29, 0xD3,                         // sub r11, r10
            0x41, 0xF6, 0x03, 0x00,                   // test byte ptr [r11], 0
            0xC3,                                     // ret
        };
        const size_t off = textSec.data.size();
        textSec.data.insert(textSec.data.end(), chkstk.begin(), chkstk.end());
        ObjSymbol sym;
        sym.name = "__chkstk";
        sym.binding = ObjSymbol::Global;
        sym.sectionIndex = 1;
        sym.offset = off;
        obj.symbols.push_back(std::move(sym));
        const uint32_t idx = static_cast<uint32_t>(obj.symbols.size() - 1);
        addImportAlias("__chkstk", idx);
    }
    if (needsHelper("rt_audio_shutdown")) {
        const uint32_t idx = addRetFn("rt_audio_shutdown", {0xC3});
        addImportAlias("rt_audio_shutdown", idx);
    }
    if (needsHelper("__vcrt_initialize")) {
        const uint32_t idx = addRetFn("__vcrt_initialize", {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3});
        addImportAlias("__vcrt_initialize", idx);
    }
    if (needsHelper("__vcrt_thread_attach")) {
        const uint32_t idx = addRetFn("__vcrt_thread_attach", {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3});
        addImportAlias("__vcrt_thread_attach", idx);
    }
    if (needsHelper("__vcrt_thread_detach")) {
        const uint32_t idx = addRetFn("__vcrt_thread_detach", {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3});
        addImportAlias("__vcrt_thread_detach", idx);
    }
    if (needsHelper("__vcrt_uninitialize")) {
        const uint32_t idx = addRetFn("__vcrt_uninitialize", {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3});
        addImportAlias("__vcrt_uninitialize", idx);
    }
    if (needsHelper("__vcrt_uninitialize_critical")) {
        const uint32_t idx = addRetFn("__vcrt_uninitialize_critical", {0xC3});
        addImportAlias("__vcrt_uninitialize_critical", idx);
    }
    if (needsHelper("__acrt_initialize")) {
        const uint32_t idx = addRetFn("__acrt_initialize", {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3});
        addImportAlias("__acrt_initialize", idx);
    }
    if (needsHelper("__acrt_thread_attach")) {
        const uint32_t idx = addRetFn("__acrt_thread_attach", {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3});
        addImportAlias("__acrt_thread_attach", idx);
    }
    if (needsHelper("__acrt_thread_detach")) {
        const uint32_t idx = addRetFn("__acrt_thread_detach", {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3});
        addImportAlias("__acrt_thread_detach", idx);
    }
    if (needsHelper("__acrt_uninitialize")) {
        const uint32_t idx = addRetFn("__acrt_uninitialize", {0xB8, 0x01, 0x00, 0x00, 0x00, 0xC3});
        addImportAlias("__acrt_uninitialize", idx);
    }
    if (needsHelper("__acrt_uninitialize_critical")) {
        const uint32_t idx = addRetFn("__acrt_uninitialize_critical", {0xC3});
        addImportAlias("__acrt_uninitialize_critical", idx);
    }
    if (needsHelper("__isa_available_init")) {
        const uint32_t idx = addRetFn("__isa_available_init", {0xC3});
        addImportAlias("__isa_available_init", idx);
    }
    if (needsHelper("__scrt_exe_initialize_mta")) {
        const uint32_t idx = addRetFn("__scrt_exe_initialize_mta", {0x31, 0xC0, 0xC3});
        addImportAlias("__scrt_exe_initialize_mta", idx);
    }

    if (needsHelper("__guard_dispatch_icall_fptr")) {
        const uint32_t dispatchIdx = addRetFn("__guard_dispatch_icall_stub", {0xFF, 0xE0});
        const uint32_t ptrIdx = addAbs64DataRef("__guard_dispatch_icall_fptr", 8, dispatchIdx);
        addImportAlias("__guard_dispatch_icall_fptr", ptrIdx);
    }

    if (dynamicSyms.count("vm_trap")) {
        const size_t off = textSec.data.size();
        textSec.data.insert(textSec.data.end(), {0xE9, 0x00, 0x00, 0x00, 0x00});

        ObjSymbol vmTrap;
        vmTrap.name = "vm_trap";
        vmTrap.binding = ObjSymbol::Global;
        vmTrap.sectionIndex = 1;
        vmTrap.offset = off;
        obj.symbols.push_back(std::move(vmTrap));

        ObjSymbol target;
        target.name = haveVmTrapDefault ? "vm_trap_default" : "rt_abort";
        target.binding = ObjSymbol::Undefined;
        const uint32_t targetIdx = static_cast<uint32_t>(obj.symbols.size());
        obj.symbols.push_back(std::move(target));

        ObjReloc reloc;
        reloc.offset = off + 1;
        reloc.type = coff_x64::kRel32;
        reloc.symIndex = targetIdx;
        reloc.addend = 0;
        textSec.relocs.push_back(reloc);
    }

    if (!textSec.data.empty())
        obj.sections.push_back(std::move(textSec));
    if (!dataSec.data.empty()) {
        if (obj.sections.size() == 1)
            obj.sections.push_back(ObjSection{});
        obj.sections.push_back(std::move(dataSec));
    }
    return obj;
}

ObjFile generateWindowsArm64Helpers(const std::unordered_set<std::string> &dynamicSyms,
                                    bool haveVmTrapDefault,
                                    bool needTlsIndex) {
    ObjFile obj;
    obj.name = "<winarm64-helpers>";
    obj.format = ObjFileFormat::COFF;
    obj.is64bit = true;
    obj.isLittleEndian = true;
    obj.machine = 0xAA64;
    obj.sections.push_back(ObjSection{});
    obj.symbols.push_back(ObjSymbol{});

    ObjSection textSec;
    textSec.name = ".text";
    textSec.executable = true;
    textSec.alloc = true;
    textSec.alignment = 16;

    ObjSection dataSec;
    dataSec.name = ".data";
    dataSec.writable = true;
    dataSec.alloc = true;
    dataSec.alignment = 8;

    auto needsHelper = [&](const std::string &name) {
        return dynamicSyms.count(name) || dynamicSyms.count("__imp_" + name);
    };

    auto addTextFn = [&](const std::string &name, const std::vector<uint32_t> &insns) {
        const size_t off = textSec.data.size();
        for (uint32_t insn : insns)
            appendArm64Insn(textSec.data, insn);

        ObjSymbol sym;
        sym.name = name;
        sym.binding = ObjSymbol::Global;
        sym.sectionIndex = 1;
        sym.offset = off;
        obj.symbols.push_back(std::move(sym));
        return static_cast<uint32_t>(obj.symbols.size() - 1);
    };

    auto addRetFn = [&](const std::string &name) {
        return addTextFn(name, {0xD65F03C0U}); // ret
    };

    auto addRetImmFn = [&](const std::string &name, uint16_t imm) {
        return addTextFn(name, {0xD2800000U | (static_cast<uint32_t>(imm) << 5), 0xD65F03C0U});
    };

    auto addData = [&](const std::string &name, const std::vector<uint8_t> &bytes, uint32_t align) {
        while ((dataSec.data.size() % align) != 0)
            dataSec.data.push_back(0);
        const size_t off = dataSec.data.size();
        dataSec.data.insert(dataSec.data.end(), bytes.begin(), bytes.end());
        ObjSymbol sym;
        sym.name = name;
        sym.binding = ObjSymbol::Global;
        sym.sectionIndex = 2;
        sym.offset = off;
        obj.symbols.push_back(std::move(sym));
        return static_cast<uint32_t>(obj.symbols.size() - 1);
    };

    auto addAbs64DataRef = [&](const std::string &name, uint32_t align, uint32_t targetSymIdx) {
        while ((dataSec.data.size() % align) != 0)
            dataSec.data.push_back(0);
        const size_t off = dataSec.data.size();
        dataSec.data.resize(off + 8, 0);
        ObjSymbol sym;
        sym.name = name;
        sym.binding = ObjSymbol::Global;
        sym.sectionIndex = 2;
        sym.offset = off;
        obj.symbols.push_back(std::move(sym));

        ObjReloc reloc;
        reloc.offset = off;
        reloc.type = coff_a64::kAddr64;
        reloc.symIndex = targetSymIdx;
        reloc.addend = 0;
        dataSec.relocs.push_back(reloc);
        return static_cast<uint32_t>(obj.symbols.size() - 1);
    };

    auto addImportAlias = [&](const std::string &name, uint32_t targetSymIdx) {
        if (dynamicSyms.count("__imp_" + name))
            addAbs64DataRef("__imp_" + name, 8, targetSymIdx);
    };

    if (needsHelper("_fltused")) {
        const uint32_t idx = addData("_fltused", {1, 0, 0, 0}, 4);
        addImportAlias("_fltused", idx);
    }
    if (needsHelper("__security_cookie")) {
        const uint32_t idx =
            addData("__security_cookie", {0x32, 0xA2, 0xDF, 0x2D, 0x99, 0x2B, 0x00, 0x00}, 8);
        addImportAlias("__security_cookie", idx);
    }
    if (needsHelper("__security_cookie_complement")) {
        const uint32_t idx = addData(
            "__security_cookie_complement", {0xCD, 0x5D, 0x20, 0xD2, 0x66, 0xD4, 0xFF, 0xFF}, 8);
        addImportAlias("__security_cookie_complement", idx);
    }
    if (needTlsIndex || needsHelper("_tls_index")) {
        const uint32_t idx = addData("_tls_index", {0, 0, 0, 0}, 4);
        addImportAlias("_tls_index", idx);
    }
    if (needsHelper("_is_c_termination_complete")) {
        const uint32_t idx = addData("_is_c_termination_complete", {0, 0, 0, 0}, 4);
        addImportAlias("_is_c_termination_complete", idx);
    }
    if (needsHelper("?_OptionsStorage@?1??__local_stdio_printf_options@@9@9")) {
        const uint32_t idx = addData(
            "?_OptionsStorage@?1??__local_stdio_printf_options@@9@9", {0, 0, 0, 0, 0, 0, 0, 0}, 8);
        addImportAlias("?_OptionsStorage@?1??__local_stdio_printf_options@@9@9", idx);
    }
    if (needsHelper("?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9")) {
        const uint32_t idx = addData(
            "?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9", {0, 0, 0, 0, 0, 0, 0, 0}, 8);
        addImportAlias("?_OptionsStorage@?1??__local_stdio_scanf_options@@9@9", idx);
    }

    if (needsHelper("__security_check_cookie")) {
        const uint32_t idx = addRetFn("__security_check_cookie");
        addImportAlias("__security_check_cookie", idx);
    }
    if (needsHelper("__security_init_cookie")) {
        const uint32_t idx = addRetFn("__security_init_cookie");
        addImportAlias("__security_init_cookie", idx);
    }
    if (needsHelper("__GSHandlerCheck")) {
        const uint32_t idx = addRetFn("__GSHandlerCheck");
        addImportAlias("__GSHandlerCheck", idx);
    }
    if (needsHelper("_RTC_CheckStackVars")) {
        const uint32_t idx = addRetFn("_RTC_CheckStackVars");
        addImportAlias("_RTC_CheckStackVars", idx);
    }
    if (needsHelper("_RTC_InitBase")) {
        const uint32_t idx = addRetImmFn("_RTC_InitBase", 0);
        addImportAlias("_RTC_InitBase", idx);
    }
    if (needsHelper("_RTC_Shutdown")) {
        const uint32_t idx = addRetFn("_RTC_Shutdown");
        addImportAlias("_RTC_Shutdown", idx);
    }
    if (needsHelper("_RTC_UninitUse")) {
        const uint32_t idx = addRetFn("_RTC_UninitUse");
        addImportAlias("_RTC_UninitUse", idx);
    }
    if (needsHelper("IID_ID3D11Texture2D")) {
        const uint32_t idx = addData("IID_ID3D11Texture2D",
                                     {0xF2, 0xAA, 0x15, 0x6F, 0x08, 0xD2, 0x89, 0x4E,
                                      0x9A, 0xB4, 0x48, 0x95, 0x35, 0xD3, 0x4F, 0x9C},
                                     4);
        addImportAlias("IID_ID3D11Texture2D", idx);
    }
    if (needsHelper("__report_rangecheckfailure")) {
        const uint32_t idx = addTextFn("__report_rangecheckfailure", {0xD4200000U, 0xD65F03C0U});
        addImportAlias("__report_rangecheckfailure", idx);
    }
    if (needsHelper("__chkstk")) {
        const uint32_t idx = addRetFn("__chkstk");
        addImportAlias("__chkstk", idx);
    }
    if (needsHelper("rt_audio_shutdown")) {
        const uint32_t idx = addRetFn("rt_audio_shutdown");
        addImportAlias("rt_audio_shutdown", idx);
    }
    if (needsHelper("__vcrt_initialize")) {
        const uint32_t idx = addRetImmFn("__vcrt_initialize", 1);
        addImportAlias("__vcrt_initialize", idx);
    }
    if (needsHelper("__vcrt_thread_attach")) {
        const uint32_t idx = addRetImmFn("__vcrt_thread_attach", 1);
        addImportAlias("__vcrt_thread_attach", idx);
    }
    if (needsHelper("__vcrt_thread_detach")) {
        const uint32_t idx = addRetImmFn("__vcrt_thread_detach", 1);
        addImportAlias("__vcrt_thread_detach", idx);
    }
    if (needsHelper("__vcrt_uninitialize")) {
        const uint32_t idx = addRetImmFn("__vcrt_uninitialize", 1);
        addImportAlias("__vcrt_uninitialize", idx);
    }
    if (needsHelper("__vcrt_uninitialize_critical")) {
        const uint32_t idx = addRetFn("__vcrt_uninitialize_critical");
        addImportAlias("__vcrt_uninitialize_critical", idx);
    }
    if (needsHelper("__acrt_initialize")) {
        const uint32_t idx = addRetImmFn("__acrt_initialize", 1);
        addImportAlias("__acrt_initialize", idx);
    }
    if (needsHelper("__acrt_thread_attach")) {
        const uint32_t idx = addRetImmFn("__acrt_thread_attach", 1);
        addImportAlias("__acrt_thread_attach", idx);
    }
    if (needsHelper("__acrt_thread_detach")) {
        const uint32_t idx = addRetImmFn("__acrt_thread_detach", 1);
        addImportAlias("__acrt_thread_detach", idx);
    }
    if (needsHelper("__acrt_uninitialize")) {
        const uint32_t idx = addRetImmFn("__acrt_uninitialize", 1);
        addImportAlias("__acrt_uninitialize", idx);
    }
    if (needsHelper("__acrt_uninitialize_critical")) {
        const uint32_t idx = addRetFn("__acrt_uninitialize_critical");
        addImportAlias("__acrt_uninitialize_critical", idx);
    }
    if (needsHelper("__isa_available_init")) {
        const uint32_t idx = addRetFn("__isa_available_init");
        addImportAlias("__isa_available_init", idx);
    }
    if (needsHelper("__scrt_exe_initialize_mta")) {
        const uint32_t idx = addRetImmFn("__scrt_exe_initialize_mta", 0);
        addImportAlias("__scrt_exe_initialize_mta", idx);
    }

    if (needsHelper("__guard_dispatch_icall_fptr")) {
        const uint32_t dispatchIdx = addTextFn("__guard_dispatch_icall_stub", {0xD61F0200U});
        const uint32_t ptrIdx = addAbs64DataRef("__guard_dispatch_icall_fptr", 8, dispatchIdx);
        addImportAlias("__guard_dispatch_icall_fptr", ptrIdx);
    }

    if (dynamicSyms.count("vm_trap")) {
        const size_t off = textSec.data.size();
        appendArm64Insn(textSec.data, 0x14000000U); // b target

        ObjSymbol vmTrap;
        vmTrap.name = "vm_trap";
        vmTrap.binding = ObjSymbol::Global;
        vmTrap.sectionIndex = 1;
        vmTrap.offset = off;
        obj.symbols.push_back(std::move(vmTrap));

        ObjSymbol target;
        target.name = haveVmTrapDefault ? "vm_trap_default" : "rt_abort";
        target.binding = ObjSymbol::Undefined;
        const uint32_t targetIdx = static_cast<uint32_t>(obj.symbols.size());
        obj.symbols.push_back(std::move(target));

        ObjReloc reloc;
        reloc.offset = off;
        reloc.type = coff_a64::kBranch26;
        reloc.symIndex = targetIdx;
        reloc.addend = 0;
        textSec.relocs.push_back(reloc);
    }

    if (!textSec.data.empty())
        obj.sections.push_back(std::move(textSec));
    if (!dataSec.data.empty()) {
        if (obj.sections.size() == 1)
            obj.sections.push_back(ObjSection{});
        obj.sections.push_back(std::move(dataSec));
    }
    return obj;
}

} // namespace

/// @brief Run the full native link pipeline: parse objects → resolve symbols →
///        merge sections → apply relocations → write executable.
/// @details Supports ELF (Linux), Mach-O (macOS), and PE (Windows). The pipeline
///          reads the user's .o and runtime .a archives, resolves all symbols
///          (including dynamic stubs on macOS), dead-strips unreachable code,
///          performs ICF, inserts branch trampolines, applies relocations, and
///          writes the final executable. Zero external tool dependencies.
int nativeLink(const NativeLinkerOptions &opts, std::ostream & /*out*/, std::ostream &err) {
    // Step 1: Read the user's object file.
    ObjFile userObj;
    if (!readObjFile(opts.objPath, userObj, err)) {
        err << "error: failed to read object file '" << opts.objPath << "'\n";
        return 1;
    }

    // Step 1b: Read extra object files (e.g., asset blob).
    std::vector<ObjFile> extraObjects;
    for (const auto &extraPath : opts.extraObjPaths) {
        ObjFile extraObj;
        if (!readObjFile(extraPath, extraObj, err)) {
            err << "warning: failed to read extra object '" << extraPath << "', skipping\n";
            continue;
        }
        extraObjects.push_back(std::move(extraObj));
    }

    // Step 2: Read all archive files.
    std::vector<Archive> archives;
    for (const auto &arPath : opts.archivePaths) {
        Archive ar;
        if (!readArchive(arPath, ar, err)) {
            err << "warning: failed to read archive '" << arPath << "', skipping\n";
            continue;
        }
        archives.push_back(std::move(ar));
    }

    // Step 3: Symbol resolution (iterative archive extraction).
    std::vector<ObjFile> initialObjects = {userObj};
    for (auto &extra : extraObjects)
        initialObjects.push_back(std::move(extra));
    if (!opts.entrySymbol.empty())
        initialObjects.push_back(makeUndefinedRootObject(userObj, opts.entrySymbol));
    std::unordered_map<std::string, GlobalSymEntry> globalSyms;
    std::vector<ObjFile> allObjects;
    std::unordered_set<std::string> dynamicSyms;
    const bool debugWindowsRuntime =
        opts.platform == LinkPlatform::Windows &&
        opts.windowsDebugRuntime.value_or(usesDebugWindowsRuntime(opts.archivePaths));

    if (!resolveSymbols(
            initialObjects, archives, globalSyms, allObjects, dynamicSyms, err, opts.platform)) {
        err << "error: symbol resolution failed\n";
        return 1;
    }
    // Step 3.5a: Generate ObjC selector stubs (macOS — objc_msgSend$selector symbols).
    // Must come before dynamic stubs since it moves symbols from dynamicSyms and
    // ensures objc_msgSend itself is in the dynamic set.
    if (opts.arch == LinkArch::AArch64 && opts.platform == LinkPlatform::macOS) {
        ObjFile objcStubs = generateObjcSelectorStubsAArch64(dynamicSyms);
        if (!objcStubs.sections.empty()) {
            const size_t objcIdx = allObjects.size();
            allObjects.push_back(std::move(objcStubs));

            const auto &stubs = allObjects[objcIdx];
            for (size_t i = 1; i < stubs.symbols.size(); ++i) {
                const auto &sym = stubs.symbols[i];
                if (sym.binding == ObjSymbol::Local || sym.binding == ObjSymbol::Undefined)
                    continue;
                GlobalSymEntry e;
                e.name = sym.name;
                e.binding = GlobalSymEntry::Global;
                e.objIndex = objcIdx;
                e.secIndex = sym.sectionIndex;
                e.offset = sym.offset;
                globalSyms[sym.name] = std::move(e);
            }
        }
    }

    // Step 3.5b: Generate dynamic symbol stubs (macOS/ELF — needed for shared library imports).
    std::vector<DllImport> peImports;
    std::unordered_map<std::string, uint32_t> peImportSlotRvas;
    if (opts.platform == LinkPlatform::Windows) {
        dynamicSyms.erase("__ImageBase");
        const bool haveVmTrapDefault = globalSyms.find("vm_trap_default") != globalSyms.end();
        const bool needTlsIndex =
            std::any_of(allObjects.begin(), allObjects.end(), [](const ObjFile &obj) {
                return std::any_of(
                    obj.sections.begin(), obj.sections.end(), [](const ObjSection &sec) {
                        return sec.alloc && sec.tls && !sec.data.empty();
                    });
            });

        if (opts.arch == LinkArch::X86_64 || opts.arch == LinkArch::AArch64) {
            ObjFile helperObj =
                (opts.arch == LinkArch::AArch64)
                    ? generateWindowsArm64Helpers(dynamicSyms, haveVmTrapDefault, needTlsIndex)
                    : generateWindowsX64Helpers(dynamicSyms, haveVmTrapDefault, needTlsIndex);
            if (!helperObj.sections.empty()) {
                const size_t helperIdx = allObjects.size();
                allObjects.push_back(std::move(helperObj));
                registerSyntheticSymbols(allObjects[helperIdx], helperIdx, globalSyms);
                for (const auto &sym : allObjects[helperIdx].symbols) {
                    if (sym.binding == ObjSymbol::Local || sym.binding == ObjSymbol::Undefined ||
                        sym.name.empty())
                        continue;
                    dynamicSyms.erase(sym.name);
                }
            }
        }

        WindowsImportPlan importPlan;
        if (!generateWindowsImports(
                opts.arch, dynamicSyms, debugWindowsRuntime, importPlan, err)) {
            return 1;
        }
        peImports = importPlan.imports;
        if (!importPlan.obj.sections.empty()) {
            const size_t importIdx = allObjects.size();
            allObjects.push_back(std::move(importPlan.obj));
            registerSyntheticSymbols(allObjects[importIdx], importIdx, globalSyms);
            for (const auto &imp : peImports) {
                for (const auto &fn : imp.functions) {
                    dynamicSyms.erase(fn);
                    dynamicSyms.erase("__imp_" + fn);
                }
            }
        }
    }

    const bool supportsDynamicStubs =
        (opts.platform == LinkPlatform::macOS && opts.arch == LinkArch::AArch64) ||
        (opts.platform == LinkPlatform::Linux && opts.arch == LinkArch::X86_64);
    if (!dynamicSyms.empty() && supportsDynamicStubs) {
        ObjFile stubObj =
            (opts.platform == LinkPlatform::Linux && opts.arch == LinkArch::X86_64)
                ? generateDynStubsX8664(dynamicSyms)
                : generateDynStubsAArch64(dynamicSyms);
        const size_t stubObjIdx = allObjects.size();
        allObjects.push_back(std::move(stubObj));

        // Manually register stub and GOT symbols in globalSyms.
        // This overrides Dynamic entries with Global entries pointing to stubs.
        const auto &stubs = allObjects[stubObjIdx];
        for (size_t i = 1; i < stubs.symbols.size(); ++i) {
            const auto &sym = stubs.symbols[i];
            if (sym.binding == ObjSymbol::Local)
                continue;

            GlobalSymEntry e;
            e.name = sym.name;
            e.binding = GlobalSymEntry::Global;
            e.objIndex = stubObjIdx;
            e.secIndex = sym.sectionIndex;
            e.offset = sym.offset;
            globalSyms[sym.name] = std::move(e);
        }
    }

    if (opts.platform == LinkPlatform::Windows) {
        dynamicSyms.erase("__ImageBase");
        dynamicSyms.erase("vm_trap");
    }

    if (!dynamicSyms.empty() && !supportsDynamicStubs) {
        std::vector<std::string> unsupported(dynamicSyms.begin(), dynamicSyms.end());
        std::sort(unsupported.begin(), unsupported.end());

        err << "error: native linker does not support dynamic imports on "
            << platformName(opts.platform) << ' ' << archName(opts.arch) << "\n";
        err << "error: supported dynamic-import targets are Windows x86_64, Windows AArch64, "
               "macOS AArch64, and Linux x86_64\n";
        err << "error: unresolved dynamic symbols:";
        for (const auto &sym : unsupported)
            err << ' ' << sym;
        err << "\n";
        return 1;
    }

    // Step 3.5c: Dead-strip unused sections from all non-synthetic input
    // objects, rooting only entry points and always-live metadata.
    deadStrip(allObjects, initialObjects.size(), globalSyms, opts.entrySymbol, err);

    // Step 3.5d: Deduplicate identical rodata strings across object files.
    deduplicateStrings(allObjects, globalSyms);

    // Step 3.5d2: Fold identical .text sections (Identical Code Folding).
    foldIdenticalCode(allObjects, globalSyms);

    // Step 3.5e: Remove global symbols that reference stripped (empty) sections.
    // After dead stripping, sections with cleared data are skipped by the merger,
    // so symbols pointing to them would resolve to invalid addresses.
    {
        std::vector<std::string> deadSyms;
        for (const auto &[name, entry] : globalSyms) {
            if (entry.binding == GlobalSymEntry::Dynamic)
                continue; // Dynamic symbols have no section reference.
            if (entry.objIndex < allObjects.size() &&
                entry.secIndex < allObjects[entry.objIndex].sections.size() &&
                allObjects[entry.objIndex].sections[entry.secIndex].data.empty()) {
                deadSyms.push_back(name);
            }
        }
        for (const auto &name : deadSyms)
            globalSyms.erase(name);
    }

    // Step 4: Merge sections and compute layout.
    LinkLayout layout;
    layout.globalSyms = std::move(globalSyms);
    if (!mergeSections(allObjects, opts.platform, opts.arch, layout, err)) {
        err << "error: section merging failed\n";
        return 1;
    }

    // Step 5: Insert branch trampolines for out-of-range AArch64 B/BL instructions.
    if (!insertBranchTrampolines(allObjects, layout, opts.arch, opts.platform, err)) {
        err << "error: branch trampoline insertion failed\n";
        return 1;
    }

    // Step 6: Apply relocations. This also resolves final symbol addresses.
    if (!applyRelocations(allObjects, layout, dynamicSyms, opts.platform, opts.arch, err)) {
        err << "error: relocation application failed\n";
        return 1;
    }

    // Step 6.25: Resolve the final entry point after symbol addresses are known.
    {
        auto it = findWithMachoFallback(layout.globalSyms, opts.entrySymbol);
        if (it != layout.globalSyms.end())
            layout.entryAddr = it->second.resolvedAddr;
    }

    // Step 6.5: Build GOT entry table for the executable writer (needed for bind opcodes).
    for (const auto &[name, entry] : layout.globalSyms) {
        if (name.size() > 6 && name.substr(0, 6) == "__got_") {
            GotEntry ge;
            ge.symbolName = name.substr(6); // Remove "__got_" prefix → original symbol name.
            ge.gotAddr = entry.resolvedAddr;
            layout.gotEntries.push_back(std::move(ge));
        }
    }
    std::sort(layout.gotEntries.begin(),
              layout.gotEntries.end(),
              [](const GotEntry &a, const GotEntry &b) { return a.symbolName < b.symbolName; });

    // Step 7: Write executable.
    bool writeOk = false;
    switch (opts.platform) {
        case LinkPlatform::Linux: {
            LinuxImportPlan importPlan;
            if (!planLinuxImports(dynamicSyms, importPlan, err))
                return 1;
            writeOk = writeElfExe(opts.exePath,
                                  layout,
                                  opts.arch,
                                  importPlan.neededLibs,
                                  dynamicSyms,
                                  opts.stackSize,
                                  opts.entrySymbol == "main",
                                  err);
            break;
        }
        case LinkPlatform::macOS: {
            MacImportPlan importPlan;
            if (!planMacImports(dynamicSyms, importPlan, err))
                return 1;

            writeOk = writeMachOExe(opts.exePath,
                                    layout,
                                    opts.arch,
                                    importPlan.dylibs,
                                    dynamicSyms,
                                    importPlan.symOrdinals,
                                    opts.stackSize,
                                    err);
            break;
        }
        case LinkPlatform::Windows: {
            if (peImports.empty())
                peImports.push_back({"kernel32.dll", {"ExitProcess"}, {}});
            for (const auto &imp : peImports) {
                for (const auto &fn : imp.functions) {
                    auto it = layout.globalSyms.find("__imp_" + fn);
                    if (it != layout.globalSyms.end())
                        peImportSlotRvas[fn] =
                            static_cast<uint32_t>(it->second.resolvedAddr - 0x140000000ULL);
                }
            }
            const bool emitStartupStub = opts.entrySymbol == "main";
            writeOk = writePeExe(opts.exePath,
                                 layout,
                                 opts.arch,
                                 peImports,
                                 peImportSlotRvas,
                                 emitStartupStub,
                                 opts.stackSize,
                                 err);
            break;
        }
    }

    if (!writeOk) {
        err << "error: failed to write executable '" << opts.exePath << "'\n";
        return 1;
    }

    return 0;
}

} // namespace viper::codegen::linker
