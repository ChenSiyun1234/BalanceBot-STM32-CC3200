# 器材清单（BOM）— BalanceBot-STM32

中文 · English version see **[BOM.en.md](BOM.en.md)**

搭建小车所需全部器材。两个固件版本（STM32 / CC3200）**共用**底盘、电机、驱动/电源板、IMU 和电池，区别只在**主控板、显示屏**，以及（仅 STM32）**蓝牙 + 超声波**模块。

> 💲 价格为**参考值**（通用元件，随卖家/地区/时间浮动）。购买链接为**按型号搜索的链接**，挑信誉好的店铺即可。
> 🛒 `淘宝` = 淘宝（国内）· `Ali` = 速卖通 AliExpress（海外）。

---

## 核心件 — 两版本共用

| # | 部件 | 数量 | 关键规格 / 作用 | 参考价 | 购买 |
|---|------|:--:|------------------|:--:|-----|
| 1 | **TARKBOT R3T 底盘** | 1 | 两轮自平衡车架，轴距 **167 mm**，亚克力层板 + 铜柱 | ¥60–120 | [塔克淘宝](https://xtark.taobao.com) · [xtark.cn](http://www.xtark.cn) |
| 2 | **MC130 编码器减速电机** | 2 | TT 电机 **约 6 V**，1:48 减速，**13 线霍尔正交**编码，Ø66 mm 轮，XH2.54-6P（M+/M-/5V/GND/A/B） | ¥40–70/对 | [塔克淘宝](https://xtark.taobao.com) |
| 3 | **TK-TB6612-MD220A 驱动/电源板** | 1 | TB6612FNG 驱动 + **RT8289 降压（5 V/5 A）** + **RT9013 LDO（3.3 V/500 mA）**——**整车电源中枢** | ¥30–50 | [塔克淘宝](https://xtark.taobao.com) · [xtark.cn](http://www.xtark.cn) |
| 4 | **MPU-6050（GY-521 模块）** | 1 | 三轴加速度 + 三轴陀螺，**I²C 0x68**，内置 DMP，板载 LDO + 上拉 | ¥6–12 | [淘宝](https://s.taobao.com/search?q=GY-521+MPU6050) · [Ali](https://www.aliexpress.com/wholesale?SearchText=GY-521+MPU6050) |
| 5 | **2S 锂电池 约 7.4 V** | 1 | 2×18650 电池盒/电池组；**6–7.4 V 接入 MD220A**（即电机电压） | ¥20–50 | [淘宝](https://s.taobao.com/search?q=2S+7.4V+18650+battery) · [Ali](https://www.aliexpress.com/wholesale?SearchText=2S+7.4V+18650+battery) |
| 6 | **杜邦线** | ~20 | 母对母，10–20 cm | ¥5 | [淘宝](https://s.taobao.com/search?q=dupont+jumper+wire) · [Ali](https://www.aliexpress.com/wholesale?SearchText=dupont+jumper+wire) |
| 7 | **SWD 烧录器（ST-Link V2）** | 1 | SWD 下载/调试，接 **PA13/PA14** | ¥8–15 | [淘宝](https://s.taobao.com/search?q=ST-Link+V2) · [Ali](https://www.aliexpress.com/wholesale?SearchText=ST-Link+V2) |
| 8 | **USB 转 TTL（CH340）**（可选） | 1 | 查看调试串口 / 6 通道调参波形 | ¥5–10 | [淘宝](https://s.taobao.com/search?q=USB+TTL+CH340) · [Ali](https://www.aliexpress.com/wholesale?SearchText=USB+TTL+CH340) |

> 💡 第 **1–3** 项（底盘 + 电机 + 驱动/电源板）通常作为 **TARKBOT R3T 平衡小车套件**一起出售——按套件买最划算。

---

## STM32 版（旗舰）

| # | 部件 | 数量 | 关键规格 / 作用 | 参考价 | 购买 |
|---|------|:--:|------------------|:--:|-----|
| 9 | **STM32F103C8T6 最小系统板（Blue Pill）** | 1 | Cortex-M3 @ 72 MHz，64 KB Flash / 20 KB RAM，LQFP48；板载 AMS1117-3.3 + PC13 LED + USB + 8 MHz/32.768 kHz 晶振——**主控** | ¥8–15 | [淘宝](https://s.taobao.com/search?q=STM32F103C8T6) · [Ali](https://www.aliexpress.com/wholesale?SearchText=STM32F103C8T6) |
| 10 | **SSD1306 0.96″ OLED（I²C）** | 1 | 128×64 单色，4 针（VCC/GND/SCL/SDA）——状态显示 | ¥8–15 | [淘宝](https://s.taobao.com/search?q=0.96+OLED+SSD1306+IIC) · [Ali](https://www.aliexpress.com/wholesale?SearchText=0.96+OLED+SSD1306+IIC) |
| 11 | **HC-05 蓝牙模块** | 1 | SPP 透传，默认 9600 波特 / 配对码 1234——手机遥控 | ¥12–25 | [淘宝](https://s.taobao.com/search?q=HC-05+bluetooth) · [Ali](https://www.aliexpress.com/wholesale?SearchText=HC-05+bluetooth) |
| 12 | **HC-SR04 超声波模块** | 1 | 2–400 cm，4 针（VCC/TRIG/ECHO/GND）——避障测距 | ¥3–8 | [淘宝](https://s.taobao.com/search?q=HC-SR04) · [Ali](https://www.aliexpress.com/wholesale?SearchText=HC-SR04) |

## CC3200 版

| # | 部件 | 数量 | 关键规格 / 作用 | 参考价 | 购买 |
|---|------|:--:|------------------|:--:|-----|
| 13 | **CC3200 LaunchPad（CC3200-LAUNCHXL）** | 1 | Cortex-M4 + 片上 Wi-Fi——主控（用于 AWS-IoT 邮件告警） | $40–60 | [TI 官网](https://www.ti.com/tool/CC3200-LAUNCHXL) · [Mouser](https://www.mouser.com/c/?q=CC3200-LAUNCHXL) |
| 14 | **SSD1351 1.5″ 彩色 OLED（SPI）** | 1 | 128×128 RGB，7 针 4 线 SPI——彩色显示 | ¥30–50 | [淘宝](https://s.taobao.com/search?q=1.5+OLED+SSD1351) · [Ali](https://www.aliexpress.com/wholesale?SearchText=1.5+OLED+SSD1351) |

---

## 重要提示

- ⚠️ **电池电压 = 电机电压 VM。** MD220A 把输入电压直接给电机；MC130 额定 **约 6 V**，所以喂 **6–7.4 V**，**绝不要 12 V**（或在固件里调低 `MOTOR_PWM_MAX`）。
- ⚠️ **Blue Pill 山寨板很多**（CKS32 / APM32 / GD32 内核）。多数能用；即便是"C8"（64 KB）也常被读成 128 KB。若运行时 Flash 存档（`W`）失败，先怀疑山寨芯片的 Flash。
- ⚠️ **MPU-6050 必须 3.3 V 供电**（其 INT 接到非 5V 容忍引脚）。用 MD220A 的 3.3 V 给它供电。
- 💡 **单电池供整车**——无需额外 BEC / 3.3 V 稳压 / 电平转换板。MD220A 的 5 V / 3.3 V 轨给主控和所有模块供电；所有地接到同一星型公共地。

## 供电小结

电池 → **MD220A 输入（J1，5.5–15 V）** → 板上降压/LDO → **5 V/5 A + 3.3 V/500 mA**（「单片机接口」排针）→ STM32 + 各模块。

→ 完整接线：**[WIRING.md](../STM32/WIRING.md)**（中文）· **[WIRING.en.md](../STM32/WIRING.en.md)**（English）
