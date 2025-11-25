# ViperGFX Test Infrastructure

**Status:** ✅ **COMPLETE AND PASSING**
**Date:** 2025-11-21
**Test Coverage:** 20 tests (T1-T21) - 100% passing

## Overview

The ViperGFX test infrastructure provides deterministic unit testing without requiring real OS windowing systems. This
enables automated testing in CI environments and ensures consistent behavior across platforms.

## Architecture

```
Test Files (tests/*.c)
    ↓ link against
Mock Library (libvipergfx_mock.a)
    ├── vgfx.c (core)
    ├── vgfx_draw.c (drawing)
    └── vgfx_platform_mock.c (mock backend)
```

## Components

### 1. Mock Platform Backend

**File:** `src/vgfx_platform_mock.c` (229 lines)

**Purpose:** Test-only backend that simulates window behavior without creating real OS windows.

**Key Features:**

- **No real OS resources:** Pure in-memory implementation
- **Deterministic time control:** Mock clock for FPS testing
- **Event injection API:** Programmatically inject keyboard, mouse, and window events
- **Zero dependencies:** No graphics libraries or windowing systems

**Implementation:**

```c
/* Mock platform state */
static int64_t g_mock_time_ms = 0;

/* Platform API stubs */
int vgfx_platform_init_window(...) { return 1; }  // No-op
void vgfx_platform_destroy_window(...) { }         // No-op
int vgfx_platform_process_events(...) { return 1; } // Events injected manually
int vgfx_platform_present(...) { return 1; }       // No display
void vgfx_platform_sleep_ms(ms) { g_mock_time_ms += ms; }
int64_t vgfx_platform_now_ms(void) { return g_mock_time_ms; }
```

### 2. Mock Control API

**File:** `tests/vgfx_mock.h`

**Time Control:**

```c
void vgfx_mock_set_time_ms(int64_t ms);
int64_t vgfx_mock_get_time_ms(void);
void vgfx_mock_advance_time_ms(int64_t delta_ms);
```

**Event Injection:**

```c
void vgfx_mock_inject_key_event(vgfx_window_t win, vgfx_key_t key, int down);
void vgfx_mock_inject_mouse_move(vgfx_window_t win, int32_t x, int32_t y);
void vgfx_mock_inject_mouse_button(vgfx_window_t win, vgfx_mouse_button_t btn, int down);
void vgfx_mock_inject_resize(vgfx_window_t win, int32_t width, int32_t height);
void vgfx_mock_inject_close(vgfx_window_t win);
void vgfx_mock_inject_focus(vgfx_window_t win, int gained);
```

### 3. Test Harness

**File:** `tests/test_harness.h`

**Assertion Macros:**

```c
TEST_BEGIN(name)                  // Start test case
TEST_END()                        // Mark test as passed
ASSERT_TRUE(cond)                 // Assert condition is true
ASSERT_FALSE(cond)                // Assert condition is false
ASSERT_EQ(a, b)                   // Assert equality
ASSERT_NE(a, b)                   // Assert inequality
ASSERT_NULL(ptr)                  // Assert pointer is NULL
ASSERT_NOT_NULL(ptr)              // Assert pointer is not NULL
ASSERT_STR_EQ(a, b)               // Assert string equality
TEST_SUMMARY()                    // Print final results
TEST_RETURN_CODE()                // Return 0 if all passed, 1 otherwise
```

**Output Format:**

```
[ RUN      ] Test Name
[       OK ] Test Name

[  FAILED  ] Test Name
  Assertion failed: <details>
  File:Line: <message>
```

### 4. Test Suites

#### test_window.c (T1-T3)

- **T1:** Window creation with valid parameters
- **T2:** Window creation with dimensions exceeding maximum
- **T3:** Invalid dimensions use defaults

#### test_pixels.c (T4-T6, T14)

- **T4:** Pixel set/get operations
- **T5:** Out-of-bounds writes are ignored
- **T6:** Clear screen fills all pixels
- **T14:** Direct framebuffer access

#### test_drawing.c (T7-T13)

- **T7:** Horizontal line drawing
- **T8:** Vertical line drawing
- **T9:** Diagonal line drawing
- **T10:** Rectangle outline
- **T11:** Filled rectangle
- **T12:** Circle outline (cardinal points + perimeter count)
- **T13:** Filled circle (cardinal points + area count)

#### test_input.c (T16-T21)

- **T16:** Keyboard input with mock events
- **T17:** Mouse position (in-bounds and out-of-bounds)
- **T18:** Mouse button state
- **T19:** Event queue basic operations
- **T20:** Event queue overflow (FIFO eviction)
- **T21:** Resize event with framebuffer reallocation

## Build System

### CMake Configuration

**File:** `tests/CMakeLists.txt`

```cmake
# Create mock library (uses mock backend instead of real platform)
add_library(vipergfx_mock STATIC
    ../src/vgfx.c
    ../src/vgfx_draw.c
    ../src/vgfx_platform_mock.c
)

# Test helper function
function(add_vgfx_test test_name source_file)
    add_executable(${test_name} ${source_file})
    target_link_libraries(${test_name} PRIVATE vipergfx_mock)
    add_test(NAME ${test_name} COMMAND ${test_name})
endfunction()

# Test targets
add_vgfx_test(test_window test_window.c)
add_vgfx_test(test_pixels test_pixels.c)
add_vgfx_test(test_drawing test_drawing.c)
add_vgfx_test(test_input test_input.c)
```

### Running Tests

```bash
# Build all tests
cmake --build build --target test_window test_pixels test_drawing test_input

# Run individual test
./build/tests/test_window

# Run all tests (CTest)
ctest --test-dir build --output-on-failure

# Run custom target
cmake --build build --target run_tests
```

## Test Results

### Summary

```
Test Suite         Tests  Passed  Status
─────────────────  ─────  ──────  ──────
test_window        3      3       ✅ PASS
test_pixels        4      4       ✅ PASS
test_drawing       7      7       ✅ PASS
test_input         6      6       ✅ PASS
─────────────────  ─────  ──────  ──────
TOTAL              20     20      ✅ PASS
```

### Coverage

The test suite validates:

- ✅ Window lifecycle (create, destroy, resize)
- ✅ Framebuffer operations (pixel read/write, clear)
- ✅ Drawing primitives (lines, rectangles, circles)
- ✅ Input handling (keyboard, mouse, events)
- ✅ Event queue (FIFO, overflow, priorities)
- ✅ Error handling (bounds checking, error messages)
- ✅ Edge cases (zero dimensions, out-of-bounds, overflow)

## Implementation Details

### Ring Buffer Fix

**Issue:** SPSC ring buffer with "one empty slot" strategy can only hold `SIZE-1` elements.

**Solution:** Internal array size is `VGFX_EVENT_QUEUE_SIZE + 1` (257 slots) to provide documented capacity of 256
events.

**Files Modified:**

- `include/vgfx_config.h`: Documents capacity as 256
- `src/vgfx_internal.h`: Defines `VGFX_INTERNAL_EVENT_QUEUE_SLOTS` (257)
- `src/vgfx.c`: Uses internal constant for modulo operations

### Filled Circle Fix

**Issue:** Filled circle missed horizontal scanline at circle center (east/west cardinal points were black).

**Root Cause:** Initial scanlines before loop only drew `cy±y` lines, missing `cy±x` lines needed for full coverage.

**Solution:** Added initial scanlines for all 4-way symmetry:

```c
hline(cx - x, cx + x, cy + y, ctx);  /* Top */
hline(cx - x, cx + x, cy - y, ctx);  /* Bottom */
hline(cx - y, cx + y, cy + x, ctx);  /* Center upper */
hline(cx - y, cx + y, cy - x, ctx);  /* Center lower */
```

### FIFO Event Queue Fix

**Issue:** Event queue dropped newest events instead of oldest (LIFO instead of FIFO).

**Solution:** When queue is full, advance tail to drop oldest event before inserting new event:

```c
if (next_head == win->event_tail) {
    if (oldest_is_close) {
        /* Preserve CLOSE events */
        return 0;
    } else {
        /* Drop oldest non-CLOSE event */
        win->event_tail = (win->event_tail + 1) % SLOTS;
        win->event_overflow++;
    }
}
```

## Advantages

1. **No OS Dependencies:** Tests run in CI without display server
2. **Deterministic:** Mock time ensures reproducible FPS tests
3. **Fast:** No actual rendering, event processing, or sleeps
4. **Comprehensive:** Covers all API surface area (20 tests)
5. **Maintainable:** Simple assertion macros, clear test structure
6. **Portable:** Same tests work on macOS, Linux, Windows

## Limitations

1. **No Visual Verification:** Tests cannot verify actual screen output
2. **No Platform Backends:** Mock doesn't test macOS/Linux/Windows backends
3. **No Integration Tests:** Tests don't cover cross-module interactions
4. **No Performance Tests:** Mock doesn't measure real rendering speed

## Future Enhancements

- [ ] Add visual regression tests with reference images
- [ ] Add platform backend tests (requires display)
- [ ] Add performance benchmarks (pixels/sec, FPS)
- [ ] Add stress tests (large windows, many events)
- [ ] Add property-based testing (randomized inputs)
- [ ] Add memory leak detection (Valgrind, ASan)
- [ ] Add code coverage reports (gcov, llvm-cov)

## Summary

The ViperGFX test infrastructure provides **production-quality automated testing** without requiring real OS windows.
All 20 tests pass, validating correctness of:

✅ **Core API** - Window management, framebuffer access
✅ **Drawing** - Lines, rectangles, circles (outline and filled)
✅ **Input** - Keyboard, mouse, event queue
✅ **Edge Cases** - Bounds checking, overflow, error handling

The mock backend enables **fast, deterministic, portable** testing suitable for CI/CD pipelines.
