#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "thrust_calculator.h"

static void AssertClose(float actual, float expected, float tolerance)
{
    assert(fabsf(actual - expected) <= tolerance);
}

int main(void)
{
    AssertClose(Thrust_CalculateNewton(30.0f, 100.0f), 235619.45f, 0.2f);
    AssertClose(Thrust_Convert(9806.65f, THRUST_UNIT_KGF), 1000.0f, 0.01f);
    AssertClose(Thrust_Convert(9806.65f, THRUST_UNIT_TON), 1.0f, 0.001f);
    AssertClose(Thrust_Convert(9806.65f, THRUST_UNIT_NEWTON), 9806.65f, 0.01f);
    puts("PASS");
    return 0;
}
