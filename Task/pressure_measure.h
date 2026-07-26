#ifndef PRESSURE_MEASURE_H
#define PRESSURE_MEASURE_H

#include <stdbool.h>
#include <stdint.h>

#define PRESSURE_ADC_CHANNEL_COUNT     (10U)
#define PRESSURE_ADC_SCAN_LENGTH        (11U)

/* 初始化 ADC 校准和测量状态。 */
void PressureMeasure_Init(void);
/* 启动或处理一次 ADC 扫描，返回 true 表示产生了新的有效压力值。 */
bool PressureMeasure_Process(void);
/* 读取最近一次有效测量结果。 */
float PressureMeasure_GetMpa(void);
float PressureMeasure_GetSignalVoltage(void);
float PressureMeasure_GetVdda(void);
/* 对十路同源 ADC 码值去极值后求平均。 */
uint16_t Pressure_CalculateRobustCode(const uint16_t *samples, uint32_t count);
/* 将 ADC 码值和实际 VDDA 换算为 MPa。 */
float Pressure_CalculateMpa(uint16_t adc_code, float vdda);

#endif
