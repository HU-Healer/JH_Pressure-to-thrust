# 液压压力与推力测量系统

## 1. 项目简介

本项目基于 `STM32F103C8T6`，使用 HAL 库完成以下功能：

- 读取 4-20mA 液压压力传感器。
- 将传感器电流转换为 0-30MPa 液压压力。
- 根据油缸缸径计算当前推力。
- 支持 `kg`、`吨`、`N` 三种推力单位。
- 使用 USART1 DMA 与淘晶驰串口屏通信。
- 使用 USART3 DMA 向 VOFA 发送调试数据。

当前屏幕变量名和屏幕事件命令暂时留空，后续统一在 `Task/screen_protocol.c` 顶部配置。

## 2. 工程目录

```text
JH_Pressure to thrust/
|-- Core/
|   |-- Inc/                 STM32 和 HAL 头文件
|   `-- Src/                 main、ADC、DMA、串口和中断文件
|-- Drivers/                 STM32F1 HAL 驱动
|-- Task/
|   |-- pressure_measure.c   ADC 采样和压力计算
|   |-- thrust_calculator.c   推力和单位换算
|   |-- screen_protocol.c     串口屏 DMA 协议
|   |-- app_control.c         应用层调度
|   `-- Vofa_send.c           VOFA 调试数据发送
|-- MDK-ARM/                 Keil 工程文件和编译输出
|-- docs/                    设计文档和实施计划
|-- tests/                   主机端纯计算测试源码
|-- 代码说明手册.md           详细代码说明
`-- README.md                本文件
```

## 3. 硬件配置

| 项目 | 配置 |
| --- | --- |
| MCU | STM32F103C8T6 |
| 外部晶振 | 8MHz，PLL 后系统时钟 72MHz |
| 压力传感器 | 24V 两线制，4-20mA 对应 0-30MPa |
| 采样电阻 | 150R，0.1% |
| 采样电压 | 4mA=0.6V，20mA=3.0V |
| 模拟缓冲 | OPA333 电压跟随器 |
| ADC | ADC1 IN0-IN9，加内部 VREFINT |
| 串口屏 | USART1，PA9 TX、PA10 RX、115200、8N1、DMA |
| 调试串口 | USART3，115200、DMA、VOFA JustFloat |

十个 ADC 输入连接的是同一个 OPA333 输出，不是十个独立传感器。程序会对十路 ADC 码值排序，去掉两个最大值和两个最小值，再对中间六个值求平均。

## 4. 程序入口

`Core/Src/main.c` 的主要流程：

```c
HAL_Init();
SystemClock_Config();
MX_GPIO_Init();
MX_DMA_Init();
MX_ADC1_Init();
MX_USART1_UART_Init();
MX_USART3_UART_Init();
AppControl_Init();

while (1)
{
    AppControl_Process();
}
```

主循环不需要手动调用 ADC、推力或屏幕函数，只需持续调用 `AppControl_Process()`。

## 5. 压力计算

ADC1 每次扫描 11 个通道：

```text
Rank 1-10：ADC1_IN0 至 ADC1_IN9
Rank 11：ADC1 VREFINT
```

程序使用 VREFINT 估算实际 VDDA：

```text
VDDA = 1.200 × 4095 / VREFINT_ADC码值
信号电压 = ADC平均码值 × VDDA / 4095
压力(MPa) = (信号电压 - 0.600) / 0.080
```

压力最终限制在 `0.0-30.0MPa`。

如需校准 VREFINT，修改：

```c
Task/pressure_measure.c

#define PRESSURE_VREFINT_CALIBRATION_VOLTAGE (1.200f)
```

## 6. 推力计算

```text
缸径(m) = cylinder_d_mm / 1000
面积 = PI × 缸径(m)² / 4
推力(N) = 压力(MPa) × 1,000,000 × 面积
推力(kg) = 推力(N) / 9.80665
推力(吨) = 推力(kg) / 1000
```

默认缸径和允许范围位于 `Task/app_control.c`：

```c
#define APP_DEFAULT_CYLINDER_DIAMETER_MM  (100.0f)
#define APP_MIN_CYLINDER_DIAMETER_MM      (1.0f)
#define APP_MAX_CYLINDER_DIAMETER_MM      (1000.0f)
```

单位切换顺序：

```text
kg -> 吨 -> N -> kg
```

## 7. 屏幕协议配置

打开：

```text
Task/screen_protocol.c
```

在文件顶部找到：

```c
#define SCREEN_TX_MEASUREMENTS_FORMAT    ""
#define SCREEN_RX_DIAMETER_PREFIX        ""
#define SCREEN_RX_UNIT_COMMAND           ""
```

### 7.1 发送压力、推力和单位

`SCREEN_TX_MEASUREMENTS_FORMAT` 的三个格式参数顺序固定为：

1. 压力值 `float`。
2. 当前单位下的推力值 `float`。
3. 单位文本 `kg`、`吨` 或 `N`。

ASCII 协议示例：

```c
#define SCREEN_TX_MEASUREMENTS_FORMAT \
    "pressure=%.1f;Thrust=%.1f;tuili=%s\\r\\n"
```

实际内容必须按照淘晶驰屏幕的串口协议填写，包括变量赋值格式、引号和帧尾。

### 7.2 接收缸径

如果屏幕发送：

```text
cylinder_d_mm=80.0
```

填写：

```c
#define SCREEN_RX_DIAMETER_PREFIX "cylinder_d_mm="
```

程序会解析后面的浮点数，并检查是否在 `1.0-1000.0mm` 范围内。

### 7.3 接收单位按钮

如果屏幕按钮发送：

```text
unit_button
```

填写：

```c
#define SCREEN_RX_UNIT_COMMAND "unit_button"
```

收到完整命令后，STM32 会切换单位并刷新推力和单位文本。

当前三个配置为空时，程序仍会正常读取压力和计算推力，但不会向屏幕发送数据，也不会处理屏幕命令。

## 8. DMA 说明

### ADC1 DMA

- DMA 通道：DMA1 Channel1。
- 方向：外设到内存。
- 数据宽度：Half Word。
- 模式：Normal。
- 每次扫描 11 个 ADC 数据。
- DMA 完成中断只设置完成标志，计算在主循环中执行。

### USART1 DMA

- RX：DMA1 Channel5。
- TX：DMA1 Channel4。
- 接收方式：`HAL_UARTEx_ReceiveToIdle_DMA()`。
- 发送方式：`HAL_UART_Transmit_DMA()`。

### USART3 DMA

保留原有 VOFA 发送模块：

- RX：DMA1 Channel3。
- TX：DMA1 Channel2。
- 使用 JustFloat 帧格式。

## 9. 编译方法

使用 Keil 打开：

```text
MDK-ARM/JH_Pressure to thrust.uvprojx
```

然后执行：

```text
Rebuild
```

工程中已经加入以下 Task 源文件：

```text
pressure_measure.c
thrust_calculator.c
screen_protocol.c
app_control.c
```

如果 Keil 工程树中看不到这些文件，请右键 `Task`，选择 `Add Existing Files to Group 'Task'`，手动添加上述四个 `.c` 文件。

## 10. 上板调试顺序

1. 不连接压力传感器，确认 3.3V、VDDA 和 GND 正常。
2. 确认 ADC 输入没有超过 3.3V。
3. 使用已知电流检查 4mA、12mA、20mA。
4. 通过 VOFA 观察 ADC 码值、信号电压、VDDA 和压力。
5. 确认压力约为 0MPa、15MPa、30MPa。
6. 填写屏幕协议配置后，再测试缸径设置和单位按钮。

## 11. 注意事项

- `Core/Src/adc.c` 已手动配置 ADC1 的 11 通道扫描和 DMA。重新用 CubeMX 生成代码时，需要重新确认这些配置。
- VREFINT 的 `1.200V` 是标称值，绝对精度要求较高时应进行实测校准。
- 十个 ADC 引脚共用一个模拟信号，不能判断具体哪一个 MCU 引脚损坏。
- 屏幕协议未确认前不要填写猜测的变量格式。
- 传感器精度、采样电阻误差、OPA333 误差、ADC 参考误差和温漂都会影响最终压力精度。

## 12. 详细说明

更完整的函数说明、变量传递关系和串口协议说明见：

```text
代码说明手册.md
```
