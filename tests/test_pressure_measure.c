#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "pressure_measure.h"

static void AssertClose(float actual, float expected, float tolerance)
{
    assert(fabsf(actual - expected) <= tolerance);
}

int main(void)
{
    const uint16_t samples[10] = {2048U, 2047U, 2048U, 2049U, 2048U,
                                  2048U, 2047U, 2048U, 2048U, 4095U};

    assert(Pressure_CalculateRobustCode(samples, 10U) == 2048U);
    AssertClose(Pressure_CalculateMpa(744U, 3.3f), 0.0f, 0.05f);
    AssertClose(Pressure_CalculateMpa(3723U, 3.3f), 30.0f, 0.05f);
    puts("PASS");
    return 0;
}
