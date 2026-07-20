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

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <oleauto.h>
// The C form of this SDK header otherwise defines an external-linkage double
// in every translation unit that includes it.
#define __UIA_OtherConstants_MODULE_DEFINED__
#include <uiautomationcore.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "vg_layout.h"
#include "vg_widget.h"
#include "vg_widgets.h"

enum {
    RT_UIA_TEST_CONTROL_TYPE_PROPERTY_ID = 30003,
    RT_UIA_TEST_NAME_PROPERTY_ID = 30005,
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

    // Stale-widget degradation: destroying the tree empties properties.
    value->lpVtbl->Release(value);
    toggle->lpVtbl->Release(toggle);
    input_simple->lpVtbl->Release(input_simple);
    checkbox_simple->lpVtbl->Release(checkbox_simple);
    button_simple->lpVtbl->Release(button_simple);
    third->lpVtbl->Release(third);
    second->lpVtbl->Release(second);

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

    fragment_root->lpVtbl->Release(fragment_root);
    root_fragment->lpVtbl->Release(root_fragment);
    root_simple->lpVtbl->Release(root_simple);
    rt_gui_accessibility_win32_test_teardown(bridge_key);
    vg_widget_destroy(root);

    printf("RTUiaProviderTests: all assertions passed\n");
    return 0;
}
