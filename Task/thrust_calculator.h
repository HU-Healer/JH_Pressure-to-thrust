#ifndef THRUST_CALCULATOR_H
#define THRUST_CALCULATOR_H

/* 推力单位枚举：内部计算统一使用 N，再转换为显示单位。 */
typedef enum
{
    THRUST_UNIT_KGF = 0,
    THRUST_UNIT_TON,
    THRUST_UNIT_NEWTON
} ThrustUnit;

/* 根据压力和缸径计算推力，压力单位 MPa，缸径单位 mm，返回单位 N。 */
float Thrust_CalculateNewton(float pressure_mpa, float cylinder_d_mm);
/* 将牛顿值转换为 kgf、吨或 N。 */
float Thrust_Convert(float force_newton, ThrustUnit unit);
/* 返回当前单位对应的文本。 */
const char *Thrust_GetUnitText(ThrustUnit unit);

#endif
