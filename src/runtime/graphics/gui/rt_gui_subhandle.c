//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/graphics/gui/rt_gui_subhandle.c
// Purpose: Runtime sub-object handle layer for GUI widgets — wraps non-widget
//          children (tree nodes, tabs, list items, menus/items, context menus,
//          status-bar and tool-bar items) in identity-stable boxed handles.
//          Split out of rt_gui_widgets.c.
//
// Key invariants:
//   - Every live sub-handle is linked into the process-global registry list and
//     carries a magic tag so stale/forged handles are rejected on lookup.
//   - Wrapping is idempotent: an existing handle for a (kind, ptr) pair is
//     reused so identity stays stable across calls.
//   - Invalidation clears the underlying pointer when its owner widget dies,
//     leaving the boxed handle safe to query (returns "invalid").
//
// Ownership/Lifetime:
//   - Sub-handles are GC objects; the registry holds weak links unlinked on
//     finalize. Underlying widget pointers are owned by the widget tree.
//
// Links: src/runtime/graphics/gui/rt_gui_widgets.c (widget lifecycle),
//        src/runtime/graphics/gui/rt_gui_internal.h (shared GUI types + API)
//
//===----------------------------------------------------------------------===//

#include "rt_gui_internal.h"
#include "rt_platform.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

//=============================================================================
// Runtime Subobject Handles
//=============================================================================

typedef enum {
    RT_GUI_HANDLE_TREE_NODE = 1,
    RT_GUI_HANDLE_TAB = 2,
    RT_GUI_HANDLE_LISTBOX_ITEM = 3,
    RT_GUI_HANDLE_MENU = 4,
    RT_GUI_HANDLE_MENU_ITEM = 5,
    RT_GUI_HANDLE_CONTEXTMENU = 6,
    RT_GUI_HANDLE_STATUSBAR_ITEM = 7,
    RT_GUI_HANDLE_TOOLBAR_ITEM = 8,
} rt_gui_subhandle_kind_t;

#define RT_GUI_SUBHANDLE_MAGIC UINT64_C(0x52544755484E444C)

typedef struct rt_gui_subhandle {
    uint64_t magic;
    uint32_t kind;
    void *ptr;
    vg_widget_t *owner_widget;
    struct rt_gui_subhandle *next;
    struct rt_gui_subhandle *prev;
} rt_gui_subhandle_t;

static rt_gui_subhandle_t *s_gui_subhandles = NULL;

/// @brief Detach a subhandle from the global intrusive list, fixing up neighbour
///        links and the list head. Leaves the node's own next/prev nulled.
static void rt_gui_subhandle_unlink(rt_gui_subhandle_t *handle) {
    if (!handle)
        return;
    if (handle->prev)
        handle->prev->next = handle->next;
    else if (s_gui_subhandles == handle)
        s_gui_subhandles = handle->next;
    if (handle->next)
        handle->next->prev = handle->prev;
    handle->next = NULL;
    handle->prev = NULL;
}

/// @brief GC finalizer for a subhandle: unlink it from the list and clear its
///        magic/ptr/owner so any lingering reference is recognized as dead.
static void rt_gui_subhandle_finalize(void *obj) {
    rt_gui_subhandle_t *handle = (rt_gui_subhandle_t *)obj;
    if (!handle)
        return;
    rt_gui_subhandle_unlink(handle);
    handle->magic = 0;
    handle->ptr = NULL;
    handle->owner_widget = NULL;
}

/// @brief Safe-cast an opaque handle to a subhandle of the expected @p kind.
/// @return The subhandle if it has the right object size, magic tag and kind; NULL otherwise.
static rt_gui_subhandle_t *rt_gui_subhandle_checked(void *handle, rt_gui_subhandle_kind_t kind) {
    if (!rt_obj_is_instance(handle, 0, sizeof(rt_gui_subhandle_t)))
        return NULL;
    rt_gui_subhandle_t *sub = (rt_gui_subhandle_t *)handle;
    if (sub->magic != RT_GUI_SUBHANDLE_MAGIC || sub->kind != (uint32_t)kind)
        return NULL;
    return sub;
}

/// @brief Mark a subhandle's target as gone by nulling its ptr/owner, while keeping
///        the handle object itself alive so stale script references fail gracefully.
static void rt_gui_subhandle_invalidate(rt_gui_subhandle_t *handle) {
    if (!handle || handle->magic != RT_GUI_SUBHANDLE_MAGIC)
        return;
    handle->ptr = NULL;
    handle->owner_widget = NULL;
}

/// @brief True if the subhandle's owner widget is still alive (or it has no owner).
/// @details A dead owner triggers invalidation (ptr/owner nulled) and a false return,
///          so callers never dereference a sub-object whose widget has been destroyed.
static bool rt_gui_subhandle_owner_is_live(rt_gui_subhandle_t *handle) {
    if (!handle)
        return false;
    if (!handle->owner_widget)
        return true;
    if (vg_widget_is_live(handle->owner_widget))
        return true;
    rt_gui_subhandle_invalidate(handle);
    return false;
}

/// @brief Find the top-level widget that owns a menu item — its context menu, or
///        the menubar reached through its parent menu. NULL if free-floating.
static vg_widget_t *rt_gui_owner_widget_for_menu_item(vg_menu_item_t *item) {
    if (!item)
        return NULL;
    if (item->owner_contextmenu)
        return &item->owner_contextmenu->base;
    if (item->parent_menu && item->parent_menu->owner_menubar)
        return &item->parent_menu->owner_menubar->base;
    return NULL;
}

/// @brief The menubar widget that owns a menu, or NULL if the menu isn't in a menubar.
static vg_widget_t *rt_gui_owner_widget_for_menu(vg_menu_t *menu) {
    return menu && menu->owner_menubar ? &menu->owner_menubar->base : NULL;
}

/// @brief Linear-search the global list for an existing subhandle wrapping (@p kind, @p ptr).
static rt_gui_subhandle_t *rt_gui_find_subhandle(rt_gui_subhandle_kind_t kind, void *ptr) {
    if (!ptr)
        return NULL;
    for (rt_gui_subhandle_t *handle = s_gui_subhandles; handle; handle = handle->next) {
        if (handle->magic == RT_GUI_SUBHANDLE_MAGIC && handle->kind == (uint32_t)kind &&
            handle->ptr == ptr)
            return handle;
    }
    return NULL;
}

/// @brief Get-or-create the GC subhandle that wraps a widget sub-object.
/// @details The entry point of the subhandle system: returns the existing handle for
///          (@p kind, @p ptr) — refreshing its owner — or allocates a new one and links
///          it into the global list. Reuse guarantees a stable identity per sub-object
///          across calls. Returns NULL on NULL ptr or allocation failure.
static void *rt_gui_wrap_subhandle(rt_gui_subhandle_kind_t kind,
                                   void *ptr,
                                   vg_widget_t *owner_widget) {
    if (!ptr)
        return NULL;
    rt_gui_subhandle_t *existing = rt_gui_find_subhandle(kind, ptr);
    if (existing) {
        existing->owner_widget = owner_widget;
        return existing;
    }
    rt_gui_subhandle_t *handle =
        (rt_gui_subhandle_t *)rt_obj_new_i64(0, (int64_t)sizeof(rt_gui_subhandle_t));
    if (!handle)
        return NULL;
    handle->magic = RT_GUI_SUBHANDLE_MAGIC;
    handle->kind = (uint32_t)kind;
    handle->ptr = ptr;
    handle->owner_widget = owner_widget;
    handle->prev = NULL;
    handle->next = s_gui_subhandles;
    if (s_gui_subhandles)
        s_gui_subhandles->prev = handle;
    s_gui_subhandles = handle;
    rt_obj_set_finalizer(handle, rt_gui_subhandle_finalize);
    return handle;
}

void *rt_gui_wrap_tree_node(vg_tree_node_t *node) {
    return rt_gui_wrap_subhandle(
        RT_GUI_HANDLE_TREE_NODE, node, node && node->owner ? &node->owner->base : NULL);
}

void *rt_gui_wrap_tab(vg_tab_t *tab) {
    return rt_gui_wrap_subhandle(
        RT_GUI_HANDLE_TAB, tab, tab && tab->owner ? &tab->owner->base : NULL);
}

void *rt_gui_wrap_listbox_item(vg_listbox_item_t *item) {
    return rt_gui_wrap_subhandle(
        RT_GUI_HANDLE_LISTBOX_ITEM, item, item && item->owner ? &item->owner->base : NULL);
}

void *rt_gui_wrap_menu(vg_menu_t *menu) {
    return rt_gui_wrap_subhandle(RT_GUI_HANDLE_MENU, menu, rt_gui_owner_widget_for_menu(menu));
}

void *rt_gui_wrap_menu_item(vg_menu_item_t *item) {
    return rt_gui_wrap_subhandle(
        RT_GUI_HANDLE_MENU_ITEM, item, rt_gui_owner_widget_for_menu_item(item));
}

void *rt_gui_wrap_contextmenu(vg_contextmenu_t *menu) {
    return rt_gui_wrap_subhandle(RT_GUI_HANDLE_CONTEXTMENU, menu, menu ? &menu->base : NULL);
}

void *rt_gui_wrap_statusbar_item(vg_statusbar_item_t *item) {
    return rt_gui_wrap_subhandle(
        RT_GUI_HANDLE_STATUSBAR_ITEM, item, item && item->owner ? &item->owner->base : NULL);
}

void *rt_gui_wrap_toolbar_item(vg_toolbar_item_t *item) {
    return rt_gui_wrap_subhandle(
        RT_GUI_HANDLE_TOOLBAR_ITEM, item, item && item->owner ? &item->owner->base : NULL);
}

vg_tree_node_t *rt_gui_tree_node_from_handle(void *handle) {
    rt_gui_subhandle_t *sub = rt_gui_subhandle_checked(handle, RT_GUI_HANDLE_TREE_NODE);
    if (!sub || !sub->ptr)
        return NULL;
    if (!rt_gui_subhandle_owner_is_live(sub))
        return NULL;
    vg_tree_node_t *node = (vg_tree_node_t *)sub->ptr;
    if (!vg_tree_node_is_live(node)) {
        rt_gui_subhandle_invalidate(sub);
        return NULL;
    }
    return node;
}

vg_tab_t *rt_gui_tab_from_handle(void *handle) {
    rt_gui_subhandle_t *sub = rt_gui_subhandle_checked(handle, RT_GUI_HANDLE_TAB);
    if (!sub || !sub->ptr)
        return NULL;
    if (!rt_gui_subhandle_owner_is_live(sub))
        return NULL;
    vg_tab_t *tab = (vg_tab_t *)sub->ptr;
    if (!vg_tab_is_live(tab)) {
        rt_gui_subhandle_invalidate(sub);
        return NULL;
    }
    return tab;
}

vg_listbox_item_t *rt_gui_listbox_item_from_handle(void *handle) {
    rt_gui_subhandle_t *sub = rt_gui_subhandle_checked(handle, RT_GUI_HANDLE_LISTBOX_ITEM);
    if (!sub || !sub->ptr)
        return NULL;
    if (!rt_gui_subhandle_owner_is_live(sub))
        return NULL;
    vg_listbox_item_t *item = (vg_listbox_item_t *)sub->ptr;
    if (!vg_listbox_item_is_live(item)) {
        rt_gui_subhandle_invalidate(sub);
        return NULL;
    }
    return item;
}

vg_menu_t *rt_gui_menu_from_handle(void *handle) {
    rt_gui_subhandle_t *sub = rt_gui_subhandle_checked(handle, RT_GUI_HANDLE_MENU);
    if (!sub || !sub->ptr)
        return NULL;
    if (!rt_gui_subhandle_owner_is_live(sub))
        return NULL;
    vg_menu_t *menu = (vg_menu_t *)sub->ptr;
    if (!vg_menu_is_live(menu)) {
        rt_gui_subhandle_invalidate(sub);
        return NULL;
    }
    return menu;
}

vg_menu_item_t *rt_gui_menu_item_from_handle(void *handle) {
    rt_gui_subhandle_t *sub = rt_gui_subhandle_checked(handle, RT_GUI_HANDLE_MENU_ITEM);
    if (!sub || !sub->ptr)
        return NULL;
    if (!rt_gui_subhandle_owner_is_live(sub))
        return NULL;
    vg_menu_item_t *item = (vg_menu_item_t *)sub->ptr;
    if (!vg_menu_item_is_live(item)) {
        rt_gui_subhandle_invalidate(sub);
        return NULL;
    }
    return item;
}

vg_contextmenu_t *rt_gui_contextmenu_from_handle(void *handle) {
    rt_gui_subhandle_t *sub = rt_gui_subhandle_checked(handle, RT_GUI_HANDLE_CONTEXTMENU);
    if (!sub || !sub->ptr)
        return NULL;
    if (!rt_gui_subhandle_owner_is_live(sub))
        return NULL;
    vg_contextmenu_t *menu = (vg_contextmenu_t *)sub->ptr;
    if (!vg_contextmenu_is_live(menu) || menu->base.type != VG_WIDGET_MENU) {
        rt_gui_subhandle_invalidate(sub);
        return NULL;
    }
    return menu;
}

vg_statusbar_item_t *rt_gui_statusbar_item_from_handle(void *handle) {
    rt_gui_subhandle_t *sub = rt_gui_subhandle_checked(handle, RT_GUI_HANDLE_STATUSBAR_ITEM);
    if (!sub || !sub->ptr)
        return NULL;
    if (!rt_gui_subhandle_owner_is_live(sub))
        return NULL;
    vg_statusbar_item_t *item = (vg_statusbar_item_t *)sub->ptr;
    if (!vg_statusbar_item_is_live(item)) {
        rt_gui_subhandle_invalidate(sub);
        return NULL;
    }
    return item;
}

vg_toolbar_item_t *rt_gui_toolbar_item_from_handle(void *handle) {
    rt_gui_subhandle_t *sub = rt_gui_subhandle_checked(handle, RT_GUI_HANDLE_TOOLBAR_ITEM);
    if (!sub || !sub->ptr)
        return NULL;
    if (!rt_gui_subhandle_owner_is_live(sub))
        return NULL;
    vg_toolbar_item_t *item = (vg_toolbar_item_t *)sub->ptr;
    if (!vg_toolbar_item_is_live(item)) {
        rt_gui_subhandle_invalidate(sub);
        return NULL;
    }
    return item;
}

void rt_gui_invalidate_widget_subhandles(vg_widget_t *subtree) {
    if (!subtree)
        return;
    for (rt_gui_subhandle_t *handle = s_gui_subhandles; handle; handle = handle->next) {
        if (handle->magic != RT_GUI_SUBHANDLE_MAGIC || !handle->owner_widget)
            continue;
        if (rt_gui_widget_tree_contains(subtree, handle->owner_widget))
            rt_gui_subhandle_invalidate(handle);
    }
}

void rt_gui_invalidate_contextmenu_contents(vg_contextmenu_t *menu) {
    if (!menu)
        return;

    for (rt_gui_subhandle_t *handle = s_gui_subhandles; handle; handle = handle->next) {
        if (handle->magic != RT_GUI_SUBHANDLE_MAGIC || handle->owner_widget != &menu->base)
            continue;
        if (handle->kind == RT_GUI_HANDLE_MENU_ITEM)
            rt_gui_subhandle_invalidate(handle);
    }

    for (size_t i = 0; i < menu->item_count; i++) {
        vg_menu_item_t *item = menu->items[i];
        if (item && item->submenu)
            rt_gui_invalidate_contextmenu_tree((vg_contextmenu_t *)item->submenu);
    }
}

void rt_gui_invalidate_contextmenu_tree(vg_contextmenu_t *menu) {
    if (!menu)
        return;
    rt_gui_invalidate_contextmenu_contents(menu);
    for (rt_gui_subhandle_t *handle = s_gui_subhandles; handle; handle = handle->next) {
        if (handle->magic != RT_GUI_SUBHANDLE_MAGIC || handle->kind != RT_GUI_HANDLE_CONTEXTMENU)
            continue;
        if (handle->ptr == menu)
            rt_gui_subhandle_invalidate(handle);
    }
}
