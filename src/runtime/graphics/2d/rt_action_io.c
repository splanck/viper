//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/2d/rt_action_io.c
// Purpose: Input-action serialization: save the current action/binding set to JSON
//   and load it back, reconstructing actions and bindings via the action core.
//
// Links: rt_action.h (public API), rt_action_internal.h (shared model),
//        rt_json_stream.h (streaming JSON parser), rt_action.c (action core)
//
//===----------------------------------------------------------------------===//

#include "rt_action.h"
#include "rt_action_internal.h"
#include "rt_input.h"
#include "rt_internal.h"
#include "rt_json_stream.h"
#include "rt_object.h"
#include "rt_platform.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_string_builder.h"

#include <stdlib.h>
#include <string.h>

extern int64_t rt_unbox_i64(void *box);

/// @brief Map a `BindingType` enum to the JSON serialization tag.
static const char *binding_type_name(BindingType type) {
    switch (type) {
        case BIND_KEY:
            return "key";
        case BIND_MOUSE_BUTTON:
            return "mouse";
        case BIND_MOUSE_X:
            return "mouse_x";
        case BIND_MOUSE_Y:
            return "mouse_y";
        case BIND_SCROLL_X:
            return "scroll_x";
        case BIND_SCROLL_Y:
            return "scroll_y";
        case BIND_PAD_BUTTON:
            return "pad_button";
        case BIND_PAD_AXIS:
            return "pad_axis";
        case BIND_PAD_BUTTON_AXIS:
            return "pad_button_axis";
        case BIND_CHORD:
            return "chord";
        default:
            return "unknown";
    }
}

/// @brief Reverse map: JSON tag string → `BindingType` enum.
///
/// Returns `BIND_NONE` for unknown tags so the loader can skip them.
static BindingType binding_type_from_name(const char *name) {
    if (strcmp(name, "key") == 0)
        return BIND_KEY;
    if (strcmp(name, "mouse") == 0)
        return BIND_MOUSE_BUTTON;
    if (strcmp(name, "mouse_x") == 0)
        return BIND_MOUSE_X;
    if (strcmp(name, "mouse_y") == 0)
        return BIND_MOUSE_Y;
    if (strcmp(name, "scroll_x") == 0)
        return BIND_SCROLL_X;
    if (strcmp(name, "scroll_y") == 0)
        return BIND_SCROLL_Y;
    if (strcmp(name, "pad_button") == 0)
        return BIND_PAD_BUTTON;
    if (strcmp(name, "pad_axis") == 0)
        return BIND_PAD_AXIS;
    if (strcmp(name, "pad_button_axis") == 0)
        return BIND_PAD_BUTTON_AXIS;
    if (strcmp(name, "chord") == 0)
        return BIND_CHORD;
    return BIND_NONE;
}

/// @brief Release a streaming JSON parser object created by rt_json_stream_new.
/// @param parser Parser handle; NULL is a no-op.
static void action_release_json_parser(void *parser) {
    if (parser && rt_obj_release_check0(parser))
        rt_obj_free(parser);
}

/// @brief Append a JSON-quoted-string-literal version of `str` to a builder.
///
/// Handles the standard JSON escape characters (`"`, `\\`, `\\n`,
/// `\\r`, `\\t`). Non-ASCII bytes pass through as-is — the persistence
/// format assumes UTF-8 throughout.
static void sb_append_json_string(rt_string_builder *sb, const char *str) {
    rt_sb_append_cstr(sb, "\"");
    while (*str) {
        char c = *str++;
        switch (c) {
            case '"':
                rt_sb_append_cstr(sb, "\\\"");
                break;
            case '\\':
                rt_sb_append_cstr(sb, "\\\\");
                break;
            case '\n':
                rt_sb_append_cstr(sb, "\\n");
                break;
            case '\r':
                rt_sb_append_cstr(sb, "\\r");
                break;
            case '\t':
                rt_sb_append_cstr(sb, "\\t");
                break;
            default:
                rt_sb_append_bytes(sb, &c, 1);
                break;
        }
    }
    rt_sb_append_cstr(sb, "\"");
}

/// @brief `Action.Save` — serialize all action+binding state to JSON.
///
/// Format: `{"actions":[{"name","type","bindings":[{"type","code","pad","value","keys":[...]}]}]}`.
/// `keys` is only present for chord bindings. The result is a fresh
/// `rt_string` that can be saved to disk and round-tripped via
/// `rt_action_load`.
rt_string rt_action_save(void) {
    RT_ASSERT_MAIN_THREAD();
    rt_string_builder sb;
    int8_t first_action;
    rt_sb_init(&sb);

    rt_sb_append_cstr(&sb, "{\"actions\":[");

    first_action = 1;
    {
        Action *a = g_actions;
        while (a) {
            int8_t first_binding;
            if (!first_action)
                rt_sb_append_cstr(&sb, ",");
            first_action = 0;

            rt_sb_append_cstr(&sb, "{\"name\":");
            sb_append_json_string(&sb, a->name);
            rt_sb_append_cstr(&sb, ",\"type\":");
            rt_sb_append_cstr(&sb, a->is_axis ? "\"axis\"" : "\"button\"");
            rt_sb_append_cstr(&sb, ",\"bindings\":[");

            first_binding = 1;
            {
                Binding *b = a->bindings;
                while (b) {
                    if (!first_binding)
                        rt_sb_append_cstr(&sb, ",");
                    first_binding = 0;

                    rt_sb_append_cstr(&sb, "{\"type\":");
                    sb_append_json_string(&sb, binding_type_name(b->type));
                    rt_sb_append_cstr(&sb, ",\"code\":");
                    rt_sb_append_int(&sb, b->code);
                    rt_sb_append_cstr(&sb, ",\"pad\":");
                    rt_sb_append_int(&sb, b->pad_index);
                    rt_sb_append_cstr(&sb, ",\"value\":");
                    rt_sb_append_double(&sb, b->value);
                    if (b->type == BIND_CHORD && b->chord_len > 0) {
                        int32_t ci;
                        rt_sb_append_cstr(&sb, ",\"keys\":[");
                        for (ci = 0; ci < b->chord_len; ci++) {
                            if (ci > 0)
                                rt_sb_append_cstr(&sb, ",");
                            rt_sb_append_int(&sb, b->chord_keys[ci]);
                        }
                        rt_sb_append_cstr(&sb, "]");
                    }
                    rt_sb_append_cstr(&sb, "}");
                    b = b->next;
                }
            }

            rt_sb_append_cstr(&sb, "]}");
            a = a->next;
        }
    }

    rt_sb_append_cstr(&sb, "]}");

    {
        rt_string result = rt_string_from_bytes(sb.data, sb.len);
        rt_sb_free(&sb);
        return result;
    }
}

/// @brief `Action.Load(json)` — restore action+binding state from a JSON string.
///
/// Inverse of `Save`. Uses the streaming JSON parser (`rt_json_stream`) so giant configs
/// don't allocate a parse tree. Existing actions are replaced only after the entire
/// document has parsed successfully.
int8_t rt_action_load(rt_string json) {
    RT_ASSERT_MAIN_THREAD();
    void *parser = NULL;
    int64_t tok = RT_JSON_TOK_ERROR;
    int8_t success = 0;
    int8_t saw_actions = 0;
    Action *saved_actions = NULL;

    if (!json)
        return 0;

    parser = rt_json_stream_new(json);
    if (!parser)
        return 0;

    if (!g_initialized)
        rt_action_init();

    saved_actions = g_actions;
    g_actions = NULL;

    tok = rt_json_stream_next(parser);
    if (tok != RT_JSON_TOK_OBJECT_START)
        goto cleanup;

    tok = rt_json_stream_next(parser);
    while (tok == RT_JSON_TOK_KEY) {
        rt_string top_key = rt_json_stream_string_value(parser);
        const char *top_key_cstr = rt_string_cstr(top_key);
        tok = rt_json_stream_next(parser);
        if (strcmp(top_key_cstr, "actions") != 0) {
            rt_json_stream_skip(parser);
            tok = rt_json_stream_next(parser);
            continue;
        }
        if (saw_actions || tok != RT_JSON_TOK_ARRAY_START)
            goto cleanup;
        saw_actions = 1;

        tok = rt_json_stream_next(parser);
        while (tok == RT_JSON_TOK_OBJECT_START) {
            char action_name[256];
            int8_t is_axis = 0;
            int8_t action_defined = 0;
            action_name[0] = '\0';

            tok = rt_json_stream_next(parser);
            while (tok == RT_JSON_TOK_KEY) {
                rt_string key = rt_json_stream_string_value(parser);
                const char *key_cstr = rt_string_cstr(key);
                tok = rt_json_stream_next(parser);

                if (strcmp(key_cstr, "name") == 0) {
                    if (tok != RT_JSON_TOK_STRING)
                        goto cleanup;
                    rt_string val = rt_json_stream_string_value(parser);
                    const char *val_cstr = rt_string_cstr(val);
                    size_t len = strlen(val_cstr);
                    if (len >= sizeof(action_name))
                        len = sizeof(action_name) - 1;
                    memcpy(action_name, val_cstr, len);
                    action_name[len] = '\0';
                } else if (strcmp(key_cstr, "type") == 0) {
                    if (tok != RT_JSON_TOK_STRING)
                        goto cleanup;
                    rt_string val = rt_json_stream_string_value(parser);
                    is_axis = (strcmp(rt_string_cstr(val), "axis") == 0) ? 1 : 0;
                } else if (strcmp(key_cstr, "bindings") == 0) {
                    if (tok != RT_JSON_TOK_ARRAY_START)
                        goto cleanup;
                    if (action_name[0] != '\0' && !action_defined) {
                        rt_string name_str = rt_const_cstr(action_name);
                        if (is_axis)
                            rt_action_define_axis(name_str);
                        else
                            rt_action_define(name_str);
                        action_defined = find_action(action_name) ? 1 : 0;
                    }

                    tok = rt_json_stream_next(parser);
                    while (tok == RT_JSON_TOK_OBJECT_START) {
                        BindingType btype = BIND_NONE;
                        int64_t code = 0;
                        int64_t pad = 0;
                        double value = 0.0;
                        int64_t chord_keys[MAX_CHORD_KEYS];
                        int32_t chord_len = 0;

                        tok = rt_json_stream_next(parser);
                        while (tok == RT_JSON_TOK_KEY) {
                            rt_string bkey = rt_json_stream_string_value(parser);
                            const char *bkey_cstr = rt_string_cstr(bkey);
                            tok = rt_json_stream_next(parser);

                            if (strcmp(bkey_cstr, "type") == 0) {
                                if (tok != RT_JSON_TOK_STRING)
                                    goto cleanup;
                                rt_string bval = rt_json_stream_string_value(parser);
                                btype = binding_type_from_name(rt_string_cstr(bval));
                            } else if (strcmp(bkey_cstr, "code") == 0) {
                                if (tok != RT_JSON_TOK_NUMBER)
                                    goto cleanup;
                                code = (int64_t)rt_json_stream_number_value(parser);
                            } else if (strcmp(bkey_cstr, "pad") == 0) {
                                if (tok != RT_JSON_TOK_NUMBER)
                                    goto cleanup;
                                pad = (int64_t)rt_json_stream_number_value(parser);
                            } else if (strcmp(bkey_cstr, "value") == 0) {
                                if (tok != RT_JSON_TOK_NUMBER)
                                    goto cleanup;
                                value = rt_json_stream_number_value(parser);
                            } else if (strcmp(bkey_cstr, "keys") == 0) {
                                if (tok != RT_JSON_TOK_ARRAY_START)
                                    goto cleanup;
                                chord_len = 0;
                                tok = rt_json_stream_next(parser);
                                while (tok == RT_JSON_TOK_NUMBER) {
                                    if (chord_len >= MAX_CHORD_KEYS)
                                        goto cleanup;
                                    chord_keys[chord_len++] =
                                        (int64_t)rt_json_stream_number_value(parser);
                                    tok = rt_json_stream_next(parser);
                                }
                                if (tok != RT_JSON_TOK_ARRAY_END)
                                    goto cleanup;
                            } else {
                                rt_json_stream_skip(parser);
                            }

                            tok = rt_json_stream_next(parser);
                        }
                        if (tok != RT_JSON_TOK_OBJECT_END)
                            goto cleanup;

                        if (btype != BIND_NONE && action_name[0] != '\0') {
                            Action *a = find_action(action_name);
                            if (a) {
                                Binding *b = create_binding(btype, code, pad, value);
                                if (b) {
                                    if (btype == BIND_CHORD) {
                                        b->chord_len = chord_len;
                                        for (int32_t ci = 0; ci < chord_len; ci++)
                                            b->chord_keys[ci] = chord_keys[ci];
                                    }
                                    add_binding(a, b);
                                }
                            }
                        }
                        tok = rt_json_stream_next(parser);
                    }
                    if (tok != RT_JSON_TOK_ARRAY_END)
                        goto cleanup;
                } else {
                    rt_json_stream_skip(parser);
                }
                tok = rt_json_stream_next(parser);
            }
            if (tok != RT_JSON_TOK_OBJECT_END)
                goto cleanup;
            if (action_name[0] != '\0' && !action_defined) {
                rt_string name_str = rt_const_cstr(action_name);
                if (is_axis)
                    rt_action_define_axis(name_str);
                else
                    rt_action_define(name_str);
            }
            tok = rt_json_stream_next(parser);
        }
        if (tok != RT_JSON_TOK_ARRAY_END)
            goto cleanup;
        tok = rt_json_stream_next(parser);
    }
    if (tok != RT_JSON_TOK_OBJECT_END)
        goto cleanup;
    tok = rt_json_stream_next(parser);
    if (tok == RT_JSON_TOK_END && saw_actions)
        success = 1;

cleanup:
    action_release_json_parser(parser);
    if (success) {
        Action *loaded_actions = g_actions;
        g_actions = saved_actions;
        rt_action_clear();
        g_actions = loaded_actions;
    } else {
        rt_action_clear();
        g_actions = saved_actions;
    }
    return success;
}
