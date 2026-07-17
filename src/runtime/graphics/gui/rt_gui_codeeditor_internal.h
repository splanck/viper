//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/gui/rt_gui_codeeditor_internal.h
// Purpose: Shared CodeEditor helpers used across the editor's feature modules
//          (syntax highlighting, gutter, folding, cursors, completion) split
//          between rt_gui_codeeditor.c and rt_gui_codeeditor_syntax.c.
//
// Key invariants:
//   - Engine-internal; included only by the gui/ CodeEditor translation units.
//   - Column helpers translate between byte and character columns for UTF-8
//     lines; the handle cast validates the widget class id.
//
// Ownership/Lifetime:
//   - Helpers borrow the caller's editor/selection; no allocation.
//
// Links: src/runtime/graphics/gui/rt_gui_codeeditor.c (editor core + features),
//        src/runtime/graphics/gui/rt_gui_codeeditor_syntax.c (highlighting)
//
//===----------------------------------------------------------------------===//
#pragma once

#include "rt_gui_internal.h"

// Shared CodeEditor helpers (defined in rt_gui_codeeditor_syntax.c).
vg_codeeditor_t *rt_codeeditor_handle_checked(void *editor);
int rt_codeeditor_gutter_slot_checked(int64_t slot, int *out_type);
int rt_codeeditor_line_length_i32(const vg_codeeditor_t *ce, int line);
int rt_codeeditor_byte_col_to_char_col(const vg_codeeditor_t *ce, int line, int byte_col);
int rt_codeeditor_char_col_to_byte_col(const vg_codeeditor_t *ce, int line, int char_col);
void rt_codeeditor_normalize_selection(vg_selection_t *selection);
