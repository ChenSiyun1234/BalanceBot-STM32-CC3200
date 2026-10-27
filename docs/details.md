# BalanceBot details

The front page covers what the robot does and what I worked on. This page has the reference material: the two builds side by side, the control loop, the hardware and the repository layout.

## The two builds

| Build | MCU | What it runs |
|-------|-----|--------------|
| [STM32](../STM32/README.md) | STM32F103C8T6 "Blue Pill" (Cortex-M3, 72 MHz) | XTARK's cascaded controller on the MPU-6050's DMP output, Bluetooth driving and live gain tuning, gains saved in flash, two OLED views |
| [CC3200](../CC3200/README.md) | TI CC3200 (Cortex-M4 with Wi-Fi) | My angle PD loop on a Kalman tilt estimate, a color OLED, and a Wi-Fi alert to AWS IoT when the cart falls |

Both run on the same TARKBOT R3T chassis.

## STM32 control loop

The control law is XTARK's, used as is in `STM32/Robot/ax_balance.c`. I adapted the IMU axis and motor signs for this cart, added the dead-zone compensation and the 32-bit output mix, and capped the two outer loops.

```
  MPU-6050 --DMP--> pitch, pitch rate -------> angle PD ----+
  (accel + gyro)                                            |
                                                            +--> sum --> dead-zone comp --> +/-3600 PWM --> TB6612 --> wheels
  encoders -------> wheel speed -------------> speed PI ----+         (32-bit mix,
                                                            |          output clamp)
  gyro (yaw) --------------------------------> turn PD -----+

  The whole loop runs at 100 Hz inside the IMU's data-ready interrupt.
```

The angle loop keeps the cart upright. The speed loop brakes a pushed cart and pulls it back toward where it started. The turn loop steers; it's off by default and the `T` command turns it on. Remote commands and the ultrasonic avoidance routine only set the speed and turn targets. They never drive the motors directly.

The ultrasonic part is an HC-SR04 driver timed with the DWT cycle counter and a scan-and-turn state machine that steers through the turn loop.

## Tech stack

C, STM32F103 StdPeriph, FreeRTOS V9, Keil µVision 5 with Arm Compiler 5, MPU-6050 DMP, TB6612FNG, HC-05, HC-SR04 and SSD1306. The CC3200 build uses TI's SimpleLink SDK, TLS to AWS IoT and Code Composer Studio.

## Hardware

Both builds share the core parts. The full list with specs and links is in [hardware/BOM.en.md](../hardware/BOM.en.md).

| Part | Role |
|------|------|
| STM32F103C8T6 "Blue Pill" | Main MCU |
| TB6612 / MD220A driver and power board | Motor driver and the cart's power hub |
| MC130 encoder gear motors (2) | Drive and wheel-speed feedback |
| MPU-6050 (GY-521) | Tilt sensing with the DMP |
| SSD1306 OLED, HC-05, HC-SR04 | Display, Bluetooth link, ranging |
| 2S Li-ion, about 7.4 V | One battery; the MD220A powers everything |

The STM32 wiring diagram:

<img src="../STM32/images/schematic_stm32.png" width="900" alt="STM32 connection schematic">

The CC3200 wiring diagram:

<img src="../CC3200/images/schematic_cc3200.png" width="900" alt="CC3200 connection schematic">

## Repository layout

```
BalanceBot-STM32-CC3200/
  STM32/               STM32 firmware, Keil project, wiring and tuning guides
  CC3200/              CC3200 firmware for Code Composer Studio
  companion-app/       web dashboard, Python REST/SSE API, AWS templates
  android-app/         Android remote (Kotlin, Bluetooth SPP)
  desktop-controller/  Python desktop remote (Tkinter)
  hardware/            bill of materials
  tools/               Python scripts that draw the pinout and wiring diagrams
  docs/                this page
```

The web dashboard also has AWS SAM and Serverless templates for a cloud backend on Lambda and DynamoDB, and an optional adapter that reads telemetry from an AWS IoT Core topic over MQTT. The robot firmware itself only uses AWS IoT in the CC3200 build, where it posts its alert to a Thing Shadow over HTTPS.
