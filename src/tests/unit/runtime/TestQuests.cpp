//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: tests/unit/runtime/TestQuests.cpp
// Purpose: Verify Viper.Game.Quests rejects embedded-NUL ids at registration and
//          does not alias them by pre-NUL prefix during lookup (VDOC-247).
// Key invariants:
//   - Registration of a NUL-bearing id traps (invalid id).
//   - A NUL-bearing lookup key does not address a registered quest by its prefix.
// Ownership/Lifetime: Test owns the tracker; runtime GC frees it.
// Links: src/runtime/game/rt_quests.c
//
//===----------------------------------------------------------------------===//

#include <cassert>
#include <cstdio>
#include <csetjmp>

extern "C" {
#include "rt_string.h"
void *rt_quests_new(void);
void *rt_quests_add_quest(void *tracker, rt_string quest_id, rt_string title);
void *rt_quests_add_stage(void *tracker, rt_string quest_id, rt_string stage_id, rt_string text);
void *rt_quests_add_flag(
    void *tracker, rt_string quest_id, rt_string stage_id, rt_string obj_id, rt_string text);
void *rt_quests_add_counter(void *tracker,
                            rt_string quest_id,
                            rt_string stage_id,
                            rt_string obj_id,
                            rt_string text,
                            int64_t target);
int8_t rt_quests_activate(void *tracker, rt_string quest_id);
int64_t rt_quests_quest_state(void *tracker, rt_string quest_id);
int8_t rt_quests_set_flag(void *tracker, rt_string quest_id, rt_string obj_id);
int8_t rt_quests_progress(void *tracker, rt_string quest_id, rt_string obj_id, int64_t amount);
int8_t rt_quests_objective_complete(void *tracker, rt_string quest_id, int64_t index);
int64_t rt_quests_objective_progress(void *tracker, rt_string quest_id, int64_t index);
int8_t rt_quests_save(void *tracker, void *savedata);
int8_t rt_quests_load(void *tracker, void *savedata);
void *rt_savedata_new(rt_string game_name);
void rt_savedata_set_string(void *savedata, rt_string key, rt_string value);
void rt_abort(const char *msg);
}

#include <cstdint>
#include <cstdio>

namespace {
jmp_buf g_trap_jmp;
bool g_trap_expected = false;
} // namespace

extern "C" void vm_trap(const char *msg) {
    if (g_trap_expected)
        longjmp(g_trap_jmp, 1);
    rt_abort(msg);
}

#define EXPECT_TRAP(expr)                                                                          \
    do {                                                                                           \
        g_trap_expected = true;                                                                    \
        if (setjmp(g_trap_jmp) == 0) {                                                             \
            (expr);                                                                                \
            g_trap_expected = false;                                                               \
            assert(!"expected trap did not fire");                                                 \
        }                                                                                          \
        g_trap_expected = false;                                                                   \
    } while (0)

int main() {
    printf("=== TestQuests (VDOC-247) ===\n");
    void *t = rt_quests_new();
    assert(t != nullptr);

    // A valid id registers as a hidden quest and can be activated.
    rt_quests_add_quest(t, rt_const_cstr("quest1"), rt_const_cstr("Quest One"));
    assert(rt_quests_activate(t, rt_const_cstr("quest1")) == 1);

    // An embedded-NUL id is rejected at registration: the full byte length is
    // validated and the NUL makes it invalid, so AddQuest traps.
    const char nulId[] = "quest2\0evil";
    rt_string nulName = rt_string_from_bytes(nulId, sizeof(nulId) - 1);
    EXPECT_TRAP(rt_quests_add_quest(t, nulName, rt_const_cstr("Bad")));

    // Register a fresh hidden quest, then confirm a NUL-bearing lookup key does not
    // alias it via the pre-NUL prefix: activation with the aliased key fails, while
    // the clean key still activates the (still-hidden) quest.
    rt_quests_add_quest(t, rt_const_cstr("questX"), rt_const_cstr("X"));
    const char aliasId[] = "questX\0evil";
    rt_string aliasName = rt_string_from_bytes(aliasId, sizeof(aliasId) - 1);
    assert(rt_quests_activate(t, aliasName) == 0);
    assert(rt_quests_activate(t, rt_const_cstr("questX")) == 1);

    // VDOC-248: SetFlag/Progress must enforce the objective kind. Build a quest
    // with one flag (index 0) and one counter (index 1) in its active stage.
    auto S = [](const char *s) { return rt_const_cstr(s); };
    rt_quests_add_quest(t, S("kq"), S("Kind Quest"));
    rt_quests_add_stage(t, S("kq"), S("ks"), S("Stage"));
    rt_quests_add_flag(t, S("kq"), S("ks"), S("flagObj"), S("Flag"));
    rt_quests_add_counter(t, S("kq"), S("ks"), S("counterObj"), S("Counter"), 5);
    assert(rt_quests_activate(t, S("kq")) == 1);

    // Wrong kind is a no-op false and leaves the objective incomplete.
    assert(rt_quests_set_flag(t, S("kq"), S("counterObj")) == 0);
    assert(rt_quests_objective_complete(t, S("kq"), 1) == 0);
    assert(rt_quests_progress(t, S("kq"), S("flagObj"), 1) == 0);
    assert(rt_quests_objective_complete(t, S("kq"), 0) == 0);

    // Correct kind succeeds. Use partial counter progress so the stage does not
    // auto-advance (which would move past the current stage and invalidate the
    // index-based completion queries).
    assert(rt_quests_set_flag(t, S("kq"), S("flagObj")) == 1);
    assert(rt_quests_objective_complete(t, S("kq"), 0) == 1);
    assert(rt_quests_progress(t, S("kq"), S("counterObj"), 3) == 1);
    assert(rt_quests_objective_complete(t, S("kq"), 1) == 0); // 3 of 5, not yet done

    // VDOC-249: a huge progress increment saturates to the target instead of
    // overflowing int64 (which is UB and could wrap negative, suppressing
    // completion). Two counters keep the stage from auto-advancing.
    rt_quests_add_quest(t, S("oq"), S("Overflow Quest"));
    rt_quests_add_stage(t, S("oq"), S("os"), S("Stage"));
    rt_quests_add_counter(t, S("oq"), S("os"), S("cA"), S("A"), 5);
    rt_quests_add_counter(t, S("oq"), S("os"), S("cB"), S("B"), 5);
    assert(rt_quests_activate(t, S("oq")) == 1);
    assert(rt_quests_progress(t, S("oq"), S("cA"), INT64_MAX) == 1);
    assert(rt_quests_objective_progress(t, S("oq"), 0) == 5); // saturated, not wrapped
    assert(rt_quests_objective_complete(t, S("oq"), 0) == 1);

    // VDOC-250: a full-budget quest (8 objectives with long ids and progress) whose
    // serialized record far exceeds the old fixed 512-byte chunk must still save.
    {
        void *big = rt_quests_new();
        rt_quests_add_quest(big, S("bigquest"), S("Big"));
        const char *stageId = "stage_padding_to_make_this_id_about_sixty_bytes_aaaaaa_bbbb";
        rt_quests_add_stage(big, S("bigquest"), S(stageId), S("St"));
        char objId[8][64];
        for (int i = 0; i < 8; ++i) {
            snprintf(objId[i], sizeof(objId[i]),
                     "objective_padding_identifier_number_%d_aaaaaaaaaaaaaaaaaa", i);
            rt_quests_add_counter(big, S("bigquest"), S(stageId), S(objId[i]), S("O"), 1000000);
        }
        assert(rt_quests_activate(big, S("bigquest")) == 1);
        for (int i = 0; i < 8; ++i)
            assert(rt_quests_progress(big, S("bigquest"), S(objId[i]), 12345 + i) == 1);

        void *sd = rt_savedata_new(S("quests-save-budget-test"));
        assert(sd != nullptr);
        // The serialized record is ~1KB (well over the removed 512-byte chunk); the
        // growable buffer serializes it successfully.
        assert(rt_quests_save(big, sd) == 1);
    }

    // VDOC-251: many Save/Load cycles must round-trip correctly without a
    // double-free/use-after-free from the temporary-string ownership fix.
    {
        void *tr = rt_quests_new();
        rt_quests_add_quest(tr, S("rq"), S("R"));
        rt_quests_add_stage(tr, S("rq"), S("rs"), S("S"));
        rt_quests_add_counter(tr, S("rq"), S("rs"), S("rc"), S("C"), 10);
        assert(rt_quests_activate(tr, S("rq")) == 1);
        assert(rt_quests_progress(tr, S("rq"), S("rc"), 4) == 1);
        void *sd = rt_savedata_new(S("quests-leak-test"));
        assert(sd != nullptr);
        for (int i = 0; i < 200; ++i) {
            assert(rt_quests_save(tr, sd) == 1);
            void *tr2 = rt_quests_new();
            rt_quests_add_quest(tr2, S("rq"), S("R"));
            rt_quests_add_stage(tr2, S("rq"), S("rs"), S("S"));
            rt_quests_add_counter(tr2, S("rq"), S("rs"), S("rc"), S("C"), 10);
            assert(rt_quests_load(tr2, sd) == 1);
            assert(rt_quests_objective_progress(tr2, S("rq"), 0) == 4);
        }
    }

    // VDOC-252: a malformed blob is rejected (Load returns false) and applied
    // transactionally — no partial mutation of registered quests — and integers
    // are parsed strictly (garbage and overflow are rejected).
    {
        void *tr = rt_quests_new();
        rt_quests_add_quest(tr, S("q1"), S("Q1"));
        rt_quests_add_stage(tr, S("q1"), S("s1"), S("S"));
        rt_quests_add_counter(tr, S("q1"), S("s1"), S("c1"), S("C"), 10);
        void *sd = rt_savedata_new(S("quests-malformed-test"));
        assert(sd != nullptr);

        // Valid prefix fields followed by an unrecognized field: the whole blob is
        // rejected, and the earlier (well-formed) s=1/o= fields are NOT applied.
        rt_savedata_set_string(
            sd, rt_const_cstr("viper.quests.v1"), rt_const_cstr("q=q1;s=1;g=0;o=s1.c1:5;junk"));
        assert(rt_quests_load(tr, sd) == 0);
        assert(rt_quests_activate(tr, S("q1")) == 1); // still hidden: nothing committed

        // Non-strict integers are rejected: trailing garbage and overflow.
        rt_savedata_set_string(
            sd, rt_const_cstr("viper.quests.v1"), rt_const_cstr("q=q1;s=notanumber"));
        assert(rt_quests_load(tr, sd) == 0);
        rt_savedata_set_string(sd,
                               rt_const_cstr("viper.quests.v1"),
                               rt_const_cstr("q=q1;o=s1.c1:99999999999999999999999"));
        assert(rt_quests_load(tr, sd) == 0);

        // A well-formed blob still loads.
        rt_savedata_set_string(
            sd, rt_const_cstr("viper.quests.v1"), rt_const_cstr("q=q1;s=1;g=0;o=s1.c1:3"));
        assert(rt_quests_load(tr, sd) == 1);
    }

    // VDOC-253: hot re-registration with a lower target must clamp retained
    // progress and re-run advancement, completing the quest instead of stranding
    // it on a stage whose only objective is now already done.
    {
        constexpr int64_t kComplete = 2; // RT_QUEST_STATE_COMPLETE
        void *tr = rt_quests_new();
        rt_quests_add_quest(tr, S("mq"), S("M"));
        rt_quests_add_stage(tr, S("mq"), S("ms"), S("S"));
        rt_quests_add_counter(tr, S("mq"), S("ms"), S("mc"), S("C"), 10);
        assert(rt_quests_activate(tr, S("mq")) == 1);
        assert(rt_quests_progress(tr, S("mq"), S("mc"), 8) == 1); // 8 of 10, active
        // Re-register the counter with target 5 (<= retained 8): objective becomes
        // complete and the single-objective stage advances, completing the quest.
        rt_quests_add_counter(tr, S("mq"), S("ms"), S("mc"), S("C"), 5);
        assert(rt_quests_quest_state(tr, S("mq")) == kComplete);
    }

    printf("All TestQuests passed!\n");
    return 0;
}
