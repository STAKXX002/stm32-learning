# STM32F446RE Nucleo - Stepper Controller Port

**Board:** Nucleo-F446RE (STM32F446RET6)  
**Toolchain:** STM32CubeMX · VS Code + STM32 extension  
**Language:** C (STM32 HAL)

---

## Overview

This repository documents the development and learning process of porting a
dual-axis closed-loop stepper motor controller from ESP32/Arduino to STM32F446RE.
Each folder is a self-contained CubeMX project representing a progressive stage,
from basic peripheral bring-up through to the full ported application.

---

## The Application

The target application drives a **dual-gantry Z axis and single X axis** stepper
system with quadrature encoder feedback and a PD position control loop.

### Original platform - ESP32 / Arduino

| Subsystem | Implementation |
|---|---|
| Step generation | AccelStepper library |
| Encoder reading | ESP32 PCNT hardware pulse counter |
| Control loop | `loop()` with variable `dt` via `micros()` |
| Serial | `Serial.readStringUntil('\n')` - blocking |

### STM32 port - F446RE

| Subsystem | Implementation |
|---|---|
| Step generation | TIM2 Output Compare toggle mode - 3 channels (Z1, Z2, X) |
| Encoder reading | TIM1, TIM3, TIM4 in hardware encoder mode (TI12) |
| Control loop | TIM6 base timer ISR at **10 kHz** (fixed `DT = 100 µs`) |
| Serial | USART2 + DMA circular buffer - non-blocking |

---

## Pin Mapping

| Signal | Port / Pin | Timer function |
|---|---|---|
| Z1 STEP | PA5 | TIM2 CH1 - OC toggle |
| Z1 DIR | PC0 | GPIO output |
| Z1 EN | PC1 | GPIO output |
| Z2 STEP | PB3 | TIM2 CH2 - OC toggle |
| Z2 DIR | PC2 | GPIO output |
| Z2 EN | PC3 | GPIO output |
| X STEP | PB10 | TIM2 CH3 - OC toggle |
| X DIR | PC4 | GPIO output |
| X EN | PC5 | GPIO output |
| UART TX | PA2 | USART2 |
| UART RX | PA3 | USART2 |

---

## Control Parameters

```c
#define STEPS_PER_MM   800       // microstepping resolution
#define CPR            4000      // encoder counts per revolution
#define STOP_DEADBAND  5.0f      // steps - settle threshold
#define MAX_SPEED      800.0f    // steps/sec
#define ACCEL          2000.0f   // steps/sec²

#define Kp             0.55f     // Z1 proportional gain
#define Kd             0.14f     // derivative gain
#define Kp_slave       0.7f      // Z2 follower gain (tracks Z1)
```

---

## Serial Command Protocol (115200 baud)

| Command | Action |
|---|---|
| `Z<mm>` | Move Z axis to absolute position in mm - e.g. `Z100` |
| `G<mm>` | Move X axis to absolute position in mm - e.g. `G50` |
| `SEQ Z10,G20,Z0,G0,2` | Run a comma-separated move sequence, repeated N times |
| `R` | Emergency stop, reset all state and queue |
| `STATUS` | Print current axis positions and mode over UART |

---

## Clock Configuration

```
HSE  = 8 MHz (Nucleo ST-Link oscillator, BYPASS mode)
PLL  M=8, N=360, P=2  →  SYSCLK = 180 MHz
AHB  /1  →  HCLK  = 180 MHz
APB1 /4  →  PCLK1 =  45 MHz  →  TIM2/3/4/6 clock = 90 MHz
APB2 /2  →  PCLK2 =  90 MHz  →  TIM1 clock = 180 MHz
```

**TIM6 at 10 kHz:** `Prescaler = 89`, `Period = 99`
→ 90 MHz ÷ (90 × 100) = 10 000 Hz

**TIM2 step interval:** `ticks = 90_000_000 / velocity_steps_per_sec` (no prescaler, 32-bit counter)

---

## Design Notes

**Fixed DT vs dynamic dt** - The ESP32 version measured `dt` each loop iteration
with `micros()`. The STM32 version runs the control loop inside a 10 kHz TIM6 ISR
making `DT = 0.0001f` a known constant, eliminating timing jitter from the
derivative term.

**No UART inside ISR** - `HAL_UART_Transmit()` must not be called from interrupt
context. Debug output is handled by setting a `volatile bool` flag inside the ISR
and doing the actual transmit in the `main()` loop.

**Hardware step generation** - TIM2 Output Compare in toggle mode auto-toggles the
STEP pins at the required frequency without CPU intervention. The ISR advances the
compare register each control tick.

**DMA circular UART RX** - `HAL_UART_Receive_DMA()` with a circular buffer allows
incoming bytes to be received without blocking the control loop. `handleSerial()`
reads from the buffer by diffing the DMA write pointer against a read cursor.

**Encoder overflow handling** - The 16-bit hardware counter wraps freely. The delta
is cast to `int16_t` to recover the signed difference across wrap boundaries, and a
`|delta| < 1000` guard rejects electrical glitch spikes.

---

## Repository Structure

```
stm32-learning/
├── 01_blink/           CubeMX + HAL GPIO - board bring-up
├── 02_uart_echo/       USART2 DMA circular RX, echo to TX
├── 03_timers/          TIM6 base IRQ, TIM2 OC toggle, TIM1 encoder mode
├── 04_encoder/         3-axis encoder read with spike filter
├── 05_stepper_pulse/   setStepVelocity() on TIM2 CH1–3
├── 06_full_port/       Complete ported application (main.c + main.h)
├── notes/
│   └── concepts.md     HAL patterns, ISR rules, timer mathematics
└── README.md
```

Each project contains only `*.ioc`, `Core/Src/`, and `Core/Inc/`.
Generated `Drivers/` and build output are excluded via `.gitignore`.

---

## Progress

- [ ] `01_blink` - GPIO, clock config, HAL_GPIO_TogglePin
- [ ] `02_uart_echo` - USART2, DMA1 Stream5, circular RX
- [ ] `03_timers` - TIM6 IRQ rate, TIM2 OC toggle, TIM1/3/4 encoder mode
- [ ] `04_encoder` - updateEncoders(), delta arithmetic, spike filter
- [ ] `05_stepper_pulse` - setStepVelocity(), tick math, DIR pin
- [ ] `06_full_port` - runZ_ISR(), runX_ISR(), closed-loop validation

---

## Build

1. Open a project's `.ioc` in STM32CubeMX and regenerate code if needed.
2. Open the folder in VS Code with the STM32 extension.
3. Build with `Ctrl+Shift+B` and flash via the on-board ST-Link.

Tested on: Windows 11 · CubeMX 6.x · STM32CubeF4 HAL 1.8.x