//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/tests/runtime/RTIdeWorkspaceTests.cpp
// Purpose: Tests for IDE workspace, asset resolver, manifest, and edit helpers.
// Key invariants:
//   - Runtime workspace helpers return structured maps/sequences with stable keys.
//   - Temporary files are isolated under per-test directories and removed on success.
// Ownership/Lifetime:
//   - Runtime strings created by tests are unref'd by the creating test.
//   - Runtime map/sequence objects are owned by the runtime test process.
// Links: src/runtime/io/rt_ide_primitives.cpp, src/runtime/io/rt_ide_primitives.h
//
//===----------------------------------------------------------------------===//

#include "rt_ide_primitives.h"
#include "rt_watcher.h"

#include "rt_internal.h"
#include "rt_map.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

namespace fs = std::filesystem;

static rt_string s(const std::string &value) {
    return rt_string_from_bytes(value.data(), value.size());
}

static std::string get_str(void *map, const char *key) {
    rt_string value = rt_map_get_str(map, rt_const_cstr(key));
    std::string out(rt_string_cstr(value), (size_t)rt_str_len(value));
    rt_string_unref(value);
    return out;
}

static void write_file(const fs::path &path, const std::string &text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

static std::string read_file(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

static fs::path temp_root() {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path root = fs::temp_directory_path() / ("viper_ide_workspace_" + std::to_string(now));
    fs::create_directories(root);
    return root;
}

static bool seq_contains_relative(void *seq, const std::string &rel) {
    for (int64_t i = 0; i < rt_seq_len(seq); i++) {
        void *entry = rt_seq_get(seq, i);
        if (get_str(entry, "relativePath") == rel)
            return true;
    }
    return false;
}

static void test_file_index_and_ignore() {
    fs::path root = temp_root();
    write_file(root / ".gitignore", "*.tmp\n!keep.tmp\nignored/\n");
    write_file(root / "src/.gitignore", "*.gen.zia\n!keep.gen.zia\n\\#literal.zia\n");
    write_file(root / "src/main.zia", "module Main;\n");
    write_file(root / "src/generated.gen.zia", "skip");
    write_file(root / "src/keep.gen.zia", "keep");
    write_file(root / "src/#literal.zia", "skip");
    write_file(root / "assets/tiles.png", "png");
    write_file(root / "build/generated.zia", "skip");
    write_file(root / ".cache/generated.zia", "skip");
    write_file(root / ".claude/worktrees/agent/src/copied.zia", "skip");
    write_file(root / ".secret.zia", "skip");
    write_file(root / "ignored/hidden.zia", "skip");
    write_file(root / "keep.tmp", "keep");

    rt_string root_s = s(root.string());
    void *entries = rt_workspace_file_index_enumerate(
        root_s, rt_const_cstr(".zia,.png,.tmp"), rt_const_cstr(""), 1);
    void *status = rt_workspace_file_index_status(
        root_s, rt_const_cstr(".zia,.png,.tmp"), rt_const_cstr(""), 1);
    assert(rt_map_get_bool(status, rt_const_cstr("valid")) == 1);
    assert(rt_map_get_int(status, rt_const_cstr("entryCount")) == rt_seq_len(entries));
    assert(rt_map_get_bool(status, rt_const_cstr("truncated")) == 0);
    assert(rt_map_get_int(status, rt_const_cstr("maxEntries")) > 0);
    assert(rt_seq_len(rt_map_get(status, rt_const_cstr("diagnostics"))) == 0);

    void *first_page = rt_workspace_file_index_page(
        root_s, rt_const_cstr(".zia,.png,.tmp"), rt_const_cstr(""), 1, 0, 2);
    assert(rt_map_get_bool(first_page, rt_const_cstr("valid")) == 1);
    assert(rt_map_get_int(first_page, rt_const_cstr("offset")) == 0);
    assert(rt_map_get_int(first_page, rt_const_cstr("emitted")) == 2);
    assert(rt_map_get_bool(first_page, rt_const_cstr("done")) == 0);
    void *first_entries = rt_map_get(first_page, rt_const_cstr("entries"));
    assert(rt_seq_len(first_entries) == 2);
    int64_t next_offset = rt_map_get_int(first_page, rt_const_cstr("nextOffset"));
    assert(next_offset == 2);

    void *second_page = rt_workspace_file_index_page(
        root_s, rt_const_cstr(".zia,.png,.tmp"), rt_const_cstr(""), 1, next_offset, 64);
    assert(rt_map_get_bool(second_page, rt_const_cstr("valid")) == 1);
    assert(rt_map_get_int(second_page, rt_const_cstr("offset")) == next_offset);
    assert(rt_map_get_bool(second_page, rt_const_cstr("done")) == 1);
    void *second_entries = rt_map_get(second_page, rt_const_cstr("entries"));
    assert(rt_seq_len(first_entries) + rt_seq_len(second_entries) == rt_seq_len(entries));

    rt_string invalid_root = s((root / "missing-page").string());
    void *invalid_page = rt_workspace_file_index_page(
        invalid_root, rt_const_cstr(".zia"), rt_const_cstr(""), 0, 0, 10);
    assert(rt_map_get_bool(invalid_page, rt_const_cstr("valid")) == 0);
    assert(rt_seq_len(rt_map_get(invalid_page, rt_const_cstr("diagnostics"))) > 0);
    rt_string_unref(invalid_root);

    rt_string missing_root = s((root / "missing").string());
    void *missing_status = rt_workspace_file_index_status(
        missing_root, rt_const_cstr(".zia"), rt_const_cstr(""), 0);
    assert(rt_map_get_bool(missing_status, rt_const_cstr("valid")) == 0);
    assert(rt_seq_len(rt_map_get(missing_status, rt_const_cstr("diagnostics"))) > 0);
    rt_string_unref(missing_root);

    assert(seq_contains_relative(entries, "src/main.zia"));
    assert(seq_contains_relative(entries, "src/keep.gen.zia"));
    assert(!seq_contains_relative(entries, "src/generated.gen.zia"));
    assert(!seq_contains_relative(entries, "src/#literal.zia"));
    assert(seq_contains_relative(entries, "assets/tiles.png"));
    assert(seq_contains_relative(entries, "keep.tmp"));
    assert(!seq_contains_relative(entries, "build/generated.zia"));
    assert(!seq_contains_relative(entries, ".cache/generated.zia"));
    assert(!seq_contains_relative(entries, ".claude/worktrees/agent/src/copied.zia"));
    assert(!seq_contains_relative(entries, ".secret.zia"));
    assert(!seq_contains_relative(entries, "ignored/hidden.zia"));

    assert(rt_workspace_file_index_should_ignore(
               root_s, rt_const_cstr("tmp/cache.bin"), rt_const_cstr("tmp/")) == 1);
    assert(rt_workspace_file_index_should_ignore(
               root_s, rt_const_cstr("keep.tmp"), rt_const_cstr("*.tmp,!keep.tmp")) == 0);
    assert(rt_workspace_file_index_should_ignore(
               root_s, rt_const_cstr("#literal.zia"), rt_const_cstr("\\#literal.zia")) == 1);
    assert(rt_workspace_file_index_should_ignore(
               root_s, rt_const_cstr("!literal.zia"), rt_const_cstr("\\!literal.zia")) == 1);
    rt_string_unref(root_s);
    fs::remove_all(root);
}

static void test_asset_resolver_and_manifest() {
    fs::path root = temp_root();
    write_file(root / "scenes/level.json", "{}");
    write_file(root / "scenes/local.png", "local");
    write_file(root / "assets/tiles.png", "tiles");

    rt_string scene = s((root / "scenes/level.json").string());
    rt_string project = s(root.string());
    void *scene_relative = rt_asset_resolver_resolve(
        scene, project, rt_const_cstr("assets"), rt_const_cstr("local.png"));
    assert(rt_map_get_bool(scene_relative, rt_const_cstr("found")) == 1);
    assert(get_str(scene_relative, "source") == "scene");

    void *asset_root = rt_asset_resolver_resolve(
        scene, project, rt_const_cstr("assets"), rt_const_cstr("tiles.png"));
    assert(rt_map_get_bool(asset_root, rt_const_cstr("found")) == 1);
    assert(get_str(asset_root, "source") == "assetRoot");

    void *missing = rt_asset_resolver_resolve(
        scene, project, rt_const_cstr("assets"), rt_const_cstr("missing.png"));
    assert(rt_map_get_bool(missing, rt_const_cstr("found")) == 0);
    assert(get_str(missing, "diagnostic").find("missing.png") != std::string::npos);

    rt_string manifest_text = rt_const_cstr("project Demo\n"
                                            "lang zia\n"
                                            "entry src/main.zia\n"
                                            "sources src\n"
                                            "exclude build\n"
                                            "asset-root assets\n"
                                            "default-scene scenes/level.json\n"
                                            "[run.play]\n"
                                            "entry src/main.zia\n"
                                            "args --dev, --scene=one\n");
    void *manifest = rt_project_manifest_parse_text(manifest_text);
    assert(rt_map_get_bool(manifest, rt_const_cstr("valid")) == 1);
    assert(get_str(manifest, "name") == "Demo");
    assert(get_str(manifest, "entry") == "src/main.zia");
    assert(rt_seq_len(rt_map_get(manifest, rt_const_cstr("runConfigs"))) == 1);

    rt_string_unref(scene);
    rt_string_unref(project);
    fs::remove_all(root);
}

static void test_workspace_watcher_batch() {
    fs::path root = temp_root();
    rt_string root_s = s(root.string());
    void *watcher = rt_watcher_new(root_s);
    rt_watcher_start(watcher);

    void *empty = rt_workspace_watcher_poll_batch(watcher, 4);
    assert(rt_seq_len(empty) == 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    write_file(root / "created.zia", "module Created;\n");

    void *batch = nullptr;
    for (int attempt = 0; attempt < 30; attempt++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        batch = rt_workspace_watcher_poll_batch(watcher, 4);
        if (rt_seq_len(batch) > 0)
            break;
    }
    assert(batch != nullptr);
    assert(rt_seq_len(batch) > 0);
    void *event = rt_seq_get(batch, 0);
    assert(get_str(event, "typeName") != "none");
    assert(!get_str(event, "path").empty());
    assert(rt_map_get_int(event, rt_const_cstr("overflowCount")) == 0);

    rt_watcher_stop(watcher);
    rt_string_unref(root_s);
    fs::remove_all(root);
}

static void add_edit(void *seq,
                     const fs::path &file,
                     int64_t sl,
                     int64_t sc,
                     int64_t el,
                     int64_t ec,
                     const std::string &text,
                     int64_t expected_size = -1) {
    void *edit = rt_map_new();
    rt_string path_s = s(file.string());
    rt_string text_s = s(text);
    rt_map_set_str(edit, rt_const_cstr("file"), path_s);
    rt_map_set_int(edit, rt_const_cstr("startLine"), sl);
    rt_map_set_int(edit, rt_const_cstr("startColumn"), sc);
    rt_map_set_int(edit, rt_const_cstr("endLine"), el);
    rt_map_set_int(edit, rt_const_cstr("endColumn"), ec);
    rt_map_set_str(edit, rt_const_cstr("newText"), text_s);
    if (expected_size >= 0)
        rt_map_set_int(edit, rt_const_cstr("expectedSize"), expected_size);
    rt_seq_push(seq, edit);
    rt_string_unref(path_s);
    rt_string_unref(text_s);
}

static void test_workspace_edits() {
    fs::path root = temp_root();
    fs::path a = root / "a.zia";
    fs::path b = root / "b.zia";
    write_file(a, "one\ntwo\n");
    write_file(b, "alpha\nbeta\n");

    void *edits = rt_seq_new_owned();
    add_edit(edits, a, 2, 1, 2, 4, "TWO");
    add_edit(edits, b, 1, 1, 1, 6, "ALPHA");

    void *valid = rt_workspace_edit_validate(edits);
    assert(rt_map_get_bool(valid, rt_const_cstr("success")) == 1);
    void *applied = rt_workspace_edit_apply(edits);
    assert(rt_map_get_bool(applied, rt_const_cstr("success")) == 1);
    assert(read_file(a) == "one\nTWO\n");
    assert(read_file(b) == "ALPHA\nbeta\n");

    write_file(a, "abcdef\n");
    void *overlap = rt_seq_new_owned();
    add_edit(overlap, a, 1, 1, 1, 4, "x");
    add_edit(overlap, a, 1, 3, 1, 6, "y");
    void *rejected = rt_workspace_edit_validate(overlap);
    assert(rt_map_get_bool(rejected, rt_const_cstr("success")) == 0);

    rt_string root_s = s(root.string());
    write_file(a, "first\nsecond\n");
    void *rooted = rt_seq_new_owned();
    add_edit(rooted, fs::path("a.zia"), 1, 1, 1, 6, "FIRST", 13);
    void *rooted_valid = rt_workspace_edit_validate_in_root(rooted, root_s);
    assert(rt_map_get_bool(rooted_valid, rt_const_cstr("success")) == 1);
    void *rooted_applied = rt_workspace_edit_apply_in_root(rooted, root_s);
    assert(rt_map_get_bool(rooted_applied, rt_const_cstr("success")) == 1);
    assert(read_file(a) == "FIRST\nsecond\n");

    void *size_mismatch = rt_seq_new_owned();
    add_edit(size_mismatch, fs::path("a.zia"), 1, 1, 1, 6, "first", 999);
    void *size_rejected = rt_workspace_edit_validate_in_root(size_mismatch, root_s);
    assert(rt_map_get_bool(size_rejected, rt_const_cstr("success")) == 0);

    fs::path outside = root.parent_path() / (root.filename().string() + "_outside.zia");
    write_file(outside, "outside\n");
    void *escaping = rt_seq_new_owned();
    add_edit(escaping, outside, 1, 1, 1, 8, "ESCAPE");
    void *escaping_rejected = rt_workspace_edit_validate_in_root(escaping, root_s);
    assert(rt_map_get_bool(escaping_rejected, rt_const_cstr("success")) == 0);
    assert(read_file(outside) == "outside\n");

    rt_string_unref(root_s);
    fs::remove(outside);
    fs::remove_all(root);
}

int main() {
    test_file_index_and_ignore();
    test_asset_resolver_and_manifest();
    test_workspace_watcher_batch();
    test_workspace_edits();
    return 0;
}
