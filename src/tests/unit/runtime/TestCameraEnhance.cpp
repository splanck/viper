//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/tests/unit/runtime/TestCameraEnhance.cpp
// Purpose: Tests for Camera.SmoothFollow and Camera.SetDeadzone.
//
//===----------------------------------------------------------------------===//

#include "tests/TestHarness.hpp"

#include <cstdlib>

extern "C" {
void *rt_camera_new(int64_t width, int64_t height);
void rt_camera_follow(void *cam, int64_t x, int64_t y);
void rt_camera_smooth_follow(void *cam, int64_t x, int64_t y, int64_t lerp);
void rt_camera_set_deadzone(void *cam, int64_t w, int64_t h);
void rt_camera_set_bounds(void *cam, int64_t min_x, int64_t min_y,
                          int64_t max_x, int64_t max_y);
int64_t rt_camera_get_x(void *cam);
int64_t rt_camera_get_y(void *cam);
}

TEST(CameraEnhance, SmoothFollowConverges) {
    void *cam = rt_camera_new(800, 600);
    // Start at origin, target at (400, 300) → desired camera pos = (0, 0)
    // Camera starts at (0,0), smooth follow toward (1000, 500)
    // Desired = (1000-400, 500-300) = (600, 200)
    for (int i = 0; i < 100; i++)
        rt_camera_smooth_follow(cam, 1000, 500, 200); // 20% lerp per frame

    // After 100 iterations at 20% lerp, should be very close to target
    int64_t x = rt_camera_get_x(cam);
    int64_t y = rt_camera_get_y(cam);
    EXPECT_TRUE(x > 590 && x <= 600);
    EXPECT_TRUE(y > 195 && y <= 200);
}

TEST(CameraEnhance, SmoothFollowInstantAt1000) {
    void *cam = rt_camera_new(800, 600);
    rt_camera_smooth_follow(cam, 1000, 500, 1000); // instant
    EXPECT_EQ(rt_camera_get_x(cam), 600);  // 1000 - 400
    EXPECT_EQ(rt_camera_get_y(cam), 200);  // 500 - 300
}

TEST(CameraEnhance, DeadzoneNoMovement) {
    void *cam = rt_camera_new(800, 600);
    rt_camera_follow(cam, 500, 400); // center on (500,400)
    int64_t base_x = rt_camera_get_x(cam);
    int64_t base_y = rt_camera_get_y(cam);

    rt_camera_set_deadzone(cam, 100, 100);
    // Move target only slightly (within deadzone)
    rt_camera_smooth_follow(cam, 510, 410, 500);
    EXPECT_EQ(rt_camera_get_x(cam), base_x);
    EXPECT_EQ(rt_camera_get_y(cam), base_y);
}

TEST(CameraEnhance, DeadzoneTriggersOutside) {
    void *cam = rt_camera_new(800, 600);
    rt_camera_follow(cam, 500, 400);
    int64_t base_x = rt_camera_get_x(cam);

    rt_camera_set_deadzone(cam, 50, 50);
    // Move target far outside deadzone
    rt_camera_smooth_follow(cam, 800, 400, 500);
    // Camera should have moved
    EXPECT_TRUE(rt_camera_get_x(cam) > base_x);
}

int main() {
    return viper_test::run_all_tests();
}
