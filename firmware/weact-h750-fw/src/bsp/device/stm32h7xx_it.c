/* Includes ------------------------------------------------------------------*/
#include "bsp.h"
#include "fault.h"
#include "stm32h7xx_it.h"
#include "hw_def.h"


/******************************************************************************/
/*           Cortex Processor Interruption and Exception Handlers             */
/******************************************************************************/

/**
  * @brief This function handles Non maskable interrupt.
  *
  * STM32H7 에서 NMI 로 올라오는 것은 사실상 HSE 의 CSS(Clock Security System) 뿐이다.
  * 이 보드는 25MHz HSE 크리스탈에서 PLL 을 물려 쓰므로 HSE 가 죽으면 정상 동작이
  * 불가능하다. 여기서 멈추면 "복구 수단인 부트로더가 복구 불가 상태"가 되므로
  * 절대 while(1) 하지 않고 리셋한다.
  */
void NMI_Handler(void)
{
  NVIC_SystemReset();
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler_C(uint32_t *p_stack)
{
  faultReset("HardFault", p_stack);
  while (1)
  {
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler_C(uint32_t *p_stack)
{
  faultReset("MemManage", p_stack);
  while (1)
  {
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler_C(uint32_t *p_stack)
{
  faultReset("BusFault", p_stack);
  while (1)
  {
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler_C(uint32_t *p_stack)
{
  faultReset("UsageFault", p_stack);
  while (1)
  {
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
#ifndef _USE_HW_RTOS
void SVC_Handler(void)
{
}
#endif

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief This function handles Pendable request for system service.
  */
#ifndef _USE_HW_RTOS
void PendSV_Handler(void)
{
}
#endif

#ifdef _USE_HW_RTOS
extern void osSystickHandler(void);

void SysTick_Handler(void)
{
  osSystickHandler();
}
#else
#ifdef _USE_HW_SWTIMER
extern void swtimerISR(void);
#endif

/**
  * @brief This function handles System tick timer.
  */
void SysTick_Handler(void)
{
  HAL_IncTick();
#ifdef _USE_HW_SWTIMER
  swtimerISR();
#endif
}
#endif
