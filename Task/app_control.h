#ifndef APP_CONTROL_H
#define APP_CONTROL_H

#include <stdbool.h>

#include "thrust_calculator.h"

/* 初始化应用状态、ADC 测量和串口屏接收。 */
void AppControl_Init(void);
/* 主循环周期调用，处理屏幕事件并刷新压力、推力。 */
void AppControl_Process(void);
float AppControl_GetCylinderDiameterMm(void);
float AppControl_GetPressureMpa(void);
float AppControl_GetThrustValue(void);
ThrustUnit AppControl_GetUnit(void);
/* 设置缸径，返回 true 表示通过范围检查。 */
bool AppControl_SetCylinderDiameter(float diameter_mm);

#endif
