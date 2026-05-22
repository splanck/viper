//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/io/rt_ide_primitives.cpp
// Purpose: Workspace, asset, manifest, and transactional edit helpers used by
//          ViperIDE and editor-style tooling.
//
//===----------------------------------------------------------------------===//

#include "rt_ide_primitives.h"

#include "rt_asset.h"
#include "rt_box.h"
#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_watcher.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#include <sys/stat.h>
#else
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

namespace {

std::string toStd(rt_string s) {
    if (!s)
        return {};
    const char *data = rt_string_cstr(s);
    const int64_t len = rt_str_len(s);
    if (!data || len <= 0)
        return {};
    return std::string(data, static_cast<size_t>(len));
}

rt_string makeString(const std::string &value) {
    return rt_string_from_bytes(value.data(), value.size());
}

void releaseObject(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

void mapSetStr(void *map, const char *key, const std::string &value) {
    rt_string s = makeString(value);
    rt_map_set_str(map, rt_const_cstr(key), s);
    rt_string_unref(s);
}

void mapSetSeq(void *map, const char *key, void *seq) {
    rt_map_set(map, rt_const_cstr(key), seq);
}

void seqPushOwned(void *seq, void *obj) {
    rt_seq_push(seq, obj);
    releaseObject(obj);
}

std::string trim(std::string_view input) {
    size_t first = 0;
    while (first < input.size() && std::isspace(static_cast<unsigned char>(input[first])))
        first++;
    size_t last = input.size();
    while (last > first && std::isspace(static_cast<unsigned char>(input[last - 1])))
        last--;
    return std::string(input.substr(first, last - first));
}

std::string lower(std::string value) {
    for (char &ch : value)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}

std::string normalizeSlashes(std::string value) {
    for (char &ch : value) {
        if (ch == '\\')
            ch = '/';
    }
    while (value.rfind("./", 0) == 0)
        value.erase(0, 2);
    return value;
}

std::vector<std::string> splitList(const std::string &value) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    char quote = 0;
    for (char ch : value) {
        if ((ch == '"' || ch == '\'') && (!quoted || quote == ch)) {
            quoted = !quoted;
            quote = quoted ? ch : 0;
            continue;
        }
        if (!quoted && (ch == ',' || ch == ';' || ch == '\n')) {
            std::string item = trim(cur);
            if (!item.empty())
                out.push_back(item);
            cur.clear();
            continue;
        }
        cur.push_back(ch);
    }
    std::string item = trim(cur);
    if (!item.empty())
        out.push_back(item);
    return out;
}

bool wildcardMatchRec(std::string_view text, std::string_view pattern) {
    size_t ti = 0;
    size_t pi = 0;
    size_t star = std::string_view::npos;
    size_t match = 0;
    while (ti < text.size()) {
        if (pi < pattern.size() && pattern[pi] == '*') {
            star = pi++;
            match = ti;
        } else if (pi < pattern.size() &&
                   (pattern[pi] == '?' || pattern[pi] == text[ti])) {
            pi++;
            ti++;
        } else if (star != std::string_view::npos) {
            pi = star + 1;
            ti = ++match;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*')
        pi++;
    return pi == pattern.size();
}

bool pathGlobMatch(std::string_view text, std::string_view pattern) {
    std::vector<std::string_view> stackText{text};
    if (pattern.find("**") == std::string_view::npos)
        return wildcardMatchRec(text, pattern);

    // Small recursive matcher for ** over normalized slash paths.
    std::function<bool(size_t, size_t)> rec = [&](size_t ti, size_t pi) -> bool {
        if (pi == pattern.size())
            return ti == text.size();
        if (pi + 1 < pattern.size() && pattern[pi] == '*' && pattern[pi + 1] == '*') {
            size_t next = pi + 2;
            if (next < pattern.size() && pattern[next] == '/')
                next++;
            for (size_t i = ti; i <= text.size(); i++) {
                if (rec(i, next))
                    return true;
            }
            return false;
        }
        if (ti >= text.size())
            return false;
        if (pattern[pi] == '*') {
            for (size_t i = ti; i <= text.size() && (i == ti || text[i - 1] != '/'); i++) {
                if (rec(i, pi + 1))
                    return true;
            }
            return false;
        }
        if (pattern[pi] == '?' || pattern[pi] == text[ti])
            return rec(ti + 1, pi + 1);
        return false;
    };
    return rec(0, 0);
}

bool patternMatchesPath(std::string pattern, const std::string &relativePath, bool isDir) {
    pattern = normalizeSlashes(trim(pattern));
    if (pattern.empty())
        return false;
    bool dirOnly = !pattern.empty() && pattern.back() == '/';
    if (dirOnly)
        pattern.pop_back();
    if (pattern.rfind("/", 0) == 0)
        pattern.erase(0, 1);

    std::string rel = normalizeSlashes(relativePath);
    std::string relDir = isDir ? rel : fs::path(rel).parent_path().generic_string();
    std::string basename = fs::path(rel).filename().generic_string();

    if (dirOnly) {
        if (isDir && (pathGlobMatch(rel, pattern) || pathGlobMatch(basename, pattern)))
            return true;
        std::string prefix = pattern;
        if (!prefix.empty() && prefix.back() != '/')
            prefix.push_back('/');
        return rel.rfind(prefix, 0) == 0 || rel.find("/" + prefix) != std::string::npos;
    }

    if (pattern.find('/') == std::string::npos) {
        if (pathGlobMatch(basename, pattern))
            return true;
        std::stringstream ss(rel);
        std::string segment;
        while (std::getline(ss, segment, '/')) {
            if (pathGlobMatch(segment, pattern))
                return true;
        }
        return false;
    }
    return pathGlobMatch(rel, pattern);
}

std::vector<std::string> readGitignorePatterns(const fs::path &root) {
    std::vector<std::string> patterns;
    std::ifstream in(root / ".gitignore");
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#')
            continue;
        patterns.push_back(line);
    }
    return patterns;
}

bool shouldIgnorePath(const fs::path &root,
                      const std::string &relativePath,
                      bool isDir,
                      const std::vector<std::string> &extraPatterns,
                      bool includeGitignore) {
    static const char *hardExcludes[] = {
        ".git/", ".hg/", ".svn/", ".viper/", ".viper-cache/", "build/", "cmake-build-*/",
        "node_modules/", ".DS_Store"};

    std::string rel = normalizeSlashes(relativePath);
    for (const char *pattern : hardExcludes) {
        if (patternMatchesPath(pattern, rel, isDir))
            return true;
    }

    std::vector<std::string> patterns = extraPatterns;
    if (includeGitignore) {
        auto fromGitignore = readGitignorePatterns(root);
        patterns.insert(patterns.end(), fromGitignore.begin(), fromGitignore.end());
    }

    bool ignored = false;
    for (std::string pattern : patterns) {
        pattern = trim(pattern);
        if (pattern.empty() || pattern[0] == '#')
            continue;
        bool negated = pattern[0] == '!';
        if (negated)
            pattern.erase(0, 1);
        if (patternMatchesPath(pattern, rel, isDir))
            ignored = !negated;
    }
    return ignored;
}

int64_t stablePathId(const std::string &path) {
    uint64_t hash = 1469598103934665603ULL;
    for (unsigned char ch : path) {
        hash ^= ch;
        hash *= 1099511628211ULL;
    }
    return static_cast<int64_t>(hash & 0x7fffffffffffffffULL);
}

int64_t fileTimeSeconds(const fs::path &path) {
#ifdef _WIN32
    struct _stat64i32 st {};
    const std::wstring wide = path.wstring();
    if (_wstat64i32(wide.c_str(), &st) != 0)
        return -1;
#else
    struct stat st {};
    if (stat(path.c_str(), &st) != 0)
        return -1;
#endif
    return static_cast<int64_t>(st.st_mtime);
}

void *makeDiagnostic(const std::string &message,
                     const std::string &file = {},
                     int64_t line = 0,
                     const std::string &code = {}) {
    void *diag = rt_map_new();
    mapSetStr(diag, "message", message);
    mapSetStr(diag, "file", file);
    mapSetStr(diag, "code", code);
    rt_map_set_int(diag, rt_const_cstr("line"), line);
    return diag;
}

void pushDiagnostic(void *seq,
                    const std::string &message,
                    const std::string &file = {},
                    int64_t line = 0,
                    const std::string &code = {}) {
    void *diag = makeDiagnostic(message, file, line, code);
    seqPushOwned(seq, diag);
}

void *makeStringSeq(const std::vector<std::string> &items) {
    void *seq = rt_seq_new_owned();
    for (const auto &item : items) {
        rt_string s = makeString(item);
        rt_seq_push(seq, s);
        rt_string_unref(s);
    }
    return seq;
}

std::string mapGetString(void *map, const char *key) {
    rt_string value = rt_map_get_str(map, rt_const_cstr(key));
    std::string out = toStd(value);
    rt_string_unref(value);
    return out;
}

std::string eventTypeName(int64_t type) {
    switch (type) {
    case RT_WATCH_EVENT_CREATED:
        return "created";
    case RT_WATCH_EVENT_MODIFIED:
        return "modified";
    case RT_WATCH_EVENT_DELETED:
        return "deleted";
    case RT_WATCH_EVENT_RENAMED:
        return "renamed";
    case RT_WATCH_EVENT_OVERFLOW:
        return "overflow";
    default:
        return "none";
    }
}

void *newManifestMap() {
    void *map = rt_map_new();
    mapSetStr(map, "name", "");
    mapSetStr(map, "version", "0.0.0");
    mapSetStr(map, "language", "zia");
    mapSetStr(map, "entry", "");
    mapSetStr(map, "defaultScene", "");
    mapSetSeq(map, "sourceGlobs", makeStringSeq({"."}));
    mapSetSeq(map, "excludes", makeStringSeq({}));
    mapSetSeq(map, "assetRoots", makeStringSeq({}));
    mapSetSeq(map, "sceneRoots", makeStringSeq({}));
    mapSetSeq(map, "runConfigs", rt_seq_new_owned());
    mapSetSeq(map, "buildConfigs", rt_seq_new_owned());
    mapSetSeq(map, "diagnostics", rt_seq_new_owned());
    rt_map_set_bool(map, rt_const_cstr("valid"), 1);
    return map;
}

void replaceStringSeq(void *map, const char *key, const std::vector<std::string> &items) {
    void *seq = makeStringSeq(items);
    rt_map_set(map, rt_const_cstr(key), seq);
    releaseObject(seq);
}

void appendToStringSeqField(void *map, const char *key, const std::string &value) {
    void *seq = rt_map_get(map, rt_const_cstr(key));
    if (!seq) {
        seq = rt_seq_new_owned();
        rt_map_set(map, rt_const_cstr(key), seq);
        releaseObject(seq);
        seq = rt_map_get(map, rt_const_cstr(key));
    }
    rt_string s = makeString(value);
    rt_seq_push(seq, s);
    rt_string_unref(s);
}

void appendConfigMap(void *map, const char *key, void *config) {
    void *seq = rt_map_get(map, rt_const_cstr(key));
    if (!seq) {
        seq = rt_seq_new_owned();
        rt_map_set(map, rt_const_cstr(key), seq);
        releaseObject(seq);
        seq = rt_map_get(map, rt_const_cstr(key));
    }
    rt_seq_push(seq, config);
}

std::pair<std::string, std::string> splitDirectiveLine(const std::string &line) {
    size_t eq = line.find('=');
    size_t colon = line.find(':');
    size_t sep = std::min(eq == std::string::npos ? line.size() : eq,
                          colon == std::string::npos ? line.size() : colon);
    if (sep != line.size())
        return {trim(std::string_view(line).substr(0, sep)),
                trim(std::string_view(line).substr(sep + 1))};
    size_t ws = line.find_first_of(" \t");
    if (ws == std::string::npos)
        return {trim(line), ""};
    return {trim(std::string_view(line).substr(0, ws)),
            trim(std::string_view(line).substr(ws + 1))};
}

std::string manifestKey(std::string key) {
    key = lower(key);
    key.erase(std::remove_if(key.begin(), key.end(), [](char c) {
                  return c == '-' || c == '_' || c == '.';
              }),
              key.end());
    return key;
}

std::vector<std::string> readLines(const std::string &text) {
    std::vector<std::string> lines;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    if (!text.empty() && text.back() == '\n')
        lines.push_back("");
    return lines;
}

std::optional<size_t> offsetForLineColumn(const std::string &text,
                                          int64_t line,
                                          int64_t column) {
    if (line < 1 || column < 1)
        return std::nullopt;
    int64_t curLine = 1;
    int64_t curCol = 1;
    for (size_t i = 0; i <= text.size(); i++) {
        if (curLine == line && curCol == column)
            return i;
        if (i == text.size())
            break;
        if (text[i] == '\n') {
            curLine++;
            curCol = 1;
        } else {
            curCol++;
        }
    }
    return std::nullopt;
}

struct EditRecord {
    std::string file;
    int64_t startLine{0};
    int64_t startColumn{0};
    int64_t endLine{0};
    int64_t endColumn{0};
    std::string newText;
    int64_t expectedMtime{-1};
    size_t startOffset{0};
    size_t endOffset{0};
};

bool loadEditRecord(void *obj, EditRecord &out, void *diagnostics, int64_t index) {
    if (!obj || rt_obj_class_id(obj) != RT_MAP_CLASS_ID) {
        pushDiagnostic(diagnostics, "workspace edit entry is not a map", "", index, "edit.invalid");
        return false;
    }
    out.file = mapGetString(obj, "file");
    out.startLine = rt_map_get_int(obj, rt_const_cstr("startLine"));
    out.startColumn = rt_map_get_int(obj, rt_const_cstr("startColumn"));
    out.endLine = rt_map_get_int(obj, rt_const_cstr("endLine"));
    out.endColumn = rt_map_get_int(obj, rt_const_cstr("endColumn"));
    out.newText = mapGetString(obj, "newText");
    out.expectedMtime = rt_map_get_int_or(obj, rt_const_cstr("expectedMtime"), -1);
    if (out.file.empty()) {
        pushDiagnostic(diagnostics, "workspace edit missing file", "", index, "edit.file");
        return false;
    }
    if (out.startLine < 1 || out.startColumn < 1 || out.endLine < 1 || out.endColumn < 1) {
        pushDiagnostic(diagnostics, "workspace edit has invalid 1-based range",
                       out.file, index, "edit.range");
        return false;
    }
    return true;
}

bool validateEditRecords(std::vector<EditRecord> &records,
                         std::unordered_map<std::string, std::string> &contents,
                         void *diagnostics) {
    bool ok = true;
    for (auto &record : records) {
        if (!contents.count(record.file)) {
            std::ifstream in(record.file, std::ios::binary);
            if (!in) {
                pushDiagnostic(diagnostics, "cannot read edit target", record.file, 0, "edit.read");
                ok = false;
                continue;
            }
            std::ostringstream buffer;
            buffer << in.rdbuf();
            contents[record.file] = buffer.str();
        }
        if (record.expectedMtime >= 0 && fileTimeSeconds(record.file) != record.expectedMtime) {
            pushDiagnostic(diagnostics, "edit target changed since expectedMtime",
                           record.file, 0, "edit.version");
            ok = false;
            continue;
        }
        auto &text = contents[record.file];
        auto start = offsetForLineColumn(text, record.startLine, record.startColumn);
        auto end = offsetForLineColumn(text, record.endLine, record.endColumn);
        if (!start || !end || *start > *end) {
            pushDiagnostic(diagnostics, "workspace edit range is outside the file",
                           record.file, 0, "edit.range");
            ok = false;
            continue;
        }
        record.startOffset = *start;
        record.endOffset = *end;
    }

    std::map<std::string, std::vector<EditRecord *>> byFile;
    for (auto &record : records)
        byFile[record.file].push_back(&record);
    for (auto &[file, vec] : byFile) {
        std::sort(vec.begin(), vec.end(), [](const EditRecord *a, const EditRecord *b) {
            return a->startOffset < b->startOffset;
        });
        for (size_t i = 1; i < vec.size(); i++) {
            if (vec[i - 1]->endOffset > vec[i]->startOffset) {
                pushDiagnostic(diagnostics, "workspace edit ranges overlap", file, 0,
                               "edit.overlap");
                ok = false;
            }
        }
    }
    return ok;
}

} // namespace

extern "C" {

void *rt_workspace_file_index_enumerate(rt_string root_s,
                                        rt_string extensions_csv,
                                        rt_string excludes_csv,
                                        int8_t include_dirs) {
    void *out = rt_seq_new_owned();
    fs::path root = toStd(root_s);
    if (root.empty())
        return out;
    std::error_code ec;
    root = fs::absolute(root, ec).lexically_normal();
    if (ec || !fs::is_directory(root, ec))
        return out;

    std::set<std::string> extensions;
    for (std::string ext : splitList(toStd(extensions_csv))) {
        if (!ext.empty() && ext[0] != '.')
            ext.insert(ext.begin(), '.');
        extensions.insert(lower(ext));
    }
    const auto extraPatterns = splitList(toStd(excludes_csv));

    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    fs::recursive_directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code relEc;
        std::string rel = normalizeSlashes(fs::relative(it->path(), root, relEc).generic_string());
        if (relEc || rel.empty() || rel == ".")
            continue;
        bool isDir = it->is_directory(ec);
        if (shouldIgnorePath(root, rel, isDir, extraPatterns, true)) {
            if (isDir)
                it.disable_recursion_pending();
            continue;
        }
        if (isDir && !include_dirs)
            continue;
        if (!isDir && !extensions.empty()) {
            std::string ext = lower(it->path().extension().generic_string());
            if (!extensions.count(ext))
                continue;
        }

        void *entry = rt_map_new();
        const std::string path = fs::absolute(it->path(), ec).lexically_normal().string();
        mapSetStr(entry, "path", path);
        mapSetStr(entry, "relativePath", rel);
        mapSetStr(entry, "name", it->path().filename().generic_string());
        mapSetStr(entry, "extension", it->path().extension().generic_string());
        mapSetStr(entry, "kind", isDir ? "directory" : "file");
        rt_map_set_bool(entry, rt_const_cstr("isDirectory"), isDir ? 1 : 0);
        rt_map_set_int(entry, rt_const_cstr("id"), stablePathId(normalizeSlashes(path)));
        rt_map_set_int(entry, rt_const_cstr("size"),
                       (!isDir && !ec) ? static_cast<int64_t>(it->file_size(ec)) : 0);
        rt_map_set_int(entry, rt_const_cstr("modified"), fileTimeSeconds(it->path()));
        seqPushOwned(out, entry);
    }
    return out;
}

int8_t rt_workspace_file_index_should_ignore(rt_string root_s,
                                             rt_string relative_path,
                                             rt_string patterns) {
    fs::path root = toStd(root_s);
    if (root.empty())
        root = ".";
    std::string rel = toStd(relative_path);
    bool isDir = !rel.empty() && (rel.back() == '/' || rel.back() == '\\');
    return shouldIgnorePath(root, rel, isDir, splitList(toStd(patterns)), true) ? 1 : 0;
}

void *rt_workspace_watcher_poll_batch(void *watcher, int64_t max_events) {
    void *events = rt_seq_new_owned();
    if (!watcher)
        return events;
    if (max_events <= 0)
        max_events = 64;
    for (int64_t i = 0; i < max_events; i++) {
        int64_t type = rt_watcher_poll(watcher);
        if (type == RT_WATCH_EVENT_NONE)
            break;
        void *event = rt_map_new();
        rt_string path = rt_watcher_event_path(watcher);
        mapSetStr(event, "path", toStd(path));
        mapSetStr(event, "typeName", eventTypeName(type));
        rt_map_set_int(event, rt_const_cstr("type"), type);
        rt_map_set_bool(event, rt_const_cstr("requiresRescan"),
                        type == RT_WATCH_EVENT_OVERFLOW ? 1 : 0);
        seqPushOwned(events, event);
    }
    return events;
}

void *rt_asset_resolver_resolve(rt_string scene_path_s,
                                rt_string project_root_s,
                                rt_string asset_roots_csv,
                                rt_string asset_path_s) {
    void *result = rt_map_new();
    const std::string assetPath = toStd(asset_path_s);
    fs::path scenePath = toStd(scene_path_s);
    fs::path projectRoot = toStd(project_root_s);
    if (projectRoot.empty())
        projectRoot = ".";
    projectRoot = fs::absolute(projectRoot).lexically_normal();

    mapSetStr(result, "path", "");
    mapSetStr(result, "displayPath", assetPath);
    mapSetStr(result, "source", "missing");
    mapSetStr(result, "diagnostic", "");
    rt_map_set_bool(result, rt_const_cstr("exists"), 0);
    rt_map_set_bool(result, rt_const_cstr("found"), 0);

    std::vector<std::pair<std::string, fs::path>> candidates;
    fs::path asset(assetPath);
    if (asset.is_absolute())
        candidates.push_back({"absolute", asset});
    if (!scenePath.empty())
        candidates.push_back({"scene", scenePath.parent_path() / asset});
    candidates.push_back({"project", projectRoot / asset});
    for (const auto &root : splitList(toStd(asset_roots_csv))) {
        fs::path assetRoot(root);
        if (assetRoot.is_relative())
            assetRoot = projectRoot / assetRoot;
        candidates.push_back({"assetRoot", assetRoot / asset});
    }

    std::error_code ec;
    for (auto &[source, candidate] : candidates) {
        candidate = candidate.lexically_normal();
        if (fs::exists(candidate, ec)) {
            const std::string resolved = fs::absolute(candidate, ec).lexically_normal().string();
            mapSetStr(result, "path", resolved);
            mapSetStr(result, "displayPath", fs::relative(candidate, projectRoot, ec).generic_string());
            mapSetStr(result, "source", source);
            rt_map_set_bool(result, rt_const_cstr("exists"), 1);
            rt_map_set_bool(result, rt_const_cstr("found"), 1);
            return result;
        }
    }

    rt_string assetName = makeString(assetPath);
    if (rt_asset_exists(assetName)) {
        mapSetStr(result, "path", assetPath);
        mapSetStr(result, "displayPath", assetPath);
        mapSetStr(result, "source", "mounted");
        rt_map_set_bool(result, rt_const_cstr("exists"), 1);
        rt_map_set_bool(result, rt_const_cstr("found"), 1);
        rt_string_unref(assetName);
        return result;
    }
    rt_string_unref(assetName);

    mapSetStr(result, "diagnostic", "asset not found: " + assetPath);
    return result;
}

void *rt_project_manifest_parse_text(rt_string text_s) {
    void *manifest = newManifestMap();
    void *diagnostics = rt_map_get(manifest, rt_const_cstr("diagnostics"));
    std::string section;
    void *sectionMap = nullptr;
    std::string sectionKind;

    int64_t lineNo = 0;
    for (std::string line : readLines(toStd(text_s))) {
        lineNo++;
        if (lineNo == 1 && line.rfind("\xEF\xBB\xBF", 0) == 0)
            line.erase(0, 3);
        std::string stripped = trim(line);
        if (stripped.empty() || stripped[0] == '#')
            continue;
        if (stripped.rfind("//", 0) == 0)
            continue;
        if (stripped.front() == '[' && stripped.back() == ']') {
            section = stripped.substr(1, stripped.size() - 2);
            sectionMap = rt_map_new();
            mapSetStr(sectionMap, "name", section);
            sectionKind.clear();
            if (section.rfind("run.", 0) == 0) {
                sectionKind = "runConfigs";
                mapSetStr(sectionMap, "name", section.substr(4));
            } else if (section.rfind("build.", 0) == 0) {
                sectionKind = "buildConfigs";
                mapSetStr(sectionMap, "name", section.substr(6));
            } else {
                pushDiagnostic(diagnostics, "unknown manifest section '" + section + "'",
                               "", lineNo, "manifest.section");
                releaseObject(sectionMap);
                sectionMap = nullptr;
            }
            if (sectionMap)
                appendConfigMap(manifest, sectionKind.c_str(), sectionMap);
            continue;
        }

        auto [key, value] = splitDirectiveLine(stripped);
        if (key.empty() || value.empty()) {
            pushDiagnostic(diagnostics, "manifest directive missing value", "", lineNo,
                           "manifest.value");
            continue;
        }
        const std::string canonical = manifestKey(key);
        if (sectionMap) {
            if (canonical == "args" || canonical == "env")
                replaceStringSeq(sectionMap, key.c_str(), splitList(value));
            else
                mapSetStr(sectionMap, key.c_str(), value);
            continue;
        }
        if (canonical == "project" || canonical == "name")
            mapSetStr(manifest, "name", value);
        else if (canonical == "version")
            mapSetStr(manifest, "version", value);
        else if (canonical == "lang" || canonical == "language")
            mapSetStr(manifest, "language", lower(value));
        else if (canonical == "entry" || canonical == "main")
            mapSetStr(manifest, "entry", value);
        else if (canonical == "sources" || canonical == "sourceglobs")
            appendToStringSeqField(manifest, "sourceGlobs", value);
        else if (canonical == "exclude" || canonical == "excludes")
            appendToStringSeqField(manifest, "excludes", value);
        else if (canonical == "assetroot" || canonical == "assetroots")
            appendToStringSeqField(manifest, "assetRoots", value);
        else if (canonical == "sceneroot" || canonical == "sceneroots")
            appendToStringSeqField(manifest, "sceneRoots", value);
        else if (canonical == "defaultscene")
            mapSetStr(manifest, "defaultScene", value);
        else if (canonical == "run") {
            void *run = rt_map_new();
            mapSetStr(run, "name", value);
            appendConfigMap(manifest, "runConfigs", run);
        } else {
            pushDiagnostic(diagnostics, "unknown manifest directive '" + key + "'",
                           "", lineNo, "manifest.directive");
        }
    }

    rt_map_set_bool(manifest, rt_const_cstr("valid"), rt_seq_len(diagnostics) == 0 ? 1 : 0);
    if (mapGetString(manifest, "name").empty())
        mapSetStr(manifest, "name", "ViperProject");
    return manifest;
}

void *rt_project_manifest_parse_file(rt_string path_s) {
    const std::string path = toStd(path_s);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        void *manifest = newManifestMap();
        void *diagnostics = rt_map_get(manifest, rt_const_cstr("diagnostics"));
        pushDiagnostic(diagnostics, "cannot open manifest", path, 0, "manifest.open");
        rt_map_set_bool(manifest, rt_const_cstr("valid"), 0);
        return manifest;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    rt_string text = makeString(buffer.str());
    void *manifest = rt_project_manifest_parse_text(text);
    rt_string_unref(text);
    if (mapGetString(manifest, "name") == "ViperProject") {
        fs::path p(path);
        if (!p.parent_path().empty())
            mapSetStr(manifest, "name", p.parent_path().filename().generic_string());
    }
    mapSetStr(manifest, "path", path);
    return manifest;
}

void *rt_workspace_edit_validate(void *edits) {
    void *result = rt_map_new();
    void *diagnostics = rt_seq_new_owned();
    std::vector<EditRecord> records;
    std::unordered_map<std::string, std::string> contents;
    bool ok = edits != nullptr;
    if (!edits) {
        pushDiagnostic(diagnostics, "workspace edits sequence is null", "", 0, "edit.null");
    } else {
        const int64_t len = rt_seq_len(edits);
        for (int64_t i = 0; i < len; i++) {
            EditRecord record;
            if (loadEditRecord(rt_seq_get(edits, i), record, diagnostics, i))
                records.push_back(std::move(record));
            else
                ok = false;
        }
        if (!validateEditRecords(records, contents, diagnostics))
            ok = false;
    }
    rt_map_set_bool(result, rt_const_cstr("success"), ok ? 1 : 0);
    rt_map_set_int(result, rt_const_cstr("editCount"), static_cast<int64_t>(records.size()));
    rt_map_set(result, rt_const_cstr("diagnostics"), diagnostics);
    releaseObject(diagnostics);
    return result;
}

void *rt_workspace_edit_apply(void *edits) {
    void *result = rt_workspace_edit_validate(edits);
    if (!rt_map_get_bool(result, rt_const_cstr("success"))) {
        rt_map_set_int(result, rt_const_cstr("appliedFiles"), 0);
        return result;
    }

    void *diagnostics = rt_map_get(result, rt_const_cstr("diagnostics"));
    std::vector<EditRecord> records;
    std::unordered_map<std::string, std::string> contents;
    const int64_t len = rt_seq_len(edits);
    for (int64_t i = 0; i < len; i++) {
        EditRecord record;
        if (loadEditRecord(rt_seq_get(edits, i), record, diagnostics, i))
            records.push_back(std::move(record));
    }
    validateEditRecords(records, contents, diagnostics);

    std::map<std::string, std::vector<EditRecord>> byFile;
    for (const auto &record : records)
        byFile[record.file].push_back(record);

    std::unordered_map<std::string, std::string> backups = contents;
    for (auto &[file, vec] : byFile) {
        std::sort(vec.begin(), vec.end(), [](const EditRecord &a, const EditRecord &b) {
            return a.startOffset > b.startOffset;
        });
        std::string &text = contents[file];
        for (const auto &edit : vec)
            text.replace(edit.startOffset, edit.endOffset - edit.startOffset, edit.newText);
    }

    int64_t applied = 0;
    for (const auto &[file, text] : contents) {
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        if (!out) {
            pushDiagnostic(diagnostics, "cannot write edit target", file, 0, "edit.write");
            rt_map_set_bool(result, rt_const_cstr("success"), 0);
            for (const auto &[rollbackFile, rollbackText] : backups) {
                std::ofstream rollback(rollbackFile, std::ios::binary | std::ios::trunc);
                if (rollback)
                    rollback << rollbackText;
            }
            rt_map_set_int(result, rt_const_cstr("appliedFiles"), 0);
            return result;
        }
        out << text;
        applied++;
    }
    rt_map_set_int(result, rt_const_cstr("appliedFiles"), applied);
    return result;
}

} // extern "C"
