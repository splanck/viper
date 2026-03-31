# Plan 03: VideoPlayer Runtime Class

## Problem

No video playback runtime class exists. Need a unified API that handles
container parsing, codec decode, audio playback, A/V sync, and frame
delivery — usable from both games (Canvas blit) and GUI (VideoWidget).

## Goal

Create `Viper.Graphics.VideoPlayer` runtime class that:
- Loads AVI (Tier 1) and OGG Theora (Tier 2) files
- Decodes video frames to Pixels objects
- Plays audio via existing rt_audio system
- Synchronizes audio and video
- Exposes frame-by-frame API for game loop integration

## Zero External Dependencies

Composes existing decoders (JPEG, Vorbis, WAV) with new container parsers.

---

## Zia API

```zia
// Construction
var player = VideoPlayer.Open("cutscene.avi");

// Properties (read-only)
var w = VideoPlayer.get_Width(player);        // frame width in pixels
var h = VideoPlayer.get_Height(player);       // frame height in pixels
var dur = VideoPlayer.get_Duration(player);   // total duration in seconds
var pos = VideoPlayer.get_Position(player);   // current position in seconds
var playing = VideoPlayer.get_IsPlaying(player); // Boolean
var frame = VideoPlayer.get_Frame(player);    // current decoded Pixels

// Playback control
VideoPlayer.Play(player);
VideoPlayer.Pause(player);
VideoPlayer.Stop(player);              // rewind to start
VideoPlayer.Seek(player, seconds);     // seek to time position

// Frame advance (call each game frame)
VideoPlayer.Update(player, dt);        // advance by delta time (seconds)

// Volume
VideoPlayer.SetVolume(player, vol);    // 0.0-1.0
```

### Game Usage Pattern

```zia
var player = VideoPlayer.Open("intro.avi");
VideoPlayer.Play(player);

while (VideoPlayer.get_IsPlaying(player)) {
    Canvas.Poll(canvas);
    var dt = Canvas.get_DeltaTime(canvas) / 1000.0;
    VideoPlayer.Update(player, dt);

    var frame = VideoPlayer.get_Frame(player);
    if (frame != null) {
        Canvas.Clear(canvas, 0);
        Canvas.Blit(canvas, frame, 0, 0);
    }
    Canvas.Flip(canvas);
}
```

### 3D Usage (video texture on mesh)

```zia
VideoPlayer.Update(player, dt);
Material3D.SetTexture(screenMat, VideoPlayer.get_Frame(player));
Canvas3D.DrawMesh(canvas, screenMesh, screenXform, screenMat);
```

## Internal Architecture

```c
// rt_videoplayer.c

typedef struct {
    void *vptr;
    /* Container */
    uint8_t *file_data;
    size_t file_len;
    int32_t container_type;  // 0=AVI, 1=OGG
    avi_context_t avi;       // AVI state (if container_type==0)
    // OGG state added in Plan 04

    /* Video state */
    int32_t width, height;
    double fps;
    double duration;
    int32_t total_frames;

    /* Playback state */
    int8_t playing;
    int8_t looping;
    double position;         // current time in seconds
    int32_t current_frame;   // current video frame index
    double volume;

    /* Frame pool: double-buffer (display + decode) */
    void *frame_display;     // Pixels — currently displayed frame
    void *frame_decode;      // Pixels — being decoded into

    /* Audio */
    int8_t has_audio;
    int32_t audio_chunk_idx; // next audio chunk to feed
} rt_videoplayer;
```

### Frame Advance Logic (`Update`)

```c
void rt_videoplayer_update(void *obj, double dt) {
    rt_videoplayer *vp = (rt_videoplayer *)obj;
    if (!vp->playing || dt <= 0) return;

    vp->position += dt;

    // Determine which frame should be displayed at current position
    int32_t target_frame = (int32_t)(vp->position * vp->fps);
    if (target_frame >= vp->total_frames) {
        if (vp->looping) {
            vp->position = 0.0;
            target_frame = 0;
            vp->audio_chunk_idx = 0;
        } else {
            vp->playing = 0;
            return;
        }
    }

    // Skip frames if behind (frame drop)
    if (target_frame > vp->current_frame) {
        // Decode only the target frame (skip intermediate frames)
        decode_video_frame(vp, target_frame);
        vp->current_frame = target_frame;

        // Swap display/decode buffers
        void *tmp = vp->frame_display;
        vp->frame_display = vp->frame_decode;
        vp->frame_decode = tmp;
    }

    // Feed audio chunks up to current position
    feed_audio(vp);
}
```

### MJPEG Decode

```c
static void decode_video_frame(rt_videoplayer *vp, int32_t frame_idx) {
    if (vp->container_type == 0) { // AVI
        uint32_t size = 0;
        const uint8_t *jpeg_data = avi_get_video_frame(&vp->avi, frame_idx, &size);
        if (!jpeg_data) return;

        void *decoded = rt_jpeg_decode_buffer(jpeg_data, size);
        if (decoded) {
            // Copy decoded pixels into frame_decode buffer
            // (reuse buffer to avoid per-frame allocation)
            copy_pixels(vp->frame_decode, decoded);
        }
    }
}
```

## Files

| File | Description |
|------|-------------|
| `src/runtime/graphics/rt_videoplayer.c` | VideoPlayer implementation (~400 LOC) |
| `src/runtime/graphics/rt_videoplayer.h` | Public API declarations |
| `src/runtime/graphics/rt_graphics_stubs.c` | Stubs for all new functions |
| `src/runtime/CMakeLists.txt` | Add source file |
| `src/il/runtime/runtime.def` | Register VideoPlayer class + methods |
| `src/il/runtime/RuntimeSignatures.cpp` | Add `#include "rt_videoplayer.h"` |
| `src/il/runtime/classes/RuntimeClasses.hpp` | Add `RTCLS_VideoPlayer` |

## LOC Estimate

~400 LOC (playback controller, frame advance, A/V feed, API wrappers).

## Testing

### Unit Test: `TestVideoPlayer.cpp`

```cpp
TEST(VideoPlayer, OpensAviFile) {
    void *player = rt_videoplayer_open(rt_string_from_cstr("tests/runtime/assets/test_video.avi"));
    ASSERT_NE(player, nullptr);
    EXPECT_GT(rt_videoplayer_get_width(player), 0);
    EXPECT_GT(rt_videoplayer_get_height(player), 0);
    EXPECT_GT(rt_videoplayer_get_duration(player), 0.0);
    EXPECT_EQ(rt_videoplayer_get_is_playing(player), 0);
}

TEST(VideoPlayer, PlayAndUpdate) {
    void *player = rt_videoplayer_open(rt_string_from_cstr("tests/runtime/assets/test_video.avi"));
    ASSERT_NE(player, nullptr);
    rt_videoplayer_play(player);
    EXPECT_EQ(rt_videoplayer_get_is_playing(player), 1);

    // Advance by one frame period
    double fps = /* get fps */;
    rt_videoplayer_update(player, 1.0 / fps + 0.001);

    void *frame = rt_videoplayer_get_frame(player);
    EXPECT_NE(frame, nullptr);
    EXPECT_GT(rt_pixels_width(frame), 0);
}

TEST(VideoPlayer, StopRewinds) {
    void *player = rt_videoplayer_open(rt_string_from_cstr("tests/runtime/assets/test_video.avi"));
    rt_videoplayer_play(player);
    rt_videoplayer_update(player, 0.5);
    EXPECT_GT(rt_videoplayer_get_position(player), 0.0);
    rt_videoplayer_stop(player);
    EXPECT_NEAR(rt_videoplayer_get_position(player), 0.0, 0.001);
    EXPECT_EQ(rt_videoplayer_get_is_playing(player), 0);
}

TEST(VideoPlayer, RejectsInvalidPath) {
    void *player = rt_videoplayer_open(rt_string_from_cstr("nonexistent.avi"));
    EXPECT_EQ(player, nullptr);
}
```

### Zia Integration Test: `test_videoplayer.zia`

```zia
module TestVideoPlayer;
bind Viper.Graphics;
bind Viper.Terminal;

func start() {
    var player = VideoPlayer.Open("tests/runtime/assets/test_video.avi");
    if (player == null) {
        Say("FAIL: could not open test video");
        return;
    }
    if (VideoPlayer.get_Width(player) <= 0) {
        Say("FAIL: width <= 0");
        return;
    }
    VideoPlayer.Play(player);
    VideoPlayer.Update(player, 0.1);
    var frame = VideoPlayer.get_Frame(player);
    if (frame == null) {
        Say("FAIL: no frame after update");
        return;
    }
    VideoPlayer.Stop(player);
    Say("PASS");
}
```

### CMake Registration

```cmake
viper_add_test(test_videoplayer
    ${CMAKE_CURRENT_SOURCE_DIR}/unit/runtime/TestVideoPlayer.cpp)
target_link_libraries(test_videoplayer PRIVATE viper_test_common)
viper_add_ctest(test_videoplayer test_videoplayer)
set_tests_properties(test_videoplayer PROPERTIES LABELS "unit;runtime")

# Zia integration test
add_test(NAME rt_test_videoplayer
    COMMAND $<TARGET_FILE:viper> run
        "${CMAKE_SOURCE_DIR}/tests/runtime/assets/test_videoplayer.zia"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(rt_test_videoplayer PROPERTIES
    PASS_REGULAR_EXPRESSION "PASS"
    LABELS "runtime"
    TIMEOUT 15)
```

## Verification

1. `./scripts/build_viper.sh`
2. `ctest --test-dir build -R test_videoplayer --output-on-failure`
3. Run demo: `viper run examples/apiaudit/graphics/video_demo.zia`
