# Plan 06: Documentation, Demos, and Test Suite

## Problem

New video playback features need documentation, demo programs, and
comprehensive test coverage.

## Goal

- Update `docs/graphics3d-guide.md` and `docs/viperlib/README.md`
- Create standalone video playback demos for game and GUI use cases
- Ensure all video features have unit tests and integration tests
- Generate test asset files (small AVI and OGV videos)

---

## Documentation Updates

### docs/viperlib/README.md

Add VideoPlayer to the Graphics module table:

```markdown
| [Graphics](docs/viperlib/graphics/README.md) | 12 | Canvas, sprites, tilemaps, cameras, bitmap fonts, **video playback** |
```

### docs/viperlib/graphics/README.md

Add VideoPlayer section:

```markdown
## VideoPlayer

Video playback for game cutscenes and GUI media widgets.

| Member | Description |
|--------|-------------|
| `Open(path)` | Load video file (AVI, OGV) |
| `Play()` | Start playback |
| `Pause()` | Pause playback |
| `Stop()` | Stop and rewind to start |
| `Seek(seconds)` | Seek to time position |
| `Update(dt)` | Advance by delta time (call each frame) |
| `SetVolume(vol)` | Set audio volume [0.0-1.0] |
| `Width` | Frame width in pixels (read-only) |
| `Height` | Frame height in pixels (read-only) |
| `Duration` | Total duration in seconds (read-only) |
| `Position` | Current position in seconds (read-only) |
| `IsPlaying` | Whether playback is active (read-only) |
| `Frame` | Current decoded Pixels frame (read-only) |

### Supported Formats

| Container | Video Codec | Audio Codec | Extension |
|-----------|-------------|-------------|-----------|
| AVI (RIFF) | MJPEG | PCM WAV | `.avi` |
| OGG | Theora | Vorbis | `.ogv` |

### Game Usage (cutscene)

\```zia
var player = VideoPlayer.Open("cutscene.ogv");
VideoPlayer.Play(player);

while (VideoPlayer.get_IsPlaying(player)) {
    Canvas.Poll(canvas);
    VideoPlayer.Update(player, Canvas.get_DeltaTime(canvas) / 1000.0);
    Canvas.Clear(canvas, 0);
    Canvas.Blit(canvas, VideoPlayer.get_Frame(player), 0, 0);
    Canvas.Flip(canvas);
}
\```

### 3D Usage (video texture)

\```zia
VideoPlayer.Update(player, dt);
Material3D.SetTexture(screenMat, VideoPlayer.get_Frame(player));
\```
```

### docs/viperlib/gui/README.md

Add VideoWidget to widget list.

---

## Demo Programs

### 1. Game Cutscene Demo

`examples/apiaudit/graphics/video_cutscene_demo.zia`

```zia
// Plays a video file as a fullscreen cutscene
// ESC skips, video auto-stops at end
module VideoCutsceneDemo;

bind Viper.Graphics;
bind Viper.Input;
bind Viper.Terminal;

func start() {
    Say("=== Video Cutscene Demo ===");
    Say("Press ESC to skip");

    var canvas = Canvas.New("Cutscene", 640, 480);
    var player = VideoPlayer.Open("tests/runtime/assets/test_video.avi");
    if (player == null) {
        Say("Error: could not open video");
        return;
    }

    VideoPlayer.Play(player);

    while (VideoPlayer.get_IsPlaying(player)) {
        var ev = Canvas.Poll(canvas);
        if (Keyboard.WasPressed(Keyboard.get_KEY_ESCAPE())) { break; }

        var dt = Canvas.get_DeltaTime(canvas) / 1000.0;
        VideoPlayer.Update(player, dt);

        var frame = VideoPlayer.get_Frame(player);
        if (frame != null) {
            Canvas.Clear(canvas, 0);
            Canvas.Blit(canvas, frame, 0, 0);
        }
        Canvas.Flip(canvas);
    }

    VideoPlayer.Stop(player);
    Say("Done.");
}
```

### 2. 3D Video Texture Demo

`examples/apiaudit/graphics3d/video_texture_demo.zia`

Renders a video playing on a 3D plane (like a TV screen in a game world).

### 3. GUI Media Player Demo

`examples/apps/mediaplayer/main.zia`

Simple media player app using VideoWidget with transport controls.

---

## Test Asset Generation

### Script: `scripts/generate_test_videos.sh`

```bash
#!/bin/bash
# Generate small test video assets for unit tests
# Requires ffmpeg (development tool only, NOT a runtime dependency)

ASSETS="tests/runtime/assets"

# 10-frame MJPEG AVI (160x120, 10fps, no audio) — ~20 KB
ffmpeg -y -f lavfi -i "testsrc=s=160x120:d=1:r=10" \
    -c:v mjpeg -q:v 5 "$ASSETS/test_video.avi"

# 10-frame MJPEG AVI with PCM audio (160x120, 10fps, 8kHz mono)
ffmpeg -y -f lavfi -i "testsrc=s=160x120:d=1:r=10" \
    -f lavfi -i "sine=f=440:d=1:sample_rate=8000" \
    -c:v mjpeg -q:v 5 -c:a pcm_s16le \
    "$ASSETS/test_video_audio.avi"

# 5-frame Theora OGV (160x120, no audio) — ~5 KB
ffmpeg -y -f lavfi -i "testsrc=s=160x120:d=0.5:r=10" \
    -c:v libtheora -q:v 5 "$ASSETS/test_theora.ogv"

# 1-second Theora+Vorbis OGV (160x120, 10fps)
ffmpeg -y -f lavfi -i "testsrc=s=160x120:d=1:r=10" \
    -f lavfi -i "sine=f=440:d=1" \
    -c:v libtheora -q:v 5 -c:a libvorbis \
    "$ASSETS/test_theora_av.ogv"

echo "Test video assets generated in $ASSETS/"
```

Note: ffmpeg is a **development tool** for generating test assets, NOT a
runtime dependency. The generated `.avi`/`.ogv` files are checked into the
repo as binary test fixtures.

---

## Complete Test Matrix

### Unit Tests (C++)

| Test | File | What It Tests |
|------|------|---------------|
| `test_jpeg_buffer_decode` | `TestJpegBufferDecode.cpp` | JPEG decode from memory buffer |
| `test_avi_parser` | `TestAviParser.cpp` | AVI RIFF parsing, stream detection |
| `test_videoplayer` | `TestVideoPlayer.cpp` | Open, play, update, stop, seek |
| `test_theora_decoder` | `TestTheoraDecoder.cpp` | Header parse, I-frame decode, YCbCr→RGB |

### Integration Tests (Zia)

| Test | File | What It Tests |
|------|------|---------------|
| `rt_test_videoplayer` | `test_videoplayer.zia` | End-to-end AVI playback |
| `rt_test_theora_playback` | `test_theora_playback.zia` | End-to-end OGV playback |
| `rt_test_videowidget` | `test_videowidget.zia` | GUI widget creation |

### Labels

All video tests should use labels `"runtime"` and optionally `"video"` for
selective execution: `ctest -L video`.

## Verification

1. `./scripts/generate_test_videos.sh` (one-time, assets committed to repo)
2. `./scripts/build_viper.sh`
3. `ctest --test-dir build -L video --output-on-failure`
4. Run all 3 demos manually and verify visual output
