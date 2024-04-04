#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

static inline double modulof(double lhs, double rhs) {
    // Store for later as C doesn't let us do modulo with floats
    double fraction = lhs - floor(lhs); 
    // Do integer modulo then add back the fraction
    // We use int64_t since we know that double-precision floating point types
    // support the integer range: -(2^53) to 2^53.
    return ((int64_t)lhs % (int64_t)floor(rhs)) + fraction;
}

/**
 * @brief       Goal: separate a floating type value into its integer and fractional
 *              parts with as little loss of precision as possible.
 */
int main(void) {
    double whole = 1.3;
    double fractional = whole - floor(whole);
    printf("whole = %f, fractional = %f\n", whole, fractional);
    printf("1.2 %% 1 = %f\n", modulof(1.2, 1.0));
    printf("1.2 %% 1 = %f\n", fmod(1.2, 1.0));
    return 0;
}
