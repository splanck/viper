//===----------------------------------------------------------------------===//
//
// Part of the Zanna project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/runtime/RTGuiAtspiLinuxTests.c
// Purpose: Validate Linux AT-SPI registration and coalesced widget-tree projection.
// Key invariants:
//   - The application root is registered before user widgets are attached.
//   - One rendered frame coalesces topology changes into a fully registered snapshot.
//   - Concurrent mutation requests are bounded and detach cancels an in-flight request.
// Ownership/Lifetime: The test owns and destroys its app and widget subtree.
// Links: src/runtime/graphics/gui/rt_gui_atspi_linux.c
//
//===----------------------------------------------------------------------===//

#include "rt_gui.h"
#include "rt_gui_atspi_linux.h"
#include "rt_gui_internal.h"
#include "rt_string.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

typedef struct atspi_request_fixture {
    vgfx_window_t window;
    uint64_t widget_id;
    int kind;
    double value;
    int result;
} atspi_request_fixture_t;

static void *run_atspi_request(void *data) {
    atspi_request_fixture_t *fixture = data;
    fixture->result = rt_gui_atspi_linux_test_request(
        fixture->window, fixture->widget_id, fixture->kind, fixture->value);
    return NULL;
}

static void note_button_click(vg_widget_t *button, void *data) {
    (void)button;
    int *clicks = data;
    (*clicks)++;
}

static void complete_request_on_gui_thread(void *app, atspi_request_fixture_t *fixture) {
    pthread_t worker;
    assert(pthread_create(&worker, NULL, run_atspi_request, fixture) == 0);
    usleep(10000);
    rt_gui_app_render(app);
    assert(pthread_join(worker, NULL) == 0);
    assert(fixture->result == 1);
}

int main(void) {
    void *app = rt_gui_app_new(rt_const_cstr("Zanna AT-SPI integration"), 240, 120);
    assert(app);
    rt_gui_app_t *state = app;
    assert(rt_gui_atspi_linux_snapshot_count(state->window) == 1);

    void *group = rt_vbox_new();
    void *label = rt_label_new(NULL, rt_const_cstr("Account"));
    void *button = rt_button_new(NULL, rt_const_cstr("Continue"));
    void *slider = rt_slider_new(NULL, 1);
    assert(group && label && button && slider);
    rt_widget_add_child(group, label);
    rt_widget_add_child(group, button);
    rt_widget_add_child(group, slider);
    rt_widget_add_child(app, group);
    rt_widget_set_accessible_name(button, rt_const_cstr("Continue to account"));
    rt_gui_app_render(app);

    assert(rt_gui_atspi_linux_snapshot_count(state->window) == 5);
    assert(rt_gui_atspi_linux_cache_item_count(state->window) == 5);

    int clicks = 0;
    vg_button_set_on_click((vg_button_t *)button, note_button_click, &clicks);
    atspi_request_fixture_t action = {.window = state->window,
                                      .widget_id = ((vg_widget_t *)button)->id,
                                      .kind = RT_GUI_ATSPI_TEST_ACTION};
    complete_request_on_gui_thread(app, &action);
    assert(clicks == 1);

    atspi_request_fixture_t value = {.window = state->window,
                                     .widget_id = ((vg_widget_t *)slider)->id,
                                     .kind = RT_GUI_ATSPI_TEST_VALUE,
                                     .value = 0.75};
    complete_request_on_gui_thread(app, &value);
    assert(rt_slider_get_value(slider) > 0.749 && rt_slider_get_value(slider) < 0.751);

    atspi_request_fixture_t first = {.window = state->window,
                                     .widget_id = ((vg_widget_t *)button)->id,
                                     .kind = RT_GUI_ATSPI_TEST_ACTION};
    pthread_t first_worker;
    assert(pthread_create(&first_worker, NULL, run_atspi_request, &first) == 0);
    usleep(10000);
    assert(rt_gui_atspi_linux_test_request(
               state->window, ((vg_widget_t *)slider)->id, RT_GUI_ATSPI_TEST_VALUE, 0.25) == 0);
    rt_gui_app_render(app);
    assert(pthread_join(first_worker, NULL) == 0);
    assert(first.result == 1);

    rt_gui_atspi_linux_announce(
        state->window, (vg_widget_t *)label, "Account is ready", VG_LIVE_REGION_POLITE);

    atspi_request_fixture_t detached = {.window = state->window,
                                        .widget_id = ((vg_widget_t *)button)->id,
                                        .kind = RT_GUI_ATSPI_TEST_ACTION};
    pthread_t detached_worker;
    assert(pthread_create(&detached_worker, NULL, run_atspi_request, &detached) == 0);
    usleep(10000);
    rt_gui_app_destroy(app);
    assert(pthread_join(detached_worker, NULL) == 0);
    assert(detached.result == 0);
    puts("RTGuiAtspiLinuxTests: PASSED");
    return 0;
}
