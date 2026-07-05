#include "../math.h"

#define PI       3.14159265358979323846
#define HALF_PI  1.57079632679489661923
#define TWO_PI   6.28318530717958647692

static double math_abs(double x) {
    return x < 0.0 ? -x : x;
}

static double wrap_pi(double x) {
    while (x > PI) x -= TWO_PI;
    while (x < -PI) x += TWO_PI;
    return x;
}

double fabs(double x) {
    return math_abs(x);
}

double sin(double x) {
    double y;

    x = wrap_pi(x);
    y = (4.0 / PI) * x - (4.0 / (PI * PI)) * x * math_abs(x);
    y = 0.225 * (y * math_abs(y) - y) + y;
    return y;
}

double cos(double x) {
    return sin(x + HALF_PI);
}

double tan(double x) {
    double c = cos(x);
    if (c > -0.000001 && c < 0.000001) {
        return sin(x) >= 0.0 ? 1000000.0 : -1000000.0;
    }
    return sin(x) / c;
}

double atan(double x) {
    double ax = math_abs(x);
    double result;

    if (ax > 1.0) {
        result = HALF_PI - atan(1.0 / ax);
    } else {
        result = (PI / 4.0) * x - x * (ax - 1.0) * (0.2447 + 0.0663 * ax);
        return result;
    }

    return x < 0.0 ? -result : result;
}

double sqrt(double x) {
    double guess;

    if (x <= 0.0) return 0.0;
    guess = x > 1.0 ? x : 1.0;
    for (int i = 0; i < 10; i++) {
        guess = 0.5 * (guess + x / guess);
    }
    return guess;
}
