//===----------------------------------------------------------------------===//
//
// Part of the Viper project, under the GNU GPL v3.
// See LICENSE for license information.
//
//===----------------------------------------------------------------------===//
//
// File: src/runtime/rt_easing.c
// Purpose: Standard easing functions for animation and interpolation.
//          All functions map t in [0,1] to an eased output value.
//
//===----------------------------------------------------------------------===//

#include "rt_easing.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

double rt_ease_linear(double t) { return t; }

double rt_ease_in_quad(double t) { return t * t; }
double rt_ease_out_quad(double t) { return t * (2.0 - t); }
double rt_ease_in_out_quad(double t)
{
    return t < 0.5 ? 2.0 * t * t : -1.0 + (4.0 - 2.0 * t) * t;
}

double rt_ease_in_cubic(double t) { return t * t * t; }
double rt_ease_out_cubic(double t) { double u = t - 1.0; return u * u * u + 1.0; }
double rt_ease_in_out_cubic(double t)
{
    return t < 0.5 ? 4.0 * t * t * t : (t - 1.0) * (2.0 * t - 2.0) * (2.0 * t - 2.0) + 1.0;
}

double rt_ease_in_quart(double t) { return t * t * t * t; }
double rt_ease_out_quart(double t) { double u = t - 1.0; return 1.0 - u * u * u * u; }
double rt_ease_in_out_quart(double t)
{
    double u = t - 1.0;
    return t < 0.5 ? 8.0 * t * t * t * t : 1.0 - 8.0 * u * u * u * u;
}

double rt_ease_in_sine(double t) { return 1.0 - cos(t * M_PI / 2.0); }
double rt_ease_out_sine(double t) { return sin(t * M_PI / 2.0); }
double rt_ease_in_out_sine(double t) { return 0.5 * (1.0 - cos(M_PI * t)); }

double rt_ease_in_expo(double t)
{
    return t <= 0.0 ? 0.0 : pow(2.0, 10.0 * (t - 1.0));
}
double rt_ease_out_expo(double t)
{
    return t >= 1.0 ? 1.0 : 1.0 - pow(2.0, -10.0 * t);
}
double rt_ease_in_out_expo(double t)
{
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    return t < 0.5
        ? 0.5 * pow(2.0, 20.0 * t - 10.0)
        : 1.0 - 0.5 * pow(2.0, -20.0 * t + 10.0);
}

double rt_ease_in_circ(double t) { return 1.0 - sqrt(1.0 - t * t); }
double rt_ease_out_circ(double t) { double u = t - 1.0; return sqrt(1.0 - u * u); }
double rt_ease_in_out_circ(double t)
{
    if (t < 0.5)
        return 0.5 * (1.0 - sqrt(1.0 - 4.0 * t * t));
    double u = 2.0 * t - 2.0;
    return 0.5 * (sqrt(1.0 - u * u) + 1.0);
}

#define BACK_C1 1.70158
#define BACK_C2 (BACK_C1 * 1.525)
#define BACK_C3 (BACK_C1 + 1.0)

double rt_ease_in_back(double t)
{
    return BACK_C3 * t * t * t - BACK_C1 * t * t;
}
double rt_ease_out_back(double t)
{
    double u = t - 1.0;
    return 1.0 + BACK_C3 * u * u * u + BACK_C1 * u * u;
}
double rt_ease_in_out_back(double t)
{
    if (t < 0.5)
    {
        double s = 2.0 * t;
        return 0.5 * (s * s * ((BACK_C2 + 1.0) * s - BACK_C2));
    }
    double s = 2.0 * t - 2.0;
    return 0.5 * (s * s * ((BACK_C2 + 1.0) * s + BACK_C2) + 2.0);
}

#define ELASTIC_C4 (2.0 * M_PI / 3.0)
#define ELASTIC_C5 (2.0 * M_PI / 4.5)

double rt_ease_in_elastic(double t)
{
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    return -pow(2.0, 10.0 * t - 10.0) * sin((10.0 * t - 10.75) * ELASTIC_C4);
}
double rt_ease_out_elastic(double t)
{
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    return pow(2.0, -10.0 * t) * sin((10.0 * t - 0.75) * ELASTIC_C4) + 1.0;
}
double rt_ease_in_out_elastic(double t)
{
    if (t <= 0.0) return 0.0;
    if (t >= 1.0) return 1.0;
    if (t < 0.5)
        return -0.5 * pow(2.0, 20.0 * t - 10.0) * sin((20.0 * t - 11.125) * ELASTIC_C5);
    return 0.5 * pow(2.0, -20.0 * t + 10.0) * sin((20.0 * t - 11.125) * ELASTIC_C5) + 1.0;
}

double rt_ease_out_bounce(double t)
{
    const double n1 = 7.5625;
    const double d1 = 2.75;
    if (t < 1.0 / d1)
        return n1 * t * t;
    if (t < 2.0 / d1)
    {
        t -= 1.5 / d1;
        return n1 * t * t + 0.75;
    }
    if (t < 2.5 / d1)
    {
        t -= 2.25 / d1;
        return n1 * t * t + 0.9375;
    }
    t -= 2.625 / d1;
    return n1 * t * t + 0.984375;
}

double rt_ease_in_bounce(double t)
{
    return 1.0 - rt_ease_out_bounce(1.0 - t);
}

double rt_ease_in_out_bounce(double t)
{
    return t < 0.5
        ? 0.5 * (1.0 - rt_ease_out_bounce(1.0 - 2.0 * t))
        : 0.5 * (1.0 + rt_ease_out_bounce(2.0 * t - 1.0));
}
