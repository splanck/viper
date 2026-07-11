//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/3d/rt_game3d_dialogue.c
// Purpose: Viper.Game3D.Dialogue3D — 3D conversation surface: a typewriter
//   line queue drawn over the world overlay (bottom panel or speaker-anchored
//   bubble via Camera3D.WorldToScreen), localization-keyed text through a
//   bound MessageBundle, optional per-line voice clips, and blocking choice
//   prompts with polled results.
// Key invariants:
//   - One shown conversation per world (show() installs; hide()/end releases).
//   - Key resolution rule: a bound bundle that has the string as a key
//     substitutes the localized text; otherwise the literal is used.
//   - Two-stage skip convention: the first skip completes the reveal, the
//     next advance() moves to the following line.
//   - Choice prompts block line advance until confirmed; results are polled
//     (choiceMade one-shot + lastChoice), never callbacks.
// Ownership/Lifetime:
//   - GC-managed; finalizer releases world/bundle/entity/clip references. The
//     world retains the shown dialogue until hidden or finished.
// Links: misc/plans/thirdpersonupgrade/25-dialogue-3d.md, rt_game3d_internal.h
//
//===----------------------------------------------------------------------===//

#include "rt_canvas3d.h"
#include "rt_game3d.h"
#include "rt_game3d_internal.h"
#include "rt_graphics3d_ids.h"
#include "rt_message_bundle.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_trap.h"
#include "rt_vec3.h"
#include <math.h>
#include <string.h>

#define GAME3D_DLG_DEFAULT_REVEAL_SPEED 40.0
#define GAME3D_DLG_AUTO_HOLD_SECONDS 1.2

//=========================================================================
// Lifecycle
//=========================================================================

/// @brief GC finalizer: release retained references (clips per line included).
static void game3d_dialogue_finalize(void *obj) {
    rt_game3d_dialogue *dialogue = (rt_game3d_dialogue *)obj;
    if (!dialogue)
        return;
    for (int32_t i = 0; i < dialogue->line_count; ++i)
        game3d_release_ref(&dialogue->lines[i].voice_clip);
    game3d_release_ref(&dialogue->world);
    game3d_release_ref(&dialogue->bundle);
    game3d_release_ref(&dialogue->speaker_entity);
}

/// @brief Create a conversation bound to @p world (shown via show()).
void *rt_game3d_dialogue_new(void *world_obj) {
    rt_game3d_world *world =
        game3d_world_checked(world_obj, "Game3D.Dialogue3D.New: invalid world");
    if (!world)
        return NULL;
    rt_game3d_dialogue *dialogue = (rt_game3d_dialogue *)rt_obj_new_i64(
        RT_G3D_GAME3D_DIALOGUE_CLASS_ID, (int64_t)sizeof(*dialogue));
    if (!dialogue) {
        rt_trap("Game3D.Dialogue3D.New: allocation failed");
        return NULL;
    }
    memset(dialogue, 0, sizeof(*dialogue));
    rt_obj_set_finalizer(dialogue, game3d_dialogue_finalize);
    game3d_assign_ref(&dialogue->world, world);
    dialogue->reveal_speed = GAME3D_DLG_DEFAULT_REVEAL_SPEED;
    dialogue->panel_alpha = 0.65;
    dialogue->name_color = 0xFFD75A;
    dialogue->last_choice = -1;
    return dialogue;
}

/// @brief Resolve text through the bound bundle (key hit) or keep the literal.
static void game3d_dialogue_resolve_text(rt_game3d_dialogue *dialogue,
                                         rt_string text,
                                         char *dst,
                                         size_t dst_size) {
    dst[0] = '\0';
    if (!text)
        return;
    rt_string resolved = text;
    if (dialogue->bundle && rt_message_bundle_has(dialogue->bundle, text))
        resolved = rt_message_bundle_get(dialogue->bundle, text);
    const char *cstr = resolved ? rt_string_cstr(resolved) : NULL;
    if (cstr) {
        strncpy(dst, cstr, dst_size - 1);
        dst[dst_size - 1] = '\0';
    }
}

//=========================================================================
// Line queue
//=========================================================================

/// @brief Shared line-append body.
static void *game3d_dialogue_say_impl(
    void *obj, rt_string speaker, rt_string text, void *voice_clip, const char *api_name) {
    rt_game3d_dialogue *dialogue = game3d_dialogue_checked(obj, api_name);
    if (!dialogue)
        return obj;
    if (dialogue->line_count >= RT_GAME3D_DLG_MAX_LINES) {
        rt_trap("Game3D.Dialogue3D.say: line queue limit reached (32)");
        return obj;
    }
    rt_game3d_dlg_line *line = &dialogue->lines[dialogue->line_count];
    memset(line, 0, sizeof(*line));
    const char *speaker_cstr = speaker ? rt_string_cstr(speaker) : NULL;
    if (speaker_cstr) {
        strncpy(line->speaker, speaker_cstr, RT_GAME3D_DLG_NAME_MAX - 1);
        line->speaker[RT_GAME3D_DLG_NAME_MAX - 1] = '\0';
    }
    game3d_dialogue_resolve_text(dialogue, text, line->text, RT_GAME3D_TL_TEXT_MAX);
    game3d_assign_ref(&line->voice_clip, voice_clip);
    dialogue->line_count += 1;
    return obj;
}

/// @brief Fluent: queue a line (text may be a localization key).
void *rt_game3d_dialogue_say(void *obj, rt_string speaker, rt_string text) {
    return game3d_dialogue_say_impl(
        obj, speaker, text, NULL, "Game3D.Dialogue3D.say: invalid dialogue");
}

/// @brief Fluent: queue a voiced line (clip plays when the line starts).
void *rt_game3d_dialogue_say_voiced(void *obj, rt_string speaker, rt_string text, void *clip) {
    return game3d_dialogue_say_impl(
        obj, speaker, text, clip, "Game3D.Dialogue3D.sayVoiced: invalid dialogue");
}

/// @brief Fluent: queue a blocking choice prompt after the current lines.
void *rt_game3d_dialogue_ask_choice(void *obj, void *options_seq) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.askChoice: invalid dialogue");
    if (!dialogue)
        return obj;
    int64_t count = options_seq ? rt_seq_len(options_seq) : 0;
    if (count <= 0 || count > RT_GAME3D_DLG_MAX_CHOICES) {
        rt_trap("Game3D.Dialogue3D.askChoice: choices must hold 1..8 options");
        return obj;
    }
    dialogue->choice_count = (int32_t)count;
    for (int32_t i = 0; i < dialogue->choice_count; ++i) {
        rt_string option = rt_seq_get_str(options_seq, i);
        game3d_dialogue_resolve_text(dialogue, option, dialogue->choices[i], RT_GAME3D_TL_TEXT_MAX);
    }
    dialogue->choice_selected = 0;
    dialogue->choice_active = 0; /* armed when the queue reaches its end */
    return obj;
}

//=========================================================================
// Playback control
//=========================================================================

/// @brief Show the conversation: install as the world's active dialogue.
void rt_game3d_dialogue_show(void *obj) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.show: invalid dialogue");
    if (!dialogue)
        return;
    rt_game3d_world *world =
        (rt_game3d_world *)rt_g3d_checked_or_null(dialogue->world, RT_G3D_GAME3D_WORLD_CLASS_ID);
    if (!world)
        return;
    rt_game3d_dialogue *previous = (rt_game3d_dialogue *)rt_g3d_checked_or_null(
        world->active_dialogue, RT_G3D_GAME3D_DIALOGUE_CLASS_ID);
    if (previous && previous != dialogue)
        previous->active = 0;
    game3d_assign_typed_ref(&world->active_dialogue, dialogue, RT_G3D_GAME3D_DIALOGUE_CLASS_ID);
    dialogue->active = 1;
    dialogue->line_index = 0;
    dialogue->reveal_chars = 0.0;
    dialogue->hold_remaining = 0.0;
    dialogue->line_started = 0;
    dialogue->choice_made = 0;
}

/// @brief Hide the conversation (releases the world slot).
void rt_game3d_dialogue_hide(void *obj) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.hide: invalid dialogue");
    if (!dialogue)
        return;
    dialogue->active = 0;
    rt_game3d_world *world =
        (rt_game3d_world *)rt_g3d_checked_or_null(dialogue->world, RT_G3D_GAME3D_WORLD_CLASS_ID);
    if (world && world->active_dialogue == (void *)dialogue)
        game3d_release_typed_ref(&world->active_dialogue, RT_G3D_GAME3D_DIALOGUE_CLASS_ID);
}

/// @brief Current line text length (revealed cap).
static size_t game3d_dialogue_line_len(const rt_game3d_dialogue *dialogue) {
    if (dialogue->line_index < 0 || dialogue->line_index >= dialogue->line_count)
        return 0;
    return strlen(dialogue->lines[dialogue->line_index].text);
}

/// @brief Advance to the next line (or arm the pending choice / finish).
void rt_game3d_dialogue_advance(void *obj) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.advance: invalid dialogue");
    if (!dialogue || !dialogue->active || dialogue->choice_active)
        return;
    size_t len = game3d_dialogue_line_len(dialogue);
    if (dialogue->reveal_chars < (double)len) {
        /* Two-stage skip: first press completes the reveal. */
        dialogue->reveal_chars = (double)len;
        return;
    }
    dialogue->line_index += 1;
    dialogue->reveal_chars = 0.0;
    dialogue->hold_remaining = 0.0;
    dialogue->line_started = 0;
    if (dialogue->line_index >= dialogue->line_count) {
        if (dialogue->choice_count > 0 && !dialogue->choice_made) {
            dialogue->choice_active = 1; /* block until confirmed */
        } else {
            rt_game3d_dialogue_hide(obj);
        }
    }
}

/// @brief Complete the current line's reveal instantly.
void rt_game3d_dialogue_skip_reveal(void *obj) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.skipReveal: invalid dialogue");
    if (dialogue && dialogue->active)
        dialogue->reveal_chars = (double)game3d_dialogue_line_len(dialogue);
}

/// @brief Move the choice highlight by @p delta (clamped).
void rt_game3d_dialogue_move_choice(void *obj, int64_t delta) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.moveChoice: invalid dialogue");
    if (!dialogue || !dialogue->choice_active)
        return;
    int64_t next = dialogue->choice_selected + delta;
    if (next < 0)
        next = 0;
    if (next >= dialogue->choice_count)
        next = dialogue->choice_count - 1;
    dialogue->choice_selected = (int32_t)next;
}

/// @brief Confirm the highlighted choice: latches choiceMade + lastChoice.
void rt_game3d_dialogue_confirm_choice(void *obj) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.confirmChoice: invalid dialogue");
    if (!dialogue || !dialogue->choice_active)
        return;
    dialogue->last_choice = dialogue->choice_selected;
    dialogue->choice_made = 1;
    dialogue->choice_active = 0;
    dialogue->choice_count = 0;
    rt_game3d_dialogue_hide(obj);
}

//=========================================================================
// Properties
//=========================================================================

int8_t rt_game3d_dialogue_get_active(void *obj) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.get_active: invalid dialogue");
    return dialogue ? dialogue->active : 0;
}

int64_t rt_game3d_dialogue_get_line_count(void *obj) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.get_lineCount: invalid dialogue");
    return dialogue ? dialogue->line_count : 0;
}

int8_t rt_game3d_dialogue_get_choice_pending(void *obj) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.choicePending: invalid dialogue");
    return dialogue ? dialogue->choice_active : 0;
}

/// @brief One-shot: a choice was confirmed since the last query.
int8_t rt_game3d_dialogue_choice_made(void *obj) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.choiceMade: invalid dialogue");
    if (!dialogue)
        return 0;
    int8_t made = dialogue->choice_made;
    dialogue->choice_made = 0;
    return made;
}

int64_t rt_game3d_dialogue_last_choice(void *obj) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.lastChoice: invalid dialogue");
    return dialogue ? dialogue->last_choice : -1;
}

/// @brief Currently displayed (revealed) text of the active line.
rt_string rt_game3d_dialogue_current_text(void *obj) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.currentText: invalid dialogue");
    static char revealed[RT_GAME3D_TL_TEXT_MAX];
    revealed[0] = '\0';
    if (dialogue && dialogue->active && dialogue->line_index < dialogue->line_count) {
        const char *full = dialogue->lines[dialogue->line_index].text;
        size_t len = strlen(full);
        size_t shown = (size_t)dialogue->reveal_chars;
        if (shown > len)
            shown = len;
        memcpy(revealed, full, shown);
        revealed[shown] = '\0';
    }
    return rt_const_cstr(revealed);
}

void rt_game3d_dialogue_set_speaker_entity(void *obj, void *entity) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.setSpeakerEntity: invalid dialogue");
    if (entity && !rt_g3d_has_class(entity, RT_G3D_GAME3D_ENTITY_CLASS_ID)) {
        rt_trap("Game3D.Dialogue3D.setSpeakerEntity: value must be Entity3D");
        return;
    }
    if (dialogue)
        game3d_assign_ref(&dialogue->speaker_entity, entity);
}

void rt_game3d_dialogue_set_anchored(void *obj, int8_t anchored) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.setAnchored: invalid dialogue");
    if (dialogue)
        dialogue->anchored = anchored ? 1 : 0;
}

void rt_game3d_dialogue_set_auto_advance(void *obj, int8_t enabled) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.setAutoAdvance: invalid dialogue");
    if (dialogue)
        dialogue->auto_advance = enabled ? 1 : 0;
}

void rt_game3d_dialogue_set_reveal_speed(void *obj, double chars_per_second) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.setRevealSpeed: invalid dialogue");
    if (dialogue)
        dialogue->reveal_speed =
            game3d_positive_clamped_or(chars_per_second, GAME3D_DLG_DEFAULT_REVEAL_SPEED, 10000.0);
}

/// @brief Bind a MessageBundle for key resolution (NULL unbinds).
void rt_game3d_dialogue_set_locale(void *obj, void *bundle) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.setLocale: invalid dialogue");
    if (dialogue)
        game3d_assign_ref(&dialogue->bundle, bundle);
}

void rt_game3d_dialogue_set_style(void *obj, double panel_alpha, int64_t name_color) {
    rt_game3d_dialogue *dialogue =
        game3d_dialogue_checked(obj, "Game3D.Dialogue3D.setStyle: invalid dialogue");
    if (dialogue) {
        dialogue->panel_alpha = game3d_clamp(game3d_finite_or(panel_alpha, 0.65), 0.0, 1.0);
        dialogue->name_color = name_color;
    }
}

//=========================================================================
// World hooks (tick + overlay)
//=========================================================================

/// @brief Typewriter tick + voice fire + auto-advance. See internal header.
void game3d_world_dialogue_tick(rt_game3d_world *world, double dt) {
    if (!world)
        return;
    rt_game3d_dialogue *dialogue = (rt_game3d_dialogue *)rt_g3d_checked_or_null(
        world->active_dialogue, RT_G3D_GAME3D_DIALOGUE_CLASS_ID);
    if (!dialogue || !dialogue->active)
        return;
    dt = game3d_clamp_dt(dt);
    if (dialogue->line_index >= dialogue->line_count)
        return; /* waiting on a choice */
    rt_game3d_dlg_line *line = &dialogue->lines[dialogue->line_index];
    if (!dialogue->line_started) {
        dialogue->line_started = 1;
        if (line->voice_clip && world->audio)
            (void)rt_game3d_audio_play2d(world->audio, line->voice_clip);
    }
    size_t len = strlen(line->text);
    if (dialogue->reveal_chars < (double)len) {
        dialogue->reveal_chars += dialogue->reveal_speed * dt;
        if (dialogue->reveal_chars >= (double)len) {
            dialogue->reveal_chars = (double)len;
            dialogue->hold_remaining = GAME3D_DLG_AUTO_HOLD_SECONDS;
        }
    } else if (dialogue->auto_advance) {
        dialogue->hold_remaining -= dt;
        if (dialogue->hold_remaining <= 0.0)
            rt_game3d_dialogue_advance(dialogue);
    }
}

/// @brief Overlay draw: bottom panel or anchored bubble + choices. See header.
void game3d_world_dialogue_overlay(rt_game3d_world *world) {
    if (!world || !world->canvas)
        return;
    rt_game3d_dialogue *dialogue = (rt_game3d_dialogue *)rt_g3d_checked_or_null(
        world->active_dialogue, RT_G3D_GAME3D_DIALOGUE_CLASS_ID);
    if (!dialogue || !dialogue->active)
        return;
    int64_t width = world->width;
    int64_t height = world->height;
    if (width <= 0 || height <= 0)
        return;

    if (dialogue->choice_active) {
        /* Choice prompt: centered list with a highlight marker. */
        int64_t panel_h = (int64_t)(dialogue->choice_count + 2) * 14;
        int64_t top = height - panel_h - 16;
        rt_canvas3d_draw_rect2d_alpha(
            world->canvas, 12, top, width - 24, panel_h, 0x101418, dialogue->panel_alpha);
        for (int32_t i = 0; i < dialogue->choice_count; ++i) {
            char row[RT_GAME3D_TL_TEXT_MAX + 4];
            snprintf(row,
                     sizeof(row),
                     "%s %s",
                     i == dialogue->choice_selected ? ">" : " ",
                     dialogue->choices[i]);
            rt_canvas3d_draw_text2d(world->canvas,
                                    24,
                                    top + 10 + (int64_t)i * 14,
                                    rt_const_cstr(row),
                                    i == dialogue->choice_selected ? 0xFFFFFF : 0xB0B8C0);
        }
        return;
    }
    if (dialogue->line_index >= dialogue->line_count)
        return;

    rt_game3d_dlg_line *line = &dialogue->lines[dialogue->line_index];
    char revealed[RT_GAME3D_TL_TEXT_MAX];
    size_t len = strlen(line->text);
    size_t shown = (size_t)dialogue->reveal_chars;
    if (shown > len)
        shown = len;
    memcpy(revealed, line->text, shown);
    revealed[shown] = '\0';

    /* Anchored bubble above the speaker entity when projectable. */
    if (dialogue->anchored) {
        rt_game3d_entity *speaker = (rt_game3d_entity *)rt_g3d_checked_or_null(
            dialogue->speaker_entity, RT_G3D_GAME3D_ENTITY_CLASS_ID);
        double pos[3];
        if (speaker && game3d_entity_alive_or_record(speaker) && world->camera &&
            game3d_entity_world_position_components(speaker, pos)) {
            double sx = 0.0;
            double sy = 0.0;
            if (rt_camera3d_world_to_screen(
                    world->camera, pos[0], pos[1] + 1.9, pos[2], width, height, &sx, &sy)) {
                int64_t bubble_w = (int64_t)(shown > 8 ? shown : 8) * 8 + 16;
                int64_t bx = (int64_t)sx - bubble_w / 2;
                if (bx < 4)
                    bx = 4;
                if (bx + bubble_w > width - 4)
                    bx = width - 4 - bubble_w;
                int64_t by = (int64_t)sy - 34;
                if (by < 4)
                    by = 4;
                rt_canvas3d_draw_rect2d_alpha(
                    world->canvas, bx, by, bubble_w, 28, 0x101418, dialogue->panel_alpha);
                if (line->speaker[0])
                    rt_canvas3d_draw_text2d(world->canvas,
                                            bx + 8,
                                            by + 4,
                                            rt_const_cstr(line->speaker),
                                            dialogue->name_color);
                rt_canvas3d_draw_text2d(
                    world->canvas, bx + 8, by + 16, rt_const_cstr(revealed), 0xFFFFFF);
                return;
            }
        }
        /* Behind camera / no speaker: fall through to the bottom panel. */
    }

    int64_t panel_h = height / 4;
    if (panel_h < 44)
        panel_h = 44;
    int64_t top = height - panel_h - 8;
    rt_canvas3d_draw_rect2d_alpha(
        world->canvas, 12, top, width - 24, panel_h, 0x101418, dialogue->panel_alpha);
    if (line->speaker[0])
        rt_canvas3d_draw_text2d(
            world->canvas, 24, top + 8, rt_const_cstr(line->speaker), dialogue->name_color);
    rt_canvas3d_draw_text2d(world->canvas, 24, top + 24, rt_const_cstr(revealed), 0xFFFFFF);
}
