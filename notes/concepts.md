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

### What was built

USART2 receiving bytes via DMA circular buffer in the background, echoing
them back over TX. This is the exact same receive mechanism used in the full
port to handle commands like `Z100` or `SEQ Z10,G20`.

### Why DMA circular buffer - not blocking reads

On ESP32, `Serial.readStringUntil('\n')` blocks - the CPU waits for bytes.
In a control system with a 10 kHz ISR that's not possible. DMA solves this:

> Tell the DMA engine once - "put every incoming USART2 byte into `rxBuffer`,
> wrap around forever" - then the CPU never touches UART again. Bytes arrive
> silently in the background while the control loop keeps running.

### How the read pointer works

`__HAL_DMA_GET_COUNTER()` returns how many slots remain in the buffer.
Subtracting from the total size gives the current DMA write position:

```c
newPos = RX_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(huart2.hdmarx);
```

`oldPos` tracks where you last read up to. Anything between `oldPos` and
`newPos` is new unprocessed data:

```
rxBuffer = [ h, e, l, l, o, _, _, _, _, _ ]
                  ↑           ↑
               oldPos       newPos
                  [  new data  ]
```

### The wrap-around case

When DMA reaches the end of the buffer it wraps back to position 0.
`newPos` ends up less than `oldPos` - read in two chunks:

```
rxBuffer = [ w, h, a, t, o, h, e, l, l, o ]
               ↑              ↑
            newPos          oldPos
[chunk 2 →]               [← chunk 1      ]
```

```c
if (newPos > oldPos)
{
    // single chunk
    memcpy(txBuffer, rxBuffer + oldPos, newPos - oldPos);
    HAL_UART_Transmit(&huart2, txBuffer, newPos - oldPos, 100);
}
else
{
    // two chunks - end of buffer + start of buffer
    memcpy(txBuffer, rxBuffer + oldPos, RX_BUFFER_SIZE - oldPos);
    HAL_UART_Transmit(&huart2, txBuffer, RX_BUFFER_SIZE - oldPos, 100);
    memcpy(txBuffer, rxBuffer, newPos);
    HAL_UART_Transmit(&huart2, txBuffer, newPos, 100);
}
oldPos = newPos;
```

### CubeMX settings for DMA UART

- USART2 → Mode: `Asynchronous`, Baud: `115200`
- DMA Settings tab → Add `USART2_RX` → Mode: `Circular`
- DMA Settings tab → Add `USART2_TX` → Mode: `Normal`
- NVIC Settings tab → enable `USART2 global interrupt`
- Start DMA before the main loop: `HAL_UART_Receive_DMA(&huart2, rxBuffer, RX_BUFFER_SIZE)`

### Connection to the full port

In `06_full_port`, `handleSerial()` uses this exact same pattern -
`newPos`/`oldPos` diff - but instead of echoing, it parses the buffer
for command strings and dispatches them to the motion controller.

---

## Stage 03 - Timers

### The three timer roles

The full port uses four timers simultaneously, each with a completely different job:

| Timer | Mode | Job |
|---|---|---|
| TIM6 | Base timer + ISR | Control loop - fires at 10 kHz, runs PD math |
| TIM2 | Output Compare | Step pulse generator - toggles STEP pins at target frequency |
| TIM1 / TIM3 / TIM4 | Encoder mode | Read quadrature encoder edges from motor shafts |

These are independent - TIM6 does not affect TIM2, TIM2 does not affect encoders.

### TIM6 - 10 kHz control loop

TIM6 is a basic timer with no output pins. It just counts up and fires an interrupt
when it overflows. Prescaler and Period are chosen to hit exactly 10 kHz:

```
TIM6 clock = 90 MHz (APB1 timer clock)
Prescaler = 89  →  90 MHz / 90 = 1 MHz tick
Period = 99     →  1 MHz / 100 = 10 000 Hz
```

The ISR callback fires 10,000 times per second:

```c
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6)
    {
        isrCount++; // runs 10,000x per second
    }
}
```

In the full port, this is where the PD control math runs - reading encoders,
computing error, and updating step velocity every 100 µs.

### TIM2 - Output Compare step pulse generation

TIM2 has a free-running 32-bit counter at 90 MHz. A compare register holds a
target value. When the counter reaches the target, hardware **automatically toggles
the STEP pin** - no CPU needed. It then fires an interrupt so you can set the next
target:

```c
// Advance target by one interval - schedules the next toggle
__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1,
    __HAL_TIM_GET_COMPARE(&htim2, TIM_CHANNEL_1) + stepInterval);
```

Speed control is just changing `stepInterval`:

```
stepInterval = 90,000,000 / desired_steps_per_second
```

Example: 500 steps/sec → stepInterval = 180,000

Three channels on TIM2 drive three motors independently (Z1, Z2, X) - all from
the same free-running counter, each with its own compare register and interval.

### TIM1/3/4 - Encoder mode

These timers are completely passive - the hardware counts quadrature A/B edges
coming from the physical encoder on each motor shaft. No ISR, no compare register.
Just read the counter whenever you need position:

```c
uint16_t cur = (uint16_t)__HAL_TIM_GET_COUNTER(&htim1);
int16_t delta = (int16_t)(cur - prev); // cast handles 16-bit wrap
encZ1 += delta;
prev = cur;
```

The cast to `int16_t` is important - it recovers the correct signed delta even
when the 16-bit counter wraps from 65535 back to 0.

### CubeMX settings summary

**TIM6:**
- Mode: Activated
- Prescaler: `89`, Period: `99`, Auto-reload: Enable
- NVIC: enable TIM6 global interrupt

**TIM2:**
- Clock Source: Internal Clock
- CH1/2/3: Output Compare No Output
- Prescaler: `0`, Period: `4294967295` (32-bit max)
- NVIC: enable TIM2 global interrupt

**TIM1/3/4:**
- Combined Channels: Encoder Mode
- Encoder Mode: TI12
- Period: `65535`

### Verifying in the debugger

- Add `isrCount` to watch - pause execution after ~1 second, value should be ~10,000
- `encZ1/Z2/X` sit at 0 with nothing connected - correct
- Live watch doesn't update while running over SWD - pause to read values
  (live updates require ITM/SWO, a separate debug channel)

---

## Stage 04 - Encoder

### What was built

Three encoder timers (TIM1/3/4) reading absolute position and printing
over UART every 500ms. No motor hardware needed - values sit at 0 with
nothing connected, which is the correct baseline.

### Absolute position tracking

The hardware timer counter is only 16-bit (0–65535). For a motor that
can spin many full rotations you need a 32-bit software accumulator.
The pattern is: read the raw counter, compute the signed delta, add to
the accumulator:

```c
uint16_t cur = (uint16_t)__HAL_TIM_GET_COUNTER(&htim1);
encZ1 += (int16_t)(cur - prevZ1);  // int16_t cast handles wrap
prevZ1 = cur;
```

The `int16_t` cast is the key - if the counter wraps from 65535 to 2,
the raw difference is `2 - 65535 = -65533` as uint16, but cast to
int16_t it becomes `+3` (correct forward movement of 3 counts).

### Blocking vs DMA UART transmit

In this stage `HAL_UART_Transmit()` is used directly (blocking) rather
than DMA. This is fine here because the loop runs at 500ms - plenty of
time to wait for a short string to transmit. In the full port the
control loop runs at 10 kHz so blocking transmit inside the ISR would
be catastrophic - that's why DMA is used there.

### CubeMX settings

- TIM1, TIM3, TIM4 → Combined Channels: Encoder Mode TI12, Period: 65535
- USART2 → Asynchronous, 115200 (no DMA needed for slow polling prints)
- No TIM2, no TIM6 - not needed for encoder reading alone

### Printing with snprintf

```c
char uartBuf[64];
int len = snprintf(uartBuf, sizeof(uartBuf),
    "Z1:%ld  Z2:%ld  X:%ld\r\n", encZ1, encZ2, encX);
HAL_UART_Transmit(&huart2, (uint8_t*)uartBuf, len, 100);
```

`snprintf` is safer than `sprintf` - the `sizeof(uartBuf)` argument
prevents buffer overflow if the numbers get large.

---

## Stage 05 - Stepper Pulse

### What was built

TIM2 Output Compare generating step pulses on 3 channels at a
configurable velocity, with a step counter printed over UART every
500ms. Verified at 500 steps/sec - counter climbs by ~500 per second.

### setStepVelocity()

The key helper function that translates a desired speed into a TIM2
compare interval:

```c
void setStepVelocity(TIM_HandleTypeDef *htim, uint32_t channel,
                     uint32_t *interval, float stepsPerSec)
{
    if (stepsPerSec < 1.0f)
    {
        // Stop - disable the channel interrupt
        __HAL_TIM_DISABLE_IT(...);
        *interval = 0;
        return;
    }
    *interval = (uint32_t)(TIM2_CLOCK / stepsPerSec);
    __HAL_TIM_SET_COMPARE(htim, channel,
        __HAL_TIM_GET_COUNTER(htim) + *interval);
    __HAL_TIM_ENABLE_IT(...);
}
```

- To stop: disable the channel interrupt, interval = 0
- To run: compute interval, schedule first compare, enable interrupt
- Speed change: just call setStepVelocity() again with new speed

### The OC callback loop

Every time TIM2 fires an OC interrupt, the callback advances the next
compare target by the current interval:

```c
void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
    {
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1,
            __HAL_TIM_GET_COMPARE(&htim2, TIM_CHANNEL_1) + intervalZ1);
        stepsZ1++;
    }
    // same for CH2 (Z2) and CH3 (X)
}
```

This creates a self-sustaining pulse train - each interrupt schedules
the next one. To change speed mid-motion, just update the interval
variable and the next interrupt picks it up automatically.

### Interval math

```
TIM2 clock = 90 MHz
interval = 90,000,000 / steps_per_sec

500  steps/sec → interval = 180,000 ticks
1000 steps/sec → interval = 90,000  ticks
2000 steps/sec → interval = 45,000  ticks
```

Minimum reliable interval is around 900 ticks (100,000 steps/sec) -
below that the ISR overhead starts to matter.

### Connection to the full port

In `06_full_port`, `setStepVelocity()` is called from inside the TIM6
10 kHz ISR with the PD controller output as the speed argument. The
three channels run independently so Z1, Z2, and X can all move at
different speeds simultaneously.

---

## Stage 06 - Full Port

### What was built

Complete port of the ESP32 dual-axis stepper swap controller to STM32F446RE.
All five previous stages combined into one working application:

- TIM6 10 kHz ISR running PD control loop
- TIM2 OC generating step pulses on Z1, Z2, X simultaneously
- TIM1/3/4 reading quadrature encoders
- USART2 DMA circular buffer receiving commands
- Serial command parser handling Z, G, SEQ, R, STATUS

### Systematic debug process

When STATUS returned nothing, the fault was isolated step by step:

1. Comment out everything, send BOOT OK after init → confirmed UART TX works
2. Add encoders → ENC OK confirmed TIM1/3/4 start fine
3. Add TIM6 → TIM6 OK confirmed 10 kHz ISR starts fine
4. Add DMA RX → DMA OK confirmed circular buffer armed
5. Add TIM2 OC → TIM2 OK confirmed step pulse timer starts fine
6. Re-enable main loop → STATUS still not responding
7. Added raw byte echo inside handleSerial() → saw characters arriving
8. Conclusion: line ending missing from serial monitor - set to CR+LF

This pattern - add one piece at a time, print a message after each - is the
standard way to debug STM32 startup issues.

### Key fixes from original port

**1. HAL_TIM_OC_Start → HAL_TIM_OC_Start_IT**

Original code used HAL_TIM_OC_Start() which starts output compare but does
not enable the interrupt. Without the interrupt, setStepVelocity() schedules
one compare event but nothing ever advances it - motor would move one step
and stop. Fixed to HAL_TIM_OC_Start_IT().

**2. Missing HAL_TIM_OC_DelayElapsedCallback**

The OC callback that advances the compare register each tick was missing
entirely. Without it the step pulse train never self-sustains.

**3. Float printf support**

snprintf with %.2f produces empty output on ARM GCC by default - float
support is stripped to save flash. Fixed by adding to CMakeLists.txt:

```cmake
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -u_printf_float
)
```

**4. EN pins initialise HIGH**

TB6600 stepper drivers are enabled active LOW. CubeMX defaults GPIO outputs
to LOW at boot - drivers were enabled before any command was sent. Fixed by
setting EN pins initial output level to HIGH in CubeMX so drivers start
disabled.

**5. TIM2 OC mode → Toggle**

CubeMX defaulted TIM2 channels to TIMING mode which fires the interrupt
but does NOT toggle the pin. Changed to TOGGLE mode so hardware toggles
PA5/PB3/PB10 automatically.

### Architecture summary

```
USART2 DMA RX          TIM6 ISR (10 kHz)
     ↓                      ↓
handleSerial()         updateEncoders()
     ↓                      ↓
executeCommand()        runZ_ISR() / runX_ISR()
     ↓                      ↓
sets target            PD math → setStepVelocity()
                               ↓
                        TIM2 OC callback
                               ↓
                       toggles PA5/PB3/PB10
```

Main loop only handles: serial parsing, sequence runner, debug prints.
All timing-critical work happens in ISR context.

### What remains for hardware validation

- Connect TB6600 drivers and verify EN/DIR/PUL wiring
- Connect encoders to TIM1/3/4 input pins
- Send Z10 and verify motor moves to 10mm position
- Tune Kp/Kd if oscillation or overshoot observed
- Test SEQ command with full swap cycle