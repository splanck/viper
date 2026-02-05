//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: rt_easing.h
// Purpose: Easing functions for animation and interpolation.
// Key invariants: Input t in [0,1], output varies by function.
// Links: src/il/runtime/classes/RuntimeClasses.inc
//
//===----------------------------------------------------------------------===//

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    double rt_ease_linear(double t);
    double rt_ease_in_quad(double t);
    double rt_ease_out_quad(double t);
    double rt_ease_in_out_quad(double t);
    double rt_ease_in_cubic(double t);
    double rt_ease_out_cubic(double t);
    double rt_ease_in_out_cubic(double t);
    double rt_ease_in_quart(double t);
    double rt_ease_out_quart(double t);
    double rt_ease_in_out_quart(double t);
    double rt_ease_in_sine(double t);
    double rt_ease_out_sine(double t);
    double rt_ease_in_out_sine(double t);
    double rt_ease_in_expo(double t);
    double rt_ease_out_expo(double t);
    double rt_ease_in_out_expo(double t);
    double rt_ease_in_circ(double t);
    double rt_ease_out_circ(double t);
    double rt_ease_in_out_circ(double t);
    double rt_ease_in_back(double t);
    double rt_ease_out_back(double t);
    double rt_ease_in_out_back(double t);
    double rt_ease_in_elastic(double t);
    double rt_ease_out_elastic(double t);
    double rt_ease_in_out_elastic(double t);
    double rt_ease_in_bounce(double t);
    double rt_ease_out_bounce(double t);
    double rt_ease_in_out_bounce(double t);

#ifdef __cplusplus
}
#endif
