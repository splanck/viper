//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/tests/runtime/RTFuzzyMatchTests.cpp
// Purpose: Tests for reusable fuzzy match helpers.
//
//===----------------------------------------------------------------------===//

#include "rt_fuzzy_match.h"

#include "rt_internal.h"
#include "rt_map.h"
#include "rt_seq.h"
#include "rt_string.h"

#include <cassert>
#include <cstring>

extern "C" void vm_trap(const char *msg) {
    rt_abort(msg);
}

int main() {
    assert(rt_fuzzy_match_score(rt_const_cstr("vf"), rt_const_cstr("viperide_file.zia")) > 0);
    assert(rt_fuzzy_match_score(rt_const_cstr("zz"), rt_const_cstr("viperide_file.zia")) == -1);

    // VDOC-059: a successful match never scores below 0, so -1 reliably
    // signals a miss even for heavily penalized matches.
    {
        char noisy[102];
        memset(noisy, 'a', 100);
        noisy[100] = 'z';
        noisy[101] = '\0';
        int64_t clamped = rt_fuzzy_match_score(rt_const_cstr("z"), rt_string_from_bytes(noisy, 101));
        assert(clamped >= 0);
    }
    assert(rt_fuzzy_match_score(rt_const_cstr("VS"), rt_const_cstr("ViperSceneEditor.zia")) >
           rt_fuzzy_match_score(rt_const_cstr("VS"), rt_const_cstr("very/slow/file.zia")));

    void *match = rt_fuzzy_match_match(rt_const_cstr("vse"), rt_const_cstr("ViperSceneEditor.zia"));
    assert(rt_map_get_bool(match, rt_const_cstr("matched")) == 1);
    void *ranges = rt_map_get(match, rt_const_cstr("ranges"));
    assert(rt_seq_len(ranges) >= 2);
    void *first = rt_seq_get(ranges, 0);
    assert(rt_map_get_int(first, rt_const_cstr("start")) == 0);
    return 0;
}
