# screen_protocol 模块说明

## 模块职责

`Task/screen_protocol.c` 是 STM32 与淘晶驰串口屏之间的通信层。它负责：

- 使用 USART1 DMA 接收屏幕发送的命令。
- 使用串口空闲 IDLE 判定一帧数据接收结束。
- 解析“设置缸径”和“切换单位”两类事件。
- 使用 USART1 DMA 发送压力、推力和单位文本。
- 在屏幕协议尚未填写时安全停用收发业务，不影响 ADC 测量与推力计算。

它不计算压力和推力，只传递数据与事件。

## 硬件与外设

| 项目 | 配置 |
| --- | --- |
| 串口 | USART1 |
| TX | PA9，STM32 发送到屏幕 RX |
| RX | PA10，屏幕 TX 发送到 STM32 |
| 波特率 | 115200 |
| 数据格式 | 8N1 |
| TX DMA | DMA1 Channel4 |
| RX DMA | DMA1 Channel5 |

串口按字节收发，一个字节是 8 位，所以收发缓冲区使用：

```c
static uint8_t s_rx_buffer[SCREEN_RX_BUFFER_SIZE];
static uint8_t s_tx_buffer[SCREEN_TX_BUFFER_SIZE];
```

`uint8_t` 表示 8 位无符号整数，范围 0-255，刚好对应一个串口字节。`static` 表示缓冲区只允许 `screen_protocol.c` 直接访问，并且在程序运行期间一直存在，DMA 才能安全读写它们。

## 模块关系

```text
app_control.c
  -> ScreenProtocol_Init()
  -> ScreenProtocol_PollEvent()
  -> ScreenProtocol_SendMeasurements()

screen_protocol.c
  -> USART1 DMA
  -> 产生缸径设置事件 / 单位切换事件
```

数据流：

```text
屏幕设置缸径
  -> USART1 RX DMA
  -> ScreenProtocol_HandleFrame()
  -> SCREEN_EVENT_SET_CYLINDER_DIAMETER
  -> AppControl_SetCylinderDiameter()

屏幕单位按钮
  -> USART1 RX DMA
  -> SCREEN_EVENT_CYCLE_UNIT
  -> app_control.c 切换 kg / 吨 / N

压力 + 推力 + 单位
  -> ScreenProtocol_SendMeasurements()
  -> USART1 TX DMA
  -> 显示屏 pressure / Thrust / tuili
```

## 用户协议配置区

打开 `Task/screen_protocol.c` 顶部，找到：

```c
#define SCREEN_TX_MEASUREMENTS_FORMAT    ""
#define SCREEN_RX_DIAMETER_PREFIX        ""
#define SCREEN_RX_UNIT_COMMAND           ""
```

这三项是唯一需要根据屏幕工程修改的位置。

这三行同样是 `#define` 宏。它们在编译前直接替换为字符串，例如空字符串 `""` 表示“尚未配置”。宏适合做这种固定协议配置，因为运行中不会改变；如果以后要让用户在运行中修改协议，则需要变量或 Flash 参数，而不是宏。

### `SCREEN_TX_MEASUREMENTS_FORMAT`

用于 STM32 向屏幕发送数据。格式字符串按固定顺序接收三个参数：

```text
第 1 个参数：pressure_mpa，float
第 2 个参数：thrust_value，float
第 3 个参数：单位文本，const char *
```

`const char *` 表示“指向只读字符串的指针”。这里它接收 `kg`、`吨` 或 `N` 这样的单位文本。`const` 的意思是发送函数只读取字符串，不修改它。

示例：屏幕采用 ASCII 文本协议时：

```c
#define SCREEN_TX_MEASUREMENTS_FORMAT \
    "pressure=%.1f;Thrust=%.1f;tuili=%s\\r\\n"
```

实际格式必须以淘晶驰屏幕文档或串口抓包结果为准。若屏幕使用二进制帧、固定帧尾或变量地址写入方式，需要将该配置和发送函数按照实际协议调整。

### `SCREEN_RX_DIAMETER_PREFIX`

用于匹配屏幕下发的缸径命令前缀。

若屏幕发送：

```text
cylinder_d_mm=80.0
```

填写：

```c
#define SCREEN_RX_DIAMETER_PREFIX "cylinder_d_mm="
```

模块会解析前缀之后的十进制整数或小数，并生成：

```c
SCREEN_EVENT_SET_CYLINDER_DIAMETER
```

缸径范围检查不在本模块进行，而是在 `app_control.c` 的 `AppControl_SetCylinderDiameter()` 中进行。

### `SCREEN_RX_UNIT_COMMAND`

用于匹配单位按钮的固定命令。

若单位按钮发送：

```text
unit_button
```

填写：

```c
#define SCREEN_RX_UNIT_COMMAND "unit_button"
```

模块收到完全相同的一帧数据时，生成：

```c
SCREEN_EVENT_CYCLE_UNIT
```

## 接收流程

1. `ScreenProtocol_Init()` 调用 `HAL_UARTEx_ReceiveToIdle_DMA()`，启动 RX DMA。
2. 屏幕数据停止发送后，USART1 IDLE 触发 `HAL_UARTEx_RxEventCallback()`。
3. 回调调用 `ScreenProtocol_HandleFrame()` 识别命令。
4. 命令被转换为 `ScreenEvent`，并设置待处理标志。
5. 回调立即重新开启 DMA 接收。
6. 主循环中的 `app_control.c` 通过 `ScreenProtocol_PollEvent()` 取走事件。

接收回调中不直接修改缸径和单位状态。这样可避免在中断里做应用业务，职责边界更清楚。

## 发送流程

1. `app_control.c` 获得新的压力并计算出当前推力。
2. 调用 `ScreenProtocol_SendMeasurements(pressure, thrust, unit)`。
3. 模块检查协议模板是否为空。
4. 使用 `snprintf()` 把压力、推力和单位写入发送缓冲区。
5. 调用 `HAL_UART_Transmit_DMA()` 开始发送。
6. `HAL_UART_TxCpltCallback()` 在发送结束后清除忙标志。

发送未完成时再次调用，会返回 `SCREEN_PROTOCOL_BUSY`，不会覆盖正在发送的缓冲区。

## 公开接口

### `ScreenProtocol_Init()`

初始化状态并启动 USART1 接收 DMA。由 `AppControl_Init()` 调用一次。

### `ScreenProtocol_IsTransmitConfigured()`

检查发送格式是否已填写。若 `SCREEN_TX_MEASUREMENTS_FORMAT` 为空，返回 `false`。

### `ScreenProtocol_SendMeasurements()`

发送压力、推力和单位文本。

返回值：

| 返回值 | 含义 |
| --- | --- |
| `SCREEN_PROTOCOL_OK` | DMA 发送已成功启动。 |
| `SCREEN_PROTOCOL_BUSY` | 上一帧尚未发送完成。 |
| `SCREEN_PROTOCOL_NOT_CONFIGURED` | 用户尚未填写发送协议模板。 |
| `SCREEN_PROTOCOL_ERROR` | 格式化失败、缓冲区不足或 HAL 启动 DMA 失败。 |

### `ScreenProtocol_PollEvent()`

接口的参数是：

```c
bool ScreenProtocol_PollEvent(ScreenEvent *event);
```

`ScreenEvent` 是结构体，把一次屏幕事件的“类型”和“附带数值”放在一起：

```c
typedef struct
{
    ScreenEventType type;
    float value;
} ScreenEvent;
```

调用时写 `ScreenProtocol_PollEvent(&event)`，`&event` 表示把变量的地址交给函数，函数通过该地址把事件内容写回来。返回的 `bool` 只表示“有没有事件”，结构体负责携带事件详情。

从协议模块取出一个事件。返回 `true` 表示已得到事件，返回 `false` 表示当前没有新事件。

## 当前安全行为

三个协议宏保持空字符串时：

- ADC 仍持续测量压力。
- 推力仍持续计算。
- USART1 接收 DMA 仍会运行。
- 收到的内容不会被识别为有效缸径或单位命令。
- 不向屏幕发送未知格式的数据。

这避免在协议未确认前对屏幕产生误写操作。

## 注意事项

- 当前解析器只支持十进制数字、可选负号和小数点，不支持科学计数法。
- 缸径命令应尽量使用 ASCII 文本，便于排查和抓包。
- 若屏幕协议具有固定帧尾，例如 `0xFF 0xFF 0xFF`，IDLE 只能辅助分帧；实际项目中应同时检查帧尾。
- 若屏幕连续快速发送多帧，当前模块只保留一个待处理事件；需要队列时可扩展为环形缓冲区。
- 屏幕变量名 `pressure`、`Thrust`、`cylinder_d_mm`、`tuili` 尚未写死，必须按最终屏幕工程的真实协议填写。

接收状态中还有：

```c
static volatile bool s_tx_busy;
static volatile bool s_event_pending;
```

`static` 让它们仅本文件可见；`volatile` 是因为 DMA/串口中断会修改这些标志，而主循环也会读取它们。它要求编译器不要把旧值长期缓存起来。

## CubeMX 自动生成与手写代码边界

### CubeMX 自动生成的部分

| 文件 | CubeMX 自动生成内容 |
| --- | --- |
| `Core/Src/usart.c` | `huart1` 句柄、`MX_USART1_UART_Init()`、PA9/PA10 GPIO、USART1 的 DMA1 Channel4/Channel5 基础配置。 |
| `Core/Inc/usart.h` | `huart1` 声明和串口初始化函数声明。 |
| `Core/Src/dma.c` | DMA 控制器时钟与 DMA 通道中断框架。 |
| `Core/Src/stm32f1xx_it.c` | `USART1_IRQHandler()`、DMA1 Channel4/Channel5 中断函数框架。 |

CubeMX 中应配置：

```text
USART1：Asynchronous，115200，8N1
PA9：USART1_TX
PA10：USART1_RX
USART1_TX DMA：DMA1 Channel4，Memory to Peripheral
USART1_RX DMA：DMA1 Channel5，Peripheral to Memory
USART1 IRQ：Enable
```

### 本项目手写的部分

| 文件或代码 | 手写内容 |
| --- | --- |
| `Task/screen_protocol.c/.h` | 整个协议模块，均为手写。 |
| `ScreenProtocol_Init()` | 在 CubeMX 初始化串口后，启动 `HAL_UARTEx_ReceiveToIdle_DMA()`。 |
| `HAL_UARTEx_RxEventCallback()` | 接收至空闲事件回调，解析屏幕帧并重启 RX DMA。 |
| `HAL_UART_TxCpltCallback()` | DMA 发送完成后清除忙标志。 |
| 三个 `SCREEN_*` 配置宏 | 由你根据屏幕工程填写，不是 CubeMX 配置。 |

CubeMX 负责“USART1 和 DMA 通道能工作”；`screen_protocol.c` 负责“字节代表什么、何时算一帧、收到命令后做什么”。两者是上下层关系，不能互相替代。

### 重新生成代码时的注意事项

- 不要删除 `Task/screen_protocol.c/.h`。
- 确认 Keil 工程的 `Task` 分组仍包含 `screen_protocol.c`。
- CubeMX 重生 `usart.c` 后，确认 USART1 仍保留 TX/RX DMA 和 USART1 中断。
- `HAL_UARTEx_RxEventCallback()` 位于手写模块中，不要在 CubeMX 生成的文件里重复定义同名函数，否则会发生链接重复定义错误。

## 附录：C 语言集中复习

本附录与前文的就近说明内容重复，用于集中复习；第一次阅读请优先按前文的代码顺序理解。

### 两个枚举分别表示什么

`screen_protocol.h` 有两个 `typedef enum`：

```c
typedef enum
{
    SCREEN_PROTOCOL_OK = 0,
    SCREEN_PROTOCOL_BUSY,
    SCREEN_PROTOCOL_NOT_CONFIGURED,
    SCREEN_PROTOCOL_ERROR
} ScreenProtocolStatus;
```

这是函数执行结果。调用者可以区分“发送已启动”“正在忙”“还没有填写协议”“发生错误”。

```c
typedef enum
{
    SCREEN_EVENT_NONE = 0,
    SCREEN_EVENT_SET_CYLINDER_DIAMETER,
    SCREEN_EVENT_CYCLE_UNIT
} ScreenEventType;
```

这是屏幕传给 MCU 的事件类型。它描述“发生了什么”，而不是函数是否成功。

这两个枚举名字相似但职责不同：`Status` 是函数返回状态，`EventType` 是用户操作事件。

### 什么是结构体 `struct`

```c
typedef struct
{
    ScreenEventType type;
    float value;
} ScreenEvent;
```

结构体把多个有关的数据打包成一个整体。这里一次屏幕事件包含两件事：

- `type`：事件种类，例如“设置缸径”。
- `value`：事件附带数值，例如 `80.0mm`。

使用方式：

```c
ScreenEvent event;

if (ScreenProtocol_PollEvent(&event))
{
    if (event.type == SCREEN_EVENT_SET_CYLINDER_DIAMETER)
    {
        // event.value 是屏幕发来的缸径
    }
}
```

点号 `.` 用来访问结构体成员，例如 `event.type`。

### `&event` 和 `ScreenEvent *event` 是什么

函数声明：

```c
bool ScreenProtocol_PollEvent(ScreenEvent *event);
```

`ScreenEvent *event` 表示函数需要一个“结构体地址”。调用时写：

```c
ScreenEvent event;
ScreenProtocol_PollEvent(&event);
```

`&event` 表示“取得变量 event 的地址”。函数内部通过这个地址把事件数据写回给调用者。这样一个函数可以同时返回：

- `bool`：有没有事件。
- `event`：事件的具体内容。

### `uint8_t` 缓冲区为什么用来收发串口

```c
static uint8_t s_rx_buffer[SCREEN_RX_BUFFER_SIZE];
static uint8_t s_tx_buffer[SCREEN_TX_BUFFER_SIZE];
```

串口按“字节”收发数据，一个字节就是 8 位，因此使用 `uint8_t` 最合适。它能保存 `0-255`，与串口原始数据一一对应。

`s_rx_buffer` 是接收缓冲区，DMA 把屏幕发送的每一个字节写到这里；`s_tx_buffer` 是发送缓冲区，程序先拼好要发的数据，再由 DMA 逐字节发送。

`SCREEN_RX_BUFFER_SIZE` 和 `SCREEN_TX_BUFFER_SIZE` 是宏常量，因为数组长度必须在编译时确定。

### `static` 和 `volatile` 在本模块中的意义

```c
static volatile bool s_tx_busy;
static volatile bool s_event_pending;
```

- `static`：仅本 `.c` 文件可以直接访问，其他模块必须调用公开函数。
- `volatile`：该变量会在中断回调和主循环之间共享，编译器不能假设它的值不会突然改变。

`s_tx_busy` 防止 DMA 还在发送时覆盖 `s_tx_buffer`；`s_event_pending` 表示有屏幕事件等待主循环处理。

### `const char *format` 和字符串格式化

```c
const char *format;
format = SCREEN_TX_MEASUREMENTS_FORMAT;
```

`format` 指向一段不可修改的格式字符串。之后：

```c
snprintf((char *)s_tx_buffer, sizeof(s_tx_buffer), format,
         pressure_mpa, thrust_value, Thrust_GetUnitText(unit));
```

`snprintf` 根据格式字符串，把压力、推力和单位文本组合成将要发送的字节。`sizeof(s_tx_buffer)` 告诉它缓冲区容量，防止写超过 128 字节。

`(char *)s_tx_buffer` 是类型转换：缓冲区原本是字节数组 `uint8_t[]`，而 `snprintf` 需要字符数组 `char *`。数据本身没有变化，只是告诉编译器“把这些字节按字符串处理”。

### `NULL`、`0` 和空字符串

源码中可见：

```c
if ((text == 0) || (text[0] == '\0'))
```

- `text == 0`：指针没有指向有效地址。初学阶段也可写成 `text == NULL`，含义更直观。
- `text[0] == '\0'`：字符串第一个字符是结束符，表示空字符串 `""`。

前者防止访问非法地址，后者判断用户有没有填写协议宏。
