/* ax_robot.c - STM32 self-balancing cart firmware (adapted from XTARK OpenCTR). */


/* Includes ------------------------------------------------------------------*/
#include "ax_robot.h"
#include "ax_cfg.h"      /* flash-persist API; implemented at the bottom of this file */

int16_t ax_acc_data[3];
int16_t ax_gyro_data[3];
int16_t ax_angle_data[3];

uint8_t ax_robot_move_enable = 1;

/* Velocity loop ON by default now that its sign is correct (ax_vel_sign=-1, brakes) and good
   gains are baked in - so the cart holds position and remote F/B work out of the box. TURN loop
   still OFF (its sign is unverified): enable with BT 'T' and check that a hand-twist makes the
   wheels RESIST (not spin up) before relying on L/R steering / avoidance. */
uint8_t ax_velocity_enable = 1;
uint8_t ax_turn_enable     = 0;

/* LIVE velocity-loop sign (+1 or -1), flipped by BT 'Y' and SAVED by 'W'. Default -1 = the
   braking direction confirmed on this cart (with +1 a push made the cart drive HARDER that
   way = positive feedback). If a push ever makes it run AWAY in the push direction again,
   press 'Y' once (then 'W' to keep it). */
int8_t ax_vel_sign = -1;

/* Gated auto-midpoint (BT 'C'): when ON, the cart finds the center of a small steady
   oscillation and adopts it as the balance midpoint. OFF by default. */
uint8_t ax_midcal_enable = 0;

/* Motor dead-zone compensation (PWM counts added to every non-zero wheel command to beat
   static friction; this is what kills the slow limit-cycle wobble Kp/Kd can't). Live-tunable
   via BT '5', and SAVED by 'W'. Default 120 = user-tuned working value (2026-06-16). */
int16_t ax_deadzone = 120;

/* OLED view: 0 = RUN (avoidance / distance / drive — default for the operating phase),
   1 = TUNE (the PID gains). BT 'D' toggles. Not saved. */
uint8_t ax_disp_mode = 0;

uint8_t ax_beep_ring = 0;

uint8_t ax_control_mode = CTL_APP;   /* HC-05 on USART2 -> Bluetooth command protocol */

int16_t ax_robot_vx = 0;
int16_t ax_robot_vw = 0;

int16_t  ax_middle_angle = 0;

double ax_wheel_vel_l = 0;
double ax_wheel_vel_r = 0;

//Ultrasonic distance, cm (0 = no echo yet)
uint16_t ax_ultra_distance = 0;
 
int16_t  ax_balance_angle;
int16_t  ax_balance_gyro;
int16_t  ax_balance_out;
int16_t  ax_balance_kp = 950;    /* user-tuned working value (2026-06-16) */
int16_t  ax_balance_kd = 50;     /* user-tuned working value */

int16_t  ax_velocity = 0;
int16_t  ax_velocity_out;
int16_t  ax_velocity_kp = 4600;  /* user-tuned working value (2026-06-16) */
int16_t  ax_velocity_ki = 4400;  /* user-tuned working value (2026-06-16) */

int16_t  ax_turn_gyro;
int16_t  ax_turn_out;
int16_t  ax_turn_kp = 1000;
int16_t  ax_turn_kd = 5000;



/* ===========================================================================
 * Config persistence (ax_cfg.h) - tuned gains + midpoint kept in internal flash.
 *
 * Lives HERE (not a separate ax_cfg.c) so it compiles with the rest of the build
 * without adding a file to the Keil project (.uvprojx). FLASH_* and __disable_irq
 * come in via ax_robot.h -> stm32f10x.h -> stm32f10x_conf.h.
 *
 * Storage = one 1KB flash page at 0x0800FC00 (last page within 64KB, well above
 * this ~37KB program, so it's safe on a 64KB or 128KB part and the linker never
 * puts code there). Values are 16-bit half-words; a magic word marks a valid set.
 *
 * NOTE: erase/program stalls the CPU and must not run code from flash, so save/
 * erase run with interrupts disabled (the 100 Hz balance ISR pauses for ~tens of
 * ms). Do 'W'/'Z' while HOLDING the cart - it will twitch briefly during the write.
 * =========================================================================== */
#define CFG_ADDR        ((uint32_t)0x0800FC00)
#define CFG_MAGIC       ((uint16_t)0xB6C8)

#define CFG_OFF_MAGIC   0    /* half-word slots */
#define CFG_OFF_BKP     1
#define CFG_OFF_BKD     2
#define CFG_OFF_VKP     3
#define CFG_OFF_VKI     4
#define CFG_OFF_TKP     5
#define CFG_OFF_TKD     6
#define CFG_OFF_MID     7
#define CFG_OFF_SIGN    8    /* velocity-loop sign (+1 / -1) */
#define CFG_OFF_DZ      9    /* motor dead-zone */

static uint16_t cfg_rd(uint32_t slot)
{
	return *(volatile uint16_t *)(CFG_ADDR + slot * 2);
}

/* config state, shown on OLED L2 col16 so a save can't fail silently:
   0=using code defaults  1='L' loaded from flash at boot  2='S' last 'W' verified OK
   3='X' last 'W' FAILED (the flash write did not stick) */
volatile uint8_t g_cfg_state = 0;

void AX_CFG_Load(void)
{
	if (cfg_rd(CFG_OFF_MAGIC) != CFG_MAGIC)
		return;                              /* nothing valid stored -> keep code defaults */

	ax_balance_kp   = (int16_t)cfg_rd(CFG_OFF_BKP);
	ax_balance_kd   = (int16_t)cfg_rd(CFG_OFF_BKD);
	ax_velocity_kp  = (int16_t)cfg_rd(CFG_OFF_VKP);
	ax_velocity_ki  = (int16_t)cfg_rd(CFG_OFF_VKI);
	ax_turn_kp      = (int16_t)cfg_rd(CFG_OFF_TKP);
	ax_turn_kd      = (int16_t)cfg_rd(CFG_OFF_TKD);
	/* midpoint is intentionally NOT loaded from flash — it always boots 0, so a stale saved
	   value can never poison startup (this was the "M = -100 on boot" bug). Press 'M' to set. */

	/* sanity-clamp the loaded gains: a previously-saved value that had overflowed int16 (e.g. vKp
	   cranked past 32767 -> wrapped negative, then 'W') would otherwise reload and break tuning.
	   Negative -> 0 (that loop simply won't drive; not dangerous). */
	if (ax_balance_kp  < 0) ax_balance_kp  = 0;
	if (ax_balance_kd  < 0) ax_balance_kd  = 0;
	if (ax_velocity_kp < 0) ax_velocity_kp = 0;
	if (ax_velocity_ki < 0) ax_velocity_ki = 0;
	if (ax_turn_kp     < 0) ax_turn_kp     = 0;
	if (ax_turn_kd     < 0) ax_turn_kd     = 0;

	/* velocity sign: accept only +1; anything else (incl. old configs / erased 0xFFFF) -> -1 */
	ax_vel_sign = ((int16_t)cfg_rd(CFG_OFF_SIGN) == 1) ? 1 : -1;

	/* dead-zone: clamp to [0,2000]; old configs / erased flash (0xFFFF -> -1) -> 0 */
	ax_deadzone = (int16_t)cfg_rd(CFG_OFF_DZ);
	if (ax_deadzone < 0 || ax_deadzone > 2000) ax_deadzone = 0;

	g_cfg_state = 1;   /* OLED 'L' = these gains came from flash (a previous 'W'), not code defaults */
}

void AX_CFG_Save(void)
{
	__disable_irq();                          /* erase/program must not be interrupted (code runs from flash) */
	FLASH_Unlock();
	FLASH_ErasePage(CFG_ADDR);
	FLASH_ProgramHalfWord(CFG_ADDR + CFG_OFF_BKP   * 2, (uint16_t)ax_balance_kp);
	FLASH_ProgramHalfWord(CFG_ADDR + CFG_OFF_BKD   * 2, (uint16_t)ax_balance_kd);
	FLASH_ProgramHalfWord(CFG_ADDR + CFG_OFF_VKP   * 2, (uint16_t)ax_velocity_kp);
	FLASH_ProgramHalfWord(CFG_ADDR + CFG_OFF_VKI   * 2, (uint16_t)ax_velocity_ki);
	FLASH_ProgramHalfWord(CFG_ADDR + CFG_OFF_TKP   * 2, (uint16_t)ax_turn_kp);
	FLASH_ProgramHalfWord(CFG_ADDR + CFG_OFF_TKD   * 2, (uint16_t)ax_turn_kd);
	FLASH_ProgramHalfWord(CFG_ADDR + CFG_OFF_MID   * 2, (uint16_t)ax_middle_angle);
	FLASH_ProgramHalfWord(CFG_ADDR + CFG_OFF_SIGN  * 2, (uint16_t)(int16_t)ax_vel_sign);
	FLASH_ProgramHalfWord(CFG_ADDR + CFG_OFF_DZ    * 2, (uint16_t)ax_deadzone);
	FLASH_ProgramHalfWord(CFG_ADDR + CFG_OFF_MAGIC * 2, CFG_MAGIC);   /* magic LAST = "all written" */
	FLASH_Lock();
	__enable_irq();

	/* read the flash BACK to confirm the write actually stuck (a write-protected or clone-chip
	   flash can fail silently). OLED L2 col16 then shows 'S' = saved OK, or 'X' = save FAILED. */
	if (cfg_rd(CFG_OFF_MAGIC) == CFG_MAGIC && cfg_rd(CFG_OFF_BKP) == (uint16_t)ax_balance_kp)
		g_cfg_state = 2;
	else
		g_cfg_state = 3;
}

void AX_CFG_Erase(void)
{
	__disable_irq();
	FLASH_Unlock();
	FLASH_ErasePage(CFG_ADDR);
	FLASH_Lock();
	__enable_irq();
}



