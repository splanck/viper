//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
// File: src/runtime/game/rt_world_projection.h
// Purpose: Stateless 2D world-to-screen projection helpers for game code.
//
//===----------------------------------------------------------------------===//
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

double rt_world_projection_linear_x(double world_x,
                                    double camera_x,
                                    double origin_x,
                                    double pixels_per_unit);
double rt_world_projection_linear_y(double world_y,
                                    double camera_y,
                                    double origin_y,
                                    double pixels_per_unit,
                                    int8_t flip_y);
double rt_world_projection_isometric_x(double world_x,
                                       double world_y,
                                       double origin_x,
                                       double tile_width);
double rt_world_projection_isometric_y(double world_x,
                                       double world_y,
                                       double origin_y,
                                       double tile_height);
double rt_world_projection_perspective_scale(double depth,
                                             double near_depth,
                                             double far_depth,
                                             double near_scale,
                                             double far_scale);
double rt_world_projection_perspective_x(double world_x,
                                         double center_x,
                                         double depth,
                                         double focal_length);
double rt_world_projection_perspective_y(double world_y,
                                         double center_y,
                                         double depth,
                                         double focal_length,
                                         int8_t flip_y);

#ifdef __cplusplus
}
#endif
