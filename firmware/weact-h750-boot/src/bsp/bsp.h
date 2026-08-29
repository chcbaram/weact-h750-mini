#ifndef BSP_H_
#define BSP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "def.h"
#include "stm32h7xx_hal.h"


//-- 하위 계층에서도 로그를 찍을 수 있도록 여기서 선언해 둔다.
//   (log.h 를 끌어오면 순환 의존이 생긴다)
void logPrintf(const char *fmt, ...);


bool bspInit(void);
bool bspDeInit(void);

void delay(uint32_t time_ms);
void delayUs(uint32_t delay_us);
uint32_t millis(void);
uint32_t micros(void);

void Error_Handler(void);


#ifdef __cplusplus
}
#endif

#endif
