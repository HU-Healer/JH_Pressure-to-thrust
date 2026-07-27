# thrust_calculator 模块说明

## 模块职责

`Task/thrust_calculator.c` 是纯计算模块，负责将液压压力和油缸缸径转换为推力，并提供 `kg`、`吨`、`N` 三种显示单位。

该模块不依赖 HAL、不访问 ADC、不操作串口，也不保存全局运行状态。因此它可以独立测试，逻辑清晰，后续修改推力公式时不会影响硬件驱动。

## 依赖关系

```text
pressure_measure.c
  -> 输出 pressure_mpa
app_control.c
  -> 调用 Thrust_CalculateNewton()
  -> 调用 Thrust_Convert()
screen_protocol.c
  -> 调用 Thrust_GetUnitText()
```

数据关系：

```text
压力(MPa) + 缸径(mm)
  -> 牛顿值(N)
  -> kg / 吨 / N
  -> 屏幕 Thrust 和 tuili
```

## 推力公式

压力 `MPa` 需要先换算为 `Pa`：

```text
1MPa = 1,000,000Pa
```

缸径 `mm` 需要换算为 `m`：

```text
d_m = cylinder_d_mm / 1000
```

油缸活塞有效面积：

```text
A = PI × d_m² / 4
```

液压力：

```text
F_N = pressure_mpa × 1,000,000 × A
```

示例：压力 30MPa、缸径 100mm：

```text
d = 0.1m
A = PI × 0.1² / 4 = 0.007854m²
F = 30,000,000 × 0.007854 ≈ 235,619N
≈ 24,027kgf
≈ 24.0吨力
```

## 单位定义

| 枚举值 | 显示文本 | 计算方式 |
| --- | --- | --- |
| `THRUST_UNIT_KGF` | `kg` | `N / 9.80665` |
| `THRUST_UNIT_TON` | `吨` | `N / 9.80665 / 1000` |
| `THRUST_UNIT_NEWTON` | `N` | `N` |

这里的 `kg` 实际上是 `kgf`，即千克力。为了和屏幕显示习惯保持一致，界面文本使用 `kg`。

这些单位名称来自头文件中的枚举：

```c
typedef enum
{
    THRUST_UNIT_KGF = 0,
    THRUST_UNIT_TON,
    THRUST_UNIT_NEWTON
} ThrustUnit;
```

`enum` 是枚举，作用是给一组相关整数取有意义的名字。它们实际可理解为 `0`、`1`、`2`，但写 `THRUST_UNIT_TON` 比直接写 `1` 清楚得多。`typedef` 让后续代码可直接写 `ThrustUnit unit;`。

## 公开接口

### `Thrust_CalculateNewton(float pressure_mpa, float cylinder_d_mm)`

输入：

- `pressure_mpa`：压力，单位 MPa。
- `cylinder_d_mm`：缸径，单位 mm。

参数和返回值都使用 `float`，因为压力、面积和推力可能带小数，例如 `15.3MPa`。`uint16_t` 只能保存整数，更适合 ADC 原始码值。`15.0f` 末尾的 `f` 表示 `float` 常量；嵌入式代码常这样写，避免把常量按默认 `double` 参与运算。

输出：推力，单位 N。

当压力或缸径小于等于零时，函数直接返回 `0.0f`，防止出现无意义的负推力。

示例：

```c
float force_newton;

force_newton = Thrust_CalculateNewton(15.0f, 80.0f);
```

### `Thrust_Convert(float force_newton, ThrustUnit unit)`

输入：牛顿值和目标单位。

输出：转换后的推力数值。

示例：

```c
float display_thrust;

display_thrust = Thrust_Convert(force_newton, THRUST_UNIT_TON);
```

函数内部的 `switch (unit)` 是按单位选择分支：每个 `case` 对应一种换算公式，`default` 是未知单位时的兜底分支。

### `Thrust_GetUnitText(ThrustUnit unit)`

输出当前单位对应的文本：`kg`、`吨` 或 `N`。

`吨` 在源码中用 UTF-8 字节转义返回，以避免 ARMCC 对 UTF-8 中文字符串的代码页兼容问题。

`const char *` 表示“指向只读字符串的指针”。`char` 是一个字符，`char *` 常表示字符串地址，`const` 表示调用者不应修改该字符串：

```c
const char *unit_text = Thrust_GetUnitText(THRUST_UNIT_KGF);
// unit_text 指向字符串 "kg"
```

## 标准调用方式

通常不需要直接在 `main.c` 调用该模块。由 `app_control.c` 统一调用：

```c
s_force_newton = Thrust_CalculateNewton(s_pressure_mpa, s_cylinder_diameter_mm);
s_thrust_value = Thrust_Convert(s_force_newton, s_unit);
```

然后将：

- `s_thrust_value` 作为屏幕的推力显示数值。
- `Thrust_GetUnitText(s_unit)` 作为屏幕单位文本。

## 单位切换关系

单位状态由 `app_control.c` 保存，当前顺序为：

```text
kg -> 吨 -> N -> kg
```

同一时刻内部的 `force_newton` 不会改变，改变的只是 `Thrust_Convert()` 的目标单位和屏幕显示结果。

## 注意事项

- 当前公式按单作用油缸的完整活塞面积计算。
- 如果需要计算双作用油缸回程推力，必须额外输入活塞杆直径，并用“活塞面积减去杆面积”计算有效面积。
- 不要将传感器压力单位误传为 `bar`、`kg/cm²` 或 `Pa`；本模块输入固定为 MPa。
- 不要将缸径单位误传为 cm 或 m；本模块输入固定为 mm。
- 显示一位小数属于屏幕格式问题，不应提前在本模块内四舍五入，以免累积误差。

## CubeMX 自动生成与手写代码边界

`thrust_calculator.c/.h` 不属于 CubeMX 自动生成范围，两个文件均为手写业务逻辑。

原因是 CubeMX 的职责是配置 STM32 外设，例如 ADC、UART、GPIO、DMA 和时钟；它不知道“液压压力、油缸缸径、推力、kg、吨”这些具体业务规则。

因此：

| 内容 | 来源 |
| --- | --- |
| `ThrustUnit` 枚举 | 手写。 |
| `Thrust_CalculateNewton()` | 手写。 |
| `Thrust_Convert()` | 手写。 |
| `Thrust_GetUnitText()` | 手写。 |
| 推力公式与 `9.80665f` | 手写的工程计算规则。 |

重新用 CubeMX 生成代码不会修改 `Task/thrust_calculator.c/.h`，因为它们不受 CubeMX 管理。它们被 Keil 工程的 `Task` 分组编译。

## 附录：C 语言集中复习

本附录与前文的就近说明内容重复，用于集中复习；第一次阅读请优先按前文的代码顺序理解。

### `typedef enum` 是什么

头文件中定义：

```c
typedef enum
{
    THRUST_UNIT_KGF = 0,
    THRUST_UNIT_TON,
    THRUST_UNIT_NEWTON
} ThrustUnit;
```

这是**枚举类型**。它的作用是给一组相关整数取有意义的名字。

可以把它理解为：

```c
THRUST_UNIT_KGF     -> 0
THRUST_UNIT_TON     -> 1
THRUST_UNIT_NEWTON  -> 2
```

但程序中写 `THRUST_UNIT_TON` 比直接写 `1` 清楚得多，也不容易弄错。

`typedef` 的作用是给类型起别名。没有 `typedef` 时通常需要写 `enum ThrustUnit`；现在可以直接写：

```c
ThrustUnit unit = THRUST_UNIT_KGF;
```

### `float` 为什么用来表示压力和推力

```c
float pressure_mpa;
float cylinder_d_mm;
float force_newton;
```

压力、电压、面积和推力换算都可能带小数，例如 `15.3MPa`、`0.600V`。`float` 是单精度浮点数，适合 STM32F103 这类 MCU 做一般测量和显示计算。

整数类型如 `uint16_t` 只能保存整数，不能直接保存 `15.3`。因此：

```c
uint16_t adc_code = 3723U;  // ADC 原始整数码值
float pressure_mpa = 30.0f; // 换算后的带小数压力
```

末尾的 `f` 表示浮点常量：`30.0f` 是 `float`，`30.0` 默认是 `double`。在嵌入式 C 中写 `f` 可以避免不必要的 double 运算。

### 函数如何接收和返回数据

```c
float Thrust_CalculateNewton(float pressure_mpa, float cylinder_d_mm);
```

从左到右读：

- 第一个 `float`：函数返回一个浮点数。
- `Thrust_CalculateNewton`：函数名称。
- 括号内两个 `float`：调用时要提供压力和缸径。

调用示例：

```c
float force_newton;

force_newton = Thrust_CalculateNewton(15.0f, 80.0f);
```

参数传入函数时是“值传递”。函数内部改变 `pressure_mpa` 不会改变调用者外部的变量。

### `const char *` 是什么

```c
const char *Thrust_GetUnitText(ThrustUnit unit);
```

`char` 是单个字符类型，`char *` 是指向字符的指针，常用于表示字符串。`const char *` 表示“指向只读字符串的指针”。

例如：

```c
const char *unit_text;

unit_text = Thrust_GetUnitText(THRUST_UNIT_KGF);
// unit_text 现在指向字符串 "kg"
```

`const` 表示调用者不应修改这个字符串内容。单位文本存放在程序常量区，不需要你手动释放内存。

### `switch` 和 `case` 是什么

`Thrust_Convert()` 中使用：

```c
switch (unit)
{
case THRUST_UNIT_TON:
    return force_newton / 9.80665f / 1000.0f;

case THRUST_UNIT_NEWTON:
    return force_newton;

default:
    return force_newton / 9.80665f;
}
```

`switch` 用于“根据一个值选择多条分支”。这里根据单位选择换算公式。`default` 是没有匹配到任何已知单位时的兜底分支，当前按 kg 处理。

### 为什么 `.h` 中放枚举，`.c` 中放公式常量

`ThrustUnit` 放在 `.h`，因为 `app_control.c`、`screen_protocol.c` 都需要知道单位类型和三个单位名称。

```c
#define THRUST_PI               (3.14159265358979323846f)
#define THRUST_STANDARD_GRAVITY (9.80665f)
```

这两个宏只被 `thrust_calculator.c` 内部使用，所以放在 `.c`。外部调用者只需要知道“传压力和缸径，得到推力”，不需要依赖内部公式常量。
