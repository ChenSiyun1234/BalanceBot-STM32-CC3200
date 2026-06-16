# STM32F103C8T6 Self-Balancing Cart — Complete PID Tuning Guide

English — see [中文](TUNING.md) for the Chinese version.

> Applies to the firmware in `self-balancing-cart/STM32/Robot/`: `ax_balance.c` / `ax_robot.c` / `ax_control.c` / `ax_task.c`
> Platform: STM32F103C8T6 ("Blue Pill", Keil device STM32F103CB) + MPU6050 (DMP) + TB6612 motor driver + dual encoders + HC-05 Bluetooth + CH340 USB-serial + OLED. StdPeriph + FreeRTOS, Arm Compiler 5.
> This guide explains the *why*, not just the steps. Every conclusion maps to a real line of firmware.

---

## Table of Contents

1. [Theory: what a self-balancing cart is + the cascaded multi-loop control](#1-theory-what-a-self-balancing-cart-is--the-cascaded-multi-loop-control)
2. [Mechanical midpoint](#2-mechanical-midpoint)
3. [Tuning setup](#3-tuning-setup)
4. [Step-by-step tuning](#4-step-by-step-tuning)
5. [Reading the waveform / reading the OLED](#5-reading-the-waveform--reading-the-oled)
6. [Symptom-to-gain troubleshooting table](#6-symptom-to-gain-troubleshooting-table)
7. [Recording values + flash config for next time](#7-recording-values--flash-config-for-next-time)
8. [Notes on the Kalman filter](#8-notes-on-the-kalman-filter)
9. [On auto-tuning](#9-on-auto-tuning)

---

## 1. Theory: what a self-balancing cart is + the cascaded multi-loop control

### 1.1 The inverted pendulum

A self-balancing cart is fundamentally an **inverted pendulum**: its center of mass sits **above** the wheel axle, so it is inherently unstable — the slightest deviation from vertical lets gravity produce an **ever-growing** toppling torque, and it falls faster and faster. Unlike an ordinary pendulum, it will not return to vertical on its own; it can only stand up **through active control**.

The core idea is simple: **whichever way the cart leans, drive the wheels that way to move the pivot (the wheels) back under the center of mass and catch it again.** Just like balancing a broom on your palm — as the broom tips forward, your hand moves forward.

Doing this well takes more than just "catching the fall." A cart that stands stably for a long time *and* obeys drive/turn commands needs **several** control loops working together. This firmware uses a **cascaded** structure, running at **100 Hz**.

### 1.2 Where the control loop runs, and how fast

Balance control does **not** run inside a FreeRTOS task — it runs inside the **MPU6050 data-ready interrupt**. The MPU6050's DMP raises an interrupt on PA5 every 10 ms (100 Hz) (EXTI Line 5 / `EXTI9_5_IRQHandler()`, the handler tests `EXTI_GetITStatus(EXTI_Line5)`). The benefit: **no matter how the RTOS schedules tasks, the balance loop runs rock-solid at a hardware-timed 100 Hz.**

What happens on each interrupt:

1. Read the IMU via the DMP:
   - `ax_balance_angle = ax_angle_data[1]` — **PITCH** (the fore/aft lean about the Y axis), in units of 0.01°.
   - `ax_balance_gyro = ax_gyro_data[1]` — raw Y-axis angular rate.
   - `ax_turn_gyro = ax_gyro_data[2]` — raw Z-axis (yaw) angular rate.
2. Convert to engineering units: `balance_angle` (deg), `balance_gyro` (deg/s, ×0.061), `turn_gyro` (deg/s).
3. Read the encoders to compute left/right wheel speeds (m/s; note this cart **reads negative when physically moving forward** — that is the sign the velocity loop expects, see Phase 2), then clear the counters.
4. Run **all the loops**, mix them into left/right PWM, and drive the motors (detailed below).

> Note: the axis choice (using PITCH `[1]`) is one of the few changes this cart makes relative to the vendor's OpenCTR original (which used `[0]`), because this cart's fore/aft lean is about the Y axis.

### 1.3 What each loop is

#### Loop 1: the ANGLE loop = PD — keeps the cart upright

**What it measures**: the lean angle `balance_angle` (from DMP pitch) and the lean rate `balance_gyro` (from the gyro Y axis).
**What it outputs**: `ax_balance_out`, a base PWM that makes the cart "chase the way it's falling and catch itself."
**Why you need it**: this is the most central loop and the first one to get right. Without it the cart falls instantly.

Code (`Balance_AngleCtl()`):

```c
angle_bias = target - angle;        // target = ax_middle_angle (the midpoint): "how far off the balance point are we"
gyro_bias  = 0 - gyro;              // desired angular rate is 0 (we don't want it rotating)
p_out = ax_balance_kp * angle_bias * 0.1f;   // P: the bigger the error, the harder the correction
d_out = ax_balance_kd * gyro_bias  * 0.1f;   // D: the faster it falls, the harder it brakes (damping)
out = p_out + d_out;
```

- **`ax_balance_kp` (balance Kp)**: the proportional gain. It sets "for every degree off the midpoint, how hard do we correct." Too small and the cart can't hold itself up and slowly topples; too large and it overreacts, lunging back and forth, breaking into **high-frequency chatter / oscillation**.
- **`ax_balance_kd` (balance Kd)**: the derivative gain, acting on **angular rate** (note `0 - gyro`). It is **damping**: the moment the cart starts to fall or swing back quickly, the D term pushes the other way and "hits the brakes," suppressing overshoot and oscillation. Too small and you get overshoot and a low-frequency sway; too large and it amplifies sensor noise, introducing **high-frequency jitter / a buzzing motor**.

> This is a **PD** loop: P and D only, **no I**. The angle loop deliberately omits the integral (an integral would fixate on midpoint error and tends to overshoot/diverge). Killing the "stands still but with a constant small lean" steady-state error is the job of the **velocity loop** and the **auto-midpoint**, below.

#### Loop 2: the VELOCITY loop = PI — keeps the cart from drifting away, lets it stop and drive

**What it measures**: the average wheel speed `wheel_vel` (from the encoders).
**What it outputs**: `ax_velocity_out`, added on top of the angle-loop output to **hold the cart at a target speed (default 0 = hold position)**.
**Why you need it**: with only the angle loop, the cart won't fall but it will **slowly drift in some direction and never stop** — because "standing up straight" and "staying put" are two different things. A slightly sloped floor or a slightly-off midpoint and it drifts forever. The velocity loop's job is: **detect that the cart is moving, gently shift its target lean, and let it brake itself back to the start.** It also executes the forward/back drive commands (using `ax_robot_vx` as the target speed).

Code (`Balance_VelocityCtl()`):

```c
if (t_vx > 700) t_vx = 700; else if (t_vx < -700) t_vx = -700;   // target-speed clamp ±700 mm/s

velocity_bias_update = 0 - r_vx;
velocity_bias = (velocity_bias * 0.6f) + (velocity_bias_update * 0.4f);   // low-pass 0.6/0.4

velocity_integral += velocity_bias * 0.01f;     // integrate the speed error (one 10 ms step)
velocity_target    = t_vx * 0.001f * 0.01f;
velocity_integral += velocity_target;           // feed the target speed into the integral too (fwd/back)

if (velocity_integral >  1.5f) velocity_integral =  1.5f;        // integral clamp ±1.5 (anti-windup)
else if (velocity_integral < -1.5f) velocity_integral = -1.5f;

p_out = -ax_velocity_kp * velocity_bias;        // note the minus sign
i_out = -ax_velocity_ki * velocity_integral;    // note the minus sign
out = (p_out + i_out) * ax_vel_sign;            // overall sign flipped live by 'Y'
```

- **`ax_velocity_kp` (velocity Kp)**: the proportional gain, reacting to "current speed." As soon as the cart moves, the P term creates a counter-correcting tendency that starts pulling it back. **Note**: the velocity loop does not directly "command a lean angle" — it outputs PWM that is added onto the angle-loop output (see below), **indirectly** shifting the cart's effective balance point / equilibrium lean so it comes back. It never commands an angle directly. Too small and it can't brake — visible drift; too large and the cart **shuffles back and forth in place (a low-frequency oscillation)**.
- **`ax_velocity_ki` (velocity Ki)**: the integral gain, reacting to "accumulated speed error." It kills **long-term drift** and **steady-state position error** — even a tiny persistent slide accumulates in the integral and eventually drags the cart back and **pins it in place**. Too small and the cart slowly floats away and won't settle; too large and you get a **slow back-and-forth oscillation** and overshoot on return.

> ### Important: the velocity loop is **PI** — there is **no velocity Kd** (no "vKd")
> There is no velocity-derivative gain in this firmware. Only `ax_velocity_kp` and `ax_velocity_ki` are declared; the tunable-gain array holds `{ balance_kp, balance_kd, velocity_kp, velocity_ki, deadzone }`, and Bluetooth `3` / `4` select **velocity Kp / velocity Ki**. The velocity loop deliberately uses no D: the wheel-speed signal is noisy, and differentiating it would amplify the noise into jitter; instead a low-pass (0.6/0.4) **smooths** the speed. So the velocity loop has just two numbers to tune: **vKp, vKi**.

Why both P and I carry a minus sign and why the target speed is fed into the integral: the velocity loop doesn't drive the motors directly — it influences speed **by shifting the cart's effective balance point**. To go forward, the cart must **lean forward a little first**; the minus sign plus the cascaded mix realizes the chain "want to go forward → create a forward lean → angle loop chases that lean → cart tips forward → moves forward." The overall sign is multiplied by `ax_vel_sign` so the brake-vs-assist polarity can be flipped live (see `Y`).

#### Loop 3: the TURN loop = PD — controls left/right turning, holds a straight line

**What it measures**: the yaw rate `turn_gyro` (gyro Z axis).
**What it outputs**: `ax_turn_out`, an **accumulating** differential term, added to one wheel and subtracted from the other to steer.
**Why you need it**: so the cart obeys `L`/`R` turn commands, and so it holds a straight line and doesn't wander off when not turning.

Code (`Balance_TurnCtl()`):

```c
bias  = t_vw - r_vw;                                  // target yaw rate - actual yaw rate
p_out = ax_turn_kp * bias * 0.001f;
d_out = ax_turn_kd * (bias - bias_last) * 0.001f;
out  += p_out + d_out;                                // accumulating output
bias_last = bias;
```

- **`ax_turn_kp` (turn Kp)**: the steering proportional, setting how quickly it responds to a turn command.
- **`ax_turn_kd` (turn Kd)**: the steering derivative/damping, suppressing overshoot and head-wag during a turn.

This is a **PD** loop (no I). Its output is `out += ...` **accumulating**: each cycle it adds the yaw **rate error** (`bias = t_vw - r_vw`) into `out`. That is effectively an integral of the yaw-rate error, giving a **heading-like** control — but note it is **not** holding a true absolute heading: it accumulates the **rate error**, not an integrated true heading; the D term is the difference of two adjacent samples (which telescopes/cancels); and **the instant the loop is disabled (`ax_robot_move_enable == 0`) `out` is cleared immediately**. So think of it as "when no turn command is given, hold down the accumulated yaw-rate error to help go straight; when `L`/`R` is given, turn at the requested rate," rather than a heading-lock that remembers and returns to some absolute bearing.

### 1.4 How the loops mix → motors

After the loops are computed they are mixed:

```c
wheel_pwm_l = ax_balance_out + ax_velocity_out - ax_turn_out;   // left wheel
wheel_pwm_r = ax_balance_out + ax_velocity_out + ax_turn_out;   // right wheel
AX_MOTOR_A_SetSpeed(wheel_pwm_l);
AX_MOTOR_B_SetSpeed(wheel_pwm_r);
```

> The mix is done in **int32** and the per-loop outputs are clamped (velocity output ±3000, turn output ±1500) so the PWM sum can never overflow.

Intuition:
- **Angle loop**: adds with the **same sign** to both wheels → they move fore/aft together to "catch" the fall.
- **Velocity loop**: also adds with the **same sign** to both wheels → they move fore/aft together to "stop / drive."
- **Turn loop**: adds with **opposite signs** (`-` and `+`) → one wheel faster, one slower, producing the differential that steers.

> ### Dead-zone compensation — the quiet fix for static-friction wobble
> Before the mixed command reaches the motors, the firmware adds the wheels' **minimum-turn PWM** (`ax_deadzone`, default **120**) to every non-zero command. Below this threshold the motors don't turn at all because of static friction, so tiny corrections from the angle loop produce no motion — the cart sits in a slow stick-slip **limit-cycle wobble** that no amount of Kp/Kd can remove. Adding the dead-zone offset means even the smallest non-zero command actually moves the wheels, which is what kills that slow static-friction wobble. The dead-zone is tunable live (Bluetooth `5`) and saved to flash.

**Why each loop is needed**: the angle loop keeps it from falling, the velocity loop keeps it from drifting / lets it hold a spot and drive, the turn loop lets it steer and go straight. Drop the angle loop → instant fall; drop the velocity loop → it won't fall but skates all over and won't stop; drop the turn loop → it can't turn and easily veers off.

---

## 2. Mechanical midpoint

### 2.1 What the midpoint is and why it matters

`ax_middle_angle` (in units of 0.01°) is the **mechanical balance point** — the pitch value the DMP reads when the cart **truly stands still with its center of mass directly above the axle**.

The angle-loop error is `angle_bias = ax_middle_angle - angle`. In other words, **the cart doesn't chase "angle = 0," it chases "angle = midpoint."**

Why it matters: "DMP reads 0" is **almost certainly not** the same as "the cart is mechanically upright." Because the battery, PCB, and sensor mounting are never perfectly symmetric, the cart's true balance posture might be pitch = +1.3° or −0.8° or so. If you set the midpoint to 0, the angle loop will fight to force the cart to the "reading-0" posture — **which is actually tilted** — and the cart either keeps pushing to one side (then the velocity loop drags it back, the two fight, it jitters) or simply can't stand. **Get the midpoint wrong and no Kp/Kd, however good, will give you a clean waveform.**

### 2.2 DMP pitch is a "gravity-absolute angle," not a "relative-to-power-on angle"

A key point: **the DMP's pitch output is an absolute lean relative to gravity, not relative to the power-on posture.** Inside the MPU6050, the DMP fuses the accelerometer (which senses the gravity direction) and the gyro to produce an attitude angle that is **long-term drift-free and gravity-referenced** (pitch `angle[1]`).

Practical consequences:

- **You don't have to hold the cart upright at power-on.** Whether the cart is tilted or in your hand at boot, the DMP's pitch is the true lean "relative to ground vertical."
- A **fixed** `ax_middle_angle` value therefore represents the **same true posture** every boot — which is exactly why "record it and hard-code / save it for next time" works (see Section 7). If pitch were power-on-relative, a recorded number would be meaningless next boot.

### 2.3 Gated auto-midpoint (self-calibrating midpoint)

The firmware has a handy feature: when **enabled (Bluetooth `C`, OFF by default), in full cascaded mode, while the cart is balancing and not being commanded to move**, it lets `ax_middle_angle` slowly drift toward the cart's "true resting lean," auto-converging on the correct mechanical midpoint.

Code:

```c
if (ax_midcal_enable && ax_velocity_enable && ax_robot_move_enable && ax_robot_vx == 0 && ax_robot_vw == 0)
{
    static int32_t mid_acc = 0;
    mid_acc += (ax_balance_angle - ax_middle_angle);   // accumulate "actual angle - current midpoint"
    if (mid_acc > 4000)       { if (ax_middle_angle <  1500) ax_middle_angle++; mid_acc = 0; }
    else if (mid_acc < -4000) { if (ax_middle_angle > -1500) ax_middle_angle--; mid_acc = 0; }
}
```

Principle: the velocity loop **pins the cart in place**, so it eventually rests at its **true balance lean**. The firmware keeps accumulating "actual angle minus current midpoint," and once that crosses the threshold (4000) it nudges the midpoint **one minimum step (1 count = 0.01°)** that way and resets. The result:

- The midpoint **auto-converges** to the cart's true balance posture; the "stands still but with a constant small lean" steady-state error **disappears on its own**.
- The drift is **very slow** (seconds-scale) and doesn't disturb balancing.
- The range is **clamped to ±15°** (`±1500`), preventing run-away.
- It is **heavily gated** — active only with the velocity loop on, at rest, AND when explicitly armed with `C` — so it can't drift on its own.

Note that `FallProtect` (kills the motors past 45°) and `PutDown` (auto-restarts when nudged within ±5° of the midpoint) are now judged **relative to `ax_middle_angle`**, not relative to absolute 0, so they work correctly even when the midpoint isn't 0.

**The practical payoff**: you don't have to hand-tune the midpoint to perfection. Turn on the velocity loop (`V`), arm auto-midpoint (`C`), set the cart roughly upright and let it stand for a moment, and the calibration converges. Then read the converged value off the OLED and either hard-code it into the firmware (Section 7) or save it to flash (`W`).

---

## 3. Tuning setup

### 3.1 Flash the firmware

Build and flash the firmware to the STM32F103C8T6. Default state after flashing (from the source):

- `ax_velocity_enable = 1` and `ax_turn_enable = 0` → **the velocity loop is ON by default and the turn loop is OFF**. The angle and velocity loops run together at boot; the turn loop's output is held cleared until you enable it. Bluetooth `V` toggles the velocity loop, `T` toggles the turn loop — for isolating a fault, work **one extra loop at a time** (e.g. turn the velocity loop OFF to debug the angle loop alone).
- Default gains (`ax_robot.c`): balance **Kp=950, Kd=50**; velocity **Kp=4600, Ki=4400**; motor **deadzone=120**; turn **Kp=1000, Kd=5000**. (These are the user-tuned working values as of 2026-06-16.)
- `ax_velocity_sign` default = **−1 (brake)** — the velocity-loop polarity that makes the motors fight a pushed wheel. Flip it live with `Y`, save it with `W`.
- `ax_robot_move_enable = 1` (armed).

> The angle loop is at its tuned working point (balance **Kp=950, Kd=50**), the velocity loop at **Kp=4600, Ki=4400**. These are already the `ax_robot.c` defaults. Tuned values can be saved into on-chip flash with Bluetooth **`W`** (survives power-off and re-flashing of the firmware), and **`Z`** erases the saved config so the next boot uses the code defaults.

### 3.2 Connect Android Bluetooth for live commands (HC-05)

- Hardware: HC-05 Bluetooth module on **USART2 @ 9600 baud**.
- On the phone install **"Serial Bluetooth Terminal"** (Android), pair and connect to the HC-05.
- Commands are **single characters, case-insensitive**; other bytes (CR/LF etc.) are ignored.
- This lets you change gains, switch modes, capture the midpoint, and drive the cart in real time **without re-flashing**, while the cart is standing.

### 3.3 Connect CH340 + Arduino Serial Plotter to watch the channels

- Hardware: USB-CH340 to the STM32's **USART1**, baud **230400**.
- On the PC use **Arduino IDE's Serial Plotter** (or SerialPlot) at 230400 baud.
- In `Control_Task` the firmware prints one line of **six numbers** every ~20 ms (~50 Hz):

| Channel | Meaning | Unit | What it's for |
|---------|---------|------|---------------|
| **ch1** | angle error (actual angle − midpoint) | 0.01° | **0 = balanced**. The main thing you watch while tuning Kp/Kd |
| **ch2** | tilt rate | deg/s | how fast the jitter/oscillation is |
| **ch3** | left wheel speed | mm/s | whether the encoder is counting (pushing a wheel should give a non-0 reading) |
| **ch4** | right wheel speed | mm/s | same; stuck at 0 = not wired |
| **ch5** | velocity-loop output velocity_out | PWM | **determines the velocity-loop sign**: pushing a wheel it must read **opposite** (see below) |
| **ch6** | turn-loop output turn_out | PWM | with the turn loop on, watch for an integrator runaway |

> **Set the velocity-loop sign from the "brake test," not from the sign of ch3/ch4 (the sign of ch3/ch4 is exactly what threw us off repeatedly).** The one and only criterion: **with the velocity loop alone enabled (`V` on, `T` off, vKi=0), push a wheel by hand → the motor must "fight back" (brake), and ch5 must be opposite to the direction you pushed.** If instead the wheel **runs away in the direction you pushed** (ch5 slams toward ±limit, same sign as your push), that's **positive feedback** → **flip the sign live with `Y`** (and `W` to save), or flip the two `ax_wheel_vel_l/ax_wheel_vel_r` lines in `ax_balance.c` together (the two lines are always one positive, one negative — left/right mirror). This cart is set to the "brake" direction.

> Note: USART1 now carries the waveform stream, so the firmware **no longer** prints gain/status text over serial; gains and modes are shown on the **OLED**.

### 3.4 Bluetooth command quick reference (full set, from `ax_control.c`)

The command set is single-character and case-insensitive.

**Drive**

| Key | Action |
|-----|--------|
| `F` | forward (target +250 mm/s) |
| `B` | back (target −250 mm/s) |
| `L` | turn left (target +60 deg/s) |
| `R` | turn right (target −60 deg/s) |
| `S` | stop |
| `O` | toggle ultrasonic avoidance |

**Loops / view**

| Key | Action |
|-----|--------|
| `V` | toggle the **velocity loop** on/off (ON by default) |
| `T` | toggle the **turn loop** on/off (OFF by default) |
| `Y` | **flip the velocity-loop sign** (brake vs assist) — see the brake test |
| `D` | toggle the OLED between the **RUN** and **TUNE** views |
| `C` | toggle the **gated auto-midpoint** on/off (OFF by default) |

**Tune**

| Key | Action |
|-----|--------|
| `1` | select **balance Kp** (then use +/-) |
| `2` | select **balance Kd** |
| `3` | select **velocity Kp** |
| `4` | select **velocity Ki** |
| `5` | select **motor dead-zone** |
| `+` (or `=`) | step the selected value **up** one step |
| `-` (or `_`) | step the selected value **down** one step (floors at 0; also recovers a value that wrapped) |
| `M` | **capture the current angle as the midpoint** `ax_middle_angle` |

**Save**

| Key | Action |
|-----|--------|
| `W` | **save ALL tuned values to flash** (gains + midpoint + dead-zone + velocity-sign), with a read-back verify. Hold the cart — there is a brief (~tens of ms) pause |
| `Z` | erase the saved config → next boot uses the code defaults |

**Step size per +/- press** (matches the selected item 1–5):

| Selected | Step | Cap |
|----------|------|-----|
| `1` balance Kp | **50** | 9999 |
| `2` balance Kd | **5** | 999 |
| `3` velocity Kp | **200** | 20000 |
| `4` velocity Ki | **100** | 20000 |
| `5` motor dead-zone | **10** | 2000 |

> The turn-loop gains **turn Kp / turn Kd are not in the Bluetooth-tunable list** (the array has only the 5 entries above). To change them, edit the source `ax_robot.c` and re-flash — or just save them along with everything else via `W` once you've set them in source (the flash config stores turn Kp/Kd too).

### 3.5 OLED layout — two views, toggled by `D`

The OLED has **two views**, switched with Bluetooth `D`:

**RUN view** (the default operating view):

```
L1:  V<0/1> T<0/1>   ARM/FALL          ← velocity/turn loop switches + armed(ARM)/fallen(FALL)
L2:  avoidance state  |  ultrasonic distance     [col 16: L / S / X flash-config flag]
L3:  drive  Vx <mm/s>   Vw <deg/s>     ← commanded forward / yaw rate
L4:  A:<angle, deg>
```

**TUNE view** (the gain-tuning view):

```
L1:  V<0/1> T<0/1>   ARM/FALL
L2:  A:<angle,deg>    M:<midpoint, raw 0.01deg value>   [col 16: L / S / X]
L3:  P:<balanceKp>    D:<balanceKd>
L4:  vP:<velocityKp>  vI:<velocityKi>
```

- **L1** left: `V1` = velocity loop on / `V0` = off, `T1` = turn loop on / `T0` = off (both 0 = angle loop only); right: `ARM` = armed, `FALL` = fall protection triggered.
- **L2** (TUNE) `A:` is the current lean (deg); `M:` is the midpoint **raw 0.01° value** (the number you ultimately copy down / save). In the RUN view L2 shows the avoidance state and ultrasonic distance.
- **L2 column 16 — the flash-config flag**: `L` = config **loaded** from flash at boot, `S` = a `W` **save succeeded** (read-back verified), `X` = a save **FAILED** verification.
- **L3 / L4** (TUNE) are the angle-loop and velocity-loop gains, updating live as you tune so you can verify each change.

---

## 4. Step-by-step tuning

> Overall approach (the vendor DB20 method): start with **I = D = 0**, raise **Kp** until it just starts oscillating, add **D** to kill the oscillation, finally add **I** to remove steady-state error. Judge by: **overshoot / rise time / steady-state error**. Below we follow that approach in this cart's multi-loop order.

### Phase 0: safety prep

- **Wheels off the ground first** (cart on a stand) for the angle-loop power-on check.
- To debug the angle loop alone, turn the velocity loop OFF (Bluetooth `V`, since it is ON by default) so the OLED L1 reads **`V0 T0`** — only the angle loop runs, velocity/turn are cleared, safest.

### Phase 1: angle loop PD (ANGLE mode)

**Goal**: the cart stands solidly, springs back to vertical when nudged, doesn't chatter, doesn't sway continuously.

1. **Verify motor direction first** (cart on stand). Tip the cart forward — **both wheels should turn in the "drive forward / catch the fall" direction**; tip it back and both should reverse. If a wheel turns the wrong way, swap that motor's AIN1/AIN2 (or BIN1/BIN2) wiring, or flip the sign of that `SetSpeed` line in `ax_balance.c`. **Tuning before the direction is right is pure waste.**

2. **Tune Kp → Kd the vendor way**:
   - First drop **Kd very low, even to 0** (Bluetooth `2` selects Kd, press `-`), leaving only Kp.
   - Bluetooth `1` selects Kp, step it `+` (step 50) **until the cart starts oscillating back and forth / high-frequency chatter**. That means Kp is near "just stiff enough" — note this value.
   - Bluetooth `2` selects Kd, step it `+` (step 5) **to damp the oscillation out**, until a nudge produces a crisp return with little overshoot. Too much Kd and you get high-frequency jitter and a buzzing motor — back it off a touch.
   - Iterate Kp/Kd until **ch1 (angle error) converges quickly back near 0 after a nudge, with no sustained oscillation**.

3. **Your starting point**: balance **Kp=950, Kd=50** (the tuned working values). You can fine-tune from here rather than starting from scratch. In the TUNE view OLED L3 should read `P:950 D: 50`.

4. **In this phase watch only ch1 / ch2**: ch1 is angle error (must converge to 0), ch2 is tilt rate (smaller = better). Ignore ch3/ch4 for now (velocity loop not yet relevant).

> Reminder: the angle loop has no I, so **the angle loop alone is allowed to slowly drift overall** — that's normal; stopping is the velocity loop's job. This phase only requires "doesn't fall, returns to vertical when nudged, doesn't chatter continuously."

### Phase 2: confirm the encoders are "counting" (before relying on the velocity loop)

This step only confirms the encoders **are working**; the sign is settled in Phase 3 with the "brake test" (the only reliable criterion — don't stare at the sign of ch3/ch4; that's what kept throwing us off).

- Cart on a stand, watch **ch3 (left wheel speed) / ch4 (right wheel speed)**.
- Spin each wheel by hand: the corresponding channel should show a **clearly non-zero reading**, positive vs negative for the two directions, returning to 0 when released.
- If a wheel **reads a flat, constant 0**: that encoder is **not wired / not counting** — more dangerous than being reversed, so check the wiring / phase order first. This cart's encoders: enc A = PA15/PB3 (TIM2), enc B = PB4/PB5 (TIM3) (remapped onto 5V-tolerant pins; defer to the encoder/TIM init source).
- Whether ch3/ch4 read positive or negative here **doesn't matter** — the velocity-loop sign is set in Phase 3 by "does the motor brake."

### Phase 3: velocity loop PI (the cart already runs it; isolate it by turning the turn loop off and verifying the sign)

**Goal**: the cart no longer drifts, **pins itself in place**, stands and stops from any starting posture; obeys `F`/`B` forward/back.

> **Step 0: set the sign with the "brake test" (still on the stand! the only reliable criterion).** With the velocity loop on (`V1`) and the turn loop off (`T0`), set `vKi=0` leaving only vKp. Push a wheel by hand either way — the motor must **fight back (brake)**, and ch5 must be **opposite** to your push. ✅ Fights back = sign correct, go tune gains. ❌ If the wheel **runs away in the direction you pushed**, ch5 slamming toward ±limit with the same sign as your push = **positive feedback** → **press `Y` to flip the velocity sign live** (then `W` to save), or flip the `ax_wheel_vel_l` / `ax_wheel_vel_r` lines in `ax_balance.c` together (keeping them one positive, one negative — left/right mirror) and re-build. Safety net: the velocity output is clamped to ±3000, so even a reversed sign won't slam-runaway the way it used to.

1. Once the sign is confirmed, **tune vKp** (Bluetooth `3` selects velocity Kp, step 200):
   - Raise it from a smaller value. vKp "pulls back the moment it moves."
   - Too small → obvious drift, can't brake; too large → the cart **shuffles back and forth in place (a low-frequency oscillation)**. Raise it until "drift is mostly suppressed but it hasn't started shuffling yet."
2. **Then tune vKi** (Bluetooth `4` selects velocity Ki, step 100; **start at 0 and add gradually**):
   - vKi removes **long-term drift and steady-state position error**, **pinning the cart at the spot**.
   - Too small → the cart slowly floats away, doesn't settle cleanly; too large → **slow back-and-forth oscillation / return overshoot**. Watch ch5 each step up: at rest it should not slowly climb toward saturation. Raise until "after release the cart actively returns to the start and stops solidly."
3. Iterate between vKp / vKi. Target waveform: **ch1 near 0, ch3/ch4 hugging 0 at rest**; a nudge and it returns and stops on its own. The cart ships at vKp=4600, vKi=4400.

> Reiterate: the velocity loop has only **vKp, vKi — no vKd**. Don't go looking for a velocity-derivative — it doesn't exist (see the red box in 1.3).

**How the velocity loop + auto-midpoint make the cart "robust to its starting position":**
- The velocity loop **pins the cart in place**, so it rests at its **true balance lean**.
- With auto-midpoint armed (`C`, Section 2.3 — active only with the velocity loop on + at rest), `ax_middle_angle` keeps slowly converging to that true lean, **zeroing the standing steady-state error**.
- The combined result: **set the cart to roughly upright from any starting posture and it finds the balance point, stops, and calibrates the midpoint** — no precise placement needed each boot. That is where "robust to starting position" comes from.

### Phase 4: turn loop PD

**Goal**: crisp left/right turns with no head-wag afterward; straight-line driving doesn't veer.

- **Verify the turn loop alone first** (on a stand): Bluetooth `T` enables the turn loop on its own (turn the velocity loop `V` off so the OLED reads `V0 T1`). At rest, with no turn command, ch6 (turn_out) **should not climb on its own** — the firmware adds a dead-zone + accumulator clamp + clear-on-disable to prevent runaway; lightly twist the cart's nose and both wheels should **differentially resist** the rotation. If ch6 still climbs monotonically at rest, it's a Z-axis gyro bias / sign issue — fix the sign / bias first.
- turn Kp / turn Kd **can't be set over Bluetooth** (not in the 5-item array); edit the source `ax_robot.c` (defaults turn Kp=1000, Kd=5000) and re-flash, or save them to flash via `W` once set in source.
- Method is the same PD: get turn Kp so it responds to `L`/`R`; raise Kp if too sluggish, add turn Kd damping if it overshoots / wags on a turn.
- Once verified, enable `V`+`T` together and test with Bluetooth `L`/`R`/`S`: is the turn crisp, does it wag after stopping, does it go straight (no turn command)?

---

## 5. Reading the waveform / reading the OLED

### 5.1 Good vs bad waveforms (Serial Plotter)

**ch1 (angle error, 0 = balanced) — the main thing you watch:**

| Symptom | Reading | Rough direction |
|---------|---------|-----------------|
| After a disturbance, **returns to 0 quickly and monotonically**, almost no overshoot | ideal | keep it |
| Returns to 0 but with **obvious overshoot, rings a few times before settling** (low-frequency large sway) | under-damped | angle-loop Kd too small (or Kp too large) |
| **Persistent high-frequency jitter**, never settles clean, buzzing motor | noise amplified | angle-loop Kd too large (or Kp too large) |
| **Slowly drifts off 0 and doesn't return** (angle loop only) | angle loop has no I — normal drift | turn on the velocity loop `V` |
| With the velocity loop on, ch1 **slowly swings back and forth, large** | velocity-loop return overshoot | vKi too large |

**ch2 (tilt rate)**: the more violent the jitter, the larger and "hairier" ch2's amplitude. A clean balance keeps ch2 fluctuating gently near 0.

**ch3 / ch4 (left/right wheel speed):**

| Symptom | Reading |
|---------|---------|
| Spinning a wheel gives a clear non-0 reading, returns to 0 on release | encoder is counting (normal); ignore the sign — set the sign via the ch5 brake test |
| Constant **0**, spinning the wheel does nothing | encoder not wired / not counting (Phase 2, check wiring) |
| At rest, **a persistent same-sign bias that keeps growing** | integral windup (encoder problem or vKi too large) |
| With the velocity loop on, at rest ch3/ch4 **oscillate back and forth, small** | velocity loop shuffling — vKp/vKi too large |

### 5.2 Reading the OLED

- **L1**: confirm the loop switches are right (Phase 1 should read `V0 T0`, Phase 3 velocity-verify `V1 T0`, Phase 4 turn-verify `V0 T1`); if the right side turns to **`FALL`** the 45° fall protection tripped (motors killed) — set the cart within ±5° of the midpoint and nudge it to auto-restart (`PutDown`).
- **L2 (TUNE view) `A:`**: current lean, near the midpoint angle when balanced; `M:` is the raw midpoint value — with the velocity loop on, at rest, and auto-midpoint armed (`C`), you'll see it **slowly self-calibrate** until stable. (Toggle the view with `D`.)
- **L2 column 16**: `L`/`S`/`X` flash-config flag — `L` loaded at boot, `S` saved OK, `X` save failed.
- **L3 / L4 (TUNE view)**: live-verify that the gain you're tuning changes as expected (one step per +/- press).

---

## 6. Symptom-to-gain troubleshooting table

| Symptom | Most likely cause | How to fix |
|---------|-------------------|------------|
| Cart falls outright, can't hold up | angle Kp too small / motor direction reversed / midpoint way off | verify motor direction (Phase 1.1) first, raise balance Kp, check midpoint |
| Stands with **high-frequency jitter**, buzzing motor | angle Kd too large (or Kp too large) | lower balance Kd (`2` then `-`); lower Kp if needed |
| After a nudge, **overshoots and sways at low frequency** a few times before settling | angle Kd too small | raise balance Kd |
| Too "soft" overall, falls at the slightest push | angle Kp too small | raise balance Kp |
| Angle loop alone slowly drifts away, won't stop | normal (angle loop has no I) | turn on the velocity loop `V` (Phase 3) |
| With velocity loop on, still **drifts one direction**, won't stop | velocity vKp/vKi too small | raise vKp first, then vKi |
| With velocity loop on, **shuffles fast in place** | velocity vKp too large | lower velocity Kp |
| With velocity loop on, **slow back-and-forth large sway / return overshoot** | velocity vKi too large | lower velocity Ki |
| **Slow stick-slip wobble that won't go away** no matter how you tune Kp/Kd | motor dead-zone too small (tiny commands don't overcome static friction) | raise the dead-zone (`5` then `+`, step 10) |
| **Runs away the instant the velocity loop is on / a push makes it accelerate, ch5 slams to ±limit** | **velocity-loop sign reversed** (positive feedback — motor assists instead of brakes) | Phase 3 "brake test": press `Y` to flip the sign live (then `W`), or flip the two encoder-sign lines in `ax_balance.c` together |
| Both wheels **push one direction hard regardless of lean**, runs away | encoder unwired / constant 0, integral windup | back to Phase 2, verify encoders; don't enable the velocity loop until verified |
| **Spins in place / accelerates spinning the instant the turn loop is on** | turn accumulator runaway / Z-gyro bias | verify `T` alone; dead-zone+clamp already added — if it still climbs, check gyro sign/bias |
| Head-**wags / overshoots** on a turn | turn Kd too small | edit source, add turn Kd, re-flash (or `W`) |
| Turns **sluggishly / won't turn** | turn Kp too small | edit source, add turn Kp, re-flash (or `W`) |
| **Veers off** even without a turn command | turn loop too weak | raise turn Kp/Kd (source) |
| Stands still but with a **constant small lean** | midpoint off | arm auto-midpoint (`C`) with the velocity loop on (`V`) to let it converge, or press `M` to capture manually |

> **The rule of thumb**: jitter = look at Kd (too large → HF jitter, too small → overshoot sway); stiffness = Kp; drift / won't-stop = vKp/vKi; the slow stick-slip wobble = the dead-zone; steering = turn Kp/Kd (source).

---

## 7. Recording values + flash config for next time

Once tuned, you have two ways to keep the values: **save them to on-chip flash with `W`** (easiest, no re-flash), or **read them off the OLED and hard-code them into the firmware**. Both survive power-off.

### 7.1 Save to flash with `W` (recommended)

- Press **`W`** to save **all** tuned values — gains (balance Kp/Kd, velocity Kp/Ki, turn Kp/Kd), the midpoint, the dead-zone, and the velocity-loop sign — into on-chip flash. Hold the cart during the save: there is a brief (~tens of ms) pause.
- The save is **read-back verified**: OLED L2 column 16 shows `S` on success or `X` if the verify failed.
- At the next boot the firmware loads the saved config and shows `L` in that column. Press **`Z`** to erase the saved config so the next boot reverts to the code defaults.

### 7.2 Read the final values off the OLED (TUNE view, toggle with `D`)

- **L2 `M:`** → the converged midpoint raw value (0.01°).
- **L3 `P:` / `D:`** → final balance Kp / Kd.
- **L4 `vP:` / `vI:`** → final velocity Kp / Ki.
- turn Kp/Kd aren't on the OLED — use the current values from your source.

### 7.3 Hard-code into `ax_robot.c`

Open `self-balancing-cart/STM32/Robot/ax_robot.c` and set these variables' initializers to your converged values:

| Variable | Meaning | e.g. (current working values) |
|----------|---------|-------------------------------|
| `ax_middle_angle` | mechanical midpoint (raw 0.01°, copy the OLED `M:`) | copy from OLED |
| `ax_balance_kp` | angle-loop Kp (copy OLED `P:`) | `950` |
| `ax_balance_kd` | angle-loop Kd (copy OLED `D:`) | `50` |
| `ax_velocity_kp` | velocity-loop Kp (copy OLED `vP:`) | `4600` |
| `ax_velocity_ki` | velocity-loop Ki (copy OLED `vI:`) | `4400` |
| `ax_deadzone` | motor dead-zone PWM | `120` |
| `ax_turn_kp` | turn-loop Kp | `1000` |
| `ax_turn_kd` | turn-loop Kd | `5000` |

For example:

```c
int16_t  ax_balance_kp = 950;    /* tuned */
int16_t  ax_balance_kd = 50;     /* tuned */
```

And the midpoint:

```c
int16_t  ax_middle_angle = 130;  /* e.g. the converged M: read off the OLED (0.01deg) */
```

> **Tip**: after hard-coding (or saving) the midpoint, the next boot stands at the correct balance point right away (because DMP pitch is a gravity-absolute angle, Section 2.2); auto-midpoint then only needs a tiny tweak, if any.

### 7.4 Flash config — already implemented

> Earlier this was a "future improvement"; it is now **done**. The `W` / `Z` flash-config feature (Section 7.1) saves the converged gains, midpoint, dead-zone, and velocity-sign into the STM32's internal flash, with a read-back verify, and reloads them at boot — so you no longer have to hand-copy values into source. Hard-coding into `ax_robot.c` (7.3) remains available as an alternative or as the baked-in default.

---

## 8. Notes on the Kalman filter

People often ask "won't adding a Kalman filter stop the jitter?" For this cart the answer is clear-cut:

### 8.1 The DMP already does sensor fusion — wrapping a Kalman filter around its output is **redundant**

This firmware's angle comes from the **MPU6050's DMP** (`AX_MPU6050_DMP_GetData`). Inside the chip the DMP **already fuses** the accelerometer and gyro into a stable, low-drift, gravity-referenced attitude angle (pitch `angle[1]`). A Kalman / complementary filter's **whole purpose is to do that fusion** — and since the DMP has already done it, **stacking another Kalman on top of the DMP's output is duplicate work**, adding latency and improving essentially nothing.

### 8.2 Kalman / complementary filtering is an **alternative** to the DMP, not an addition

If you **don't use the DMP** and instead read the **raw** accelerometer + raw gyro and **fuse them yourself** with a Kalman or complementary filter to get the angle — that is a valid **alternative route**. Its upside is **full control** over the fusion and **lower latency** (the DMP has a fixed pipeline delay); its downside is that it's a **big change**: rewriting attitude estimation, recalibrating, and re-tuning the whole angle loop, with non-trivial risk and effort. It and the DMP are **either/or**, not "DMP plus."

### 8.3 The current jitter is a **PID / midpoint problem, not a sensor-noise problem**

The key point: **the jitter you see now is rooted in PID tuning or the midpoint, not in sensor noise.** The DMP's angle is already quite clean. Nearly every jitter/sway in Sections 5 and 6 maps to a gain (balance Kd too large → HF jitter, too small → overshoot sway; midpoint off → constant-bias fighting), to the dead-zone (too small → static-friction wobble), or to an encoder / velocity-loop issue. **So adding Kalman won't cure the current jitter** — the right move is to tune Kp/Kd/vKp/vKi and the dead-zone and calibrate the midpoint per Sections 4 and 6. Get PID and the midpoint right first; only then consider whether to swap the attitude-estimation scheme.

---

## 9. On auto-tuning

Honestly, about what can and can't be automated:

### 9.1 Auto-finding the **midpoint** — implemented ✅

Auto-finding the mechanical midpoint is **already done**: it's the **gated auto-midpoint** of Section 2.3. With the velocity loop on, at rest, and armed with `C`, it slowly converges `ax_middle_angle` to the cart's true balance lean. This automation is mature, reliable, and in use.

### 9.2 Auto-tuning the **PID gains** — not recommended ⚠️

Auto-tuning the **PID gains (Kp/Kd/vKp/vKi)** is a different matter entirely. Auto-tuning PID on real hardware **that can fall over** is **risky, research-grade**:

- Auto-tuning algorithms (relay feedback, various adaptive / extremum-seeking methods) generally need to **deliberately push the system to the edge of oscillation** to probe the boundary — on an inverted pendulum that means **deliberately driving the cart toward falling repeatedly**, which easily leads to real falls and broken hardware.
- It needs reliable experiment reset, safety limits, and failure detection — a lot of engineering, with no guaranteed stable convergence.
- For a course / prototype cart, the cost/benefit is poor and the risk is high.

**Therefore hardware PID auto-tuning is not recommended for this cart.** The **practical, reliable path is the manual flow of Section 4**: the vendor "Kp→Kd→Ki" method + live Bluetooth tuning + serial-plotter judging + OLED read-out, then save with `W` (or hard-code). Leave the midpoint to the implemented auto-calibration and tune the gains by hand — currently the most dependable combination.

---

### Appendix: firmware locations referenced by this guide

- `self-balancing-cart/STM32/Robot/ax_balance.c` — the multi-loop control laws, ISR, dead-zone compensation, gated auto-midpoint, fall/put-down protection
- `self-balancing-cart/STM32/Robot/ax_robot.c` — global variables and initializers for all gains, the midpoint, the dead-zone, and the velocity sign
- `self-balancing-cart/STM32/Robot/ax_control.c` — the HC-05 single-character Bluetooth commands (drive + live tuning) and the flash-config save/erase
- `self-balancing-cart/STM32/Robot/ax_task.c` — the six-channel serial waveform stream, OLED RUN/TUNE display, ultrasonic, heartbeat
