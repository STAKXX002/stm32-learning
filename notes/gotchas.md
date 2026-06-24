# STM32 Gotchas - Hard Lessons & Sanity Savers

A collection of things that will waste your time if you don't know them.
Learned the hard way so you don't have to.

---

## Toolchain

### Don't fall into the STM32 ecosystem rabbit hole

ST has approximately 47 different tools with "Cube" in the name:
- STM32CubeIDE
- STM32CubeMX
- STM32CubeProgrammer
- STM32CubeMonitor
- STM32Cube[YourAxisHere]

You need exactly **two** of these: **CubeMX** (config + code gen) and
**VS Code + STM32 extension** (build + flash + debug). That's it.
CubeIDE is Eclipse in a trenchcoat. CubeProgrammer is only needed if
you brick the board. Ignore everything else.

### The STM32 extension has a blink button - use it first

Before spending 20 minutes wondering why your flash isn't working,
click the blink button next to your device in the STM32 panel. If the
ST-Link LED blinks, the extension found your board. If nothing happens,
your problem is USB, not code.

### CMake not STM32CubeIDE format

When CubeMX asks for toolchain, always pick **CMake**. The STM32CubeIDE
format works only inside CubeIDE. CMake works everywhere - VS Code,
command line, CI. Future you will thank present you.

---

## Hardware

### Check your header pins before you blame the code

Improperly soldered or bent header pins on the Nucleo can short adjacent
pins together. If your board isn't turning on or behaves erratically
before you've written a single line of code, grab a multimeter and check
voltage at 3.3V and GND pins first. A short will pull the rail down and
the board will just... not work. This will feel like a firmware problem.
It is not a firmware problem.

### The EN pin trap - active LOW means HIGH to disable

TB6600 and most stepper drivers enable on LOW. CubeMX initialises all
GPIO outputs to LOW by default. This means your stepper drivers are
**on at boot** before your code does anything useful. Always set EN
pins initial output level to HIGH in CubeMX so drivers start disabled
and your code explicitly enables them when needed.

### PA5 is the onboard LED AND TIM2 CH1

PA5 is the Nucleo's green LED. PA5 is also TIM2 Channel 1.
If you use TIM2 CH1 for step pulses, the LED will flicker at your step
frequency - which is actually a useful debug indicator. Just don't
confuse "LED is doing something weird" with "my code is broken". It's
probably just TIM2 doing its job.

---

## Timers

### TIM2 OC default mode is TIMING not TOGGLE

CubeMX sets Output Compare mode to `TIMING` by default. TIMING fires
the interrupt but **does not touch the pin**. You need `TOGGLE` to
actually toggle the step pin. Your code will compile, flash, and appear
to run perfectly - the motor just won't move. Check this first if
step generation seems dead.

### HAL_TIM_OC_Start vs HAL_TIM_OC_Start_IT

`HAL_TIM_OC_Start()` - starts the timer, no interrupt.
`HAL_TIM_OC_Start_IT()` - starts the timer, enables interrupt.

Without `_IT`, your OC callback never fires, the compare register never
advances, and the motor moves exactly one step and stops. This is an
easy one to miss and an annoying one to debug.

### APB timer clocks are doubled - know your actual clock

Your timer clock is NOT always the same as PCLK. When the APB divider
is greater than 1, the timer clock is doubled:

```
APB1 = 45 MHz  →  TIM2/3/4/6 clock = 90 MHz
APB2 = 90 MHz  →  TIM1 clock = 180 MHz
```

Getting this wrong means your ISR fires at the wrong rate, your step
intervals are off by 2x, and everything is subtly broken in a way
that's very hard to spot.

---

## UART & DMA

### Serial monitor line endings - always set CR+LF

If you send a command and get no response, the first thing to check is
not your code. Check your serial monitor's line ending setting. Most
default to "No line ending". Your STM32 is waiting for `\r` or `\n`
that never arrives. Set it to **CR+LF** and 90% of "serial isn't
working" problems disappear.

### Never call HAL_UART_Transmit inside an ISR

It works. Until it doesn't. Blocking transmit inside a 10 kHz ISR
will either corrupt your DMA buffer, block the ISR for too long, or
both. The correct pattern:

```c
// In ISR - set a flag
volatile bool debugPrint = true;

// In main loop - do the actual print
if (debugPrint) {
    debugPrint = false;
    HAL_UART_Transmit(...);
}
```

### Float printf is disabled by default

`snprintf("%.2f", value)` produces an empty string on ARM GCC unless
you explicitly enable float support. Add this to CMakeLists.txt:

```cmake
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE -u_printf_float)
```

This catches everyone once. Usually at the worst possible moment.

---

## Debugging

### When in doubt, binary search your startup

If something doesn't work, don't stare at the code. Comment out
everything and add it back one piece at a time, printing a message
after each addition:

```c
HAL_UART_Transmit(..., "ENC OK\r\n", ...);   // encoders work
HAL_UART_Transmit(..., "TIM6 OK\r\n", ...);  // ISR starts
HAL_UART_Transmit(..., "DMA OK\r\n", ...);   // DMA armed
```

The message that doesn't appear tells you exactly where it broke.
This sounds tedious. It is 10x faster than guessing.

### Live watch doesn't update while running over SWD

In VS Code debugger, watch variables don't update while the CPU is
running - you have to pause execution to read them. This is normal
SWD behaviour, not a bug. If you want live updates without pausing
you need ITM/SWO - a separate debug output channel worth setting up
for serious debugging sessions.

### A HardFault that resets looks like a blinking LED

If your board seems to boot, flash the LED briefly, then reset and
repeat - that's almost certainly a HardFault landing in Error_Handler
which loops forever until the watchdog resets it. Add this to catch it:

```c
void HardFault_Handler(void) {
    while(1) {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        for(volatile int i = 0; i < 500000; i++);
    }
}
```

Fast rapid blinking = HardFault confirmed. Now you know what you're
dealing with.

### volatile is not optional for ISR-shared variables

Any variable written in an ISR and read in main (or vice versa) must
be declared `volatile`. Without it the compiler optimises the read away
and your main loop never sees the updated value. This produces bugs that
appear and disappear depending on optimisation level - extremely
confusing to debug.

---

## CubeMX

### USER CODE blocks are sacred - never put code outside them

CubeMX regenerates everything outside `/* USER CODE BEGIN */` /
`/* USER CODE END */` blocks every time you change the .ioc and
regenerate. Anything outside these blocks will be silently deleted.
This includes function implementations, includes, and defines.

### Regenerate → check your OC mode didn't reset

CubeMX sometimes resets Output Compare mode back to TIMING when you
regenerate. After any regeneration, double check:
- TIM2 CH1/2/3 OC mode is still TOGGLE
- EN pin initial levels are still HIGH
- NVIC interrupts are still enabled

Takes 30 seconds to check. Saves 30 minutes of confusion.