//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/text/rt_fuzzy_match.cpp
// Purpose: Reusable fuzzy matching and quick-open ranking helpers.
//
//===----------------------------------------------------------------------===//

#include "rt_fuzzy_match.h"

#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace {

std::string toStd(rt_string s) {
    if (!s)
        return {};
    const char *data = rt_string_cstr(s);
    int64_t len = rt_str_len(s);
    if (!data || len <= 0)
        return {};
    return std::string(data, static_cast<size_t>(len));
}

void releaseObject(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

void mapSetStr(void *map, const char *key, const std::string &value) {
    rt_string k = rt_const_cstr(key);
    rt_string s = rt_string_from_bytes(value.data(), value.size());
    rt_map_set_str(map, k, s);
    rt_string_unref(s);
    rt_string_unref(k);
}

void mapSetInt(void *map, const char *key, int64_t value) {
    rt_string k = rt_const_cstr(key);
    rt_map_set_int(map, k, value);
    rt_string_unref(k);
}

void mapSetBool(void *map, const char *key, int8_t value) {
    rt_string k = rt_const_cstr(key);
    rt_map_set_bool(map, k, value);
    rt_string_unref(k);
}

void mapSetObj(void *map, const char *key, void *value) {
    rt_string k = rt_const_cstr(key);
    rt_map_set(map, k, value);
    rt_string_unref(k);
}

bool isBoundary(const std::string &s, size_t i) {
    if (i == 0)
        return true;
    char prev = s[i - 1];
    char cur = s[i];
    if (prev == '/' || prev == '\\' || prev == '_' || prev == '-' || prev == '.' || prev == ' ')
        return true;
    return std::islower(static_cast<unsigned char>(prev)) &&
           std::isupper(static_cast<unsigned char>(cur));
}

struct MatchResult {
    bool matched{false};
    int64_t score{-1};
    std::vector<int64_t> positions;
};

MatchResult computeMatch(const std::string &query, const std::string &candidate) {
    MatchResult result;
    if (query.empty()) {
        result.matched = true;
        result.score = 0;
        return result;
    }
    size_t qi = 0;
    int64_t score = 0;
    int64_t last = -2;
    for (size_t ci = 0; ci < candidate.size() && qi < query.size(); ci++) {
        char qc = static_cast<char>(std::tolower(static_cast<unsigned char>(query[qi])));
        char cc = static_cast<char>(std::tolower(static_cast<unsigned char>(candidate[ci])));
        if (qc != cc)
            continue;
        result.positions.push_back(static_cast<int64_t>(ci));
        score += 16;
        if (query[qi] == candidate[ci])
            score += 2;
        if (isBoundary(candidate, ci))
            score += 8;
        if (static_cast<int64_t>(ci) == last + 1)
            score += 12;
        else
            score -= static_cast<int64_t>(ci) - last - 1;
        last = static_cast<int64_t>(ci);
        qi++;
    }
    if (qi != query.size())
        return result;
    score -= static_cast<int64_t>(candidate.size() - query.size()) / 4;
    score -= result.positions.empty() ? 0 : result.positions.front();
    result.matched = true;
    result.score = score;
    return result;
}

void *makeRanges(const std::vector<int64_t> &positions) {
    void *ranges = rt_seq_new_owned();
    if (positions.empty())
        return ranges;
    int64_t start = positions[0];
    int64_t prev = positions[0];
    auto pushRange = [&](int64_t s, int64_t e) {
        void *range = rt_map_new();
        mapSetInt(range, "start", s);
        mapSetInt(range, "end", e);
        rt_seq_push(ranges, range);
        releaseObject(range);
    };
    for (size_t i = 1; i < positions.size(); i++) {
        if (positions[i] == prev + 1) {
            prev = positions[i];
            continue;
        }
        pushRange(start, prev + 1);
        start = prev = positions[i];
    }
    pushRange(start, prev + 1);
    return ranges;
}

} // namespace

extern "C" {

int64_t rt_fuzzy_match_score(rt_string query, rt_string candidate) {
    return computeMatch(toStd(query), toStd(candidate)).score;
}

void *rt_fuzzy_match_match(rt_string query, rt_string candidate) {
    std::string q = toStd(query);
    std::string c = toStd(candidate);
    MatchResult m = computeMatch(q, c);
    void *result = rt_map_new();
    mapSetBool(result, "matched", m.matched ? 1 : 0);
    mapSetInt(result, "score", m.score);
    mapSetStr(result, "query", q);
    mapSetStr(result, "candidate", c);
    void *ranges = makeRanges(m.positions);
    mapSetObj(result, "ranges", ranges);
    releaseObject(ranges);
    return result;
}

} // extern "C"
