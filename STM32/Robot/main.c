/* main.c - STM32F103C8T6 self-balancing cart (discrete-module build).
 *
 * Hardware (generic "blue pill" C8T6 core board + hand-wired modules):
 *   - MPU6050  : soft I2C  SCL=PB10 SDA=PB11, data-ready INT=PA5 (EXTI9_5, DMP)
 *   - TB6612   : PWMA=PA8(TIM1_CH1) PWMB=PA11(TIM1_CH4)
 *                AIN1/2=PB12/PB13 BIN1/2=PB14/PB15 STBY=PA4
 *   - Encoders : A=TIM2(PA15,PB3)  B=TIM3(PB4,PB5)  [remapped onto FT pins so the
 *                MD220A's 5V Hall signals are safe; default PA0/1/6/7 aren't FT]
 *   - OLED     : JiangXie SSD1306, GND=PB6 VCC=PB7 SCL=PB8 SDA=PB9 (soft I2C)
 *   - HC-05 BT : USART2  TX=PA2  RX=PA3  (Bluetooth command protocol)
 *   - HC-SR04  : TRIG=PB0  ECHO=PA12
 *   - LED      : on-board PC13 (heartbeat)
 *   - UART1    : PA9/PA10 debug printf (optional)
 *
 * The balance loop runs in the MPU6050 INT (EXTI9_5_IRQHandler, ax_balance.c).
 * FreeRTOS hosts the slower supervisor tasks (see ax_task.c).
 */

/* Includes ------------------------------------------------------------------*/
#include "stm32f10x.h"
#include <stdio.h>
#include "ax_robot.h"
#include "ax_cfg.h"

#define START_TASK_PRIO		1
#define START_STK_SIZE		256
TaskHandle_t StartTask_Handler = NULL;
void Start_Task(void *pvParameters);

#define CONTROL_TASK_PRIO	5
#define CONTROL_STK_SIZE	256
TaskHandle_t Control_Task_Handle = NULL;
void Control_Task(void *pvParameters);

#define ULTRA_TASK_PRIO		4
#define ULTRA_STK_SIZE		128
TaskHandle_t Ultra_Task_Handle = NULL;
void Ultra_Task(void *pvParameters);

#define DISP_TASK_PRIO		3
#define DISP_STK_SIZE		128
TaskHandle_t Disp_Task_Handle = NULL;
void Disp_Task(void *pvParameters);

#define HEARTBEAT_TASK_PRIO	2
#define HEARTBEAT_STK_SIZE	128
TaskHandle_t Heartbeat_Task_Handle = NULL;
void Heartbeat_Task(void *pvParameters);


int main(void)
{
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);

	AX_MOTOR_Init();                          /* TB6612, motors held stopped */

	AX_DELAY_Init();

	AX_CFG_Load();   /* if a tuned config was saved (BT 'W'), load it over the code defaults */

	/* Cold-boot settle: let the 3.3V rail + MPU6050 finish powering up before we
	   init OLED/MPU. Without this settling delay, a cold battery start
	   runs MPU init too early -> it fails -> the DMP never loads -> the balance ISR
	   never fires and only the PC13 heartbeat blinks (a manual RESET "fixes" it
	   because the rails are already up). */
	AX_Delayms(1500);

	AX_JTAG_Set(JTAG_SWD_DISABLE);            /* free PA15/PB3/PB4 for encoders ... */
	AX_JTAG_Set(SWD_ENABLE);                  /* ... but keep SWD for flashing */

	AX_LED_Init();                            /* PC13 */

	AX_UART1_Init(230400);                    /* debug printf */
	AX_UART2_Init(9600);                      /* HC-05 Bluetooth (HC-05 default UART baud) */

	AX_URSC_Init();                           /* HC-SR04 (TRIG PB0 / ECHO PA12) */

	AX_ENCODER_A_Init();                       /* TIM2 remap PA15/PB3 (after JTAG off) */
	AX_ENCODER_B_Init();                       /* TIM3 remap PB4/PB5 */

	OLED_Init();

	/* Hold the balance output off until the DMP is up, so a spurious data-ready
	   INT during DMP init can't drive the motors from a garbage angle. */
	ax_robot_move_enable = 0;
	AX_MPU6050_Init();
	AX_MPU6050_DMP_Init();
	AX_Delayms(50);
	ax_robot_move_enable = 1;

	xTaskCreate((TaskFunction_t )Start_Task,
				(const char*    )"Start_Task",
				(uint16_t       )START_STK_SIZE,
				(void*          )NULL,
				(UBaseType_t    )START_TASK_PRIO,
				(TaskHandle_t*  )&StartTask_Handler);

	vTaskStartScheduler();

	while (1);
}


void Start_Task(void *pvParameters)
{
	AX_LED_On();

	OLED_Clear();
	OLED_ShowString(1, 1, " Balance  Cart  ");
	OLED_ShowString(2, 1, "  C8T6  STM32   ");
	OLED_ShowString(3, 1, " starting ...   ");

	taskENTER_CRITICAL();

	xTaskCreate((TaskFunction_t )Control_Task,
			(const char*    )"Control_Task",
			(uint16_t       )CONTROL_STK_SIZE,
			(void*          )NULL,
			(UBaseType_t    )CONTROL_TASK_PRIO,
			(TaskHandle_t*  )&Control_Task_Handle);

	xTaskCreate((TaskFunction_t )Ultra_Task,
			(const char*    )"Ultra_Task",
			(uint16_t       )ULTRA_STK_SIZE,
			(void*          )NULL,
			(UBaseType_t    )ULTRA_TASK_PRIO,
			(TaskHandle_t*  )&Ultra_Task_Handle);

	xTaskCreate((TaskFunction_t )Disp_Task,
			(const char*    )"Disp_Task",
			(uint16_t       )DISP_STK_SIZE,
			(void*          )NULL,
			(UBaseType_t    )DISP_TASK_PRIO,
			(TaskHandle_t*  )&Disp_Task_Handle);

	xTaskCreate((TaskFunction_t )Heartbeat_Task,
			(const char*    )"Heartbeat_Task",
			(uint16_t       )HEARTBEAT_STK_SIZE,
			(void*          )NULL,
			(UBaseType_t    )HEARTBEAT_TASK_PRIO,
			(TaskHandle_t*  )&Heartbeat_Task_Handle);

	vTaskDelete(StartTask_Handler);

	taskEXIT_CRITICAL();
}
