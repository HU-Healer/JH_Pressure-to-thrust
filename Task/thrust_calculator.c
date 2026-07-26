/*
 * 文件功能：根据液压压力和油缸缸径计算推力。
 * 内部统一保留浮点精度，显示时再由屏幕格式控制小数位数。
 */
#include "thrust_calculator.h"

/* 几何计算和重力换算所需的常量。 */
#define THRUST_PI                 (3.14159265358979323846f)
#define THRUST_STANDARD_GRAVITY   (9.80665f)

float Thrust_CalculateNewton(float pressure_mpa, float cylinder_d_mm)
{
    float diameter_m;
    float area_m2;

    /* 非法输入不参与计算，避免得到负推力或异常结果。 */
    if ((pressure_mpa <= 0.0f) || (cylinder_d_mm <= 0.0f))
    {
        return 0.0f;
    }

    /* 缸径由 mm 换算为 m，再计算活塞有效面积。 */
    diameter_m = cylinder_d_mm / 1000.0f;
    area_m2 = THRUST_PI * diameter_m * diameter_m / 4.0f;
    return pressure_mpa * 1000000.0f * area_m2;
}

float Thrust_Convert(float force_newton, ThrustUnit unit)
{
    /* kgf 和吨均以标准重力 9.80665N/kg 进行换算。 */
    switch (unit)
    {
    case THRUST_UNIT_TON:
        return force_newton / THRUST_STANDARD_GRAVITY / 1000.0f;

    case THRUST_UNIT_NEWTON:
        return force_newton;

    case THRUST_UNIT_KGF:
    default:
        return force_newton / THRUST_STANDARD_GRAVITY;
    }
}

const char *Thrust_GetUnitText(ThrustUnit unit)
{
    switch (unit)
    {
    case THRUST_UNIT_TON:
        /* UTF-8 编码的“吨”，避免 ARMCC 将 UTF-8 源码字符串误当作本地代码页。 */
        return "\xE5\x90\xA8";

    case THRUST_UNIT_NEWTON:
        return "N";

    case THRUST_UNIT_KGF:
    default:
        return "kg";
    }
}
