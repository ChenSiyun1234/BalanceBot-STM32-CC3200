# Bill of Materials — BalanceBot-STM32

English · 中文版见 **[BOM.md](BOM.md)**

Everything needed to build the cart. The two firmware builds (STM32 / CC3200) **share** the chassis, motors, driver/power board, IMU, and battery; they differ only in the MCU board, the display, and (STM32 only) the Bluetooth + ultrasonic modules.

> 💲 Prices are **indicative** (commodity parts; they vary by seller/region/time). Buy links are **search links** that land on the part by model — pick any reputable listing.
> 🛒 `淘宝` = Taobao (China) · `Ali` = AliExpress (international).

---

## Core — shared by both builds

| # | Part | Qty | Key specs & role | ≈ Price | Buy |
|---|------|:--:|------------------|:--:|-----|
| 1 | **TARKBOT R3T chassis** | 1 | Two-wheel self-balancing frame, **167 mm** wheelbase, acrylic decks + standoffs | ¥60–120 | [XTARK 淘宝](https://xtark.taobao.com) · [xtark.cn](http://www.xtark.cn) |
| 2 | **MC130 encoder gear-motors** | 2 | TT motor **~6 V**, 1:48 gear, **13-line Hall quadrature** encoder, Ø66 mm wheel, XH2.54-6P (M+/M-/5V/GND/A/B) | ¥40–70 /pr | [XTARK 淘宝](https://xtark.taobao.com) |
| 3 | **TK-TB6612-MD220A driver / power board** | 1 | TB6612FNG H-bridge + **RT8289 buck (5 V/5 A)** + **RT9013 LDO (3.3 V/500 mA)** — **the cart's power hub** | ¥30–50 | [XTARK 淘宝](https://xtark.taobao.com) · [xtark.cn](http://www.xtark.cn) |
| 4 | **MPU-6050 (GY-521 module)** | 1 | 3-axis accel + 3-axis gyro, **I²C 0x68**, on-chip DMP, on-board LDO + pull-ups | ¥6–12 | [淘宝](https://s.taobao.com/search?q=GY-521+MPU6050) · [Ali](https://www.aliexpress.com/wholesale?SearchText=GY-521+MPU6050) |
| 5 | **2S Li-ion battery ~7.4 V** | 1 | 2×18650 holder/pack; **6–7.4 V into MD220A** (= motor voltage) | ¥20–50 | [淘宝](https://s.taobao.com/search?q=2S+7.4V+18650+battery) · [Ali](https://www.aliexpress.com/wholesale?SearchText=2S+7.4V+18650+battery) |
| 6 | **Dupont jumper wires** | ~20 | Female-female, 10–20 cm | ¥5 | [淘宝](https://s.taobao.com/search?q=dupont+jumper+wire) · [Ali](https://www.aliexpress.com/wholesale?SearchText=dupont+jumper+wire) |
| 7 | **SWD programmer (ST-Link V2)** | 1 | SWD download/debug on **PA13/PA14** | ¥8–15 | [淘宝](https://s.taobao.com/search?q=ST-Link+V2) · [Ali](https://www.aliexpress.com/wholesale?SearchText=ST-Link+V2) |
| 8 | **USB-TTL adapter (CH340)** *(optional)* | 1 | View the debug serial / 6-channel tuning plot | ¥5–10 | [淘宝](https://s.taobao.com/search?q=USB+TTL+CH340) · [Ali](https://www.aliexpress.com/wholesale?SearchText=USB+TTL+CH340) |

> 💡 Items **1–3** (chassis + motors + driver/power board) usually ship together as the **TARKBOT R3T balancing-cart kit** — cheapest to buy as the kit.

---

## STM32 build (flagship)

| # | Part | Qty | Key specs & role | ≈ Price | Buy |
|---|------|:--:|------------------|:--:|-----|
| 9 | **STM32F103C8T6 "Blue Pill"** | 1 | Cortex-M3 @ 72 MHz, 64 KB flash / 20 KB RAM, LQFP48; on-board AMS1117-3.3 + PC13 LED + USB + 8 MHz/32.768 kHz crystals — **main MCU** | ¥8–15 | [淘宝](https://s.taobao.com/search?q=STM32F103C8T6) · [Ali](https://www.aliexpress.com/wholesale?SearchText=STM32F103C8T6) |
| 10 | **SSD1306 0.96″ OLED (I²C)** | 1 | 128×64 mono, 4-pin (VCC/GND/SCL/SDA) — status display | ¥8–15 | [淘宝](https://s.taobao.com/search?q=0.96+OLED+SSD1306+IIC) · [Ali](https://www.aliexpress.com/wholesale?SearchText=0.96+OLED+SSD1306+IIC) |
| 11 | **HC-05 Bluetooth** | 1 | SPP over UART, default 9600 baud / PIN 1234 — phone remote | ¥12–25 | [淘宝](https://s.taobao.com/search?q=HC-05+bluetooth) · [Ali](https://www.aliexpress.com/wholesale?SearchText=HC-05+bluetooth) |
| 12 | **HC-SR04 ultrasonic** | 1 | 2–400 cm range, 4-pin (VCC/TRIG/ECHO/GND) — obstacle avoidance | ¥3–8 | [淘宝](https://s.taobao.com/search?q=HC-SR04) · [Ali](https://www.aliexpress.com/wholesale?SearchText=HC-SR04) |

## CC3200 build

| # | Part | Qty | Key specs & role | ≈ Price | Buy |
|---|------|:--:|------------------|:--:|-----|
| 13 | **CC3200 LaunchPad (CC3200-LAUNCHXL)** | 1 | Cortex-M4 + on-chip Wi-Fi — main MCU (enables the AWS-IoT email alerts) | $40–60 | [TI](https://www.ti.com/tool/CC3200-LAUNCHXL) · [Mouser](https://www.mouser.com/c/?q=CC3200-LAUNCHXL) |
| 14 | **SSD1351 1.5″ colour OLED (SPI)** | 1 | 128×128 RGB, 7-pin 4-wire SPI — colour display | ¥30–50 | [淘宝](https://s.taobao.com/search?q=1.5+OLED+SSD1351) · [Ali](https://www.aliexpress.com/wholesale?SearchText=1.5+OLED+SSD1351) |

---

## Important notes

- ⚠️ **Battery voltage = motor VM.** The MD220A passes its input straight to the motors. MC130 is rated **~6 V**, so feed **6–7.4 V** — **never 12 V** (or lower `MOTOR_PWM_MAX` in firmware).
- ⚠️ **Blue Pill clones are everywhere** (CKS32 / APM32 / GD32 cores). Most work fine; flash often reads as 128 KB even on a "C8" (64 KB) part. If runtime flash-save (`W`) ever fails, suspect a clone's flash.
- ⚠️ **MPU-6050 must run at 3.3 V** (its INT goes to a non-5 V-tolerant pin). Power it from the MD220A 3.3 V.
- 💡 **One battery powers everything** — no separate BEC / 3.3 V regulator / level-shifter board. The MD220A's 5 V/3.3 V rails feed the MCU and all modules; tie all grounds to one star point.

## Power summary

Battery → **MD220A input (J1, 5.5–15 V)** → on-board buck/LDO → **5 V / 5 A + 3.3 V / 500 mA** on the "MCU interface" header → STM32 + every module.

→ Full wiring: **[WIRING.en.md](../STM32/WIRING.en.md)** (English) · **[WIRING.md](../STM32/WIRING.md)** (中文)
