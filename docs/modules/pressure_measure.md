# pressure_measure 模块说明

## 模块职责

`Task/pressure_measure.c` 负责把 ADC1 读取到的模拟电压转换为液压压力。它不处理油缸缸径、推力单位或串口屏协议，只输出最近一次有效的压力、信号电压和 VDDA。

传感器链路如下：

```text
4-20mA 压力传感器
  -> 150R 精密采样电阻
  -> 0.6-3.0V
  -> OPA333 电压跟随器
  -> 10k 串联保护电阻
  -> ADC1 IN0-IN9
```

ADC1 还读取内部 `VREFINT`，用于估算实际 `VDDA`。

## 依赖关系

```text
adc.c / adc.h
  -> 配置 ADC1 的 11 通道扫描和 DMA1 Channel1
  -> pressure_measure.c
  -> app_control.c
  -> thrust_calculator.c
  -> screen_protocol.c
```

- `adc.c` 提供全局 ADC 句柄 `hadc1` 和 DMA 句柄 `hdma_adc1`。
- 本模块通过 HAL 函数启动 ADC DMA。
- `app_control.c` 定期调用本模块，获得新的压力值后再计算推力。

## ADC 扫描顺序

| DMA 缓冲区下标 | ADC 通道 | 内容 |
| --- | --- | --- |
| `0-9` | `IN0-IN9` | 同一个 OPA333 输出的十路 ADC 采样值 |
| `10` | `VREFINT` | STM32 内部参考电压采样值 |

宏定义：

```c
#define PRESSURE_ADC_CHANNEL_COUNT (10U)
#define PRESSURE_ADC_SCAN_LENGTH    (11U)
```

这两行要在这里直接理解：`#define` 是预处理宏定义。编译前会进行文本替换，例如：

```c
uint16_t sorted[PRESSURE_ADC_CHANNEL_COUNT];
```

可以理解为：

```c
uint16_t sorted[(10U)];
```

宏不是变量：不占 RAM、没有运行时地址、运行中不能修改，适合固定数量和固定配置。

`10U` 表示“无符号整数常量 10”，`U` 是 `unsigned` 的缩写。它不是 `uint8_t` 或 `uint16_t`。`uint16_t` 是变量类型，决定变量可以存放 16 位无符号整数；`10U` 只是一个固定数值。

```c
#define PRESSURE_ADC_CHANNEL_COUNT (10U) // 固定数值常量
uint16_t signal_code;                     // 可变化的 16 位无符号变量
```

这里写 `10U` 是因为循环计数和数组大小都是非负数，能减少有符号/无符号比较的编译器警告。写 `10` 通常也能工作，但 `10U` 是嵌入式代码中的常见写法。

这两个宏放在 `pressure_measure.h`，是因为它们定义了模块约定：`Pressure_CalculateRobustCode()` 要接收 10 个样本，调用者或测试代码也需要知道该规则。只在一个 `.c` 内部使用、外部不需要知道的常量，通常更适合放在该 `.c` 内。

十个 ADC 引脚并不是十个独立压力输入。它们是同一模拟信号的多路 ADC 读取，用于降低偶发异常值的影响。

## 采样与滤波流程

`PressureMeasure_Process()` 在主循环中被周期调用。

1. 当没有 ADC 扫描任务运行时，调用 `HAL_ADC_Start_DMA()` 启动一次 11 通道扫描。
2. DMA 传输完成后，`HAL_ADC_ConvCpltCallback()` 将完成标志置位。
3. 下次进入 `PressureMeasure_Process()` 时，读取 DMA 缓冲区。
4. 将十个外部 ADC 码值排序。
5. 丢弃最小两个值和最大两个值。
6. 对中间六个值求平均，得到 `signal_code`。
7. 读取第 11 个数据 `vrefint_code`，估算实际 VDDA。
8. 将平均码值换算为信号电压和 MPa 压力。
9. 启动下一次 ADC DMA 扫描。

这样做的特点是：DMA 回调中不做浮点计算，回调只设置完成标志；排序、平均和压力计算都在主循环完成，降低中断占用时间。

## 压力计算公式

### 1. VDDA 计算

```text
VDDA = VREFINT_标称电压 × 4095 / VREFINT_ADC码值
```

当前代码使用：

```c
#define PRESSURE_VREFINT_CALIBRATION_VOLTAGE (1.200f)
```

这是 STM32F103 内部参考电压的标称值。若要提高绝对精度，应通过实测校准该常量，或增加零点和满量程的软件校准系数。

### 2. ADC 码值转信号电压

```text
信号电压(V) = ADC平均码值 × VDDA / 4095
```

### 3. 信号电压转压力

4mA 对应 0MPa，150R 上为 0.6V；20mA 对应 30MPa，150R 上为 3.0V。因此：

```text
每 1MPa 对应电压变化 = (3.0 - 0.6) / 30 = 0.08V
压力(MPa) = (信号电压 - 0.6) / 0.08
```

结果最终限制在 `0.0-30.0MPa`。

## 公开接口

### `PressureMeasure_Init()`

用途：初始化测量状态并调用 `HAL_ADCEx_Calibration_Start(&hadc1)` 对 ADC1 校准。

调用时机：所有 CubeMX 外设初始化完成后调用一次。当前由 `AppControl_Init()` 调用，不需要在 `main.c` 中重复调用。

### `PressureMeasure_Process()`

用途：启动 ADC 扫描或处理已完成的扫描。

返回值：

- `true`：本次已得到新的有效压力。
- `false`：扫描刚启动、DMA 尚未完成，或 VDDA 数据异常。

调用方式：主循环中持续调用，当前应用层以 100ms 为周期调用。

```c
if (PressureMeasure_Process())
{
    float pressure_mpa = PressureMeasure_GetMpa();
}
```

### `PressureMeasure_GetMpa()`

返回最近一次有效压力，单位 MPa。

### `PressureMeasure_GetSignalVoltage()`

返回最近一次有效传感器电压，正常工作范围应接近 `0.6-3.0V`。

### `PressureMeasure_GetVdda()`

返回通过 VREFINT 推算的 VDDA。该值适合用于观察电源稳定性。

### `Pressure_CalculateRobustCode()`

函数声明中的 `const uint16_t *samples` 可以拆开理解：`uint16_t` 是数组中每个 ADC 码值的类型；`*samples` 是指向第一个元素的地址；`const` 表示函数承诺不修改调用者的原始数组。

```c
uint16_t values[10];
Pressure_CalculateRobustCode(values, 10U);
```

数组名 `values` 传入函数时会自动传递第一个元素的地址。

纯计算函数。输入十个 ADC 码值，输出去掉四个极值后的六路平均 ADC 码值。主要用于模块内部，也可用于主机端测试。

### `Pressure_CalculateMpa()`

纯计算函数。输入 ADC 码值与 VDDA，输出限制到 0-30MPa 的压力值。

## 关键状态变量

| 变量 | 含义 |
| --- | --- |
| `s_adc_dma_buffer` | 保存 10 路外部 ADC 数据和 1 路 VREFINT 数据。 |
| `s_adc_conversion_complete` | DMA 完成中断置位，主循环读取后清零。 |
| `s_adc_conversion_running` | 标记当前是否已经启动一轮 ADC DMA 扫描。 |
| `s_pressure_mpa` | 最近一次有效液压压力。 |
| `s_signal_voltage` | 最近一次有效传感器电压。 |
| `s_vdda` | 最近一次通过 VREFINT 估算的 VDDA。 |

表中的变量定义会出现：

```c
static float s_pressure_mpa;
static volatile bool s_adc_conversion_complete;
```

文件最外层的 `static` 表示“仅本 `.c` 文件能直接访问”。因此其他模块必须调用 `PressureMeasure_GetMpa()`，不能随意修改 `s_pressure_mpa`。`volatile` 用于中断和主循环共享的变量：DMA 中断会写完成标志，主循环会读它；它要求编译器每次读取内存中的最新值。

## 使用注意事项

- ADC1 必须配置为 `IN0-IN9 + VREFINT` 共 11 路扫描。
- 所有 ADC 通道应使用较长采样时间，当前建议 `239.5 cycles`。
- DMA 必须使用 `DMA1 Channel1`、外设到内存、半字宽、Normal 模式。
- VDDA 在 `2.70-3.60V` 外时，模块会丢弃本轮数据并保持上一轮有效结果。
- 传感器断线时，电压可能低于 0.6V，计算结果会被限制为 0MPa；若需要断线报警，应在应用层增加低电流判定。
- 传感器的精度、温漂、150R 电阻误差、运放误差和 VREFINT 标称误差都会进入最终压力误差。

## 与其他模块的关系

本模块输出：`pressure_mpa`。

`app_control.c` 获取这个压力后，调用 `Thrust_CalculateNewton()` 计算推力；然后将压力和推力交给 `screen_protocol.c` 刷新屏幕。

## CubeMX 自动生成与手写代码边界

### CubeMX 自动生成的部分

以下文件的基本框架来自 CubeMX：

| 文件 | CubeMX 自动生成内容 |
| --- | --- |
| `Core/Src/adc.c` | `hadc1` 句柄、`MX_ADC1_Init()`、ADC GPIO 初始化框架。 |
| `Core/Inc/adc.h` | `hadc1` 声明和 `MX_ADC1_Init()` 函数声明。 |
| `Core/Src/dma.c` | DMA 控制器时钟初始化与中断优先级框架。 |
| `Core/Src/stm32f1xx_it.c` | 中断服务函数文件框架。 |
| `Core/Src/main.c` | `HAL_Init()`、时钟配置、外设初始化顺序和主循环框架。 |

CubeMX 的配置来源是工程根目录的：

```text
JH_Pressure to thrust.ioc
```

### 本项目手写或修改的部分

| 文件 | 手写内容 |
| --- | --- |
| `Task/pressure_measure.c/.h` | 整个压力测量模块，均为手写。 |
| `Core/Src/adc.c` | ADC1 改为 11 通道扫描、IN0-IN9 加 VREFINT、长采样时间、DMA1 Channel1 配置。 |
| `Core/Inc/adc.h` | 新增 `hdma_adc1` 的外部声明。 |
| `Core/Src/dma.c` | 新增 DMA1 Channel1 中断使能。 |
| `Core/Src/stm32f1xx_it.c` | 新增 `DMA1_Channel1_IRQHandler()`。 |
| `Core/Src/main.c` | 新增 `AppControl_Init()` 与 `AppControl_Process()` 调用。 |

### 为什么要区分它们

CubeMX 重新生成代码时，会重写它管理的 `.c` 文件。放在 `/* USER CODE BEGIN */` 和 `/* USER CODE END */` 之间的内容通常会保留；放在这些区域外的手工修改可能被覆盖。

本工程中的 ADC 11 通道配置属于对 `adc.c` 的手工修改。重新生成前，先确认 CubeMX 中已经配置了同样的 ADC 设置，避免生成后退回为单通道。

### 在 CubeMX 中应确认的 ADC 设置

```text
ADC1 Regular Conversion：11 个 Rank
Rank 1-10：IN0-IN9
Rank 11：VREFINT
Scan Conversion Mode：Enable
Sampling Time：239.5 Cycles
ADC1 DMA：DMA1 Channel1
DMA Direction：Peripheral to Memory
DMA Data Width：Half Word
DMA Mode：Normal
DMA1 Channel1 IRQ：Enable
```

## 附录：C 语言集中复习

本附录与前文的就近说明内容重复，用于集中复习；第一次阅读请优先按前文的代码顺序理解。

这一节结合本模块的真实代码解释 C 语言写法。刚开始不需要把所有术语背下来，先理解“它解决什么问题”。

### 什么是宏定义 `#define`

代码中有：

```c
#define PRESSURE_ADC_CHANNEL_COUNT (10U)
#define PRESSURE_ADC_SCAN_LENGTH    (11U)
```

`#define` 是 C 语言的**预处理宏定义**。编译器真正编译 C 代码之前，预处理器会先做文本替换。

例如：

```c
uint16_t sorted[PRESSURE_ADC_CHANNEL_COUNT];
```

预处理后可以理解为：

```c
uint16_t sorted[(10U)];
```

宏不是变量：

- 不占 RAM。
- 没有运行时地址。
- 不能在程序运行中修改。
- 适合表示固定数量、寄存器位、缓冲区长度和固定公式参数。

### 为什么要给 `10U` 加 `U`

`10U` 表示“数值为 10 的无符号整数常量”。末尾的 `U` 是 `unsigned` 的缩写。

```c
10      // 普通 int 常量，通常是有符号整数
10U     // unsigned int 常量，无符号整数
10UL    // unsigned long 常量
```

这里的 `U` 不是单位，也不是 `uint8_t` 或 `uint16_t`。

在本模块中，数组下标和循环计数使用 `uint32_t`，例如：

```c
uint32_t i;
for (i = 0U; i < PRESSURE_ADC_CHANNEL_COUNT; ++i)
```

把 `0`、`10` 这类非负常量写成 `0U`、`10U`，可以减少“有符号数和无符号数比较”的编译器警告。它是一种嵌入式 C 的常见习惯，不写 `U` 程序通常也能工作。

### `10U` 和 `uint8_t` / `uint16_t` 不是一回事

这两个概念容易混淆：

```c
#define PRESSURE_ADC_CHANNEL_COUNT (10U) // 一个固定数值常量
uint16_t signal_code;                     // 一个可改变的 16 位无符号变量
```

`uint8_t`、`uint16_t`、`uint32_t` 是**变量类型**，决定变量占多少位、能保存多大的数；它们不能直接替代 `10U`。

| 类型 | 位数 | 取值范围 | 本模块中的例子 |
| --- | --- | --- | --- |
| `uint8_t` | 8 位 | 0-255 | 不适合保存 12 位 ADC 码值。 |
| `uint16_t` | 16 位 | 0-65535 | 保存 ADC 的 0-4095 码值。 |
| `uint32_t` | 32 位 | 0-4294967295 | 保存累加值、循环计数和毫秒时间。 |
| `float` | 单精度浮点 | 有小数 | 保存电压、压力和 VDDA。 |

例如以下代码是合理的：

```c
uint16_t adc_code = 3723U;
```

左边的 `uint16_t` 说明变量存储格式，右边的 `3723U` 是赋给它的常量值。

### 为什么宏放在 `.h` 而不是 `.c`

`.h` 是头文件，通常放“其他文件需要知道的声明和约定”；`.c` 放“具体实现细节”。

`PRESSURE_ADC_CHANNEL_COUNT` 放在 `pressure_measure.h` 的原因是：它不仅决定本模块内部数组长度，也定义了函数 `Pressure_CalculateRobustCode()` 期望接收几个 ADC 样本。其他代码或测试代码若调用这个函数，需要知道正确数量是 10。

```c
uint16_t Pressure_CalculateRobustCode(const uint16_t *samples, uint32_t count);
```

因此它属于模块公开约定，放 `.h` 合理。

判断规则：

| 常量用途 | 建议位置 |
| --- | --- |
| 多个 `.c` 文件都需要使用 | 放 `.h`。 |
| 是公开函数的输入规则或硬件接口规则 | 放 `.h`。 |
| 只被本 `.c` 内部实现使用 | 放 `.c`，避免其他文件依赖内部细节。 |

`PRESSURE_ADC_SCAN_LENGTH` 当前只在 `pressure_measure.c` 中使用，从严格封装角度，它可以放在 `.c`。目前保留在 `.h` 是为了让“10 路外部 ADC + 1 路 VREFINT”的硬件扫描关系清晰可见；两种写法都可以，关键是团队约定一致。

### `static` 是什么

本模块中：

```c
static float s_pressure_mpa;
static void PressureMeasure_StartConversion(void);
```

文件最外层的 `static` 表示“仅本 `.c` 文件可见”：

- `s_pressure_mpa` 的数据在整个程序运行期间一直存在，但其他 `.c` 文件不能直接访问。
- `PressureMeasure_StartConversion()` 是内部辅助函数，其他 `.c` 文件不能调用。

这样可以防止别的模块随意修改测量状态。其他模块必须通过 `PressureMeasure_GetMpa()` 获取压力。

### `volatile` 是什么

```c
static volatile bool s_adc_conversion_complete;
```

这个变量会被两个不同执行位置访问：DMA 完成中断写入它，主循环读取它。`volatile` 告诉编译器：“每次都从内存重新读取，不要把旧值一直缓存到寄存器里。”

它适合硬件寄存器、中断与主循环共享的简单标志位。`volatile` 不等于线程安全；复杂共享数据仍需要临界区或其他同步方式。

### 指针 `*` 和数组

函数声明：

```c
uint16_t Pressure_CalculateRobustCode(const uint16_t *samples, uint32_t count);
```

`samples` 是指向 `uint16_t` 数据的指针。调用时传入数组：

```c
uint16_t values[10];
Pressure_CalculateRobustCode(values, 10U);
```

数组名 `values` 在传入函数时会自动变成“第一个元素的地址”。`const` 表示函数承诺不修改调用者提供的原始数组。
