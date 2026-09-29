# Self-Balancing Cart — STM32 Firmware

Self-balancing two-wheel robot on a generic **STM32F103C8T6 "Blue Pill"** with hand-wired discrete modules (MPU-6050 IMU, TB6612 motor/power board, dual encoders, HC-05 Bluetooth, HC-SR04 ultrasonic, SSD1306 OLED).

The balance controller comes from **XTARK**, the vendor of the TARKBOT R3T chassis. Its cascaded loop runs in the **MPU-6050 data-ready interrupt at 100 Hz** on the on-chip **DMP** output, so the cart stays upright independent of the RTOS scheduler. I ported the vendor's firmware from its OpenCTR board to the Blue Pill. Around the vendor's control law I wrote the TB6612 motor driver and the Bluetooth command set. I also rewrote the flash save and fixed the problems listed under [Engineering notes](#engineering-notes).

| | |
|---|---|
| **MCU** | STM32F103C8T6 (Cortex-M3, 72 MHz, 64 KB flash / 20 KB RAM) |
| **Toolchain** | Keil MDK µVision 5 · Arm Compiler 5 (V5.06) |
| **Library / RTOS** | STM32F10x StdPeriph + FreeRTOS V9 (heap_4, 8 KB heap) |
| **Status** | Running on hardware — balancing, remote drive, live tuning & flash config verified |

---

## Demo

| Self-balancing + Bluetooth remote | OLED telemetry |
|:---:|:---:|
| <img src="images/stm32_balancing.gif" width="240" alt="STM32 cart self-balancing"> | <img src="images/stm32_oled.jpg" width="320" alt="STM32 OLED status display"> |

---

## Features

- **Cascaded balance control (XTARK's)** — inner **angle PD** (tilt + gyro) + outer **velocity PI** (encoders) + **turn PD** (yaw rate), summed into the motor command at **100 Hz** inside `EXTI9_5_IRQHandler` (`Robot/ax_balance.c`). The law is the vendor's, verbatim. I adapted the IMU axis and motor signs for this cart and tuned the gains by hand: `Kp 950 / Kd 50`, `vKp 4600 / vKi 4400`.
- **Motor dead-zone compensation** — my addition. It adds the wheels' minimum-turn PWM to every non-zero command so small corrections actually move the cart, eliminating the static-friction **limit-cycle wobble** that no Kp/Kd value can remove.
- **Live PID tuning over Bluetooth** — the vendor's firmware tuned gains over a framed protocol; I replaced it with single-character commands. Select a gain (`1`–`5`) and step it with `+`/`-`; flip the velocity sign live (`Y`). A 6-channel plot streams on USART1. No recompile.
- **Flash-persisted config** — I rewrote the vendor's flash save. `W` saves the gains, dead zone, midpoint and velocity sign to the last flash page, with the valid marker written last. A **read-back check** shows `S` (saved) or `X` (failed) on the OLED, and `L` when a saved config loaded at boot. The midpoint always boots at 0, so a stale saved value can't break startup. `Z` erases.
- **Bluetooth remote** — HC-05 SPP, single-character commands; drive `F/B/L/R/S` only set `vx`/`vw` targets while the balance loop keeps the cart upright. `L`/`R` steer through the turn loop, which `T` switches on.
- **Ultrasonic obstacle avoidance** — my HC-SR04 driver times the echo with the DWT cycle counter, and the sensor is read about 20 times a second. Forward drive is always blocked under 20 cm. `O` enables a **scan-and-turn state machine** (`Robot/ax_avoid.c`): under 25 cm it stops the cart and commands an in-place turn to sweep the fixed sensor ±60°, then turns toward the clearest heading. It steers through the turn loop, which is off by default (`T` switches it on), and only sets the drive targets.
- **Self-recovery & safety** — the vendor's fall protection cuts the motors past ±45°, and `Balance_PutDown()` re-arms when the cart is set upright and nudged; I made both measure from the balance midpoint. The velocity integral is clamped. Output caps on each loop and an int32 PWM mix guard against runaway and overflow.
- **OLED telemetry** — SSD1306, two views (`D` toggles): **RUN** (avoidance state / distance / drive targets / angle) and **TUNE** (live gains).

---

## Bluetooth command set

HC-05 on **USART2 @ 9600** (transparent SPP). Send single characters from any BT serial app (*Serial Bluetooth Terminal* on Android works well — map each letter to a macro button). Parsed in `Robot/ax_control.c`. These 21 commands replace the vendor's framed Bluetooth protocol.

| Group | Keys |
|-------|------|
| **Drive** | `F` fwd · `B` back · `L`/`R` turn · `S` stop · `O` toggle avoidance |
| **Loops/view** | `V` velocity loop · `T` turn loop · `Y` flip velocity sign · `C` auto-midpoint · `D` OLED RUN/TUNE view |
| **Tune** | `1`–`5` select Kp / Kd / vKp / vKi / dead-zone · `+`/`-` step · `M` capture midpoint |
| **Save** | `W` save all to flash (verified) · `Z` erase saved config |

Full tuning procedure: **[TUNING.en.md](TUNING.en.md)** (English) · **[TUNING.md](TUNING.md)** (中文).

---

## Wiring

<img src="images/schematic_stm32.png" width="920" alt="STM32 connection schematic — MCU pins wired to every module, with power rails">

<sub>↑ full connection schematic · also: a [pin-out card](images/pinout_stm32.png) showing every Blue-Pill header pin.</sub>

**Key signals** — summary only; wire from the full guide **[WIRING.en.md](WIRING.en.md)** (English) · **[WIRING.md](WIRING.md)** (中文), which has the full table, power-rail plan, and 5 V-tolerance cautions:

| Function | Peripheral | STM32 pin(s) |
|----------|-----------|--------------|
| Motor PWM A / B | TIM1_CH1 / CH4 | PA8 / PA11 |
| Motor dir + STBY | GPIO | AIN/BIN PB12–PB15, STBY PA4 |
| Encoders L / R | TIM2 / TIM3 (remap) | PA15·PB3 / PB4·PB5 |
| MPU-6050 | soft-I²C + INT | SCL PB10, SDA PB11, **INT PA5** |
| OLED (SSD1306) | soft-I²C | SCL PB8, SDA PB9 |
| HC-SR04 | GPIO (DWT timing) | TRIG PB0, ECHO PA12 |
| HC-05 | USART2 | PA2 / PA3 |
| Heartbeat LED | GPIO | PC13 |

**Notable**: the encoder pins are the vendor's choice. TIM2/TIM3 are remapped onto **5 V-tolerant** pins (the default PA0/1/6/7 are not), letting the TB6612 board's 5 V Hall signals wire straight to the 3.3 V MCU. JTAG is disabled (SWD kept) to free those pins.

---

## Build & flash

1. Open `Project/xproject.uvprojx` in **Keil µVision 5** (device `STM32F103CB`; install the `Keil.STM32F1xx_DFP` pack if prompted).
2. **Build** (`F7`).
3. Flash over **SWD** (PA13/PA14) — ST-Link or CMSIS-DAP. *(Lower the SWD clock to ~1 MHz if programming fails on long/clone wiring.)*

Build symbols `USE_STDPERIPH_DRIVER, STM32F10X_MD`; startup `startup_stm32f10x_md.s`.

---

## Engineering notes

A few problems I found on this cart and fixed in firmware:

- **Velocity-loop positive feedback** — a pushed cart sped up in the push direction instead of braking. The 6-channel plot showed a sign error in the velocity loop. I added a runtime **sign toggle** (`Y`, saved by `W`) so the braking direction is set in one keypress instead of guess-and-reflash.
- **Dead-zone limit cycle** — a slow wobble that no Kp/Kd could remove; root cause was the motor dead-band. Fixed with **dead-zone compensation** on the final PWM (tunable, default 120).
- **int16 PWM overflow** — at large tilt the `angle + velocity + turn` mix could wrap and spike the motors full-reverse; hardened by computing the mix in **int32** and clamping to the rail.
- **Silent flash-save failures** — added a **read-back check** after every save. It reads back the valid marker and the first gain, and the OLED shows `X` when the write did not stick.
- **Cold-boot reliability** — the MPU/DMP needs the 3.3 V rail settled first. `main()` now waits 1.5 s before the IMU init, and the DMP init retries `mpu_init()` up to 10 times, 100 ms apart. The motors stay disabled until the DMP runs.

The split is the vendor's: the balance loop runs in the IMU interrupt (`ax_balance.c`), and FreeRTOS tasks (`ax_task.c`) do the slower work. The 6-channel plot and the two OLED views in those tasks are mine. All gains live in `Robot/ax_robot.c`.

---

*Sibling build: the [CC3200 version](../CC3200/README.md) is my EEC 172 final project. It balances with an angle PD loop on a Kalman tilt estimate and posts Wi-Fi alerts to AWS IoT.*
