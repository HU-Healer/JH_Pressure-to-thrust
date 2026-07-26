/*
 * 文件功能：应用层调度。
 * 本文件不直接操作 ADC 寄存器，只协调测量、计算和屏幕通信模块。
 */
#include "app_control.h"

#include <stdbool.h>

#include "main.h"
#include "pressure_measure.h"
#include "screen_protocol.h"

/* 缸径默认值和安全输入范围，单位均为 mm。 */
#define APP_DEFAULT_CYLINDER_DIAMETER_MM  (100.0f)
#define APP_MIN_CYLINDER_DIAMETER_MM      (1.0f)
#define APP_MAX_CYLINDER_DIAMETER_MM      (1000.0f)
#define APP_REFRESH_INTERVAL_MS           (100U)

/* 应用层保存的最新测量值和当前显示单位。 */
static float s_cylinder_diameter_mm;
static float s_pressure_mpa;
static float s_thrust_value;
static float s_force_newton;
static ThrustUnit s_unit;
static uint32_t s_last_refresh_tick;

/* 按 kg、吨、N 的顺序循环显示单位。 */
static void AppControl_CycleUnit(void)
{
    if (s_unit == THRUST_UNIT_NEWTON)
    {
        s_unit = THRUST_UNIT_KGF;
    }
    else
    {
        s_unit = (ThrustUnit)(s_unit + 1);
    }
}

/* 取出串口屏协议层产生的事件，并更新应用状态。 */
static void AppControl_HandleScreenEvents(void)
{
    ScreenEvent event;

    while (ScreenProtocol_PollEvent(&event))
    {
        if (event.type == SCREEN_EVENT_SET_CYLINDER_DIAMETER)
        {
            (void)AppControl_SetCylinderDiameter(event.value);
        }
        else if (event.type == SCREEN_EVENT_CYCLE_UNIT)
        {
            AppControl_CycleUnit();
        }
    }
}

void AppControl_Init(void)
{
    /* 上电先使用默认缸径和 kg 单位，等待屏幕传来新的设置。 */
    s_cylinder_diameter_mm = APP_DEFAULT_CYLINDER_DIAMETER_MM;
    s_pressure_mpa = 0.0f;
    s_force_newton = 0.0f;
    s_thrust_value = 0.0f;
    s_unit = THRUST_UNIT_KGF;
    s_last_refresh_tick = HAL_GetTick();

    PressureMeasure_Init();
    ScreenProtocol_Init();
}

void AppControl_Process(void)
{
    uint32_t now = HAL_GetTick();

    /* 屏幕事件优先处理，确保缸径和单位状态及时生效。 */
    AppControl_HandleScreenEvents();

    if ((uint32_t)(now - s_last_refresh_tick) < APP_REFRESH_INTERVAL_MS)
    {
        return;
    }

    s_last_refresh_tick = now;
    /* 仅在得到新一轮有效压力时重新计算推力并刷新屏幕。 */
    if (PressureMeasure_Process())
    {
        s_pressure_mpa = PressureMeasure_GetMpa();
        s_force_newton = Thrust_CalculateNewton(s_pressure_mpa, s_cylinder_diameter_mm);
        s_thrust_value = Thrust_Convert(s_force_newton, s_unit);
        (void)ScreenProtocol_SendMeasurements(s_pressure_mpa, s_thrust_value, s_unit);
    }
}

float AppControl_GetCylinderDiameterMm(void)
{
    return s_cylinder_diameter_mm;
}

float AppControl_GetPressureMpa(void)
{
    return s_pressure_mpa;
}

float AppControl_GetThrustValue(void)
{
    return s_thrust_value;
}

ThrustUnit AppControl_GetUnit(void)
{
    return s_unit;
}

bool AppControl_SetCylinderDiameter(float diameter_mm)
{
    if ((diameter_mm < APP_MIN_CYLINDER_DIAMETER_MM) || (diameter_mm > APP_MAX_CYLINDER_DIAMETER_MM))
    {
        return false;
    }

    s_cylinder_diameter_mm = diameter_mm;
    return true;
}
