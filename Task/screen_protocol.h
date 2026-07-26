#ifndef SCREEN_PROTOCOL_H
#define SCREEN_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#include "thrust_calculator.h"

/* 屏幕发送结果，便于上层区分忙、未配置和格式错误。 */
typedef enum
{
    SCREEN_PROTOCOL_OK = 0,
    SCREEN_PROTOCOL_BUSY,
    SCREEN_PROTOCOL_NOT_CONFIGURED,
    SCREEN_PROTOCOL_ERROR
} ScreenProtocolStatus;

/* 屏幕下发给 MCU 的事件类型。 */
typedef enum
{
    SCREEN_EVENT_NONE = 0,
    SCREEN_EVENT_SET_CYLINDER_DIAMETER,
    SCREEN_EVENT_CYCLE_UNIT
} ScreenEventType;

typedef struct
{
    ScreenEventType type;
    float value;
} ScreenEvent;

/* 启动 USART1 接收至空闲 DMA。 */
void ScreenProtocol_Init(void);
bool ScreenProtocol_IsTransmitConfigured(void);
/* 按配置格式发送压力、当前推力和单位文本。 */
ScreenProtocolStatus ScreenProtocol_SendMeasurements(float pressure_mpa, float thrust_value, ThrustUnit unit);
/* 从协议层取出一个待处理事件。 */
bool ScreenProtocol_PollEvent(ScreenEvent *event);

#endif
