#include "../stdlib.h"
#include "../stdint.h"

/* Minimal GCC runtime support for freestanding 32-bit builds.
 * Provides the helper symbols required by the Doom port when linking
 * without libgcc. */

uint64_t __udivdi3(uint64_t a, uint64_t b) {
    uint64_t quotient = 0;
    uint64_t remainder = 0;
    int bits;

    if (b == 0) {
        return 0;
    }

    for (bits = 63; bits >= 0; --bits) {
        remainder = (remainder << 1) | ((a >> bits) & 1);
        if (remainder >= b) {
            remainder -= b;
            quotient |= (uint64_t)1 << bits;
        }
    }

    return quotient;
}

uint64_t __umoddi3(uint64_t a, uint64_t b) {
    uint64_t quotient = __udivdi3(a, b);
    return a - (quotient * b);
}

int64_t __divdi3(int64_t a, int64_t b) {
    int neg = 0;
    uint64_t ua;
    uint64_t ub;
    uint64_t result;

    if (a < 0) {
        neg ^= 1;
        ua = (uint64_t)(-a);
    } else {
        ua = (uint64_t)a;
    }

    if (b < 0) {
        neg ^= 1;
        ub = (uint64_t)(-b);
    } else {
        ub = (uint64_t)b;
    }

    result = __udivdi3(ua, ub);

    if (neg) {
        result = (uint64_t)(-(int64_t)result);
    }

    return (int64_t)result;
}

int64_t __moddi3(int64_t a, int64_t b) {
    int neg = 0;
    uint64_t ua;
    uint64_t ub;
    uint64_t result;

    if (a < 0) {
        neg ^= 1;
        ua = (uint64_t)(-a);
    } else {
        ua = (uint64_t)a;
    }

    if (b < 0) {
        neg ^= 1;
        ub = (uint64_t)(-b);
    } else {
        ub = (uint64_t)b;
    }

    result = __umoddi3(ua, ub);

    if (neg) {
        result = (uint64_t)(-(int64_t)result);
    }

    return (int64_t)result;
}
