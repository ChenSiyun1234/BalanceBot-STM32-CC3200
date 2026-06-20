# BalanceBot-STM32

> A two-wheel **self-balancing robot** — cascaded-PID balance · Bluetooth remote · ultrasonic obstacle avoidance · live PID tuning · flash-saved config.

<!-- GitHub topics for search: self-balancing-robot, stm32, inverted-pendulum, pid-control, freertos, two-wheel-robot, embedded-c, obstacle-avoidance, robotics, cortex-m -->

Bare-metal embedded-C firmware for a two-wheel **inverted-pendulum robot** that balances itself, drives over Bluetooth, holds its position when pushed, and scans-and-turns around obstacles. Built on two MCU platforms, with companion **desktop and Android remote-control apps**. *(UC Davis EEC 172 final project.)*

![Language](https://img.shields.io/badge/Embedded-C-blue)
![MCU](https://img.shields.io/badge/MCU-STM32F103%20%C2%B7%20CC3200-green)
![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS-orange)
![Control](https://img.shields.io/badge/Control-Cascaded%20PID-red)
![Status](https://img.shields.io/badge/status-running%20on%20hardware-brightgreen)

A two-wheel cart keeps itself upright with a **cascaded three-loop PID controller** (angle + velocity + heading) running at **100 Hz inside the IMU's data-ready interrupt**, fed by **on-chip DMP sensor fusion**. It is remote-controlled over Bluetooth, returns to its start point when pushed, sweeps a fixed ultrasonic sensor to find the clearest heading around obstacles, tunes its own gains live, and persists them to internal flash.

---

## What it demonstrates

- **Real-time control systems** — cascaded angle-PD / velocity-PI / heading-PD control of an inverted pendulum, with integral anti-windup, **motor dead-zone compensation** (kills the limit-cycle wobble), output saturation, and fall protection.
- **Bare-metal + RTOS firmware** — register-level peripheral drivers (StdPeriph) under **FreeRTOS**; the safety-critical balance loop is **hardware-paced in an ISR**, fully decoupled from the task scheduler.
- **Sensor fusion** — MPU-6050 gyro/accel fused by the on-chip **DMP** (quaternion → pitch/yaw) over a software-I²C bus, driven by a data-ready interrupt.
- **Embedded systems debugging** — diagnosed and fixed a velocity-loop **positive-feedback runaway** (live sign-toggle), a motor **dead-zone limit cycle**, an int16 **PWM-mix overflow**, and added **read-back-verified** flash writes.
- **Hardware integration** — TB6612 H-bridge PWM, quadrature encoders, HC-05 Bluetooth (SPP), HC-SR04 ultrasonic (DWT-cycle-counter timing), SSD1306 OLED, and a 5 V-tolerant pin remap so 5 V encoder signals reach a 3.3 V MCU safely.
- **IoT / networking** *(CC3200 build)* — Wi-Fi + **AWS IoT over TLS** (Thing Shadow → IoT Rule → SNS) emails an alert on power-up and on a fall.

---

## Two implementations

| Build | MCU | Highlights | Status |
|-------|-----|-----------|--------|
| [**STM32**](STM32/README.md) — *flagship* | STM32F103C8T6 "Blue Pill" (Cortex-M3, 72 MHz) | Bluetooth remote, ultrasonic scan-and-turn avoidance, **live PID tuning**, flash-persisted config, dual OLED views | ✅ Balancing, remote & tuning verified on hardware |
| [**CC3200**](CC3200/README.md) | TI CC3200 (Cortex-M4 + Wi-Fi) | Kalman sensor fusion, colour OLED, **Wi-Fi / AWS-IoT** email alerts | ✅ Working |

Both run the same control law on the same TARKBOT R3T chassis; they differ in MCU, sensor-fusion method (DMP vs. Kalman), and feature focus (motion/UX vs. networking).

### ▶ STM32 build — *flagship*

Bluetooth remote · ultrasonic scan-and-turn avoidance · live PID tuning · flash-persisted config. **[Full details & build →](STM32/README.md)**

| Self-balancing + remote | OLED telemetry |
|:---:|:---:|
| <img src="STM32/images/stm32_balancing.gif" width="220" alt="STM32 cart self-balancing"> | <img src="STM32/images/stm32_oled.jpg" width="280" alt="STM32 OLED status"> |

<img src="STM32/images/schematic_stm32.png" width="900" alt="STM32 connection schematic — MCU wired to every module">

### ▶ CC3200 build

Kalman sensor fusion · colour OLED · **Wi-Fi / AWS-IoT** email alerts on power-up and fall. **[Full details & build →](CC3200/README.md)**

| Self-balancing | OLED telemetry |
|:---:|:---:|
| <img src="CC3200/images/cc3200_balancing.gif" width="220" alt="CC3200 cart self-balancing"> | <img src="CC3200/images/cc3200_oled.gif" width="220" alt="CC3200 OLED status"> |

<img src="CC3200/images/schematic_cc3200.png" width="900" alt="CC3200 connection schematic — MCU wired to every module">

---

## Remote-Control Apps (Desktop & Mobile)

Two companion apps drive the robot over its HC-05 Bluetooth link — rounding the project into a full **hardware → firmware → software** stack. Both speak the firmware's command set (`F`/`B`/`L`/`R`/`S` + a speed digit); hold a direction to drive, release to stop.

| Desktop controller — Python | Mobile app — Android / Kotlin |
|:---:|:---:|
| <img src="desktop-controller/docs/screenshot.png" width="300" alt="Python desktop remote controller"> | <img src="android-app/docs/screenshot.png" width="190" alt="Android remote-control app"> |
| Tkinter GUI + keyboard control · serial/Bluetooth I/O · hardware-free simulation mode. **[Details →](desktop-controller/README.md)** | Touch D-pad over classic-Bluetooth **SPP** · speed slider · runtime-permission handling. **[Details →](android-app/README.md)** |

---

## Control architecture

```
  MPU-6050 ──DMP──▶ pitch θ, rate ─────────▶ angle PD ───┐
  (accel+gyro)                                           │
                                                         ├─▶ Σ ─▶ dead-zone comp ─▶ ±3600 PWM ─▶ TB6612 ─▶ wheels
  encoders ──────▶ wheel speed ────────────▶ velocity PI ┤        (anti-overflow,
                                                         │         output clamp)
  gyro (yaw) ─────────────────────────────▶ heading PD ──┘
                                                              ▲
        the whole loop runs at 100 Hz inside the IMU ISR ─────┘
```

The **angle loop** keeps it upright; the **velocity loop** stops it and drives it back to where it started when pushed; the **heading loop** steers and powers the in-place rotation used to scan for obstacles. Remote and avoidance only set the `vx`/`vw` targets — the balance loop is never interrupted.

---

## Tech stack

`C` · STM32F103 StdPeriph · FreeRTOS V9 · Keil µVision 5 / Arm Compiler 5 · MPU-6050 DMP · TB6612FNG · HC-05 · HC-SR04 · SSD1306
*(CC3200 build: TI SimpleLink SDK · AWS IoT · TLS · Code Composer Studio)*

---

## Hardware · 器材

Two firmware builds on the **TARKBOT R3T** chassis; the core parts are shared. Full bilingual list → **[hardware/BOM.md](hardware/BOM.md)**.

| Part | 部件 | Role · 作用 |
|------|------|-------------|
| STM32F103C8T6 "Blue Pill" | STM32 最小系统板 | Main MCU (flagship) · 主控 |
| TB6612 / MD220A driver + power board | TB6612 驱动 / 电源板 | Motor driver **and the cart's power hub** · 电机驱动 + 整车电源中枢 |
| MC130 encoder gear-motors ×2 | MC130 编码电机 ×2 | Drive + wheel-speed feedback · 驱动 + 测速 |
| MPU-6050 (GY-521) | 六轴 IMU | Tilt / attitude sensing (DMP) · 姿态 |
| SSD1306 OLED · HC-05 · HC-SR04 | OLED · 蓝牙 · 超声波 | Display · remote · avoidance · 显示 / 遥控 / 避障 |
| 2S Li-ion ~7.4 V | 2S 锂电池 ~7.4 V | One battery → MD220A powers all · 单电池经 MD220A 供整车 |

→ **Full bill of materials** (specs · indicative prices · buy links): **[English](hardware/BOM.en.md)** · **[中文](hardware/BOM.md)**

---

## Repository

```
self-balancing-cart/
├── STM32/        flagship firmware — Keil project + WIRING + TUNING (中 / EN)
├── CC3200/       TI CCS firmware — Wi-Fi / AWS-IoT build
├── desktop-controller/  Python desktop remote app (Tkinter + Bluetooth)
├── android-app/         Android (Kotlin) remote app — Bluetooth SPP
├── hardware/     bill of materials — BOM.en.md (EN) + BOM.md (中)
├── tools/        diagram generators (pinout / schematic, Python)
└── README.md     (this file)
```

**Build docs:** **[STM32](STM32/README.md)** · **[CC3200](CC3200/README.md)** · **[Desktop app](desktop-controller/README.md)** · **[Android app](android-app/README.md)** · Bill of materials **[EN](hardware/BOM.en.md)** / **[中](hardware/BOM.md)**

**Guides (bilingual):**
| | English | 中文 |
|---|---|---|
| Wiring / hardware | **[WIRING.en.md](STM32/WIRING.en.md)** | **[WIRING.md](STM32/WIRING.md)** |
| PID tuning | **[TUNING.en.md](STM32/TUNING.en.md)** | **[TUNING.md](STM32/TUNING.md)** |

---

## Acknowledgements

Control structure and baseline gains adapted from the **XTARK / TARKBOT R3T** balancing firmware; CC3200 OLED driver from **Adafruit GFX**; networking from the **TI SimpleLink** examples.
