# STM32 Concepts - Running Notes

A living document of things learned at each stage.

---

## Stage 01 - Blink

### Workflow established

1. **CubeMX** - project generation
   - Use "Board Selector" and search `NUCLEO-F446RE` instead of the MCU selector -
     it pre-configures the Nucleo's ST-Link oscillator automatically
   - When asked "Initialize all peripherals with their default mode?" → **No**,
     configure only what you need
   - Enable HSE under System Core → RCC → `BYPASS Clock Source` (Nucleo has no
     crystal, it uses the ST-Link oscillator)
   - Enable USART2 under Connectivity → `Asynchronous` - auto-assigns PA2/PA3
     which are wired to the ST-Link virtual COM port on the Nucleo
   - Set PA5 as `GPIO_Output`, label it `LD2` - this is the onboard green LED
   - Toolchain: **CMake + GCC ARM** (modern VS Code workflow, not STM32CubeIDE)
   - Generate code into the project folder

2. **VS Code** - build and flash
   - Open the generated folder, VS Code + STM32 extension picks up the CMake
     project automatically
   - Set build configuration to **STM32 build** in the extension
   - Under the STM32 Devices panel, four buttons appear next to the detected board:
     Rename · **Flash Firmware** · **Blink** · Copy Serial ID
   - **Blink** flashes the ST-Link LED on the Nucleo - use this first to confirm
     the extension has found the board before attempting a flash
   - **Flash Firmware** programs the `.elf` onto the chip

### Code

User code lives only inside `/* USER CODE BEGIN */` / `/* USER CODE END */` blocks.
CubeMX respects these on regeneration - anything outside gets overwritten.

```c
/* USER CODE BEGIN 3 */
HAL_GPIO_TogglePin(LD2_GPIO_Port, LD2_Pin);
HAL_Delay(500);
/* USER CODE END 3 */
```

`HAL_Delay()` is based on SysTick which runs off HCLK (180 MHz here) - so
`HAL_Delay(500)` is an accurate 500 ms.

### Clock config - 180 MHz from HSE

```
HSE = 8 MHz (ST-Link oscillator, BYPASS mode)
PLL: M=8, N=360, P=2  →  SYSCLK = 180 MHz
AHB  /1  →  HCLK  = 180 MHz   (CPU, SysTick)
APB1 /4  →  PCLK1 =  45 MHz   (TIM2/3/4/6/7...)
APB2 /2  →  PCLK2 =  90 MHz   (TIM1/8/9/10/11...)
```

APB timer clocks are doubled when the divider is > 1, so:
- APB1 timer clock = 45 × 2 = **90 MHz**
- APB2 timer clock = 90 × 2 = **180 MHz**

This matters later for TIM2 step generation and TIM6 ISR rate calculations.

### What gets committed vs ignored

| Commit | Ignore |
|---|---|
| `*.ioc` | `Drivers/` (HAL library - regenerated) |
| `Core/Src/main.c` | `build/` (compiled output) |
| `Core/Inc/main.h` | `startup_stm32f446xx.s` (regenerated) |
| `CMakeLists.txt` | `.vscode/` (local IDE settings) |
| `CMakePresets.json` | `.mxproject`, `.clangd` |
| `STM32F446XX_FLASH.ld` | |
| `cmake/` | |

---

## Stage 02 - UART Echo

_Notes go here after completing 02_uart_echo._

---

## Stage 03 - Timers

_Notes go here after completing 03_timers._

---

## Stage 04 - Encoder

_Notes go here after completing 04_encoder._

---

## Stage 05 - Stepper Pulse

_Notes go here after completing 05_stepper_pulse._

---

## Stage 06 - Full Port

_Notes go here after completing 06_full_port._