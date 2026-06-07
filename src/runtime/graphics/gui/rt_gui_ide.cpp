//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/graphics/rt_gui_ide.cpp
// Purpose: GUI automation, virtualization, and command/accessibility helpers
//          for IDE-style applications.
//
//===----------------------------------------------------------------------===//

#include "rt_gui_ide.h"

#include "rt_map.h"
#include "rt_object.h"
#include "rt_seq.h"
#include "rt_string.h"
#include "rt_trap.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <new>
#include <set>
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

rt_string makeString(const std::string &value) {
    return rt_string_from_bytes(value.data(), value.size());
}

void releaseObject(void *obj) {
    if (obj && rt_obj_release_check0(obj))
        rt_obj_free(obj);
}

void mapSetStr(void *map, const char *key, const std::string &value) {
    rt_string s = makeString(value);
    rt_map_set_str(map, rt_const_cstr(key), s);
    rt_string_unref(s);
}

struct WidgetRecord {
    std::string id;
    std::string type;
    std::string name;
    int64_t x{0};
    int64_t y{0};
    int64_t w{0};
    int64_t h{0};
};

struct EventRecord {
    std::string type;
    std::string value;
    int64_t x{0};
    int64_t y{0};
    int64_t button{0};
    int64_t modifiers{0};
    int64_t frame{0};
};

struct HarnessState {
    int64_t frame{0};
    std::vector<WidgetRecord> widgets;
    std::vector<EventRecord> events;
    std::string focus;
};

struct HarnessHandle {
    HarnessState *state{nullptr};
};

HarnessHandle *requireHarness(void *obj) {
    if (!obj || rt_obj_class_id(obj) != RT_GUI_TEST_HARNESS_CLASS_ID) {
        rt_trap("GUI.TestHarness: invalid handle");
        return nullptr;
    }
    auto *h = static_cast<HarnessHandle *>(obj);
    if (!h->state) {
        rt_trap("GUI.TestHarness: destroyed handle");
        return nullptr;
    }
    return h;
}

void harnessFinalizer(void *obj) {
    auto *h = static_cast<HarnessHandle *>(obj);
    delete h->state;
    h->state = nullptr;
}

void *widgetToMap(const WidgetRecord *w) {
    void *map = rt_map_new();
    if (!w) {
        rt_map_set_bool(map, rt_const_cstr("found"), 0);
        return map;
    }
    rt_map_set_bool(map, rt_const_cstr("found"), 1);
    mapSetStr(map, "id", w->id);
    mapSetStr(map, "type", w->type);
    mapSetStr(map, "name", w->name);
    rt_map_set_int(map, rt_const_cstr("x"), w->x);
    rt_map_set_int(map, rt_const_cstr("y"), w->y);
    rt_map_set_int(map, rt_const_cstr("width"), w->w);
    rt_map_set_int(map, rt_const_cstr("height"), w->h);
    return map;
}

bool intersects(const WidgetRecord &w, int64_t x, int64_t y, int64_t width, int64_t height) {
    return !(w.x + w.w <= x || x + width <= w.x || w.y + w.h <= y || y + height <= w.y);
}

struct VirtualListState {
    int64_t rowCount{0};
    int64_t rowHeight{20};
    int64_t viewportHeight{100};
    int64_t overscan{2};
    std::map<int64_t, std::string> rowIds;
    std::string selectedId;
};

struct VirtualListHandle {
    VirtualListState *state{nullptr};
};

VirtualListHandle *requireList(void *obj) {
    if (!obj || rt_obj_class_id(obj) != RT_GUI_VIRTUAL_LIST_CLASS_ID) {
        rt_trap("GUI.VirtualList: invalid handle");
        return nullptr;
    }
    auto *h = static_cast<VirtualListHandle *>(obj);
    if (!h->state) {
        rt_trap("GUI.VirtualList: destroyed handle");
        return nullptr;
    }
    return h;
}

void listFinalizer(void *obj) {
    auto *h = static_cast<VirtualListHandle *>(obj);
    delete h->state;
    h->state = nullptr;
}

std::string rowId(VirtualListState &state, int64_t row) {
    auto it = state.rowIds.find(row);
    if (it != state.rowIds.end())
        return it->second;
    return std::to_string(row);
}

struct VirtualTreeState;
void eraseChildId(VirtualTreeState &state, const std::string &parent, const std::string &id);
bool subtreeContains(VirtualTreeState &state,
                     const std::string &root,
                     const std::string &needle,
                     std::set<std::string> &seen);
void removeVirtualTreeDescendants(VirtualTreeState &state,
                                  const std::string &id,
                                  std::set<std::string> &seen);

struct TreeNode {
    std::string id;
    std::string text;
    std::string parent;
    std::vector<std::string> children;
    bool expanded{false};
    bool loaded{false};
};

struct VirtualTreeState {
    std::map<std::string, TreeNode> nodes;
    std::string selectedId;
};

struct VirtualTreeHandle {
    VirtualTreeState *state{nullptr};
};

VirtualTreeHandle *requireTree(void *obj) {
    if (!obj || rt_obj_class_id(obj) != RT_GUI_VIRTUAL_TREE_CLASS_ID) {
        rt_trap("GUI.VirtualTree: invalid handle");
        return nullptr;
    }
    auto *h = static_cast<VirtualTreeHandle *>(obj);
    if (!h->state) {
        rt_trap("GUI.VirtualTree: destroyed handle");
        return nullptr;
    }
    return h;
}

void treeFinalizer(void *obj) {
    auto *h = static_cast<VirtualTreeHandle *>(obj);
    delete h->state;
    h->state = nullptr;
}

struct CommandState {
    std::string id;
    std::string label;
    std::string accessibleLabel;
    std::string accessibleDescription;
    bool enabled{true};
    bool checked{false};
};

struct CommandStateHandle {
    CommandState *state{nullptr};
};

CommandStateHandle *requireCommandState(void *obj) {
    if (!obj || rt_obj_class_id(obj) != RT_GUI_COMMAND_STATE_CLASS_ID) {
        rt_trap("GUI.CommandState: invalid handle");
        return nullptr;
    }
    auto *h = static_cast<CommandStateHandle *>(obj);
    if (!h->state) {
        rt_trap("GUI.CommandState: destroyed handle");
        return nullptr;
    }
    return h;
}

void commandStateFinalizer(void *obj) {
    auto *h = static_cast<CommandStateHandle *>(obj);
    delete h->state;
    h->state = nullptr;
}

void collectVisible(VirtualTreeState &state, const std::string &id, int64_t depth, void *rows) {
    auto it = state.nodes.find(id);
    if (it == state.nodes.end())
        return;
    const TreeNode &node = it->second;
    void *map = rt_map_new();
    mapSetStr(map, "id", node.id);
    mapSetStr(map, "text", node.text);
    mapSetStr(map, "parentId", node.parent);
    rt_map_set_int(map, rt_const_cstr("depth"), depth);
    rt_map_set_bool(map, rt_const_cstr("expanded"), node.expanded ? 1 : 0);
    rt_map_set_bool(
        map, rt_const_cstr("needsPopulate"), (!node.loaded && node.children.empty()) ? 1 : 0);
    rt_seq_push(rows, map);
    releaseObject(map);
    if (!node.expanded)
        return;
    for (const auto &child : node.children)
        collectVisible(state, child, depth + 1, rows);
}

void eraseChildId(VirtualTreeState &state, const std::string &parent, const std::string &id) {
    auto pit = state.nodes.find(parent);
    if (pit == state.nodes.end())
        return;
    auto &children = pit->second.children;
    children.erase(std::remove(children.begin(), children.end(), id), children.end());
}

bool subtreeContains(VirtualTreeState &state,
                     const std::string &root,
                     const std::string &needle,
                     std::set<std::string> &seen) {
    if (root == needle)
        return true;
    if (!seen.insert(root).second)
        return false;
    auto it = state.nodes.find(root);
    if (it == state.nodes.end())
        return false;
    for (const auto &child : it->second.children) {
        if (subtreeContains(state, child, needle, seen))
            return true;
    }
    return false;
}

void removeVirtualTreeDescendants(VirtualTreeState &state,
                                  const std::string &id,
                                  std::set<std::string> &seen) {
    auto it = state.nodes.find(id);
    if (it == state.nodes.end() || !seen.insert(id).second)
        return;
    std::vector<std::string> children = it->second.children;
    for (const auto &child : children) {
        removeVirtualTreeDescendants(state, child, seen);
        if (child != id)
            state.nodes.erase(child);
    }
    it = state.nodes.find(id);
    if (it != state.nodes.end())
        it->second.children.clear();
}

double channelLuminance(int64_t c) {
    double v = static_cast<double>(c & 0xff) / 255.0;
    return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double luminance(int64_t rgb) {
    return 0.2126 * channelLuminance((rgb >> 16) & 0xff) +
           0.7152 * channelLuminance((rgb >> 8) & 0xff) + 0.0722 * channelLuminance(rgb & 0xff);
}

} // namespace

extern "C" {

void *rt_gui_test_harness_new(void) {
    auto *h = static_cast<HarnessHandle *>(
        rt_obj_new_i64(RT_GUI_TEST_HARNESS_CLASS_ID, sizeof(HarnessHandle)));
    if (!h)
        return nullptr;
    h->state = new (std::nothrow) HarnessState();
    if (!h->state) {
        releaseObject(h);
        return nullptr;
    }
    rt_obj_set_finalizer(h, harnessFinalizer);
    return h;
}

void rt_gui_test_harness_clear(void *harness) {
    auto *h = requireHarness(harness);
    h->state->widgets.clear();
    h->state->events.clear();
    h->state->focus.clear();
}

int64_t rt_gui_test_harness_tick(void *harness, int64_t frames) {
    auto *h = requireHarness(harness);
    if (frames < 1)
        frames = 1;
    h->state->frame += frames;
    return h->state->frame;
}

void rt_gui_test_harness_register_widget(void *harness,
                                         rt_string id,
                                         rt_string type,
                                         rt_string name,
                                         int64_t x,
                                         int64_t y,
                                         int64_t w,
                                         int64_t hgt) {
    auto *h = requireHarness(harness);
    try {
        WidgetRecord rec{toStd(id), toStd(type), toStd(name), x, y, w, hgt};
        auto it = std::find_if(h->state->widgets.begin(),
                               h->state->widgets.end(),
                               [&](const WidgetRecord &wrec) { return wrec.id == rec.id; });
        if (it == h->state->widgets.end())
            h->state->widgets.push_back(rec);
        else
            *it = rec;
    } catch (const std::bad_alloc &) {
        return;
    }
}

void *rt_gui_test_harness_find_by_id(void *harness, rt_string id) {
    auto *h = requireHarness(harness);
    try {
        std::string key = toStd(id);
        auto it = std::find_if(h->state->widgets.begin(),
                               h->state->widgets.end(),
                               [&](const WidgetRecord &w) { return w.id == key; });
        return widgetToMap(it == h->state->widgets.end() ? nullptr : &*it);
    } catch (const std::bad_alloc &) {
        return widgetToMap(nullptr);
    }
}

void *rt_gui_test_harness_find_by_name(void *harness, rt_string name) {
    auto *h = requireHarness(harness);
    try {
        std::string key = toStd(name);
        auto it = std::find_if(h->state->widgets.begin(),
                               h->state->widgets.end(),
                               [&](const WidgetRecord &w) { return w.name == key; });
        return widgetToMap(it == h->state->widgets.end() ? nullptr : &*it);
    } catch (const std::bad_alloc &) {
        return widgetToMap(nullptr);
    }
}

void *rt_gui_test_harness_find_by_type(void *harness, rt_string type) {
    auto *h = requireHarness(harness);
    try {
        std::string key = toStd(type);
        auto it = std::find_if(h->state->widgets.begin(),
                               h->state->widgets.end(),
                               [&](const WidgetRecord &w) { return w.type == key; });
        return widgetToMap(it == h->state->widgets.end() ? nullptr : &*it);
    } catch (const std::bad_alloc &) {
        return widgetToMap(nullptr);
    }
}

void rt_gui_test_harness_send_key(void *harness, rt_string key, int64_t modifiers) {
    auto *h = requireHarness(harness);
    try {
        h->state->events.push_back({"key", toStd(key), 0, 0, 0, modifiers, h->state->frame});
    } catch (const std::bad_alloc &) {
        return;
    }
}

void rt_gui_test_harness_send_mouse(
    void *harness, rt_string event_type, int64_t x, int64_t y, int64_t button) {
    auto *h = requireHarness(harness);
    try {
        h->state->events.push_back({toStd(event_type), "", x, y, button, 0, h->state->frame});
        for (auto it = h->state->widgets.rbegin(); it != h->state->widgets.rend(); ++it) {
            if (x >= it->x && y >= it->y && x < it->x + it->w && y < it->y + it->h) {
                h->state->focus = it->id;
                break;
            }
        }
    } catch (const std::bad_alloc &) {
        return;
    }
}

rt_string rt_gui_test_harness_get_focus(void *harness) {
    auto *h = requireHarness(harness);
    return makeString(h->state->focus);
}

void *rt_gui_test_harness_focus_order(void *harness) {
    auto *h = requireHarness(harness);
    void *seq = rt_seq_new_owned();
    for (const auto &w : h->state->widgets) {
        rt_string id = makeString(w.id);
        rt_seq_push(seq, id);
        rt_string_unref(id);
    }
    return seq;
}

void *rt_gui_test_harness_capture_region(
    void *harness, int64_t x, int64_t y, int64_t w, int64_t hgt) {
    auto *h = requireHarness(harness);
    void *snapshot = rt_map_new();
    rt_map_set_int(snapshot, rt_const_cstr("x"), x);
    rt_map_set_int(snapshot, rt_const_cstr("y"), y);
    rt_map_set_int(snapshot, rt_const_cstr("width"), w);
    rt_map_set_int(snapshot, rt_const_cstr("height"), hgt);
    int64_t hits = 0;
    for (const auto &widget : h->state->widgets) {
        if (intersects(widget, x, y, w, hgt))
            hits++;
    }
    rt_map_set_int(snapshot, rt_const_cstr("nonBlankPixels"), hits > 0 ? w * hgt : 0);
    rt_map_set_bool(snapshot, rt_const_cstr("nonBlank"), hits > 0 ? 1 : 0);
    return snapshot;
}

int8_t rt_gui_test_harness_assert_nonblank(void *snapshot) {
    if (!snapshot || rt_obj_class_id(snapshot) != RT_MAP_CLASS_ID)
        return 0;
    return rt_map_get_bool(snapshot, rt_const_cstr("nonBlank"));
}

void *rt_virtual_list_new(int64_t row_count, int64_t row_height, int64_t viewport_height) {
    auto *h = static_cast<VirtualListHandle *>(
        rt_obj_new_i64(RT_GUI_VIRTUAL_LIST_CLASS_ID, sizeof(VirtualListHandle)));
    if (!h)
        return nullptr;
    h->state = new (std::nothrow) VirtualListState();
    if (!h->state) {
        releaseObject(h);
        return nullptr;
    }
    h->state->rowCount = std::max<int64_t>(0, row_count);
    h->state->rowHeight = std::max<int64_t>(1, row_height);
    h->state->viewportHeight = std::max<int64_t>(1, viewport_height);
    rt_obj_set_finalizer(h, listFinalizer);
    return h;
}

void rt_virtual_list_set_count(void *list, int64_t row_count) {
    auto *h = requireList(list);
    h->state->rowCount = std::max<int64_t>(0, row_count);
}

void rt_virtual_list_set_row_id(void *list, int64_t row, rt_string id) {
    auto *h = requireList(list);
    try {
        if (row >= 0 && row < h->state->rowCount)
            h->state->rowIds[row] = toStd(id);
    } catch (const std::bad_alloc &) {
        return;
    }
}

void *rt_virtual_list_visible_range(void *list, int64_t scroll_y) {
    auto *h = requireList(list);
    auto &s = *h->state;
    if (scroll_y < 0)
        scroll_y = 0;
    int64_t first = scroll_y / s.rowHeight;
    first = std::max<int64_t>(0, first - s.overscan);
    int64_t visible = (s.viewportHeight + s.rowHeight - 1) / s.rowHeight + s.overscan * 2;
    int64_t end = std::min<int64_t>(s.rowCount, first + visible);
    void *map = rt_map_new();
    rt_map_set_int(map, rt_const_cstr("start"), first);
    rt_map_set_int(map, rt_const_cstr("end"), end);
    rt_map_set_int(map, rt_const_cstr("count"), std::max<int64_t>(0, end - first));
    return map;
}

void rt_virtual_list_select_id(void *list, rt_string id) {
    auto *h = requireList(list);
    try {
        h->state->selectedId = toStd(id);
    } catch (const std::bad_alloc &) {
        return;
    }
}

rt_string rt_virtual_list_get_selected_id(void *list) {
    auto *h = requireList(list);
    return makeString(h->state->selectedId);
}

int64_t rt_virtual_list_get_selected_index(void *list) {
    auto *h = requireList(list);
    try {
        for (int64_t row = 0; row < h->state->rowCount; row++) {
            if (rowId(*h->state, row) == h->state->selectedId)
                return row;
        }
    } catch (const std::bad_alloc &) {
        return -1;
    }
    return -1;
}

void *rt_virtual_tree_new(void) {
    auto *h = static_cast<VirtualTreeHandle *>(
        rt_obj_new_i64(RT_GUI_VIRTUAL_TREE_CLASS_ID, sizeof(VirtualTreeHandle)));
    if (!h)
        return nullptr;
    h->state = new (std::nothrow) VirtualTreeState();
    if (!h->state) {
        releaseObject(h);
        return nullptr;
    }
    try {
        h->state->nodes[""] = TreeNode{"", "", "", {}, true, true};
    } catch (const std::bad_alloc &) {
        delete h->state;
        h->state = nullptr;
        releaseObject(h);
        return nullptr;
    }
    rt_obj_set_finalizer(h, treeFinalizer);
    return h;
}

void rt_virtual_tree_add_node(void *tree, rt_string parent_id, rt_string id_s, rt_string text) {
    auto *h = requireTree(tree);
    try {
        std::string parent = toStd(parent_id);
        std::string id = toStd(id_s);
        std::string node_text = toStd(text);
        if (id.empty())
            return;
        if (id == parent)
            return;
        auto &state = *h->state;
        auto existing = state.nodes.find(id);
        if (existing != state.nodes.end()) {
            std::set<std::string> seen;
            if (subtreeContains(state, id, parent, seen))
                return;
        }

        if (!state.nodes.count(parent)) {
            state.nodes[parent] = TreeNode{parent, parent, "", {}, false, false};
            auto root = state.nodes.find("");
            if (!parent.empty() && root != state.nodes.end()) {
                auto &root_children = root->second.children;
                if (std::find(root_children.begin(), root_children.end(), parent) ==
                    root_children.end())
                    root_children.push_back(parent);
            }
        }

        existing = state.nodes.find(id);
        if (existing == state.nodes.end()) {
            state.nodes[id] = TreeNode{id, node_text, parent, {}, false, false};
        } else {
            if (existing->second.parent != parent)
                eraseChildId(state, existing->second.parent, id);
            existing->second.id = id;
            existing->second.text = node_text;
            existing->second.parent = parent;
        }

        auto &children = state.nodes[parent].children;
        if (std::find(children.begin(), children.end(), id) == children.end())
            children.push_back(id);
        state.nodes[parent].loaded = true;
    } catch (const std::bad_alloc &) {
        return;
    }
}

void *rt_virtual_tree_expand(void *tree, rt_string id_s) {
    auto *h = requireTree(tree);
    void *map = rt_map_new();
    try {
        std::string id = toStd(id_s);
        auto it = h->state->nodes.find(id);
        if (it == h->state->nodes.end()) {
            rt_map_set_bool(map, rt_const_cstr("found"), 0);
            rt_map_set_bool(map, rt_const_cstr("needsPopulate"), 1);
            return map;
        }
        it->second.expanded = true;
        rt_map_set_bool(map, rt_const_cstr("found"), 1);
        rt_map_set_bool(map, rt_const_cstr("expanded"), 1);
        rt_map_set_bool(map,
                        rt_const_cstr("needsPopulate"),
                        (!it->second.loaded && it->second.children.empty()) ? 1 : 0);
    } catch (const std::bad_alloc &) {
        rt_map_set_bool(map, rt_const_cstr("found"), 0);
        rt_map_set_bool(map, rt_const_cstr("needsPopulate"), 1);
    }
    return map;
}

void rt_virtual_tree_collapse(void *tree, rt_string id_s) {
    auto *h = requireTree(tree);
    try {
        auto it = h->state->nodes.find(toStd(id_s));
        if (it != h->state->nodes.end())
            it->second.expanded = false;
    } catch (const std::bad_alloc &) {
        return;
    }
}

void rt_virtual_tree_select_id(void *tree, rt_string id) {
    auto *h = requireTree(tree);
    try {
        h->state->selectedId = toStd(id);
    } catch (const std::bad_alloc &) {
        return;
    }
}

rt_string rt_virtual_tree_get_selected_id(void *tree) {
    auto *h = requireTree(tree);
    return makeString(h->state->selectedId);
}

void *rt_virtual_tree_visible_rows(void *tree) {
    auto *h = requireTree(tree);
    void *rows = rt_seq_new_owned();
    auto root = h->state->nodes.find("");
    if (root != h->state->nodes.end()) {
        for (const auto &id : root->second.children)
            collectVisible(*h->state, id, 0, rows);
    }
    return rows;
}

void rt_virtual_tree_refresh_subtree(void *tree, rt_string id_s) {
    auto *h = requireTree(tree);
    try {
        std::string id = toStd(id_s);
        auto it = h->state->nodes.find(id);
        if (it == h->state->nodes.end())
            return;
        std::set<std::string> seen;
        removeVirtualTreeDescendants(*h->state, it->first, seen);
        if (!h->state->selectedId.empty() &&
            h->state->nodes.find(h->state->selectedId) == h->state->nodes.end())
            h->state->selectedId.clear();
        it = h->state->nodes.find(id);
        if (it == h->state->nodes.end())
            return;
        it->second.loaded = false;
    } catch (const std::bad_alloc &) {
        return;
    }
}

void *rt_command_state_new(rt_string id, rt_string label) {
    auto *h = static_cast<CommandStateHandle *>(
        rt_obj_new_i64(RT_GUI_COMMAND_STATE_CLASS_ID, sizeof(CommandStateHandle)));
    if (!h)
        return nullptr;
    h->state = new (std::nothrow) CommandState();
    if (!h->state) {
        releaseObject(h);
        return nullptr;
    }
    try {
        h->state->id = toStd(id);
        h->state->label = toStd(label);
        h->state->accessibleLabel = h->state->label;
    } catch (const std::bad_alloc &) {
        delete h->state;
        h->state = nullptr;
        releaseObject(h);
        return nullptr;
    }
    rt_obj_set_finalizer(h, commandStateFinalizer);
    return h;
}

void rt_command_state_set_enabled(void *state, int8_t enabled) {
    requireCommandState(state)->state->enabled = enabled != 0;
}

int8_t rt_command_state_get_enabled(void *state) {
    return requireCommandState(state)->state->enabled ? 1 : 0;
}

void rt_command_state_set_checked(void *state, int8_t checked) {
    requireCommandState(state)->state->checked = checked != 0;
}

int8_t rt_command_state_get_checked(void *state) {
    return requireCommandState(state)->state->checked ? 1 : 0;
}

void rt_command_state_set_accessible(void *state, rt_string label, rt_string description) {
    auto *s = requireCommandState(state)->state;
    try {
        s->accessibleLabel = toStd(label);
        s->accessibleDescription = toStd(description);
    } catch (const std::bad_alloc &) {
        return;
    }
}

void *rt_command_state_snapshot(void *state) {
    auto *s = requireCommandState(state)->state;
    void *map = rt_map_new();
    mapSetStr(map, "id", s->id);
    mapSetStr(map, "label", s->label);
    mapSetStr(map, "accessibleLabel", s->accessibleLabel);
    mapSetStr(map, "accessibleDescription", s->accessibleDescription);
    rt_map_set_bool(map, rt_const_cstr("enabled"), s->enabled ? 1 : 0);
    rt_map_set_bool(map, rt_const_cstr("checked"), s->checked ? 1 : 0);
    return map;
}

double rt_accessibility_contrast_ratio(int64_t fg_rgb, int64_t bg_rgb) {
    double a = luminance(fg_rgb);
    double b = luminance(bg_rgb);
    if (a < b)
        std::swap(a, b);
    return (a + 0.05) / (b + 0.05);
}

int8_t rt_accessibility_meets_contrast(int64_t fg_rgb, int64_t bg_rgb, double min_ratio) {
    if (!std::isfinite(min_ratio) || min_ratio <= 0.0)
        min_ratio = 4.5;
    return rt_accessibility_contrast_ratio(fg_rgb, bg_rgb) >= min_ratio ? 1 : 0;
}

void *rt_accessibility_high_contrast_tokens(void) {
    void *map = rt_map_new();
    rt_map_set_int(map, rt_const_cstr("background"), 0x000000);
    rt_map_set_int(map, rt_const_cstr("foreground"), 0xffffff);
    rt_map_set_int(map, rt_const_cstr("accent"), 0x00d7ff);
    rt_map_set_int(map, rt_const_cstr("warning"), 0xffd75f);
    rt_map_set_int(map, rt_const_cstr("error"), 0xff5f5f);
    return map;
}

} // extern "C"
