//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
/**
 * @file treeview.c
 * @brief Hierarchical tree view widget implementation for the libwidget toolkit.
 *
 * This file implements a tree view widget that displays a hierarchical structure
 * of nodes with expand/collapse functionality. Tree views are essential for
 * displaying nested data structures, commonly used for:
 * - File system browsers (folders and files)
 * - Configuration hierarchies (settings categories)
 * - Object explorers (property trees)
 * - Document outlines
 *
 * ## Node Structure
 *
 * Each node in the tree contains:
 * - **text**: Display label (up to 63 characters)
 * - **children**: Array of child nodes (dynamically sized)
 * - **expanded**: Whether children are visible
 * - **user_data**: Application-specific pointer for custom data
 *
 * ## Visual Design
 *
 * The tree view consists of:
 * 1. **Sunken Frame**: A 3D sunken border indicating an interactive content area
 * 2. **Indented Nodes**: Each level indents 16 pixels from its parent
 * 3. **Expand/Collapse Boxes**: 9x9 pixel boxes with +/- symbols for nodes with children
 * 4. **Selection Highlight**: Blue background for the selected node
 *
 * ## Interaction Model
 *
 * - **Clicking the +/- box**: Toggles the expanded state of that node
 * - **Clicking the node text**: Selects that node
 * - **Arrow keys**: Navigate up/down through visible nodes
 * - **Left arrow**: Collapse current node or move to parent
 * - **Right arrow**: Expand current node or move to first child
 *
 * ## Invisible Root
 *
 * The tree has an invisible root node that is never displayed but serves as the
 * parent for all top-level visible nodes. This simplifies the tree structure
 * by ensuring every visible node has a parent.
 *
 * @see widget.h for the treeview_t and tree_node_t structure definitions
 */
//===----------------------------------------------------------------------===//

#include <widget.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Height of each tree node row in pixels.
 *
 * Each node occupies exactly 18 pixels of vertical space, providing room for
 * a single line of text plus the expand/collapse box.
 */
#define ITEM_HEIGHT      18

/**
 * @brief Horizontal indentation per tree level in pixels.
 *
 * Each level of depth adds 16 pixels of left indentation. This creates the
 * visual hierarchy that makes parent-child relationships clear.
 */
#define INDENT_WIDTH     16

/**
 * @brief Size of the expand/collapse box in pixels (9x9 square).
 *
 * The box contains a + (collapsed) or - (expanded) symbol and is positioned
 * just to the left of the node's text.
 */
#define EXPAND_BOX_SIZE  9

/**
 * @brief Initial capacity for a node's children array.
 *
 * Nodes start with space for 8 children. When this capacity is exceeded,
 * the array doubles in size.
 */
#define INITIAL_CAPACITY 8

//===----------------------------------------------------------------------===//
// Internal Helpers
//===----------------------------------------------------------------------===//

/**
 * @brief Counts the total number of visible nodes in a subtree.
 *
 * This function recursively counts how many nodes would be visible starting
 * from a given node. A node is visible if all its ancestors are expanded.
 *
 * The count includes:
 * - The node itself (if depth >= 0)
 * - All visible descendants (children of expanded nodes)
 *
 * @param node  The root of the subtree to count.
 * @param depth The current depth. Pass -1 for the invisible root (it won't be
 *              counted), 0 for top-level visible nodes.
 *
 * @return The number of visible nodes in the subtree.
 *
 * @note This function is marked unused because it's for future scrollbar
 *       calculations but is currently not called.
 */
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

/**
 * @brief Finds the visible node at a given visual index.
 *
 * This function maps a visual row index (0 = first visible node, 1 = second,
 * etc.) to the actual tree_node_t. It walks the tree in display order,
 * counting visible nodes until it reaches the target index.
 *
 * @param node  The current node to examine (start with root's children).
 * @param index Pointer to the remaining index. Decremented as nodes are passed.
 *              When it reaches 0, we've found the target node.
 * @param depth The current depth (-1 for root, 0+ for visible nodes).
 *
 * @return Pointer to the node at the given index, or NULL if index exceeds
 *         the number of visible nodes.
 *
 * @note The function modifies *index as it traverses, so the caller should
 *       pass a copy if they need to preserve the original value.
 */
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

/**
 * @brief Calculates the depth of a node in the tree.
 *
 * The depth is the number of ancestor nodes between this node and the invisible
 * root. Top-level visible nodes have depth 0, their children have depth 1, etc.
 *
 * @param node The node whose depth to calculate.
 *
 * @return The depth of the node (0 for top-level, 1+ for nested nodes).
 *         Returns a negative value if node is the root or has no parent.
 */
static int treeview_get_depth(tree_node_t *node) {
    int depth = 0;
    while (node->parent) {
        depth++;
        node = node->parent;
    }
    return depth - 1; // Don't count root
}

/**
 * @brief Recursively paints a node and its visible children.
 *
 * This function draws a single node and, if the node is expanded, recursively
 * draws all its children. Each node is rendered with:
 *
 * 1. **Selection highlight**: Blue background if this node is selected
 * 2. **Expand/collapse box**: A 9x9 box with +/- for nodes with children
 * 3. **Node text**: The display label, white if selected, black otherwise
 *
 * The function tracks the current Y position via the `y` pointer, incrementing
 * it by ITEM_HEIGHT for each node drawn.
 *
 * @param tv     Pointer to the tree view widget.
 * @param win    Pointer to the GUI window for drawing operations.
 * @param node   The node to paint.
 * @param y      Pointer to the current Y coordinate. Updated as nodes are drawn.
 * @param depth  The depth of this node (affects indentation).
 * @param x_base The X coordinate of the content area's left edge.
 * @param y_base The Y coordinate of the content area's top edge.
 *
 * @note Nodes outside the visible viewport (above y_base or below the widget
 *       bottom) are not drawn, but their Y position is still tracked.
 */
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

/**
 * @brief Renders the tree view with frame, nodes, and selection.
 *
 * This paint handler draws the complete tree view visual representation:
 *
 * 1. **Sunken Frame**: A 3D sunken border around the entire widget
 * 2. **White Background**: The content area for clean readability
 * 3. **Tree Nodes**: All visible nodes with proper indentation
 *
 * The painting starts from the invisible root's children (the top-level
 * visible nodes) and recursively paints expanded subtrees.
 *
 * @param w   Pointer to the base widget structure (cast to treeview_t internally).
 * @param win Pointer to the GUI window for drawing operations.
 *
 * @note Scrolling is handled by adjusting the initial Y position based on
 *       scroll_offset before starting the recursive paint.
 */
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

/**
 * @brief Handles mouse click events on the tree view.
 *
 * This handler processes left-button clicks to either:
 * 1. Toggle a node's expanded state (if clicked on the +/- box)
 * 2. Select a node (if clicked on the node's text area)
 *
 * The clicked node is found by mapping the Y coordinate to a visual row index,
 * then walking the tree to find the node at that index.
 *
 * ## Expand/Collapse Detection
 *
 * A click is considered to be on the expand/collapse box if:
 * - The node has children (otherwise there's no box)
 * - The X coordinate is within the 9x9 box region (calculated based on depth)
 *
 * @param w      Pointer to the base widget structure (cast to treeview_t internally).
 * @param x      X coordinate of the click in widget-local space.
 * @param y      Y coordinate of the click in widget-local space.
 * @param button Mouse button identifier. Only left click (button 0) is processed.
 *
 * @note When expanding/collapsing, the on_expand callback is invoked.
 * @note When selecting, the on_select callback is invoked.
 */
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

/**
 * @brief Handles keyboard events for tree navigation.
 *
 * This handler processes arrow keys to navigate the tree structure:
 *
 * ## Supported Keys
 *
 * | Key         | Keycode | Action                                           |
 * |-------------|---------|--------------------------------------------------|
 * | Up Arrow    | 0x52    | Move to previous visible node or parent          |
 * | Down Arrow  | 0x51    | Move to next visible node (child if expanded)    |
 * | Left Arrow  | 0x50    | Collapse node (if expanded) or move to parent    |
 * | Right Arrow | 0x4F    | Expand node (if collapsed) or move to first child|
 *
 * The navigation respects the tree structure:
 * - Up/Down move through visually adjacent nodes
 * - Left/Right navigate the parent-child hierarchy
 *
 * @param w       Pointer to the base widget structure (cast to treeview_t internally).
 * @param keycode The USB HID keycode of the pressed key.
 * @param ch      The character representation of the key (unused).
 *
 * @note Navigation is only active when a node is selected.
 * @note The on_select and on_expand callbacks are invoked as appropriate.
 */
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

/**
 * @brief Creates a new tree view widget with an empty tree.
 *
 * This function allocates and initializes a tree view with an invisible root
 * node. The root node is never displayed but serves as the parent for all
 * top-level visible nodes.
 *
 * Default properties:
 * - **Size**: 200x150 pixels
 * - **Position**: (0, 0) - use widget_set_position() to place
 * - **Colors**: White background, black text
 * - **Selection**: No node selected
 * - **Scrolling**: Starts at top (scroll_offset = 0)
 *
 * ## Tree Construction
 *
 * After creating the tree view, add nodes using treeview_add_node():
 *
 * @code
 * treeview_t *tv = treeview_create(parent);
 *
 * // Add top-level nodes (pass NULL as parent)
 * tree_node_t *folder1 = treeview_add_node(tv, NULL, "Documents");
 * tree_node_t *folder2 = treeview_add_node(tv, NULL, "Pictures");
 *
 * // Add children to a node
 * treeview_add_node(tv, folder1, "Resume.txt");
 * treeview_add_node(tv, folder1, "Notes.md");
 * @endcode
 *
 * @param parent Pointer to the parent widget container. If non-NULL, the tree
 *               view is added to this parent's child list.
 *
 * @return Pointer to the newly created tree view, or NULL if memory allocation
 *         failed.
 *
 * @see treeview_add_node() To add nodes to the tree
 * @see treeview_get_root() To access the invisible root for iteration
 */
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

/**
 * @brief Recursively frees a node and all its descendants.
 *
 * This helper function frees the children array of a node after recursively
 * freeing all child nodes. The node struct itself is not freed because nodes
 * are stored inline in their parent's children array.
 *
 * @param node The node to free. May be NULL (no-op).
 */
static void tree_node_free(tree_node_t *node) {
    if (!node)
        return;

    for (int i = 0; i < node->child_count; i++) {
        tree_node_free(&node->children[i]);
    }
    free(node->children);
}

/**
 * @brief Adds a new child node to the tree.
 *
 * This function creates a new node as a child of the specified parent. If
 * parent is NULL, the node is added as a top-level node (child of the
 * invisible root).
 *
 * The new node is:
 * - Initially collapsed (expanded = false)
 * - Has no children
 * - Has the provided text label
 *
 * @param tv     Pointer to the tree view widget. If NULL, returns NULL.
 * @param parent The parent node, or NULL for a top-level node.
 * @param text   The display text for the new node. Copied into a 64-byte
 *               internal buffer (maximum 63 characters).
 *
 * @return Pointer to the newly created node, or NULL if memory allocation
 *         failed. This pointer can be used as a parent for adding child nodes.
 *
 * @note The children array of the parent is dynamically resized as needed.
 *
 * @see treeview_remove_node() To remove a node
 * @see tree_node_set_text() To change a node's text after creation
 */
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

/**
 * @brief Removes a node and all its descendants from the tree.
 *
 * This function removes the specified node from its parent's children array,
 * freeing the node's subtree in the process. Remaining siblings are shifted
 * to fill the gap.
 *
 * @param tv   Pointer to the tree view widget. If NULL, does nothing.
 * @param node The node to remove. Must not be the invisible root. If NULL or
 *             has no parent, does nothing.
 *
 * @note If the removed node was selected, the selection is cleared.
 *
 * @note The node pointer becomes invalid after this call.
 */
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

/**
 * @brief Removes all nodes from the tree.
 *
 * This function frees all nodes in the tree, leaving only the invisible root.
 * After clearing:
 * - The root has no children
 * - Selection is cleared
 * - Scroll position is reset to top
 *
 * @param tv Pointer to the tree view widget. If NULL, does nothing.
 *
 * @see treeview_remove_node() To remove a single node
 */
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

/**
 * @brief Returns the invisible root node of the tree.
 *
 * The root node is never displayed but serves as the parent for all top-level
 * nodes. Use this to iterate over top-level nodes:
 *
 * @code
 * tree_node_t *root = treeview_get_root(tv);
 * for (int i = 0; i < tree_node_get_child_count(root); i++) {
 *     tree_node_t *child = tree_node_get_child(root, i);
 *     // process top-level node...
 * }
 * @endcode
 *
 * @param tv Pointer to the tree view widget. If NULL, returns NULL.
 *
 * @return Pointer to the invisible root node, or NULL if tv is NULL.
 */
tree_node_t *treeview_get_root(treeview_t *tv) {
    return tv ? tv->root : NULL;
}

/**
 * @brief Returns the currently selected node.
 *
 * @param tv Pointer to the tree view widget. If NULL, returns NULL.
 *
 * @return Pointer to the selected node, or NULL if no node is selected.
 *
 * @see treeview_set_selected() To change the selection programmatically
 */
tree_node_t *treeview_get_selected(treeview_t *tv) {
    return tv ? tv->selected : NULL;
}

/**
 * @brief Programmatically selects a node.
 *
 * This function changes the selected node without triggering the on_select
 * callback.
 *
 * @param tv   Pointer to the tree view widget. If NULL, does nothing.
 * @param node The node to select, or NULL to clear selection.
 *
 * @note This function does NOT invoke the on_select callback.
 *
 * @see treeview_get_selected() To retrieve the current selection
 */
void treeview_set_selected(treeview_t *tv, tree_node_t *node) {
    if (tv) {
        tv->selected = node;
    }
}

/**
 * @brief Expands a node to show its children.
 *
 * @param tv   Pointer to the tree view widget (unused, may be NULL).
 * @param node The node to expand. If NULL, does nothing.
 *
 * @note This function does NOT invoke the on_expand callback.
 *
 * @see treeview_collapse() To hide children
 * @see treeview_toggle() To toggle the expanded state
 */
void treeview_expand(treeview_t *tv, tree_node_t *node) {
    (void)tv;
    if (node) {
        node->expanded = true;
    }
}

/**
 * @brief Collapses a node to hide its children.
 *
 * @param tv   Pointer to the tree view widget (unused, may be NULL).
 * @param node The node to collapse. If NULL, does nothing.
 *
 * @note This function does NOT invoke the on_expand callback.
 *
 * @see treeview_expand() To show children
 * @see treeview_toggle() To toggle the expanded state
 */
void treeview_collapse(treeview_t *tv, tree_node_t *node) {
    (void)tv;
    if (node) {
        node->expanded = false;
    }
}

/**
 * @brief Toggles a node's expanded state.
 *
 * If the node is expanded, it becomes collapsed. If collapsed, it becomes
 * expanded.
 *
 * @param tv   Pointer to the tree view widget (unused, may be NULL).
 * @param node The node to toggle. If NULL, does nothing.
 *
 * @note This function does NOT invoke the on_expand callback.
 *
 * @see treeview_expand() To explicitly expand
 * @see treeview_collapse() To explicitly collapse
 */
void treeview_toggle(treeview_t *tv, tree_node_t *node) {
    (void)tv;
    if (node) {
        node->expanded = !node->expanded;
    }
}

/**
 * @brief Registers a callback for node selection events.
 *
 * The on_select callback is invoked when the user selects a node by clicking
 * on it or using keyboard navigation. The callback receives a pointer to the
 * selected node.
 *
 * @param tv       Pointer to the tree view widget. If NULL, does nothing.
 * @param callback The function to call when selection changes, or NULL to
 *                 remove any existing callback.
 * @param data     User-defined data passed to the callback function.
 *
 * @note The callback is NOT invoked for programmatic selection via
 *       treeview_set_selected().
 */
void treeview_set_onselect(treeview_t *tv, treeview_select_fn callback, void *data) {
    if (tv) {
        tv->on_select = callback;
        tv->callback_data = data;
    }
}

/**
 * @brief Registers a callback for node expand/collapse events.
 *
 * The on_expand callback is invoked when the user expands or collapses a node
 * by clicking the +/- box or using keyboard navigation (Left/Right arrows).
 *
 * @param tv       Pointer to the tree view widget. If NULL, does nothing.
 * @param callback The function to call when a node is expanded/collapsed,
 *                 or NULL to remove any existing callback.
 * @param data     User-defined data passed to the callback function.
 *
 * @note This uses the same callback_data as on_select.
 *
 * @note The callback is NOT invoked for programmatic expansion via
 *       treeview_expand(), treeview_collapse(), or treeview_toggle().
 */
void treeview_set_onexpand(treeview_t *tv, treeview_select_fn callback, void *data) {
    if (tv) {
        tv->on_expand = callback;
        tv->callback_data = data;
    }
}

//===----------------------------------------------------------------------===//
// Tree Node API
//===----------------------------------------------------------------------===//

/**
 * @brief Changes the display text of a tree node.
 *
 * @param node The node to modify. If NULL, does nothing.
 * @param text The new text label. Copied into a 64-byte buffer (max 63 chars).
 *             If NULL, does nothing.
 *
 * @see tree_node_get_text() To retrieve the current text
 */
void tree_node_set_text(tree_node_t *node, const char *text) {
    if (node && text) {
        strncpy(node->text, text, sizeof(node->text) - 1);
        node->text[sizeof(node->text) - 1] = '\0';
    }
}

/**
 * @brief Retrieves the display text of a tree node.
 *
 * @param node The node to query. If NULL, returns NULL.
 *
 * @return Pointer to the node's text buffer (read-only), or NULL if node is NULL.
 *
 * @see tree_node_set_text() To change the text
 */
const char *tree_node_get_text(tree_node_t *node) {
    return node ? node->text : NULL;
}

/**
 * @brief Associates application-specific data with a tree node.
 *
 * The user_data pointer can store any application-specific data that should
 * be associated with the node. Common uses include:
 * - File system entries (file info structs)
 * - Configuration objects
 * - Database records
 *
 * @param node The node to modify. If NULL, does nothing.
 * @param data The user data pointer to store.
 *
 * @note The tree view does not manage this memory. The application is
 *       responsible for freeing user data before the node is removed.
 *
 * @see tree_node_get_user_data() To retrieve the stored data
 */
void tree_node_set_user_data(tree_node_t *node, void *data) {
    if (node) {
        node->user_data = data;
    }
}

/**
 * @brief Retrieves application-specific data from a tree node.
 *
 * @param node The node to query. If NULL, returns NULL.
 *
 * @return The user data pointer previously set, or NULL if none was set.
 *
 * @see tree_node_set_user_data() To store user data
 */
void *tree_node_get_user_data(tree_node_t *node) {
    return node ? node->user_data : NULL;
}

/**
 * @brief Returns the number of children a node has.
 *
 * @param node The node to query. If NULL, returns 0.
 *
 * @return The number of direct children of this node.
 *
 * @see tree_node_get_child() To access individual children
 */
int tree_node_get_child_count(tree_node_t *node) {
    return node ? node->child_count : 0;
}

/**
 * @brief Retrieves a child node by index.
 *
 * @param node  The parent node. If NULL, returns NULL.
 * @param index The zero-based index of the child.
 *
 * @return Pointer to the child node, or NULL if index is out of range.
 *
 * @see tree_node_get_child_count() To get the number of children
 */
tree_node_t *tree_node_get_child(tree_node_t *node, int index) {
    if (!node || index < 0 || index >= node->child_count)
        return NULL;
    return &node->children[index];
}

/**
 * @brief Returns the parent of a tree node.
 *
 * @param node The node to query. If NULL, returns NULL.
 *
 * @return Pointer to the parent node, or NULL if this is the invisible root.
 *
 * @note Top-level visible nodes return the invisible root as their parent.
 */
tree_node_t *tree_node_get_parent(tree_node_t *node) {
    return node ? node->parent : NULL;
}
