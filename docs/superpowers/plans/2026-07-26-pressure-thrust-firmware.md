# 液压压力与推力固件实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 STM32F103C8T6 建立可测量 4-20mA 液压压力、计算油缸推力并预留显示屏协议配置区的 HAL 固件。

**Architecture:** 将无需 HAL 的计算逻辑拆至 `Task/thrust_calculator`，使其可由主机端 C 测试直接验证；ADC DMA、VREFINT 和十路采样筛选集中在 `Task/pressure_measure`；USART1 DMA 与空白协议配置集中在 `Task/screen_protocol`。`Task/app_control` 仅协调这些模块，`main.c` 仅初始化并周期调用。

**Tech Stack:** C11、STM32Cube HAL F1、STM32F103C8T6、ADC1 DMA、USART1 DMA、Keil MDK 工程。

---

## 文件结构

- `Task/thrust_calculator.[ch]`：压力、缸径、面积、推力与 N/kg/吨转换，不依赖 HAL。
- `Task/pressure_measure.[ch]`：ADC1 DMA 扫描缓冲、VREFINT、十路样本排序均值、压力换算。
- `Task/screen_protocol.[ch]`：USART1 DMA 接收、发送和唯一的用户协议配置区。
- `Task/app_control.[ch]`：应用状态、周期测量、推力刷新、屏幕事件处理。
- `Core/Src/adc.c`：ADC1 改为 11 项扫描、连续 DMA、长采样时间。
- `Core/Src/main.c`：启动应用并每 100ms 执行应用周期任务。
- `Core/Src/stm32f1xx_it.c`：增加 ADC1 DMA1 Channel1 中断转发。
- `Core/Inc/adc.h`：导出 ADC1 DMA 句柄。
- `tests/test_thrust_calculator.c`：主机端纯计算单元测试。
- `代码说明手册.md`：用户可维护的功能、函数、公式和协议配置说明。

### Task 1: 推力计算模块

**Files:**
- Create: `Task/thrust_calculator.h`
- Create: `Task/thrust_calculator.c`
- Create: `tests/test_thrust_calculator.c`

- [ ] **Step 1: 写入失败测试**

```c
assert_close(Thrust_CalculateNewton(30.0f, 100.0f), 235619.45f, 0.1f);
assert_close(Thrust_Convert(9806.65f, THRUST_UNIT_KGF), 1000.0f, 0.01f);
assert_close(Thrust_Convert(9806.65f, THRUST_UNIT_TON), 1.0f, 0.001f);
assert_close(Thrust_Convert(9806.65f, THRUST_UNIT_NEWTON), 9806.65f, 0.01f);
```

- [ ] **Step 2: 运行测试，确认其因缺少模块而失败**

Run: `gcc -std=c11 -ITask tests/test_thrust_calculator.c Task/thrust_calculator.c -lm -o tests/test_thrust_calculator.exe`

Expected: 编译失败，提示 `thrust_calculator.h` 或对应函数不存在。

- [ ] **Step 3: 实现最小计算模块**

```c
float Thrust_CalculateNewton(float pressure_mpa, float cylinder_d_mm)
{
    float diameter_m = cylinder_d_mm / 1000.0f;
    float area_m2 = 3.14159265358979323846f * diameter_m * diameter_m / 4.0f;
    return pressure_mpa * 1000000.0f * area_m2;
}
```

- [ ] **Step 4: 重新运行测试**

Run: `tests\\test_thrust_calculator.exe`

Expected: 输出 `PASS`，并以非零退出码表示断言失败。

### Task 2: ADC1 DMA 与压力测量模块

**Files:**
- Create: `Task/pressure_measure.h`
- Create: `Task/pressure_measure.c`
- Modify: `Core/Src/adc.c`
- Modify: `Core/Inc/adc.h`
- Modify: `Core/Src/stm32f1xx_it.c`

- [ ] **Step 1: 写入纯函数失败测试**

```c
uint16_t samples[10] = {2048, 2047, 2048, 2049, 2048, 2048, 2047, 2048, 2048, 4095};
assert(Pressure_CalculateRobustCode(samples, 10U) == 2048U);
assert_close(Pressure_CalculateMpa(744U, 3.3f), 0.0f, 0.05f);
assert_close(Pressure_CalculateMpa(3723U, 3.3f), 30.0f, 0.05f);
```

- [ ] **Step 2: 运行测试，确认因函数缺失而失败**

Run: `gcc -std=c11 -ITask tests/test_pressure_measure.c Task/pressure_measure.c -lm -o tests/test_pressure_measure.exe`

Expected: 编译失败，提示 `Pressure_CalculateRobustCode` 或 `Pressure_CalculateMpa` 不存在。

- [ ] **Step 3: 实现测量模块并改造 ADC1**

ADC1 使用 11 次规则转换：IN0-IN9 排名 1-10，VREFINT 排名 11；全部使用 `ADC_SAMPLETIME_239CYCLES_5`；启用扫描、连续转换、DMA 循环模式。`pressure_measure.c` 使用常量 `VREFINT_TYPICAL_VOLTAGE = 1.20f` 和 `VDDA = 1.20 * 4095 / vrefint_code`，排序后丢弃最低和最高各两项，对剩余六项取均值。

- [ ] **Step 4: 重新运行纯函数测试**

Run: `tests\\test_pressure_measure.exe`

Expected: 输出 `PASS`。

- [ ] **Step 5: 编译 Keil 工程**

Run: 在 Keil MDK 中打开 `MDK-ARM/JH_Pressure to thrust.uvprojx` 并执行 Rebuild。

Expected: 0 Error；如 Keil 未安装，记录为环境限制，不将旧 `.axf` 视为本次验证。

### Task 3: 显示屏协议占位与 USART1 DMA

**Files:**
- Create: `Task/screen_protocol.h`
- Create: `Task/screen_protocol.c`

- [ ] **Step 1: 写入失败测试**

```c
ScreenProtocol_Init();
assert(ScreenProtocol_IsConfigured() == false);
assert(ScreenProtocol_SendMeasurements(1.0f, 2.0f, THRUST_UNIT_KGF) == SCREEN_PROTOCOL_NOT_CONFIGURED);
```

- [ ] **Step 2: 运行测试，确认因模块缺失而失败**

Run: `gcc -std=c11 -ITask tests/test_screen_protocol_config.c Task/screen_protocol.c -o tests/test_screen_protocol_config.exe`

Expected: 编译失败，提示 `screen_protocol.h` 不存在。

- [ ] **Step 3: 实现协议模块**

将用户可编辑的协议配置放在 `screen_protocol.c` 文件首部的 `用户后续填写位置` 区域。发送模板、设置缸径命令和切换单位命令初始均为空；模块必须检测空配置并返回 `SCREEN_PROTOCOL_NOT_CONFIGURED`。接收使用 `HAL_UARTEx_ReceiveToIdle_DMA`，IDLE 回调将完整帧交给解析函数；USART1 DMA TX 忙时返回 `SCREEN_PROTOCOL_BUSY`。

- [ ] **Step 4: 重新运行配置测试**

Run: `tests\\test_screen_protocol_config.exe`

Expected: 输出 `PASS`。

### Task 4: 应用调度和主循环接入

**Files:**
- Create: `Task/app_control.h`
- Create: `Task/app_control.c`
- Modify: `Core/Src/main.c`

- [ ] **Step 1: 写入失败测试**

```c
AppControl_Init();
assert(AppControl_GetUnit() == THRUST_UNIT_KGF);
assert(AppControl_SetCylinderDiameter(0.0f) == false);
assert(AppControl_SetCylinderDiameter(100.0f) == true);
```

- [ ] **Step 2: 运行测试，确认因模块缺失而失败**

Run: `gcc -std=c11 -ITask tests/test_app_control.c Task/app_control.c Task/thrust_calculator.c -lm -o tests/test_app_control.exe`

Expected: 编译失败，提示 `app_control.h` 不存在。

- [ ] **Step 3: 实现应用控制**

默认单位为 kg，默认缸径为 100mm；允许缸径为 1-1000mm。`AppControl_Process()` 每 100ms 获取最新压力并计算推力；若屏幕协议尚未配置，继续测量与计算，但不向 USART1 发送数据。单位按钮事件依序切换 kg、吨、N。

- [ ] **Step 4: 在 main.c 接入模块**

在所有 CubeMX 初始化之后调用 `AppControl_Init()`；主循环调用 `AppControl_Process()`。保留现有 USART3/VOFA 文件，不在本任务中改变其接口。

- [ ] **Step 5: 编译 Keil 工程**

Run: 在 Keil MDK 中执行 Rebuild。

Expected: 0 Error。

### Task 5: 说明手册与最终验证

**Files:**
- Create: `代码说明手册.md`

- [ ] **Step 1: 编写手册**

手册使用 UTF-8 中文，列出文件职责、公开函数、变量流向、`0.6-3.0V -> 0-30MPa` 公式、推力公式、单位换算、100ms 刷新机制、USART1 DMA 工作方式，以及 `screen_protocol.c` 顶部用户配置区的每个字段应填写什么。

- [ ] **Step 2: 执行全部主机端测试**

Run: `tests\\test_thrust_calculator.exe; tests\\test_pressure_measure.exe; tests\\test_screen_protocol_config.exe; tests\\test_app_control.exe`

Expected: 全部输出 `PASS`。

- [ ] **Step 3: 检查工程文件**

Run: `rg -n "用户后续填写位置|pressure|Thrust|cylinder_d_mm|tuili" Task 代码说明手册.md`

Expected: 协议配置区与手册均明确标出变量填写位置。

## 自检结论

- 覆盖范围：计算、ADC/VREFINT、DMA、屏幕协议占位、主循环和说明手册均有对应任务。
- 占位检查：运行时代码唯一允许的空白内容是用户要求保留的显示屏协议配置；其余行为有确定默认值。
- 一致性：`ThrustUnit` 在计算、屏幕和应用控制模块中使用同一枚举；压力统一以 MPa 表示，缸径统一以 mm 表示。
