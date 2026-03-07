/*
 * Arduino Math Functions
 *
 * map() function
 *
 * Licensed under the Apache License 2.0
 */

#include "Arduino.h"

long map(long x, long in_min, long in_max, long out_min, long out_max)
{
    // Handle edge case where input range is zero
    if (in_min == in_max) {
        return out_min;
    }

    // Calculate the mapped value
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
