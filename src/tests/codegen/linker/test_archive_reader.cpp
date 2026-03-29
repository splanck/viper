//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/codegen/linker/test_archive_reader.cpp
// Purpose: Regression coverage for COFF archive parsing on Windows.
//
//===----------------------------------------------------------------------===//

#include "codegen/common/LinkerSupport.hpp"
#include "codegen/common/RuntimeComponents.hpp"
#include "codegen/common/linker/ArchiveReader.hpp"
#include "codegen/common/linker/ObjFileReader.hpp"
#include "codegen/common/linker/SymbolResolver.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <vector>

using namespace viper::codegen;
using namespace viper::codegen::common;
using namespace viper::codegen::linker;

static int gFail = 0;

static void check(bool cond, const char *msg, int line) {
    if (!cond) {
        std::cerr << "FAIL line " << line << ": " << msg << "\n";
        ++gFail;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)
#define ASSERT(cond)                                                                               \
    do {                                                                                           \
        check((cond), #cond, __LINE__);                                                            \
        if (!(cond))                                                                               \
            return EXIT_FAILURE;                                                                   \
    } while (0)

static ObjFile makeUndefinedCaller() {
    ObjFile obj;
    obj.name = "caller.obj";
    obj.format = ObjFileFormat::COFF;
    obj.is64bit = true;
    obj.isLittleEndian = true;
    obj.machine = 0x8664;

    obj.sections.push_back({});
    ObjSection text;
    text.name = ".text";
    text.executable = true;
    text.alloc = true;
    text.alignment = 16;
    text.data = {0xC3};
    obj.sections.push_back(std::move(text));

    obj.symbols.push_back({});

    ObjSymbol mainSym;
    mainSym.name = "main";
    mainSym.sectionIndex = 1;
    mainSym.binding = ObjSymbol::Global;
    obj.symbols.push_back(std::move(mainSym));

    ObjSymbol trap;
    trap.name = "rt_trap";
    trap.binding = ObjSymbol::Undefined;
    obj.symbols.push_back(std::move(trap));

    ObjSymbol init;
    init.name = "rt_init_stack_safety";
    init.binding = ObjSymbol::Undefined;
    obj.symbols.push_back(std::move(init));

    return obj;
}

static ObjFile makeWindowsBaseClosureCaller() {
    ObjFile obj;
    obj.name = "base-closure.obj";
    obj.format = ObjFileFormat::COFF;
    obj.is64bit = true;
    obj.isLittleEndian = true;
    obj.machine = 0x8664;

    obj.sections.push_back({});
    ObjSection text;
    text.name = ".text";
    text.executable = true;
    text.alloc = true;
    text.alignment = 16;
    text.data = {0xC3};
    obj.sections.push_back(std::move(text));

    obj.symbols.push_back({});

    ObjSymbol mainSym;
    mainSym.name = "main";
    mainSym.sectionIndex = 1;
    mainSym.binding = ObjSymbol::Global;
    obj.symbols.push_back(std::move(mainSym));

    for (const char *name : {"rt_trap",
                             "rt_init_stack_safety",
                             "rt_seq_with_capacity",
                             "rt_seq_len",
                             "rt_seq_get",
                             "rt_seq_push",
                             "rt_obj_new_i64",
                             "rt_obj_free",
                             "rt_type_registry_init",
                             "rt_type_registry_cleanup",
                             "rt_hash_ensure_seeded_",
                             "rt_siphash_k0_",
                             "rt_siphash_k1_",
                             "rt_siphash_seeded_",
                             "rt_file_channel_fd",
                             "rt_file_channel_get_eof",
                             "rt_file_channel_set_eof",
                             "rt_file_state_cleanup"}) {
        ObjSymbol sym;
        sym.name = name;
        sym.binding = ObjSymbol::Undefined;
        obj.symbols.push_back(std::move(sym));
    }

    return obj;
}

int main() {
#if !defined(_WIN32)
    std::cout << "SKIPPED (non-Windows)\n";
    return EXIT_SUCCESS;
#else
    const auto buildDir = findBuildDir();
    ASSERT(buildDir.has_value());

    const auto archivePath = runtimeArchivePath(*buildDir, archiveNameForComponent(RtComponent::Base));
    ASSERT(fileExists(archivePath));

    Archive ar;
    std::ostringstream err;
    ASSERT(readArchive(archivePath.string(), ar, err));
    CHECK(err.str().empty());

    CHECK(ar.symbolIndex.count("rt_trap") == 1);
    CHECK(ar.symbolIndex.count("rt_init_stack_safety") == 1);

    const auto trapIt = ar.symbolIndex.find("rt_trap");
    ASSERT(trapIt != ar.symbolIndex.end());
    ASSERT(trapIt->second < ar.members.size());

    ObjFile trapObj;
    const auto trapMemberBytes = extractMember(ar, ar.members[trapIt->second]);
    ASSERT(!trapMemberBytes.empty());
    ASSERT(readObjFile(trapMemberBytes.data(),
                       trapMemberBytes.size(),
                       ar.members[trapIt->second].name,
                       trapObj,
                       err));

    bool sawTrapDef = false;
    for (const auto &sym : trapObj.symbols) {
        if (sym.name == "rt_trap" && sym.binding != ObjSymbol::Undefined) {
            sawTrapDef = true;
            break;
        }
    }
    CHECK(sawTrapDef);

    std::vector<ObjFile> initialObjects = {makeUndefinedCaller()};
    std::vector<Archive> archives = {ar};
    std::unordered_map<std::string, GlobalSymEntry> globalSyms;
    std::vector<ObjFile> allObjects;
    std::unordered_set<std::string> dynamicSyms;
    std::ostringstream resolveErr;

    const bool resolved =
        resolveSymbols(initialObjects, archives, globalSyms, allObjects, dynamicSyms, resolveErr);
    if (!resolved)
        std::cerr << resolveErr.str();
    ASSERT(resolved);
    CHECK(dynamicSyms.count("rt_trap") == 0);
    CHECK(dynamicSyms.count("rt_init_stack_safety") == 0);
    CHECK(globalSyms.count("rt_trap") == 1);
    CHECK(globalSyms["rt_trap"].binding == GlobalSymEntry::Global);
    CHECK(globalSyms.count("rt_init_stack_safety") == 1);
    CHECK(globalSyms["rt_init_stack_safety"].binding == GlobalSymEntry::Global);

    auto loadArchive = [&](RtComponent comp) -> Archive {
        const auto path = runtimeArchivePath(*buildDir, archiveNameForComponent(comp));
        Archive lib;
        if (!fileExists(path)) {
            check(false, "fileExists(path)", __LINE__);
            return lib;
        }
        std::ostringstream loadErr;
        if (!readArchive(path.string(), lib, loadErr)) {
            std::cerr << loadErr.str();
            check(false, "readArchive(path.string(), lib, loadErr)", __LINE__);
            return lib;
        }
        check(loadErr.str().empty(), "loadErr.str().empty()", __LINE__);
        return lib;
    };

    Archive collections = loadArchive(RtComponent::Collections);
    Archive oop = loadArchive(RtComponent::Oop);
    Archive text = loadArchive(RtComponent::Text);
    Archive iofs = loadArchive(RtComponent::IoFs);

    CHECK(collections.symbolIndex.count("rt_seq_with_capacity") == 1);
    CHECK(oop.symbolIndex.count("rt_obj_new_i64") == 1);
    CHECK(text.symbolIndex.count("rt_hash_ensure_seeded_") == 1);
    CHECK(iofs.symbolIndex.count("rt_file_channel_fd") == 1);

    archives = {ar, collections, oop, text, iofs};
    initialObjects = {makeUndefinedCaller()};
    globalSyms.clear();
    allObjects.clear();
    dynamicSyms.clear();
    std::ostringstream transitiveErr;
    const bool transitiveResolved =
        resolveSymbols(initialObjects, archives, globalSyms, allObjects, dynamicSyms, transitiveErr);
    if (!transitiveResolved)
        std::cerr << transitiveErr.str();
    ASSERT(transitiveResolved);
    for (const char *name : {"rt_seq_with_capacity",
                             "rt_seq_len",
                             "rt_seq_get",
                             "rt_seq_push",
                             "rt_obj_new_i64",
                             "rt_obj_free",
                             "rt_type_registry_init",
                             "rt_type_registry_cleanup",
                             "rt_hash_ensure_seeded_",
                             "rt_siphash_k0_",
                             "rt_siphash_k1_",
                             "rt_siphash_seeded_",
                             "rt_file_channel_fd",
                             "rt_file_channel_get_eof",
                             "rt_file_channel_set_eof",
                             "rt_file_state_cleanup"}) {
        CHECK(dynamicSyms.count(name) == 0);
        CHECK(globalSyms.count(name) == 1);
        CHECK(globalSyms[name].binding == GlobalSymEntry::Global);
    }

    initialObjects = {makeWindowsBaseClosureCaller()};
    globalSyms.clear();
    allObjects.clear();
    dynamicSyms.clear();
    std::ostringstream closureErr;
    const bool closureResolved =
        resolveSymbols(initialObjects, archives, globalSyms, allObjects, dynamicSyms, closureErr);
    if (!closureResolved)
        std::cerr << closureErr.str();
    ASSERT(closureResolved);
    for (const char *name : {"rt_seq_with_capacity",
                             "rt_seq_len",
                             "rt_seq_get",
                             "rt_seq_push",
                             "rt_obj_new_i64",
                             "rt_obj_free",
                             "rt_type_registry_init",
                             "rt_type_registry_cleanup",
                             "rt_hash_ensure_seeded_",
                             "rt_siphash_k0_",
                             "rt_siphash_k1_",
                             "rt_siphash_seeded_",
                             "rt_file_channel_fd",
                             "rt_file_channel_get_eof",
                             "rt_file_channel_set_eof",
                             "rt_file_state_cleanup"}) {
        CHECK(dynamicSyms.count(name) == 0);
        CHECK(globalSyms.count(name) == 1);
        CHECK(globalSyms[name].binding == GlobalSymEntry::Global);
    }

    if (gFail == 0) {
        std::cout << "All ArchiveReader tests passed.\n";
        return EXIT_SUCCESS;
    }
    std::cerr << gFail << " ArchiveReader test(s) FAILED.\n";
    return EXIT_FAILURE;
#endif
}
