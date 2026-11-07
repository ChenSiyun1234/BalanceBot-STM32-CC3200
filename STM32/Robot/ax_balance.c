/* ax_balance.c - self-balancing control (STM32F103C8T6)
 *
 * My control design:
 * a cascaded angle-PD + velocity-PI + turn-PD loop running at 100 Hz inside the
 * MPU6050 data-ready interrupt (DMP, EXTI line 5). For this cart in particular:
 *
 *   1. IMU axis: this cart's forward/back tilt is rotation about the Y axis, so
 *      the balance angle/rate use the DMP PITCH outputs angle[1] / gyro[1].
 *      Yaw stays gyro[2].
 *   2. Motors: route the wheel PWM values to the TB6612 through
 *      AX_MOTOR_A/B_SetSpeed. As on the
 *      CC3200 build (same R3T chassis), a forward lean must spin BOTH wheels
 *      forward - if a wheel runs the wrong way, swap that motor's AIN1/AIN2 (or
 *      BIN1/BIN2) wires, or flip the sign in the SetSpeed call below.
 *
 * Gains live in ax_robot.c (angle kp1200/kd80 [user-tuned], velocity kp8000/ki4000,
 * turn kp1000/kd5000).
 *
 * BRING-UP (both outer loops default OFF = angle loop alone): tune the angle loop
 * first (ANGL). Then bring the VELOCITY loop up alone (BT 'V') and the TURN loop up
 * alone (BT 'T') - NEVER both at once - so a sign/windup fault in one can't hide
 * behind the other. Do it WHEELS OFF THE GROUND:
 *   - angle loop: a forward lean must spin both wheels the "catch the fall" way and
 *     reverse with the lean; fix a wrong wheel by swapping its AIN1/AIN2 (or BIN1/2).
 *   - encoders (CRITICAL sign): push each wheel PHYSICALLY FORWARD; with our motors
 *     (SetSpeed + = forward) the velocity loop is stabilizing ONLY if ax_wheel_vel
 *     reads NEGATIVE for forward (plot ch3/ch4 go negative). If one reads positive,
 *     flip ONLY that encoder line's sign below. Getting this backwards makes the
 *     velocity loop POSITIVE feedback -> the cart charges off at full power.
 */
#include "ax_balance.h"
#include "ax_robot.h"

volatile uint32_t g_balance_isr_cnt = 0;   /* DEBUG: increments each data-ready balance ISR */

/* Bring-up safety caps on each OUTER loop's contribution (PWM counts; rail = +/-3600).
   Conservative so a wrong sign or integral wind-up can't slam the cart at full power;
   raise these once it balances reliably in full cascade. */
#define VEL_OUT_LIMIT    3000   /* velocity-loop authority cap. Raised to 3000 (leaves ~600 headroom for
                                   the angle loop below the +/-3600 rail) so the velocity loop finally has
                                   enough authority to STOP and RETURN a pushed cart. The old 1800 was
                                   throttling the WHOLE velocity output, which is why vKp=9800 did nothing. */
#define TURN_OUT_LIMIT   1500

int16_t Balance_AngleCtl(int16_t target, float angle, float gyro);
int16_t Balance_VelocityCtl(int16_t t_vx, float r_vx);
int16_t Balance_TurnCtl(int16_t t_vw, float r_vw);
void Balance_FallProtect(void);
void Balance_PutDown(void);
void Balance_AutoMidpoint(void);

/* MPU6050 INT (PA5/EXTI line 5), fires every 10 ms (100 Hz) when DMP data is ready */
void EXTI9_5_IRQHandler(void)
{
    static float balance_angle, balance_gyro, turn_gyro;
    static float wheel_vel;
    int32_t wheel_pwm_l, wheel_pwm_r;   /* int32 so the mix + dead-zone offset can never overflow
                                           int16 and sign-flip into a full-reverse spike (the value is
                                           clamped to +/-3600 below before it reaches the motor). */

    if (EXTI_GetITStatus(EXTI_Line5) != RESET)
    {
        EXTI_ClearITPendingBit(EXTI_Line5);
        g_balance_isr_cnt++;   /* DEBUG: count data-ready events to measure the loop rate */

        /* read the IMU via the DMP */
        AX_MPU6050_DMP_GetData(ax_gyro_data, ax_acc_data, ax_angle_data);
        ax_balance_angle = ax_angle_data[1];   /* PITCH (rotation about Y), 0.01 deg */
        ax_balance_gyro  = ax_gyro_data[1];    /* Y-axis rate, raw +/-2000 dps */
        ax_turn_gyro     = ax_gyro_data[2];    /* Z-axis (yaw) rate, raw +/-2000 dps */

        balance_angle = 0.01f  * ax_balance_angle;   /* deg */
        balance_gyro  = 0.061f * ax_balance_gyro;    /* deg/s (2000/32768) */
        turn_gyro     = 0.061f * ax_turn_gyro;       /* deg/s */

        /* wheel speeds from the encoders (m/s).
           SIGN — set EMPIRICALLY by the brake test, NOT from theory (the theory assumed a
           motor direction that turned out reversed on this cart). The only rule that
           matters: in velocity-ONLY mode (BT 'V' on, 'T' off, vKi=0) push a wheel by hand
           -> the motor must FIGHT you (brake) and ch5 (velocity_out) must OPPOSE the push.
           If instead the wheel runs AWAY in the direction you push (positive feedback),
           flip BOTH lines below together (they stay mirror-opposite, left vs right).
           These signs were confirmed by that test to BRAKE on this cart. */
        ax_wheel_vel_l = (float) ((int16_t)AX_ENCODER_A_GetCounter() * WHEEL_SCALE);
        AX_ENCODER_A_SetCounter(0);
        ax_wheel_vel_r = (float)-((int16_t)AX_ENCODER_B_GetCounter() * WHEEL_SCALE);
        AX_ENCODER_B_SetCounter(0);
        wheel_vel = (ax_wheel_vel_l + ax_wheel_vel_r) / 2;

        /* the three cascaded PID loops */
        ax_balance_out  = Balance_AngleCtl(ax_middle_angle, balance_angle, balance_gyro);
        ax_velocity_out = ax_vel_sign * Balance_VelocityCtl(ax_robot_vx, wheel_vel);  /* 'Y' flips sign live */
        ax_turn_out     = Balance_TurnCtl(ax_robot_vw, turn_gyro);

        /* bring the outer loops up INDEPENDENTLY (BT 'V' velocity, 'T' turn) */
        if (!ax_velocity_enable) ax_velocity_out = 0;
        if (!ax_turn_enable)     ax_turn_out     = 0;

        /* BRING-UP SAFETY: cap each outer loop so neither can overpower the angle loop
           or peg the motor rail if its sign/windup is still wrong */
        if (ax_velocity_out >  VEL_OUT_LIMIT)  ax_velocity_out =  VEL_OUT_LIMIT;
        if (ax_velocity_out < -VEL_OUT_LIMIT)  ax_velocity_out = -VEL_OUT_LIMIT;
        if (ax_turn_out     >  TURN_OUT_LIMIT) ax_turn_out     =  TURN_OUT_LIMIT;
        if (ax_turn_out     < -TURN_OUT_LIMIT) ax_turn_out     = -TURN_OUT_LIMIT;

        /* mix into left/right PWM */
        wheel_pwm_l = ax_balance_out + ax_velocity_out - ax_turn_out;
        wheel_pwm_r = ax_balance_out + ax_velocity_out + ax_turn_out;

        /* MOTOR DEAD-ZONE COMPENSATION: below ~a few hundred counts the motor makes no torque
           (static friction), so tiny balance corrections don't move the wheel and the cart
           settles into a slow limit-cycle wobble Kp/Kd cannot remove. Shove every NON-zero
           command past the dead-band so the wheel responds from the first count; leave 0
           untouched so it can rest. ax_deadzone is live-tunable (BT '5') - raise until the
           slow rock just stops (too high -> buzzy chatter at rest). */
        if      (wheel_pwm_l > 0) wheel_pwm_l += ax_deadzone;
        else if (wheel_pwm_l < 0) wheel_pwm_l -= ax_deadzone;
        if      (wheel_pwm_r > 0) wheel_pwm_r += ax_deadzone;
        else if (wheel_pwm_r < 0) wheel_pwm_r -= ax_deadzone;
        if (wheel_pwm_l >  3600) wheel_pwm_l =  3600;   /* re-clamp to the rail AFTER the offset */
        if (wheel_pwm_l < -3600) wheel_pwm_l = -3600;
        if (wheel_pwm_r >  3600) wheel_pwm_r =  3600;
        if (wheel_pwm_r < -3600) wheel_pwm_r = -3600;

        if (ax_robot_move_enable == 1)
        {
            AX_MOTOR_A_SetSpeed((int16_t)wheel_pwm_l);    /* motor directions follow this cart's wiring: */
            AX_MOTOR_B_SetSpeed((int16_t)wheel_pwm_r);    /* wheels now co-rotate AND a forward lean drives them forward (catch) */
        }
        else
        {
            AX_MOTOR_A_SetSpeed(0);
            AX_MOTOR_B_SetSpeed(0);
        }

        Balance_AutoMidpoint();   /* gated auto-midpoint (BT 'C') - safe: tracks the center of a
                                     small steady oscillation only, freezes during any disturbance */

        Balance_PutDown();      /* auto-restart when set near the midpoint and nudged */
        Balance_FallProtect();  /* kill motors if tilt > 45 deg from the midpoint */
    }
}

/* Angle loop - PD on tilt + gyro */
int16_t Balance_AngleCtl(int16_t target, float angle, float gyro)
{
    float angle_bias, gyro_bias;
    int16_t p_out, d_out, out;

    /* target (ax_middle_angle) is raw 0.01deg; angle is in deg -> scale target to match.
       WITHOUT this *0.01f the angle loop read the midpoint as DEGREES (100x too big), so
       pressing 'M' set a huge tilt target and the cart bolted. FallProtect/PutDown already
       use 0.01deg, so now all three agree. */
    angle_bias = (target * 0.01f) - angle;
    gyro_bias  = 0 - gyro;

    p_out = ax_balance_kp * angle_bias * 0.1f;
    d_out = ax_balance_kd * gyro_bias  * 0.1f;
    out = p_out + d_out;

    return out;
}

/* Velocity loop - PI on wheel speed (encoders); target vx in mm/s, 0..700 */
int16_t Balance_VelocityCtl(int16_t t_vx, float r_vx)
{
    static float velocity_bias = 0, velocity_bias_update;
    static float velocity_integral = 0;
    float velocity_target;
    int16_t p_out, i_out, out;

    if (t_vx >  700) t_vx =  700;
    else if (t_vx < -700) t_vx = -700;

    velocity_bias_update = 0 - r_vx;
    velocity_bias = (velocity_bias * 0.6f) + (velocity_bias_update * 0.4f);   /* low-pass */

    velocity_integral += velocity_bias * 0.01f;                /* 10 ms step */
    velocity_target = t_vx * 0.001f * 0.01f;
    velocity_integral += velocity_target;

    /* hold the integral at 0 whenever disarmed OR while vKi==0 (the whole P-only
       bring-up), so the first time you raise vKi it starts clean (no wind-up jolt) */
    if (ax_robot_move_enable == 0 || ax_velocity_ki == 0)
        velocity_integral = 0;

    /* clamp below the PWM rail: i_out = vKi*integral, so keep vKi*clamp < 3600
       (e.g. vKi=4000 * 0.8 = 3200) so the integral alone can never peg the motors */
    if (velocity_integral >  0.8f) velocity_integral =  0.8f;
    else if (velocity_integral < -0.8f) velocity_integral = -0.8f;

    p_out = -ax_velocity_kp * velocity_bias;
    i_out = -ax_velocity_ki * velocity_integral;
    out = p_out + i_out;

    return out;
}

/* Turn loop - PD on yaw rate (accumulating output) */
int16_t Balance_TurnCtl(int16_t t_vw, float r_vw)
{
    static float bias = 0, bias_last = 0;
    static int16_t p_out = 0, d_out = 0, out = 0;

    /* reset the accumulator whenever the cart is disarmed OR the turn loop is off, so a
       standing yaw-rate bias can't wind it up (out += ... integrates forever otherwise) */
    if (ax_robot_move_enable == 0 || ax_turn_enable == 0)
    {
        out = 0;
        bias_last = 0;
    }

    bias = t_vw - r_vw;

    /* deadband: when NOT commanding a turn, ignore small yaw readings (gyro drift) so
       they don't slowly integrate the accumulator into an unwanted spin */
    if (t_vw == 0 && bias < 2.0f && bias > -2.0f)
        bias = 0;

    p_out = ax_turn_kp * bias * 0.001f;
    d_out = ax_turn_kd * (bias - bias_last) * 0.001f;
    out += p_out + d_out;
    bias_last = bias;

    /* hard clamp the accumulator so it can never alone saturate the motors */
    if (out >  TURN_OUT_LIMIT) out =  TURN_OUT_LIMIT;
    if (out < -TURN_OUT_LIMIT) out = -TURN_OUT_LIMIT;

    return out;
}

/* Auto-restart: while disabled, if set upright (<5 deg) and a wheel is nudged, re-enable */
void Balance_PutDown(void)
{
    static uint16_t cont;
    int16_t wheel_l, wheel_r;

    wheel_l = (int16_t)(ax_wheel_vel_l * 1000);
    wheel_r = (int16_t)(ax_wheel_vel_r * 1000);

    if (ax_robot_move_enable == 0)
    {
        if (cont == 0)
        {
            if ((ax_balance_angle - ax_middle_angle) < 500 && (ax_balance_angle - ax_middle_angle) > -500 && wheel_l == 0 && wheel_r == 0)
                cont++;   /* within +/-5 deg of the balance MIDPOINT (not absolute 0) */
        }
        else
        {
            if (cont < 50)
            {
                cont++;
                if (wheel_l > 1 || wheel_l < -1 || wheel_r > 1 || wheel_r < -1)
                {
                    ax_robot_vx = 0;
                    ax_robot_vw = 0;
                    ax_robot_move_enable = 1;
                    cont = 0;
                }
            }
            else
            {
                cont = 0;
            }
        }
    }
}

/* Fall protection: tilt more than 45 deg FROM THE MIDPOINT -> kill the motors */
void Balance_FallProtect(void)
{
    if (((ax_balance_angle - ax_middle_angle) > 4500) || ((ax_balance_angle - ax_middle_angle) < -4500))
        ax_robot_move_enable = 0;
}

/* Gated auto-midpoint (BT 'C'): when the cart sits in a SMALL, steady, uncommanded oscillation,
   slowly move ax_middle_angle toward the CENTER of that swing (the true balance point), cancelling
   the standing-bias wobble. It tracks the center of a BOUNDED min/max window - NOT a free
   integrator - so it cannot drift off during a fall the way the old removed version did. Every
   gate must hold or it freezes AND resets, so it never updates while the cart is pushed/falling.
   All angles in raw 0.01 deg. */
void Balance_AutoMidpoint(void)
{
    static int16_t  wmin = 32767, wmax = -32768;
    static uint16_t wcnt = 0;
    int16_t dev, center, step;

    dev = ax_balance_angle - ax_middle_angle;

    /* GATES: armed + feature on + not commanded + near-upright + slow + not rolling + vel not railed */
    if ( ax_robot_move_enable == 0
      || ax_midcal_enable     == 0
      || ax_robot_vx != 0 || ax_robot_vw != 0
      || dev > 500 || dev < -500
      || ax_balance_gyro > 500 || ax_balance_gyro < -500
      || ax_wheel_vel_l > 0.03 || ax_wheel_vel_l < -0.03
      || ax_wheel_vel_r > 0.03 || ax_wheel_vel_r < -0.03
      || ax_velocity_out >= VEL_OUT_LIMIT || ax_velocity_out <= -VEL_OUT_LIMIT )
    {
        wmin = 32767; wmax = -32768; wcnt = 0;          /* reset the window */
        return;
    }

    if (ax_balance_angle < wmin) wmin = ax_balance_angle;
    if (ax_balance_angle > wmax) wmax = ax_balance_angle;
    wcnt++;

    if ((wmax - wmin) > 400)                            /* swing too big to be a small limit cycle */
    {
        wmin = 32767; wmax = -32768; wcnt = 0;
        return;
    }

    if (wcnt >= 150)                                   /* ~1.5 s of clean small oscillation */
    {
        center = (int16_t)(((int32_t)wmin + wmax) / 2);
        step   = (int16_t)((center - ax_middle_angle) / 4);        /* slew 1/4 of the way... */
        if (step == 0 && center != ax_middle_angle)
            step = (center > ax_middle_angle) ? 1 : -1;            /* ...at least 1 count, so it converges */
        ax_middle_angle += step;
        if (ax_middle_angle >  300) ax_middle_angle =  300;        /* hard clamp +/-3 deg, safety */
        if (ax_middle_angle < -300) ax_middle_angle = -300;
        wmin = 32767; wmax = -32768; wcnt = 0;
    }
}
