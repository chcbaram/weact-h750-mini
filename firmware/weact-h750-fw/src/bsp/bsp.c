#include "bsp.h"
#include "hw_def.h"


extern void cliLoopIdle(void);


static void SystemClock_Config(void);
static void PeriphCommonClock_Config(void);
static void bspMpuInit(void);




bool bspInit(void)
{
  //-- 캐시는 HAL_Init() 보다 먼저 켠다.
  //   HAL_Init() 안에서 이미 메모리를 만지기 시작하므로, 중간에 캐시를 켜면
  //   캐시/메모리 불일치가 생길 수 있다.
  //
  SCB_EnableICache();
  SCB_EnableDCache();

  HAL_Init();

  SystemClock_Config();
  PeriphCommonClock_Config();

  /*
   * D2 도메인 SRAM(SRAM1/2/3, 0x30000000~) 은 리셋 직후 클럭이 꺼져 있다.
   * 켜지 않고 접근하면 버스 폴트가 난다.
   *
   * 이 프로젝트는 .non_cache 섹션(= DMA 버퍼, LCD 프레임버퍼)을 D2 SRAM 에 두므로
   * 반드시 hwInit() 보다 먼저, 즉 여기서 켜야 한다.
   *
   * 실기에서 확인 : 이걸 빼먹으면 uartOpen() 의 HAL_UART_Init() 에서 HardFault 가
   * 나고, faultReset() 이 리셋을 걸어 부팅 루프에 빠진다.
   */
  __HAL_RCC_D2SRAM1_CLK_ENABLE();
  __HAL_RCC_D2SRAM2_CLK_ENABLE();
  __HAL_RCC_D2SRAM3_CLK_ENABLE();

  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();

  bspMpuInit();

  return true;
}

/*
 * 앱으로 점프하기 직전 정리.
 *
 * 인터럽트를 전부 막지 않으면, 앱이 벡터 테이블(VTOR)을 옮기기 전에 남아 있던
 * 인터럽트가 떠서 부트로더의 핸들러 주소로 뛰어버린다.
 *
 * MPU 는 여기서 끄지 않는다. 앱이 자기 bspInit() 에서 다시 설정하고, 그 전까지는
 * QSPI 영역이 실행 가능해야 하기 때문이다.
 */
bool bspDeInit(void)
{
  for (int i=0; i<8; i++)
  {
    NVIC->ICER[i] = 0xFFFFFFFF;
    NVIC->ICPR[i] = 0xFFFFFFFF;
    __DSB();
    __ISB();
  }

  SysTick->CTRL = 0;
  SysTick->LOAD = 0;
  SysTick->VAL  = 0;

  //-- QSPI 를 새로 구웠다면 캐시에 옛 내용이 남아 있다. 반드시 비우고 넘긴다.
  SCB_CleanInvalidateDCache();
  SCB_InvalidateICache();

  return true;
}

void delay(uint32_t time_ms)
{
  uint32_t pre_time = millis();

  while (millis() - pre_time < time_ms)
  {
#ifdef _USE_HW_CLI
    //-- 블로킹 구간에서도 CLI/USB/모듈이 계속 돌게 한다.
    cliLoopIdle();
#endif
  }
}

void delayUs(uint32_t delay_us)
{
  uint32_t pre_time = micros();

  while (micros() - pre_time < delay_us)
  {
  }
}

uint32_t millis(void)
{
  return HAL_GetTick();
}

/*
 * SysTick 카운터로 마이크로초를 만든다.
 *
 * ms 와 VAL 을 두 번 읽어 비교하는 이유 : 두 읽기 사이에 SysTick 이 wrap 하면
 * ms 는 올라갔는데 VAL 은 큰 값이라 시간이 거꾸로 간다.
 */
uint32_t micros(void)
{
  uint32_t m0 = HAL_GetTick();
  uint32_t u0 = SysTick->VAL;
  uint32_t m1 = HAL_GetTick();
  uint32_t u1 = SysTick->VAL;
  uint32_t tms = SysTick->LOAD + 1;

  if (m1 > m0)
  {
    return (m1 * 1000 + ((tms - u1) * 1000) / tms);
  }
  else
  {
    return (m0 * 1000 + ((tms - u0) * 1000) / tms);
  }
}

void Error_Handler(void)
{
  __disable_irq();
  while (1)
  {
  }
}

/*
 * 클럭 트리
 *
 *   HSE 25MHz (X1)
 *     -> PLL1 : M=5 (5MHz) -> N=160 (VCO 800MHz) -> P=2 -> SYSCLK 400MHz
 *        D1CPRE=1 -> CPU 400MHz
 *        HPRE=2   -> HCLK/AXI 200MHz
 *        APB1~4=2 -> 100MHz
 *
 *   480MHz(VOS0) 로 올리려면 다이 리비전이 V 이상이어야 하고 오버드라이브가
 *   필요하다. 브링업 단계에서는 확실한 400MHz(VOS1)로 간다.
 *
 *   FLASH_LATENCY_2 : VOS1 에서 140 < HCLK <= 210MHz 구간의 값. HCLK 가 200MHz
 *   이므로 2 WS 다. HCLK 를 바꾸면 이 값도 같이 바꿔야 한다.
 *
 *   HSI48 : USB 48MHz 용. 이 보드는 VBUS 가 MCU 에 연결돼 있지 않아 CRS 의
 *           SYNC 소스로 USB SOF 를 쓰는 것과 무관하게 HSI48 자체는 필요하다.
 *   LSE   : RTC 백업 레지스터(리셋 카운터/부트 모드)를 위해 켠다.
 */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);
  while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
  {
  }

  //-- 백업 도메인(RTC 백업 레지스터) 쓰기 잠금 해제. resetInit() 이 여기 의존한다.
  HAL_PWR_EnableBkUpAccess();
  __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);

  RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_HSE
                                        | RCC_OSCILLATORTYPE_LSE
                                        | RCC_OSCILLATORTYPE_HSI48;
  RCC_OscInitStruct.HSEState            = RCC_HSE_ON;
  RCC_OscInitStruct.LSEState            = RCC_LSE_ON;
  RCC_OscInitStruct.HSI48State          = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM            = 5;
  RCC_OscInitStruct.PLL.PLLN            = 160;
  RCC_OscInitStruct.PLL.PLLP            = 2;
  RCC_OscInitStruct.PLL.PLLQ            = 8;
  RCC_OscInitStruct.PLL.PLLR            = 2;
  RCC_OscInitStruct.PLL.PLLRGE          = RCC_PLL1VCIRANGE_2;   // 4 ~ 8 MHz
  RCC_OscInitStruct.PLL.PLLVCOSEL       = RCC_PLL1VCOWIDE;      // 192 ~ 836 MHz
  RCC_OscInitStruct.PLL.PLLFRACN        = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK   | RCC_CLOCKTYPE_SYSCLK
                                   | RCC_CLOCKTYPE_PCLK1  | RCC_CLOCKTYPE_PCLK2
                                   | RCC_CLOCKTYPE_D3PCLK1| RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider  = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider  = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/*
 * 주변장치 클럭
 *
 *   QSPI : PLL2R = 200MHz. QUADSPI 프리스케일러 1 -> SCK 100MHz.
 *   USB  : HSI48
 *   RTC  : LSE
 *
 *
 * QSPI 를 D1HCLK 이나 PLL1Q 가 아니라 **PLL2** 에서 뽑는 것이 핵심이다.
 *
 *   부트로더가 memory-mapped 를 켜고 앱으로 점프하면, 앱은 QUADSPI 를 다시
 *   초기화하지 않는다. 못 한다 - 자기가 그 QSPI 에서 실행 중이기 때문이다.
 *   즉 **여기서 잡은 QSPI 설정이 그대로 앱의 XiP 설정이 된다.**
 *
 *   그런데 앱은 자기 SystemClock_Config() 를 돌린다. QSPI 커널 클럭을 PLL1 이나
 *   D1HCLK 에 물려두면, 앱이 SYSCLK 을 바꾸는 순간 자기가 실행 중인 플래시의
 *   타이밍이 같이 흔들린다.
 *
 *   실제 사례 : 아두이노 코어의 WeAct H750 variant 는 480MHz 로 올리면서
 *   QspiClockSelection = RCC_QSPICLKSOURCE_PLL (PLL1Q = 48MHz) 로 바꾼다.
 *   프리스케일러는 부트로더가 남긴 값 그대로라 SCK 가 50MHz -> 12MHz 로 떨어진다.
 *   앱이 죽지는 않지만 XiP 가 4배 느려진다.
 *
 *   PLL2 에서 뽑으면 앱이 PLL1 을 어떻게 만지든 QSPI 는 영향을 받지 않는다.
 *   (앱 쪽에서는 PeriphClockSelection 에서 RCC_PERIPHCLK_QSPI 를 빼야 한다)
 */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_QSPI
                                           | RCC_PERIPHCLK_USB
                                           | RCC_PERIPHCLK_RTC;

  /*
   * PLL2 : 25MHz / M=5 = 5MHz -> N=80 -> VCO 400MHz -> R=2 -> 200MHz
   *
   * 프리스케일러를 0(분주 없음)으로 두고 커널을 100MHz 로 하는 방법도 있지만,
   * 그렇게 하면 SSHIFT(half-cycle 샘플 시프트)를 함께 쓰기 어렵다. 커널을 200MHz 로
   * 올리고 프리스케일러 1 을 유지하는 쪽이 안전하다.
   */
  PeriphClkInitStruct.PLL2.PLL2M        = 5;
  PeriphClkInitStruct.PLL2.PLL2N        = 80;
  PeriphClkInitStruct.PLL2.PLL2P        = 2;
  PeriphClkInitStruct.PLL2.PLL2Q        = 2;
  PeriphClkInitStruct.PLL2.PLL2R        = 2;
  PeriphClkInitStruct.PLL2.PLL2RGE      = RCC_PLL2VCIRANGE_2;   // 4 ~ 8 MHz
  PeriphClkInitStruct.PLL2.PLL2VCOSEL   = RCC_PLL2VCOWIDE;      // 192 ~ 836 MHz
  PeriphClkInitStruct.PLL2.PLL2FRACN    = 0;

  PeriphClkInitStruct.QspiClockSelection = RCC_QSPICLKSOURCE_PLL2;
  PeriphClkInitStruct.UsbClockSelection  = RCC_USBCLKSOURCE_HSI48;
  PeriphClkInitStruct.RTCClockSelection  = RCC_RTCCLKSOURCE_LSE;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/*
 * MPU
 *
 *  TEX C B  의미
 *  ----------------------------------------------------------------
 *   0  0 0  Strongly Ordered
 *   0  0 1  Device (shared)
 *   0  1 0  Normal, Write-through, no write allocate
 *   0  1 1  Normal, Write-back, no write allocate
 *   1  0 0  Normal, Non-cacheable
 *   1  1 1  Normal, Write-back, write and read allocate
 *
 * BaseAddress 는 Size 에 정렬돼 있어야 한다. 안 그러면 조용히 엉뚱한 영역이 잡힌다.
 */
void bspMpuInit(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  HAL_MPU_Disable();

  //-- R0 : 정의되지 않은 영역 전체를 막는다.
  //   SubRegionDisable = 0x87 -> 512MB 단위 서브리전 0,1,2,7 을 비활성화해
  //   0x00000000~0x5FFFFFFF (플래시/RAM/주변장치) 와 0xE0000000~ (PPB) 는
  //   기본 메모리맵을 그대로 쓰게 두고, 0x60000000~0xDFFFFFFF 만 차단한다.
  //   QSPI(0x90000000)도 여기 걸리지만 아래 R15 가 덮어쓴다(번호가 클수록 우선).
  MPU_InitStruct.Number           = MPU_REGION_NUMBER0;
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.BaseAddress      = 0x00000000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  //-- R1 : AXI SRAM 512KB. .data/.bss/stack 이 여기 있다. Write-through.
  MPU_InitStruct.Number           = MPU_REGION_NUMBER1;
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.BaseAddress      = 0x24000000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_512KB;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_CACHEABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL0;
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  //-- R2 : D2 SRAM. LCD 프레임버퍼와 SPI4 DMA 버퍼(.non_cache)가 여기 있다.
  //   DMA 가 직접 읽는 메모리라 non-cacheable 로 잡아야 캐시 유지보수 없이 안전하다.
  //   실제 SRAM1+2+3 은 288KB 지만 MPU 크기는 2의 거듭제곱이어야 해서 512KB 로 잡는다.
  MPU_InitStruct.Number           = MPU_REGION_NUMBER2;
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.BaseAddress      = 0x30000000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_512KB;
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;             // Normal, Non-cacheable
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_DISABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /*
   * R15 : QSPI. 앱이 여기서 XiP 로 실행되므로 반드시 **실행 가능**해야 한다.
   *   DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE 이 "실행 허용" 이다. 이름이 헷갈린다.
   *
   * 두 가지가 중요하다.
   *
   * 1) 크기를 실제 플래시(8MB)에 정확히 맞춘다.
   *    16MB 로 잡으면 0x90800000~0x90FFFFFF 에는 플래시가 없는데 Normal 메모리로
   *    보인다. Cortex-M7 은 Normal 메모리를 **투기적으로(speculative) 프리페치**
   *    하므로, 없는 영역을 읽으려다 QUADSPI 를 엉뚱한 상태로 만든다.
   *
   * 2) 부트로더에서는 **non-cacheable** 로 둔다 (TEX=1, C=0, B=0).
   *    부트로더는 QSPI 에서 실행하지 않으므로 캐시가 필요 없고, 오히려 자기가
   *    다시 굽는 메모리를 캐싱하면 stale 읽기가 끝없이 생긴다.
   *    실기에서 이것 때문에 물렸다 - 소거/쓰기 직후 읽기가 옛 내용으로 나왔다.
   *
   *    앱은 자기 bspInit() 에서 cacheable 로 다시 잡아 XiP 성능을 얻는다.
   *    점프 직후 앱이 MPU 를 다시 잡기 전까지는 non-cacheable 로 실행되는데,
   *    느릴 뿐 동작에는 문제가 없다.
   */
  MPU_InitStruct.Number           = MPU_REGION_NUMBER15;
  MPU_InitStruct.Enable           = MPU_REGION_ENABLE;
  MPU_InitStruct.BaseAddress      = 0x90000000;
  MPU_InitStruct.Size             = MPU_REGION_SIZE_8MB;      // 실제 W25Q64 = 8MB
  MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
  MPU_InitStruct.IsBufferable     = MPU_ACCESS_NOT_BUFFERABLE;
  MPU_InitStruct.IsCacheable      = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsShareable      = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.TypeExtField     = MPU_TEX_LEVEL1;           // Normal, non-cacheable
  MPU_InitStruct.SubRegionDisable = 0x00;
  MPU_InitStruct.DisableExec      = MPU_INSTRUCTION_ACCESS_ENABLE;
  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}
