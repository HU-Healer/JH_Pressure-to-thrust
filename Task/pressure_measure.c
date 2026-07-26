/*
 * 文件功能：ADC1 十路同源采样、VREFINT 参考测量和液压压力换算。
 * ADC1 的 DMA 缓冲区顺序为 IN0-IN9、VREFINT。
 */
#include "pressure_measure.h"

#include "adc.h"

/* 传感器 4-20mA 经 150R 采样电阻后对应 0.6-3.0V。 */
#define PRESSURE_ADC_FULL_SCALE             (4095.0f) // ADC 12 位分辨率对应的最大码值
#define PRESSURE_VREFINT_CALIBRATION_VOLTAGE (1.200f) // 标定的内部参考电压
#define PRESSURE_ZERO_CURRENT_VOLTAGE        (0.600f) // 4mA 对应的电压
#define PRESSURE_FULL_SCALE_MPA              (30.0f) // 20mA 对应的压力
#define PRESSURE_VOLT_PER_MPA                (0.080f) // 20mA 对应的电压增量
#define PRESSURE_MIN_VDDA                    (2.70f) // 3.3V 电源允许的最小值，低于此值可能导致 ADC 采样不准。
#define PRESSURE_MAX_VDDA                    (3.60f) // 3.3V 电源允许的最大值，超过此值可能损坏 ADC。

static uint16_t s_adc_dma_buffer[PRESSURE_ADC_SCAN_LENGTH]; // ADC DMA 缓冲区，包含十路采样和 VREFINT
static volatile bool s_adc_conversion_complete; // DMA 完成标志，由中断置位，主循环读取后清零
static bool s_adc_conversion_running; // ADC 转换正在进行标志，避免重复启动 DMA
static float s_pressure_mpa; // 当前有效压力值，单位 MPa
static float s_signal_voltage; // 当前有效信号电压值，单位 V
static float s_vdda; // 当前有效 VDDA 电源电压值，单位 V

/* 启动一轮 11 通道 ADC DMA 扫描。 */
static void PressureMeasure_StartConversion(void)
{
    s_adc_conversion_complete = false;
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_adc_dma_buffer, PRESSURE_ADC_SCAN_LENGTH) == HAL_OK)
    {
        s_adc_conversion_running = true;
    }
}

/* 将输入值限制在指定范围内，防止压力超出传感器量程。 */
static float Pressure_Clamp(float value, float minimum, float maximum)
{
    if (value < minimum)
    {
        return minimum;
    }

    if (value > maximum)
    {
        return maximum;
    }

    return value;
}

void PressureMeasure_Init(void)
{
    /* 清除旧数据，并执行 STM32 ADC 自校准。 */
    s_adc_conversion_complete = false;
    s_adc_conversion_running = false;
    s_pressure_mpa = 0.0f;
    s_signal_voltage = 0.0f;
    s_vdda = 0.0f;

    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }
}

bool PressureMeasure_Process(void)
{
    uint16_t signal_code;
    uint16_t vrefint_code;
    float vdda;
    bool updated = false;

    /* 没有正在进行的扫描时，先启动一轮。 */
    if (!s_adc_conversion_running)
    {
        PressureMeasure_StartConversion();
        return false;
    }

    /* DMA 尚未完成时不读取缓冲区，保留上一轮有效结果。 */
    if (!s_adc_conversion_complete)
    {
        return false;
    }

    s_adc_conversion_complete = false;
    s_adc_conversion_running = false;

    /* 去掉十路采样中的两个最大值和两个最小值，再求中间六路平均值。 */
    signal_code = Pressure_CalculateRobustCode(s_adc_dma_buffer, PRESSURE_ADC_CHANNEL_COUNT);
    vrefint_code = s_adc_dma_buffer[PRESSURE_ADC_CHANNEL_COUNT];
    if (vrefint_code != 0U)
    {
        /* 利用内部基准反推本次实际 VDDA，降低 3.3V 电源误差的影响。 */
        vdda = PRESSURE_VREFINT_CALIBRATION_VOLTAGE * PRESSURE_ADC_FULL_SCALE / (float)vrefint_code;
        if ((vdda >= PRESSURE_MIN_VDDA) && (vdda <= PRESSURE_MAX_VDDA))
        {
            s_vdda = vdda;
            s_signal_voltage = (float)signal_code * s_vdda / PRESSURE_ADC_FULL_SCALE;
            s_pressure_mpa = Pressure_CalculateMpa(signal_code, s_vdda);
            updated = true;
        }
    }

    PressureMeasure_StartConversion();
    return updated;
}

float PressureMeasure_GetMpa(void)
{
    return s_pressure_mpa;
}

float PressureMeasure_GetSignalVoltage(void)
{
    return s_signal_voltage;
}

float PressureMeasure_GetVdda(void)
{
    return s_vdda;
}

uint16_t Pressure_CalculateRobustCode(const uint16_t *samples, uint32_t count)
{
    uint16_t sorted[PRESSURE_ADC_CHANNEL_COUNT];
    uint32_t i;
    uint32_t j;
    uint32_t sum = 0U;
    uint16_t value;

    if ((samples == 0) || (count != PRESSURE_ADC_CHANNEL_COUNT))
    {
        return 0U;
    }

    /* 复制到局部数组，避免排序破坏 DMA 原始缓冲区。 */
    for (i = 0U; i < PRESSURE_ADC_CHANNEL_COUNT; ++i)
    {
        sorted[i] = samples[i];
    }

    /* 十个数值量很小，使用简单排序即可，且不引入额外库。 */
    for (i = 0U; i < PRESSURE_ADC_CHANNEL_COUNT - 1U; ++i)
    {
        for (j = 0U; j < PRESSURE_ADC_CHANNEL_COUNT - 1U - i; ++j)
        {
            if (sorted[j] > sorted[j + 1U])
            {
                value = sorted[j];
                sorted[j] = sorted[j + 1U];
                sorted[j + 1U] = value;
            }
        }
    }

    for (i = 2U; i < PRESSURE_ADC_CHANNEL_COUNT - 2U; ++i)
    {
        sum += sorted[i];
    }

    return (uint16_t)((sum + 3U) / 6U);
}

float Pressure_CalculateMpa(uint16_t adc_code, float vdda)
{
    float signal_voltage;
    float pressure_mpa;

    if (vdda <= 0.0f)
    {
        return 0.0f;
    }

    /* ADC码值 -> 电压 -> 压力：P=(V-0.6)/0.08。 */
    signal_voltage = (float)adc_code * vdda / PRESSURE_ADC_FULL_SCALE;
    pressure_mpa = (signal_voltage - PRESSURE_ZERO_CURRENT_VOLTAGE) / PRESSURE_VOLT_PER_MPA;
    return Pressure_Clamp(pressure_mpa, 0.0f, PRESSURE_FULL_SCALE_MPA);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    /* DMA 完成中断只置位，实际计算在主循环中完成。 */
    if (hadc->Instance == ADC1)
    {
        s_adc_conversion_complete = true;
    }
}
