#ifndef PRESSURE_MEASURE_H
#define PRESSURE_MEASURE_H

#include <stdbool.h>
#include <stdint.h>

#define PRESSURE_ADC_CHANNEL_COUNT     (10U)
#define PRESSURE_ADC_SCAN_LENGTH        (11U)

/* 初始化 ADC 校准和测量状态。 */
void PressureMeasure_Init(void);// 初始化压力测量
/* 启动或处理一次 ADC 扫描，返回 true 表示产生了新的有效压力值。 */
bool PressureMeasure_Process(void);// 处理压力测量
/* 读取最近一次有效测量结果。 */
float PressureMeasure_GetMpa(void); // 获取压力值 (MPa)
float PressureMeasure_GetSignalVoltage(void);// 获取信号电压 (V)
float PressureMeasure_GetVdda(void);// 获取实际 VDDA (V)
/* 获取十路 IN0-IN9 去极值平均后的压力信号 ADC 码值。 */
uint16_t PressureMeasure_GetSignalAdcCode(void);
/* 获取 ADC1 采样到的内部 VREFINT 原始 ADC 码值。 */
uint16_t PressureMeasure_GetVrefintAdcCode(void);
/* 对十路同源 ADC 码值去极值后求平均。 */
uint16_t Pressure_CalculateRobustCode(const uint16_t *samples, uint32_t count);
/* 将 ADC 码值和实际 VDDA 换算为 MPa。 */
float Pressure_CalculateMpa(uint16_t adc_code, float vdda);

#endif
