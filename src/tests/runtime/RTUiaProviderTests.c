//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTUiaProviderTests.c
// Purpose: Headless vtable tests for the Windows UI Automation provider
//          bridge. Builds a semantic widget tree directly, obtains the root
//          provider through the test seam (no UIA client, no HWND, no
//          uiautomationcore.dll needed), and exercises QueryInterface
//          routing, navigation, properties, and pattern gating.
// Key invariants:
//   - Every AddRef acquired here is paired with a Release; the teardown seam
//     clears the simulated bridge slot.
//   - Property BSTRs are freed with SysFreeString.
// Links: src/runtime/graphics/gui/rt_gui_accessibility_win32.c
//
//===----------------------------------------------------------------------===//

// COM calls are part of the assertions below, so eliding assert would skip
// provider initialization and turn later checks into null dereferences.
#ifdef NDEBUG
#undef NDEBUG
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <oleauto.h>
// The C form of this SDK header otherwise defines an external-linkage double
// in every translation unit that includes it.
#define __UIA_OtherConstants_MODULE_DEFINED__
#include <uiautomationcore.h>

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "vg_layout.h"
#include "vg_widget.h"
#include "vg_widgets.h"

enum {
    RT_UIA_TEST_CONTROL_TYPE_PROPERTY_ID = 30003,
    RT_UIA_TEST_NAME_PROPERTY_ID = 30005,
    RT_UIA_TEST_APPEND_RUNTIME_ID = 3,
};

// Test seams exported by the Windows accessibility adapter.
extern IRawElementProviderSimple *rt_gui_accessibility_win32_test_root(void *bridge_key,
                                                                       vg_widget_t *root);
extern void rt_gui_accessibility_win32_test_teardown(void *bridge_key);

/// @brief Assert a VT_BSTR VARIANT equals the expected UTF-16 text, then clear it.
static void expect_bstr_property(IRawElementProviderSimple *provider,
                                 PROPERTYID property_id,
                                 const wchar_t *expected) {
    VARIANT value;
    VariantInit(&value);
    HRESULT hr = provider->lpVtbl->GetPropertyValue(provider, property_id, &value);
    assert(hr == S_OK);
    assert(value.vt == VT_BSTR);
    assert(value.bstrVal != NULL);
    assert(wcscmp(value.bstrVal, expected) == 0);
    VariantClear(&value);
}

int main(void) {
    static int bridge_key_storage;
    void *bridge_key = &bridge_key_storage;

    // Semantic fixture: root VBox with a button, a checkbox, and a text input.
    vg_widget_t *root = vg_vbox_create(4.0f);
    assert(root != NULL);
    vg_button_t *button = vg_button_create(root, "Build Project");
    vg_checkbox_t *checkbox = vg_checkbox_create(root, "Word Wrap");
    vg_textinput_t *input = vg_textinput_create(root);
    assert(button && checkbox && input);
    ((vg_widget_t *)button)->id = UINT64_C(0xFEDCBA9876543210);
    vg_textinput_set_text(input, "hello");

    IRawElementProviderSimple *root_simple = rt_gui_accessibility_win32_test_root(bridge_key, root);
    assert(root_simple != NULL);

    // Provider options identify a server-side provider.
    enum ProviderOptions options = 0;
    assert(root_simple->lpVtbl->get_ProviderOptions(root_simple, &options) == S_OK);
    assert((options & ProviderOptions_ServerSideProvider) != 0);

    // The root answers Fragment and FragmentRoot; children only Fragment.
    IRawElementProviderFragment *root_fragment = NULL;
    assert(root_simple->lpVtbl->QueryInterface(
               root_simple, &IID_IRawElementProviderFragment, (void **)&root_fragment) == S_OK);
    IRawElementProviderFragmentRoot *fragment_root = NULL;
    assert(root_simple->lpVtbl->QueryInterface(
               root_simple, &IID_IRawElementProviderFragmentRoot, (void **)&fragment_root) == S_OK);

    // Navigation: first child is the button; sibling chain reaches the input.
    IRawElementProviderFragment *first_child = NULL;
    assert(root_fragment->lpVtbl->Navigate(
               root_fragment, NavigateDirection_FirstChild, &first_child) == S_OK);
    assert(first_child != NULL);

    IRawElementProviderSimple *button_simple = NULL;
    assert(first_child->lpVtbl->QueryInterface(
               first_child, &IID_IRawElementProviderSimple, (void **)&button_simple) == S_OK);
    expect_bstr_property(button_simple, RT_UIA_TEST_NAME_PROPERTY_ID, L"Build Project");
    vg_widget_set_accessible_name((vg_widget_t *)button, "\xC3\x28");
    expect_bstr_property(button_simple, RT_UIA_TEST_NAME_PROPERTY_ID, L"");
    vg_widget_set_accessible_name((vg_widget_t *)button, NULL);

    VARIANT control_type;
    VariantInit(&control_type);
    assert(button_simple->lpVtbl->GetPropertyValue(
               button_simple, RT_UIA_TEST_CONTROL_TYPE_PROPERTY_ID, &control_type) == S_OK);
    assert(control_type.vt == VT_I4);
    assert(control_type.lVal == 50000 /* UIA_ButtonControlTypeId */);
    VariantClear(&control_type);

    // Pattern gating: the button invokes but does not toggle.
    IUnknown *pattern = NULL;
    assert(button_simple->lpVtbl->GetPatternProvider(button_simple, 10000 /* Invoke */, &pattern) ==
           S_OK);
    assert(pattern != NULL);
    pattern->lpVtbl->Release(pattern);
    pattern = NULL;
    assert(button_simple->lpVtbl->GetPatternProvider(button_simple, 10015 /* Toggle */, &pattern) ==
           S_OK);
    assert(pattern == NULL);

    // Runtime IDs exist for children and are suppressed for the root.
    SAFEARRAY *runtime_id = NULL;
    assert(first_child->lpVtbl->GetRuntimeId(first_child, &runtime_id) == S_OK);
    assert(runtime_id != NULL);
    LONG lower = -1;
    LONG upper = -1;
    assert(SafeArrayGetLBound(runtime_id, 1, &lower) == S_OK && lower == 0);
    assert(SafeArrayGetUBound(runtime_id, 1, &upper) == S_OK && upper == 2);
    LONG runtime_part = 0;
    LONG runtime_index = 0;
    assert(SafeArrayGetElement(runtime_id, &runtime_index, &runtime_part) == S_OK);
    assert(runtime_part == RT_UIA_TEST_APPEND_RUNTIME_ID);
    runtime_index = 1;
    assert(SafeArrayGetElement(runtime_id, &runtime_index, &runtime_part) == S_OK);
    assert(runtime_part == (LONG)(uint32_t)UINT64_C(0x76543210));
    runtime_index = 2;
    assert(SafeArrayGetElement(runtime_id, &runtime_index, &runtime_part) == S_OK);
    assert(runtime_part == (LONG)(uint32_t)UINT64_C(0xFEDCBA98));
    SafeArrayDestroy(runtime_id);
    runtime_id = NULL;
    assert(root_fragment->lpVtbl->GetRuntimeId(root_fragment, &runtime_id) == S_OK);
    assert(runtime_id == NULL);

    // Sibling walk: button -> checkbox -> input -> end.
    IRawElementProviderFragment *second = NULL;
    assert(first_child->lpVtbl->Navigate(first_child, NavigateDirection_NextSibling, &second) ==
           S_OK);
    assert(second != NULL);
    IRawElementProviderFragment *third = NULL;
    assert(second->lpVtbl->Navigate(second, NavigateDirection_NextSibling, &third) == S_OK);
    assert(third != NULL);
    IRawElementProviderFragment *fourth = NULL;
    assert(third->lpVtbl->Navigate(third, NavigateDirection_NextSibling, &fourth) == S_OK);
    assert(fourth == NULL);

    // Checkbox toggle state follows the widget state.
    IRawElementProviderSimple *checkbox_simple = NULL;
    assert(second->lpVtbl->QueryInterface(
               second, &IID_IRawElementProviderSimple, (void **)&checkbox_simple) == S_OK);
    IToggleProvider *toggle = NULL;
    assert(checkbox_simple->lpVtbl->GetPatternProvider(
               checkbox_simple, 10015 /* Toggle */, (IUnknown **)&toggle) == S_OK);
    assert(toggle != NULL);
    enum ToggleState toggle_state = ToggleState_Indeterminate;
    assert(toggle->lpVtbl->get_ToggleState(toggle, &toggle_state) == S_OK);
    assert(toggle_state == ToggleState_Off);
    ((vg_widget_t *)checkbox)->state |= VG_STATE_CHECKED;
    assert(toggle->lpVtbl->get_ToggleState(toggle, &toggle_state) == S_OK);
    assert(toggle_state == ToggleState_On);

    // Text input surfaces the Value pattern with live text.
    IRawElementProviderSimple *input_simple = NULL;
    assert(third->lpVtbl->QueryInterface(
               third, &IID_IRawElementProviderSimple, (void **)&input_simple) == S_OK);
    IValueProvider *value = NULL;
    assert(input_simple->lpVtbl->GetPatternProvider(
               input_simple, 10002 /* Value */, (IUnknown **)&value) == S_OK);
    assert(value != NULL);
    BSTR text = NULL;
    assert(value->lpVtbl->get_Value(value, &text) == S_OK);
    assert(text != NULL && wcscmp(text, L"hello") == 0);
    SysFreeString(text);
    BOOL read_only = TRUE;
    assert(value->lpVtbl->get_IsReadOnly(value, &read_only) == S_OK);
    assert(read_only == FALSE);
    assert(value->lpVtbl->SetValue(value, L"updated") == S_OK);
    const wchar_t malformed_utf16[] = {(wchar_t)0xD800, L'\0'};
    assert(value->lpVtbl->SetValue(value, malformed_utf16) == E_INVALIDARG);
    text = NULL;
    assert(value->lpVtbl->get_Value(value, &text) == S_OK);
    assert(text != NULL && wcscmp(text, L"updated") == 0);
    SysFreeString(text);

    // Parent navigation returns to the root; the root has no parent.
    IRawElementProviderFragment *parent = NULL;
    assert(first_child->lpVtbl->Navigate(first_child, NavigateDirection_Parent, &parent) == S_OK);
    assert(parent != NULL);
    parent->lpVtbl->Release(parent);
    parent = NULL;
    assert(root_fragment->lpVtbl->Navigate(root_fragment, NavigateDirection_Parent, &parent) ==
           S_OK);
    assert(parent == NULL);
    IRawElementProviderFragment *point_provider = (IRawElementProviderFragment *)(uintptr_t)1;
    assert(fragment_root->lpVtbl->ElementProviderFromPoint(
               fragment_root, NAN, 0.0, &point_provider) == E_INVALIDARG);
    assert(point_provider == NULL);
    point_provider = (IRawElementProviderFragment *)(uintptr_t)1;
    assert(fragment_root->lpVtbl->ElementProviderFromPoint(
               fragment_root, 1.0e100, 0.0, &point_provider) == E_INVALIDARG);
    assert(point_provider == NULL);

    // Stale-widget degradation: destroying the tree empties properties.
    value->lpVtbl->Release(value);
    input_simple->lpVtbl->Release(input_simple);
    checkbox_simple->lpVtbl->Release(checkbox_simple);
    button_simple->lpVtbl->Release(button_simple);
    third->lpVtbl->Release(third);

    vg_widget_destroy((vg_widget_t *)button);
    VARIANT stale_name;
    VariantInit(&stale_name);
    assert(first_child->lpVtbl->QueryInterface(
               first_child, &IID_IRawElementProviderSimple, (void **)&button_simple) == S_OK);
    assert(button_simple->lpVtbl->GetPropertyValue(
               button_simple, RT_UIA_TEST_NAME_PROPERTY_ID, &stale_name) == S_OK);
    assert(stale_name.vt == VT_EMPTY);
    VariantClear(&stale_name);
    button_simple->lpVtbl->Release(button_simple);
    first_child->lpVtbl->Release(first_child);

    // Reusing the same bridge slot invalidates every provider from the old attachment,
    // even while its widget remains live.
    rt_gui_accessibility_win32_test_teardown(bridge_key);
    vg_widget_t *replacement_root = vg_vbox_create(2.0f);
    vg_slider_t *slider = vg_slider_create(replacement_root, VG_SLIDER_HORIZONTAL);
    assert(replacement_root && slider);
    IRawElementProviderSimple *replacement_simple =
        rt_gui_accessibility_win32_test_root(bridge_key, replacement_root);
    assert(replacement_simple != NULL);

    VARIANT detached_name;
    VariantInit(&detached_name);
    assert(second->lpVtbl->QueryInterface(
               second, &IID_IRawElementProviderSimple, (void **)&checkbox_simple) == S_OK);
    assert(checkbox_simple->lpVtbl->GetPropertyValue(
               checkbox_simple, RT_UIA_TEST_NAME_PROPERTY_ID, &detached_name) == S_OK);
    assert(detached_name.vt == VT_EMPTY);
    VariantClear(&detached_name);
    checkbox_simple->lpVtbl->Release(checkbox_simple);
    IRawElementProviderFragment *detached_next = (IRawElementProviderFragment *)(uintptr_t)1;
    assert(second->lpVtbl->Navigate(second, NavigateDirection_NextSibling, &detached_next) == S_OK);
    assert(detached_next == NULL);
    toggle_state = ToggleState_Indeterminate;
    assert(toggle->lpVtbl->get_ToggleState(toggle, &toggle_state) == E_FAIL);
    assert(toggle_state == ToggleState_Off);

    // RangeValue publishes finite, ordered, clamped data even if widget state is corrupt.
    slider->min_value = 10.0f;
    slider->max_value = -10.0f;
    slider->value = INFINITY;
    IRawElementProviderFragment *replacement_fragment = NULL;
    assert(replacement_simple->lpVtbl->QueryInterface(replacement_simple,
                                                      &IID_IRawElementProviderFragment,
                                                      (void **)&replacement_fragment) == S_OK);
    IRawElementProviderFragment *slider_fragment = NULL;
    assert(replacement_fragment->lpVtbl->Navigate(
               replacement_fragment, NavigateDirection_FirstChild, &slider_fragment) == S_OK);
    assert(slider_fragment != NULL);
    IRawElementProviderSimple *slider_simple = NULL;
    assert(slider_fragment->lpVtbl->QueryInterface(
               slider_fragment, &IID_IRawElementProviderSimple, (void **)&slider_simple) == S_OK);
    IRangeValueProvider *range = NULL;
    assert(slider_simple->lpVtbl->GetPatternProvider(
               slider_simple, 10003 /* RangeValue */, (IUnknown **)&range) == S_OK);
    assert(range != NULL);
    double range_value = 0.0;
    assert(range->lpVtbl->get_Minimum(range, &range_value) == S_OK && range_value == -10.0);
    assert(range->lpVtbl->get_Maximum(range, &range_value) == S_OK && range_value == 10.0);
    assert(range->lpVtbl->get_Value(range, &range_value) == S_OK && range_value == -10.0);
    assert(range->lpVtbl->get_LargeChange(range, &range_value) == S_OK && range_value == 2.0);

    range->lpVtbl->Release(range);
    slider_simple->lpVtbl->Release(slider_simple);
    slider_fragment->lpVtbl->Release(slider_fragment);
    replacement_fragment->lpVtbl->Release(replacement_fragment);
    replacement_simple->lpVtbl->Release(replacement_simple);
    rt_gui_accessibility_win32_test_teardown(bridge_key);
    vg_widget_destroy(replacement_root);

    toggle->lpVtbl->Release(toggle);
    second->lpVtbl->Release(second);
    fragment_root->lpVtbl->Release(fragment_root);
    root_fragment->lpVtbl->Release(root_fragment);
    root_simple->lpVtbl->Release(root_simple);
    vg_widget_destroy(root);

    printf("RTUiaProviderTests: all assertions passed\n");
    return 0;
}
