/*
 * 文件功能：淘晶驰屏幕 USART1 DMA 协议层。
 * 屏幕实际变量名和事件命令暂不确定，统一在下方用户配置区填写。
 */
#include "screen_protocol.h"

#include <stdio.h>
#include <string.h>

#include "usart.h"

/* ==================== 用户后续填写位置：显示屏通信协议 ====================
 * 下面四项必须按显示屏软件中“串口发送/接收”的实际格式填写。
 * 为空字符串时，程序仍会测量和计算，但不会发送或响应屏幕命令。
 *
 * 示例仅说明格式，不能直接使用：
 * #define SCREEN_TX_MEASUREMENTS_FORMAT "pressure=%.1f;Thrust=%.1f;tuili=%s\\r\\n"
 * #define SCREEN_RX_DIAMETER_PREFIX "cylinder_d_mm="
 * #define SCREEN_RX_UNIT_COMMAND "unit_button"
 *
 * 若你的屏幕要求 0xFF 0xFF 0xFF 结尾，可将这些字节直接写入格式字符串末尾。
 */
#define SCREEN_TX_MEASUREMENTS_FORMAT    ""  /* 用户后续填写位置：屏幕变量发送格式 */
#define SCREEN_RX_DIAMETER_PREFIX        ""  /* 用户后续填写位置：缸径命令前缀 */
#define SCREEN_RX_UNIT_COMMAND           ""  /* 用户后续填写位置：单位切换命令 */
/* ======================================================================== */

/* 接收和发送缓冲区大小，按当前协议帧长度预留。 */
#define SCREEN_RX_BUFFER_SIZE             (64U)
#define SCREEN_TX_BUFFER_SIZE             (128U)

static uint8_t s_rx_buffer[SCREEN_RX_BUFFER_SIZE];
static uint8_t s_tx_buffer[SCREEN_TX_BUFFER_SIZE];
static volatile bool s_tx_busy;
static volatile bool s_event_pending;
static ScreenEvent s_pending_event;

/* 判断某一项协议配置是否为空。 */
static bool ScreenProtocol_IsEmpty(const char *text)
{
    return (text == 0) || (text[0] == '\0');
}

/* 开启一次 USART1 接收，使用空闲线判断一帧结束。 */
static void ScreenProtocol_StartReceive(void)
{
    if (HAL_UARTEx_ReceiveToIdle_DMA(&huart1, s_rx_buffer, sizeof(s_rx_buffer)) == HAL_OK)
    {
        __HAL_DMA_DISABLE_IT(huart1.hdmarx, DMA_IT_HT);
    }
}

/* 解析屏幕命令中十进制格式的缸径数值。 */
static bool ScreenProtocol_ParseFloat(const uint8_t *text, uint16_t length, float *value)
{
    uint16_t index = 0U;
    bool negative = false;
    bool has_digit = false;
    float result = 0.0f;
    float factor = 0.1f;

    if ((text == 0) || (value == 0) || (length == 0U))
    {
        return false;
    }

    if (text[index] == '-')
    {
        negative = true;
        ++index;
    }

    while ((index < length) && (text[index] >= '0') && (text[index] <= '9'))
    {
        result = result * 10.0f + (float)(text[index] - '0');
        has_digit = true;
        ++index;
    }

    if ((index < length) && (text[index] == '.'))
    {
        ++index;
        while ((index < length) && (text[index] >= '0') && (text[index] <= '9'))
        {
            result += (float)(text[index] - '0') * factor;
            factor *= 0.1f;
            has_digit = true;
            ++index;
        }
    }

    if (!has_digit)
    {
        return false;
    }

    *value = negative ? -result : result;
    return true;
}

/* 识别缸径设置和单位按钮命令，并转成应用层事件。 */
static void ScreenProtocol_HandleFrame(const uint8_t *frame, uint16_t length)
{
    uint16_t prefix_length;
    float diameter;

    if ((frame == 0) || (length == 0U))
    {
        return;
    }

    if (!ScreenProtocol_IsEmpty(SCREEN_RX_DIAMETER_PREFIX))
    {
        prefix_length = (uint16_t)strlen(SCREEN_RX_DIAMETER_PREFIX);
        if ((length > prefix_length) && (memcmp(frame, SCREEN_RX_DIAMETER_PREFIX, prefix_length) == 0) &&
            ScreenProtocol_ParseFloat(&frame[prefix_length], (uint16_t)(length - prefix_length), &diameter))
        {
            s_pending_event.type = SCREEN_EVENT_SET_CYLINDER_DIAMETER;
            s_pending_event.value = diameter;
            s_event_pending = true;
            return;
        }
    }

    if (!ScreenProtocol_IsEmpty(SCREEN_RX_UNIT_COMMAND) &&
        (length == strlen(SCREEN_RX_UNIT_COMMAND)) &&
        (memcmp(frame, SCREEN_RX_UNIT_COMMAND, length) == 0))
    {
        s_pending_event.type = SCREEN_EVENT_CYCLE_UNIT;
        s_pending_event.value = 0.0f;
        s_event_pending = true;
    }
}

void ScreenProtocol_Init(void)
{
    /* 清除协议状态后启动接收 DMA。 */
    s_tx_busy = false;
    s_event_pending = false;
    s_pending_event.type = SCREEN_EVENT_NONE;
    s_pending_event.value = 0.0f;
    ScreenProtocol_StartReceive();
}

bool ScreenProtocol_IsTransmitConfigured(void)
{
    /* 空模板表示用户尚未填写屏幕变量发送协议。 */
    return !ScreenProtocol_IsEmpty(SCREEN_TX_MEASUREMENTS_FORMAT);
}

ScreenProtocolStatus ScreenProtocol_SendMeasurements(float pressure_mpa, float thrust_value, ThrustUnit unit)
{
    const char *format;
    int length;

    /* 协议未填写时安全返回，不发送未知格式的数据。 */
    if (!ScreenProtocol_IsTransmitConfigured())
    {
        return SCREEN_PROTOCOL_NOT_CONFIGURED;
    }

    if (s_tx_busy)
    {
        return SCREEN_PROTOCOL_BUSY;
    }

    /* 格式参数顺序：压力、推力、单位文本。 */
    format = SCREEN_TX_MEASUREMENTS_FORMAT;
    length = snprintf((char *)s_tx_buffer, sizeof(s_tx_buffer), format,
                      pressure_mpa, thrust_value, Thrust_GetUnitText(unit));
    if ((length <= 0) || ((uint32_t)length >= sizeof(s_tx_buffer)))
    {
        return SCREEN_PROTOCOL_ERROR;
    }

    s_tx_busy = true;
    if (HAL_UART_Transmit_DMA(&huart1, s_tx_buffer, (uint16_t)length) != HAL_OK)
    {
        s_tx_busy = false;
        return SCREEN_PROTOCOL_ERROR;
    }

    return SCREEN_PROTOCOL_OK;
}

bool ScreenProtocol_PollEvent(ScreenEvent *event)
{
    /* 主循环取走事件后，清除待处理标志。 */
    if ((!s_event_pending) || (event == 0))
    {
        return false;
    }

    *event = s_pending_event;
    s_event_pending = false;
    s_pending_event.type = SCREEN_EVENT_NONE;
    return true;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    /* UART 空闲中断回调中解析当前帧，并立即重启 DMA 接收。 */
    if (huart->Instance == USART1)
    {
        ScreenProtocol_HandleFrame(s_rx_buffer, size);
        ScreenProtocol_StartReceive();
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    /* DMA 发送完成后允许下一帧发送。 */
    if (huart->Instance == USART1)
    {
        s_tx_busy = false;
    }
}
