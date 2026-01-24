//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: user/libwidget/src/treeview.c
// Purpose: TreeView widget implementation.
//
//===----------------------------------------------------------------------===//

#include <widget.h>
#include <stdlib.h>
#include <string.h>

#define ITEM_HEIGHT      18
#define INDENT_WIDTH     16
#define EXPAND_BOX_SIZE  9
#define INITIAL_CAPACITY 8

//===----------------------------------------------------------------------===//
// Internal Helpers
//===----------------------------------------------------------------------===//

__attribute__((unused))
static int treeview_count_visible(tree_node_t *node, int depth) {
    if (!node)
        return 0;

    int count = (depth >= 0) ? 1 : 0; // Don't count root itself

    if (node->expanded || depth < 0) {
        for (int i = 0; i < node->child_count; i++) {
            count += treeview_count_visible(&node->children[i], depth + 1);
        }
    }

    return count;
}

static tree_node_t *treeview_find_at_index(tree_node_t *node, int *index, int depth) {
    if (!node)
        return NULL;

    if (depth >= 0) {
        if (*index == 0)
            return node;
        (*index)--;
    }

    if (node->expanded || depth < 0) {
        for (int i = 0; i < node->child_count; i++) {
            tree_node_t *found = treeview_find_at_index(&node->children[i], index, depth + 1);
            if (found)
                return found;
        }
    }

    return NULL;
}

static int treeview_get_depth(tree_node_t *node) {
    int depth = 0;
    while (node->parent) {
        depth++;
        node = node->parent;
    }
    return depth - 1; // Don't count root
}

static void treeview_paint_node(treeview_t *tv, gui_window_t *win, tree_node_t *node, int *y,
                                int depth, int x_base, int y_base) {
    if (!node || depth < 0)
        return;

    int x = x_base + depth * INDENT_WIDTH;

    // Check if visible
    if (*y >= y_base && *y < y_base + tv->base.height - 4) {
        bool is_selected = (node == tv->selected);

        // Draw selection highlight
        if (is_selected) {
            gui_fill_rect(win, x_base, *y, tv->base.width - 4, ITEM_HEIGHT, WB_BLUE);
        }

        // Draw expand/collapse box if has children
        if (node->child_count > 0) {
            int box_x = x - INDENT_WIDTH + 3;
            int box_y = *y + (ITEM_HEIGHT - EXPAND_BOX_SIZE) / 2;

            // Draw box
            gui_fill_rect(win, box_x, box_y, EXPAND_BOX_SIZE, EXPAND_BOX_SIZE, WB_WHITE);
            gui_draw_rect(win, box_x, box_y, EXPAND_BOX_SIZE, EXPAND_BOX_SIZE, WB_BLACK);

            // Draw +/- sign
            int cx = box_x + EXPAND_BOX_SIZE / 2;
            int cy = box_y + EXPAND_BOX_SIZE / 2;

            gui_draw_hline(win, cx - 2, cx + 2, cy, WB_BLACK);
            if (!node->expanded) {
                gui_draw_vline(win, cx, cy - 2, cy + 2, WB_BLACK);
            }
        }

        // Draw text
        uint32_t text_color = is_selected ? WB_WHITE : WB_BLACK;
        if (!tv->base.enabled) {
            text_color = WB_GRAY_MED;
        }

        gui_draw_text(win, x + 4, *y + 4, node->text, text_color);
    }

    *y += ITEM_HEIGHT;

    // Draw children if expanded
    if (node->expanded) {
        for (int i = 0; i < node->child_count; i++) {
            treeview_paint_node(tv, win, &node->children[i], y, depth + 1, x_base, y_base);
        }
    }
}

//===----------------------------------------------------------------------===//
// TreeView Paint Handler
//===----------------------------------------------------------------------===//

static void treeview_paint(widget_t *w, gui_window_t *win) {
    treeview_t *tv = (treeview_t *)w;

    int x = w->x;
    int y = w->y;
    int width = w->width;
    int height = w->height;

    // Draw sunken frame
    draw_3d_sunken(win, x, y, width, height, WB_WHITE, WB_WHITE, WB_GRAY_DARK);

    // Fill background
    gui_fill_rect(win, x + 2, y + 2, width - 4, height - 4, WB_WHITE);

    // Paint nodes
    if (tv->root) {
        int paint_y = y + 2 - tv->scroll_offset * ITEM_HEIGHT;
        for (int i = 0; i < tv->root->child_count; i++) {
            treeview_paint_node(tv, win, &tv->root->children[i], &paint_y, 0, x + 2, y + 2);
        }
    }
}

//===----------------------------------------------------------------------===//
// TreeView Event Handlers
//===----------------------------------------------------------------------===//

static void treeview_click(widget_t *w, int x, int y, int button) {
    if (button != 0)
        return;

    treeview_t *tv = (treeview_t *)w;

    // Calculate which item was clicked
    int item_index = (y - 2) / ITEM_HEIGHT + tv->scroll_offset;
    int temp_index = item_index;

    tree_node_t *clicked_node = NULL;
    if (tv->root) {
        for (int i = 0; i < tv->root->child_count && !clicked_node; i++) {
            clicked_node = treeview_find_at_index(&tv->root->children[i], &temp_index, 0);
        }
    }

    if (!clicked_node)
        return;

    // Check if click is on expand box
    int depth = treeview_get_depth(clicked_node);
    int box_x = 2 + depth * INDENT_WIDTH - INDENT_WIDTH + 3;

    if (clicked_node->child_count > 0 && x >= box_x && x < box_x + EXPAND_BOX_SIZE) {
        // Toggle expansion
        clicked_node->expanded = !clicked_node->expanded;
        if (tv->on_expand) {
            tv->on_expand(clicked_node, tv->callback_data);
        }
    } else {
        // Select node
        tv->selected = clicked_node;
        if (tv->on_select) {
            tv->on_select(clicked_node, tv->callback_data);
        }
    }
}

static void treeview_key(widget_t *w, int keycode, char ch) {
    (void)ch;
    treeview_t *tv = (treeview_t *)w;

    if (!tv->selected)
        return;

    switch (keycode) {
    case 0x52: // Up arrow
        // Find previous visible node
        // Simplified: just select parent if no previous sibling
        if (tv->selected->parent && tv->selected->parent != tv->root) {
            tree_node_t *parent = tv->selected->parent;
            int idx = -1;
            for (int i = 0; i < parent->child_count; i++) {
                if (&parent->children[i] == tv->selected) {
                    idx = i;
                    break;
                }
            }
            if (idx > 0) {
                tv->selected = &parent->children[idx - 1];
            } else {
                tv->selected = parent;
            }
            if (tv->on_select) {
                tv->on_select(tv->selected, tv->callback_data);
            }
        }
        break;

    case 0x51: // Down arrow
        // Find next visible node
        if (tv->selected->expanded && tv->selected->child_count > 0) {
            tv->selected = &tv->selected->children[0];
        } else if (tv->selected->parent) {
            tree_node_t *parent = tv->selected->parent;
            int idx = -1;
            for (int i = 0; i < parent->child_count; i++) {
                if (&parent->children[i] == tv->selected) {
                    idx = i;
                    break;
                }
            }
            if (idx >= 0 && idx < parent->child_count - 1) {
                tv->selected = &parent->children[idx + 1];
            }
        }
        if (tv->on_select) {
            tv->on_select(tv->selected, tv->callback_data);
        }
        break;

    case 0x50: // Left arrow - collapse or go to parent
        if (tv->selected->expanded && tv->selected->child_count > 0) {
            tv->selected->expanded = false;
            if (tv->on_expand) {
                tv->on_expand(tv->selected, tv->callback_data);
            }
        } else if (tv->selected->parent && tv->selected->parent != tv->root) {
            tv->selected = tv->selected->parent;
            if (tv->on_select) {
                tv->on_select(tv->selected, tv->callback_data);
            }
        }
        break;

    case 0x4F: // Right arrow - expand or go to first child
        if (tv->selected->child_count > 0) {
            if (!tv->selected->expanded) {
                tv->selected->expanded = true;
                if (tv->on_expand) {
                    tv->on_expand(tv->selected, tv->callback_data);
                }
            } else {
                tv->selected = &tv->selected->children[0];
                if (tv->on_select) {
                    tv->on_select(tv->selected, tv->callback_data);
                }
            }
        }
        break;
    }
}

//===----------------------------------------------------------------------===//
// TreeView API
//===----------------------------------------------------------------------===//

treeview_t *treeview_create(widget_t *parent) {
    treeview_t *tv = (treeview_t *)malloc(sizeof(treeview_t));
    if (!tv)
        return NULL;

    memset(tv, 0, sizeof(treeview_t));

    // Initialize base widget
    tv->base.type = WIDGET_TREEVIEW;
    tv->base.parent = parent;
    tv->base.visible = true;
    tv->base.enabled = true;
    tv->base.bg_color = WB_WHITE;
    tv->base.fg_color = WB_BLACK;
    tv->base.width = 200;
    tv->base.height = 150;

    // Set handlers
    tv->base.on_paint = treeview_paint;
    tv->base.on_click = treeview_click;
    tv->base.on_key = treeview_key;

    // Create invisible root node
    tv->root = (tree_node_t *)malloc(sizeof(tree_node_t));
    if (!tv->root) {
        free(tv);
        return NULL;
    }
    memset(tv->root, 0, sizeof(tree_node_t));
    tv->root->expanded = true;

    // Add to parent
    if (parent) {
        widget_add_child(parent, (widget_t *)tv);
    }

    return tv;
}

static void tree_node_free(tree_node_t *node) {
    if (!node)
        return;

    for (int i = 0; i < node->child_count; i++) {
        tree_node_free(&node->children[i]);
    }
    free(node->children);
}

tree_node_t *treeview_add_node(treeview_t *tv, tree_node_t *parent, const char *text) {
    if (!tv)
        return NULL;

    if (!parent) {
        parent = tv->root;
    }

    // Grow children array if needed
    if (parent->child_count >= parent->child_capacity) {
        int new_cap = parent->child_capacity ? parent->child_capacity * 2 : INITIAL_CAPACITY;
        tree_node_t *new_children =
            (tree_node_t *)realloc(parent->children, new_cap * sizeof(tree_node_t));
        if (!new_children)
            return NULL;
        parent->children = new_children;
        parent->child_capacity = new_cap;
    }

    tree_node_t *node = &parent->children[parent->child_count++];
    memset(node, 0, sizeof(tree_node_t));
    node->parent = parent;

    if (text) {
        strncpy(node->text, text, sizeof(node->text) - 1);
        node->text[sizeof(node->text) - 1] = '\0';
    }

    return node;
}

void treeview_remove_node(treeview_t *tv, tree_node_t *node) {
    if (!tv || !node || !node->parent)
        return;

    tree_node_t *parent = node->parent;

    // Find index
    int idx = -1;
    for (int i = 0; i < parent->child_count; i++) {
        if (&parent->children[i] == node) {
            idx = i;
            break;
        }
    }

    if (idx < 0)
        return;

    // Free node's children
    tree_node_free(node);

    // Clear selection if needed
    if (tv->selected == node) {
        tv->selected = NULL;
    }

    // Shift remaining children
    memmove(&parent->children[idx], &parent->children[idx + 1],
            (parent->child_count - idx - 1) * sizeof(tree_node_t));
    parent->child_count--;
}

void treeview_clear(treeview_t *tv) {
    if (!tv || !tv->root)
        return;

    for (int i = 0; i < tv->root->child_count; i++) {
        tree_node_free(&tv->root->children[i]);
    }
    free(tv->root->children);
    tv->root->children = NULL;
    tv->root->child_count = 0;
    tv->root->child_capacity = 0;
    tv->selected = NULL;
    tv->scroll_offset = 0;
}

tree_node_t *treeview_get_root(treeview_t *tv) {
    return tv ? tv->root : NULL;
}

tree_node_t *treeview_get_selected(treeview_t *tv) {
    return tv ? tv->selected : NULL;
}

void treeview_set_selected(treeview_t *tv, tree_node_t *node) {
    if (tv) {
        tv->selected = node;
    }
}

void treeview_expand(treeview_t *tv, tree_node_t *node) {
    (void)tv;
    if (node) {
        node->expanded = true;
    }
}

void treeview_collapse(treeview_t *tv, tree_node_t *node) {
    (void)tv;
    if (node) {
        node->expanded = false;
    }
}

void treeview_toggle(treeview_t *tv, tree_node_t *node) {
    (void)tv;
    if (node) {
        node->expanded = !node->expanded;
    }
}

void treeview_set_onselect(treeview_t *tv, treeview_select_fn callback, void *data) {
    if (tv) {
        tv->on_select = callback;
        tv->callback_data = data;
    }
}

void treeview_set_onexpand(treeview_t *tv, treeview_select_fn callback, void *data) {
    if (tv) {
        tv->on_expand = callback;
        tv->callback_data = data;
    }
}

//===----------------------------------------------------------------------===//
// Tree Node API
//===----------------------------------------------------------------------===//

void tree_node_set_text(tree_node_t *node, const char *text) {
    if (node && text) {
        strncpy(node->text, text, sizeof(node->text) - 1);
        node->text[sizeof(node->text) - 1] = '\0';
    }
}

const char *tree_node_get_text(tree_node_t *node) {
    return node ? node->text : NULL;
}

void tree_node_set_user_data(tree_node_t *node, void *data) {
    if (node) {
        node->user_data = data;
    }
}

void *tree_node_get_user_data(tree_node_t *node) {
    return node ? node->user_data : NULL;
}

int tree_node_get_child_count(tree_node_t *node) {
    return node ? node->child_count : 0;
}

tree_node_t *tree_node_get_child(tree_node_t *node, int index) {
    if (!node || index < 0 || index >= node->child_count)
        return NULL;
    return &node->children[index];
}

tree_node_t *tree_node_get_parent(tree_node_t *node) {
    return node ? node->parent : NULL;
}
