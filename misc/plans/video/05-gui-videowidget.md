# Plan 05: GUI VideoWidget

## Problem

VideoPlayer provides the decode/playback engine, but GUI applications need
a widget that handles rendering, transport controls, and layout integration
within the Viper.GUI widget hierarchy.

## Goal

Create `Viper.GUI.VideoWidget` that wraps a VideoPlayer with:
- Video frame rendering (auto-sized to parent container)
- Transport controls (play/pause/stop buttons)
- Timeline scrubber (seek bar showing progress)
- Volume slider
- Fullscreen toggle

## Zero External Dependencies

Built on existing Viper.GUI widget classes. Follows the pattern of
existing complex widgets (e.g., `rt_gui_image.c` for images).

---

## Zia API

```zia
// Create video widget as child of a container
var video = VideoWidget.New(parentPanel, "movie.ogv");

// Playback control (delegates to internal VideoPlayer)
VideoWidget.Play(video);
VideoWidget.Pause(video);
VideoWidget.Stop(video);

// Properties
VideoWidget.set_ShowControls(video, true);   // show/hide transport bar
VideoWidget.set_AutoPlay(video, true);       // play on creation
VideoWidget.set_Loop(video, true);           // loop playback
VideoWidget.set_Volume(video, 0.8);

// The widget handles its own rendering and input
// It redraws automatically during the GUI event loop
```

## Internal Design

```c
typedef struct {
    // Base widget fields (inherits from rt_gui_widget)
    void *vptr;
    // ... standard widget layout fields ...

    // Video
    void *player;           // rt_videoplayer instance
    void *last_frame;       // cached Pixels for rendering

    // Controls
    int8_t show_controls;
    int8_t auto_play;
    int8_t looping;
    double volume;

    // Scrubber state
    int8_t scrubbing;       // user dragging timeline
    double scrub_pos;       // position during drag
} rt_gui_videowidget;
```

### Rendering

Each GUI frame tick:
1. Call `rt_videoplayer_update(player, dt)` to advance decode
2. Get current frame via `rt_videoplayer_get_frame(player)`
3. Blit frame Pixels to widget area (scaled to fit)
4. If `show_controls`: draw transport bar overlay at bottom
   - Play/pause button (toggle icon)
   - Timeline bar (filled proportionally to position/duration)
   - Time display (MM:SS / MM:SS)
   - Volume icon + slider

### Input Handling

- Click on video area: toggle play/pause
- Click on timeline bar: seek to position
- Drag timeline scrubber: live seek preview
- Click volume icon: mute toggle
- Double-click: fullscreen toggle

## Files

| File | Description |
|------|-------------|
| `src/runtime/gui/rt_gui_videowidget.c` | VideoWidget implementation (~300 LOC) |
| `src/runtime/gui/rt_gui_videowidget.h` | Public API |
| `src/runtime/graphics/rt_graphics_stubs.c` | Stubs |
| `src/il/runtime/runtime.def` | Register VideoWidget class |

## LOC Estimate

~300 LOC (widget rendering, control drawing, input handling, VideoPlayer delegation).

## Testing

### Zia Integration Test

```zia
module TestVideoWidget;
bind Viper.GUI;
bind Viper.Terminal;

func start() {
    var app = App.New("Video Widget Test", 640, 480);
    var panel = Panel.New(App.Root(app));
    var video = VideoWidget.New(panel, "tests/runtime/assets/test_video.avi");
    if (video == null) {
        Say("FAIL: could not create VideoWidget");
        return;
    }
    VideoWidget.set_ShowControls(video, true);
    Say("PASS");
}
```

### CMake Registration

```cmake
# Zia test
add_test(NAME rt_test_videowidget
    COMMAND $<TARGET_FILE:viper> run
        "${CMAKE_SOURCE_DIR}/tests/runtime/gui/test_videowidget.zia"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(rt_test_videowidget PROPERTIES
    PASS_REGULAR_EXPRESSION "PASS"
    LABELS "runtime;gui"
    TIMEOUT 15)
```

## Verification

1. `./scripts/build_viper.sh`
2. `ctest --test-dir build -R test_videowidget --output-on-failure`
3. Demo: `viper run examples/apps/mediaplayer/main.zia`
