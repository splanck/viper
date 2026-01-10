// vg_treeview.c - TreeView widget implementation
#include "../../include/vg_ide_widgets.h"
#include "../../include/vg_theme.h"
#include "../../include/vg_event.h"
#include <stdlib.h>
#include <string.h>

//=============================================================================
// Forward Declarations
//=============================================================================

static void treeview_destroy(vg_widget_t* widget);
static void treeview_measure(vg_widget_t* widget, float available_width, float available_height);
static void treeview_paint(vg_widget_t* widget, void* canvas);
static bool treeview_handle_event(vg_widget_t* widget, vg_event_t* event);
static bool treeview_can_focus(vg_widget_t* widget);

static void free_node(vg_tree_node_t* node);
static int count_visible_nodes(vg_tree_node_t* node);
static vg_tree_node_t* get_node_at_index(vg_tree_node_t* root, int index, int* current);
static int get_node_index(vg_tree_node_t* root, vg_tree_node_t* target, int* current);

//=============================================================================
// TreeView VTable
//=============================================================================

static vg_widget_vtable_t g_treeview_vtable = {
    .destroy = treeview_destroy,
    .measure = treeview_measure,
    .arrange = NULL,
    .paint = treeview_paint,
    .handle_event = treeview_handle_event,
    .can_focus = treeview_can_focus,
    .on_focus = NULL
};

//=============================================================================
// Helper Functions
//=============================================================================

static void free_node(vg_tree_node_t* node) {
    if (!node) return;

    // Free children recursively
    vg_tree_node_t* child = node->first_child;
    while (child) {
        vg_tree_node_t* next = child->next_sibling;
        free_node(child);
        child = next;
    }

    if (node->text) {
        free((void*)node->text);
    }
    free(node);
}

static int count_visible_nodes(vg_tree_node_t* node) {
    if (!node) return 0;

    int count = 0;
    for (vg_tree_node_t* child = node->first_child; child; child = child->next_sibling) {
        count++;  // Count this child
        if (child->expanded) {
            count += count_visible_nodes(child);  // Count expanded children
        }
    }
    return count;
}

static vg_tree_node_t* get_node_at_index(vg_tree_node_t* root, int target_index, int* current) {
    for (vg_tree_node_t* child = root->first_child; child; child = child->next_sibling) {
        if (*current == target_index) {
            return child;
        }
        (*current)++;

        if (child->expanded && child->first_child) {
            vg_tree_node_t* found = get_node_at_index(child, target_index, current);
            if (found) return found;
        }
    }
    return NULL;
}

static int get_node_index(vg_tree_node_t* root, vg_tree_node_t* target, int* current) {
    for (vg_tree_node_t* child = root->first_child; child; child = child->next_sibling) {
        if (child == target) {
            return *current;
        }
        (*current)++;

        if (child->expanded && child->first_child) {
            int found = get_node_index(child, target, current);
            if (found >= 0) return found;
        }
    }
    return -1;
}

//=============================================================================
// TreeView Implementation
//=============================================================================

vg_treeview_t* vg_treeview_create(vg_widget_t* parent) {
    vg_treeview_t* tree = calloc(1, sizeof(vg_treeview_t));
    if (!tree) return NULL;

    // Initialize base widget
    vg_widget_init(&tree->base, VG_WIDGET_TREEVIEW, &g_treeview_vtable);

    // Create root node
    tree->root = calloc(1, sizeof(vg_tree_node_t));
    if (!tree->root) {
        free(tree);
        return NULL;
    }
    tree->root->expanded = true;  // Root is always expanded
    tree->root->depth = -1;

    // Get theme
    vg_theme_t* theme = vg_theme_get_current();

    // Initialize treeview-specific fields
    tree->selected = NULL;
    tree->font = NULL;
    tree->font_size = theme->typography.size_normal;

    // Appearance
    tree->row_height = 22.0f;
    tree->indent_size = 16.0f;
    tree->icon_size = 16.0f;
    tree->icon_gap = 4.0f;
    tree->text_color = theme->colors.fg_primary;
    tree->selected_bg = theme->colors.bg_selected;
    tree->hover_bg = theme->colors.bg_hover;

    // Scrolling
    tree->scroll_y = 0;
    tree->visible_start = 0;
    tree->visible_count = 0;

    // Callbacks
    tree->on_select = NULL;
    tree->on_select_data = NULL;
    tree->on_expand = NULL;
    tree->on_expand_data = NULL;
    tree->on_activate = NULL;
    tree->on_activate_data = NULL;

    // State
    tree->hovered = NULL;

    // Set minimum size
    tree->base.constraints.min_width = 100.0f;
    tree->base.constraints.min_height = 100.0f;

    // Add to parent
    if (parent) {
        vg_widget_add_child(parent, &tree->base);
    }

    return tree;
}

static void treeview_destroy(vg_widget_t* widget) {
    vg_treeview_t* tree = (vg_treeview_t*)widget;

    if (tree->root) {
        free_node(tree->root);
        tree->root = NULL;
    }
}

static void treeview_measure(vg_widget_t* widget, float available_width, float available_height) {
    vg_treeview_t* tree = (vg_treeview_t*)widget;

    int visible = count_visible_nodes(tree->root);
    float content_height = visible * tree->row_height;

    widget->measured_width = available_width > 0 ? available_width : 200;
    widget->measured_height = content_height;

    if (widget->measured_height < available_height && available_height > 0) {
        widget->measured_height = available_height;
    }

    // Apply constraints
    if (widget->measured_width < widget->constraints.min_width) {
        widget->measured_width = widget->constraints.min_width;
    }
    if (widget->measured_height < widget->constraints.min_height) {
        widget->measured_height = widget->constraints.min_height;
    }
}

static void paint_node(vg_treeview_t* tree, void* canvas, vg_tree_node_t* node,
                       float x, float* y, float width) {
    vg_theme_t* theme = vg_theme_get_current();

    for (vg_tree_node_t* child = node->first_child; child; child = child->next_sibling) {
        float row_y = *y;

        // Check if visible
        if (row_y + tree->row_height >= tree->scroll_y &&
            row_y < tree->scroll_y + tree->base.height) {

            float display_y = tree->base.y + row_y - tree->scroll_y;
            float indent = x + child->depth * tree->indent_size;

            // Draw background for selected/hovered
            if (child == tree->selected) {
                // Draw selected background
                // TODO: Use vgfx primitives
                (void)tree->selected_bg;
            } else if (child == tree->hovered) {
                // Draw hover background
                // TODO: Use vgfx primitives
                (void)tree->hover_bg;
            }

            // Draw expand/collapse arrow if has children
            if (child->has_children || child->first_child) {
                // TODO: Draw arrow/triangle indicator
                (void)theme;
            }

            // Draw text
            if (tree->font && child->text) {
                vg_font_metrics_t font_metrics;
                vg_font_get_metrics(tree->font, tree->font_size, &font_metrics);

                float text_x = indent + tree->icon_size + tree->icon_gap;
                float text_y = display_y + (tree->row_height + font_metrics.ascent - font_metrics.descent) / 2.0f;

                vg_font_draw_text(canvas, tree->font, tree->font_size,
                                  text_x, text_y, child->text, tree->text_color);
            }
        }

        *y += tree->row_height;

        // Paint children if expanded
        if (child->expanded && child->first_child) {
            paint_node(tree, canvas, child, x, y, width);
        }
    }
}

static void treeview_paint(vg_widget_t* widget, void* canvas) {
    vg_treeview_t* tree = (vg_treeview_t*)widget;

    // Draw background
    // TODO: Use vgfx primitives

    // Paint nodes
    float y = 0;
    paint_node(tree, canvas, tree->root, widget->x, &y, widget->width);
}

static vg_tree_node_t* find_node_at_y(vg_treeview_t* tree, vg_tree_node_t* node, float target_y, float* current_y) {
    for (vg_tree_node_t* child = node->first_child; child; child = child->next_sibling) {
        float row_start = *current_y;
        float row_end = row_start + tree->row_height;

        if (target_y >= row_start && target_y < row_end) {
            return child;
        }

        *current_y += tree->row_height;

        if (child->expanded && child->first_child) {
            vg_tree_node_t* found = find_node_at_y(tree, child, target_y, current_y);
            if (found) return found;
        }
    }
    return NULL;
}

static bool treeview_handle_event(vg_widget_t* widget, vg_event_t* event) {
    vg_treeview_t* tree = (vg_treeview_t*)widget;

    if (widget->state & VG_STATE_DISABLED) {
        return false;
    }

    switch (event->type) {
        case VG_EVENT_MOUSE_MOVE: {
            // Find node at position
            float y = event->mouse.y - widget->y + tree->scroll_y;
            float current_y = 0;
            vg_tree_node_t* old_hover = tree->hovered;
            tree->hovered = find_node_at_y(tree, tree->root, y, &current_y);
            if (old_hover != tree->hovered) {
                widget->needs_paint = true;
            }
            return false;
        }

        case VG_EVENT_MOUSE_LEAVE:
            if (tree->hovered) {
                tree->hovered = NULL;
                widget->needs_paint = true;
            }
            return false;

        case VG_EVENT_CLICK: {
            float y = event->mouse.y - widget->y + tree->scroll_y;
            float current_y = 0;
            vg_tree_node_t* clicked = find_node_at_y(tree, tree->root, y, &current_y);

            if (clicked) {
                // Check if clicked on expand arrow
                float indent = clicked->depth * tree->indent_size;
                if (event->mouse.x < indent + tree->icon_size) {
                    // Toggle expand
                    vg_treeview_toggle(tree, clicked);
                } else {
                    // Select node
                    vg_treeview_select(tree, clicked);
                }
                return true;
            }
            return false;
        }

        case VG_EVENT_DOUBLE_CLICK: {
            if (tree->selected && tree->on_activate) {
                tree->on_activate(widget, tree->selected, tree->on_activate_data);
            }
            return true;
        }

        case VG_EVENT_KEY_DOWN:
            if (tree->selected) {
                switch (event->key.key) {
                    case VG_KEY_UP: {
                        // Select previous node
                        int current = 0;
                        int index = get_node_index(tree->root, tree->selected, &current);
                        if (index > 0) {
                            current = 0;
                            vg_tree_node_t* prev = get_node_at_index(tree->root, index - 1, &current);
                            if (prev) {
                                vg_treeview_select(tree, prev);
                            }
                        }
                        return true;
                    }
                    case VG_KEY_DOWN: {
                        // Select next node
                        int current = 0;
                        int index = get_node_index(tree->root, tree->selected, &current);
                        current = 0;
                        vg_tree_node_t* next = get_node_at_index(tree->root, index + 1, &current);
                        if (next) {
                            vg_treeview_select(tree, next);
                        }
                        return true;
                    }
                    case VG_KEY_LEFT:
                        // Collapse or go to parent
                        if (tree->selected->expanded && tree->selected->first_child) {
                            vg_treeview_collapse(tree, tree->selected);
                        } else if (tree->selected->parent && tree->selected->parent != tree->root) {
                            vg_treeview_select(tree, tree->selected->parent);
                        }
                        return true;
                    case VG_KEY_RIGHT:
                        // Expand or go to first child
                        if (!tree->selected->expanded && (tree->selected->has_children || tree->selected->first_child)) {
                            vg_treeview_expand(tree, tree->selected);
                        } else if (tree->selected->first_child) {
                            vg_treeview_select(tree, tree->selected->first_child);
                        }
                        return true;
                    case VG_KEY_ENTER:
                        if (tree->on_activate) {
                            tree->on_activate(widget, tree->selected, tree->on_activate_data);
                        }
                        return true;
                    default:
                        break;
                }
            }
            return false;

        case VG_EVENT_MOUSE_WHEEL:
            tree->scroll_y -= event->wheel.delta_y * tree->row_height * 3;
            if (tree->scroll_y < 0) tree->scroll_y = 0;

            int visible = count_visible_nodes(tree->root);
            float max_scroll = visible * tree->row_height - widget->height;
            if (max_scroll < 0) max_scroll = 0;
            if (tree->scroll_y > max_scroll) tree->scroll_y = max_scroll;

            widget->needs_paint = true;
            return true;

        default:
            break;
    }

    return false;
}

static bool treeview_can_focus(vg_widget_t* widget) {
    return widget->enabled && widget->visible;
}

//=============================================================================
// TreeView API
//=============================================================================

vg_tree_node_t* vg_treeview_get_root(vg_treeview_t* tree) {
    return tree ? tree->root : NULL;
}

vg_tree_node_t* vg_treeview_add_node(vg_treeview_t* tree, vg_tree_node_t* parent, const char* text) {
    if (!tree) return NULL;

    vg_tree_node_t* node = calloc(1, sizeof(vg_tree_node_t));
    if (!node) return NULL;

    node->text = text ? strdup(text) : strdup("");
    node->expanded = false;
    node->selected = false;
    node->has_children = false;

    // Add to parent
    vg_tree_node_t* actual_parent = parent ? parent : tree->root;
    node->parent = actual_parent;
    node->depth = actual_parent->depth + 1;

    if (actual_parent->last_child) {
        actual_parent->last_child->next_sibling = node;
        node->prev_sibling = actual_parent->last_child;
        actual_parent->last_child = node;
    } else {
        actual_parent->first_child = node;
        actual_parent->last_child = node;
    }
    actual_parent->child_count++;
    actual_parent->has_children = true;

    tree->base.needs_layout = true;
    tree->base.needs_paint = true;

    return node;
}

void vg_treeview_remove_node(vg_treeview_t* tree, vg_tree_node_t* node) {
    if (!tree || !node || node == tree->root) return;

    // Update selection if needed
    if (tree->selected == node) {
        tree->selected = NULL;
    }
    if (tree->hovered == node) {
        tree->hovered = NULL;
    }

    // Remove from parent's child list
    vg_tree_node_t* parent = node->parent;
    if (parent) {
        if (node->prev_sibling) {
            node->prev_sibling->next_sibling = node->next_sibling;
        } else {
            parent->first_child = node->next_sibling;
        }
        if (node->next_sibling) {
            node->next_sibling->prev_sibling = node->prev_sibling;
        } else {
            parent->last_child = node->prev_sibling;
        }
        parent->child_count--;
        parent->has_children = parent->first_child != NULL;
    }

    free_node(node);

    tree->base.needs_layout = true;
    tree->base.needs_paint = true;
}

void vg_treeview_clear(vg_treeview_t* tree) {
    if (!tree) return;

    // Free all children of root
    vg_tree_node_t* child = tree->root->first_child;
    while (child) {
        vg_tree_node_t* next = child->next_sibling;
        free_node(child);
        child = next;
    }

    tree->root->first_child = NULL;
    tree->root->last_child = NULL;
    tree->root->child_count = 0;
    tree->root->has_children = false;
    tree->selected = NULL;
    tree->hovered = NULL;
    tree->scroll_y = 0;

    tree->base.needs_layout = true;
    tree->base.needs_paint = true;
}

void vg_treeview_expand(vg_treeview_t* tree, vg_tree_node_t* node) {
    if (!tree || !node) return;

    if (!node->expanded) {
        node->expanded = true;
        tree->base.needs_layout = true;
        tree->base.needs_paint = true;

        if (tree->on_expand) {
            tree->on_expand(&tree->base, node, true, tree->on_expand_data);
        }
    }
}

void vg_treeview_collapse(vg_treeview_t* tree, vg_tree_node_t* node) {
    if (!tree || !node) return;

    if (node->expanded) {
        node->expanded = false;
        tree->base.needs_layout = true;
        tree->base.needs_paint = true;

        if (tree->on_expand) {
            tree->on_expand(&tree->base, node, false, tree->on_expand_data);
        }
    }
}

void vg_treeview_toggle(vg_treeview_t* tree, vg_tree_node_t* node) {
    if (!tree || !node) return;

    if (node->expanded) {
        vg_treeview_collapse(tree, node);
    } else {
        vg_treeview_expand(tree, node);
    }
}

void vg_treeview_select(vg_treeview_t* tree, vg_tree_node_t* node) {
    if (!tree) return;

    if (tree->selected != node) {
        if (tree->selected) {
            tree->selected->selected = false;
        }
        tree->selected = node;
        if (node) {
            node->selected = true;
        }
        tree->base.needs_paint = true;

        if (tree->on_select && node) {
            tree->on_select(&tree->base, node, tree->on_select_data);
        }
    }
}

void vg_treeview_scroll_to(vg_treeview_t* tree, vg_tree_node_t* node) {
    if (!tree || !node) return;

    // Get node index
    int current = 0;
    int index = get_node_index(tree->root, node, &current);
    if (index < 0) return;

    float node_y = index * tree->row_height;

    // Scroll if needed
    if (node_y < tree->scroll_y) {
        tree->scroll_y = node_y;
    } else if (node_y + tree->row_height > tree->scroll_y + tree->base.height) {
        tree->scroll_y = node_y + tree->row_height - tree->base.height;
    }

    tree->base.needs_paint = true;
}

void vg_tree_node_set_data(vg_tree_node_t* node, void* data) {
    if (node) {
        node->user_data = data;
    }
}

void vg_treeview_set_font(vg_treeview_t* tree, vg_font_t* font, float size) {
    if (!tree) return;

    tree->font = font;
    tree->font_size = size > 0 ? size : vg_theme_get_current()->typography.size_normal;
    tree->base.needs_layout = true;
    tree->base.needs_paint = true;
}

void vg_treeview_set_on_select(vg_treeview_t* tree, vg_tree_select_callback_t callback, void* user_data) {
    if (!tree) return;

    tree->on_select = callback;
    tree->on_select_data = user_data;
}

void vg_treeview_set_on_expand(vg_treeview_t* tree, vg_tree_expand_callback_t callback, void* user_data) {
    if (!tree) return;

    tree->on_expand = callback;
    tree->on_expand_data = user_data;
}

void vg_treeview_set_on_activate(vg_treeview_t* tree, vg_tree_activate_callback_t callback, void* user_data) {
    if (!tree) return;

    tree->on_activate = callback;
    tree->on_activate_data = user_data;
}
