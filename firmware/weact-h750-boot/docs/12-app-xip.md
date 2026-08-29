# 12. 앱(XiP) 쪽 규약

## 목적

QSPI 에서 XiP 로 실행되는 애플리케이션이 지켜야 할 것과, 부트로더가 넘겨주는 상태를
확정한다. **아두이노 코어(별도 저장소)와 자체 앱 프로젝트가 모두 이 문서를 따른다.**

## 부트로더가 넘겨주는 상태 (확정)

앱이 **다시 안 해도 되는 것**:

| 항목 | 상태 |
|---|---|
| 전원 | `PWR_LDO_SUPPLY`, **VOS1** |
| FLASH latency | `FLASH_LATENCY_2` |
| HSE / LSE / HSI48 | 전부 **ON**. 백업 도메인 쓰기 잠금 해제됨 |
| PLL1 | HSE 기반 M=5 N=160 P=2 Q=8 R=2 → SYSCLK **400MHz** |
| PLL2 | **사용 안 함** |
| PLL3 | **사용 안 함** — 앱이 자유롭게 쓸 것 |
| 버스 | AHB=DIV2(**200MHz**), APB1~4=DIV2(**100MHz**) |
| QSPI 커널 | **`RCC_QSPICLKSOURCE_D1HCLK`** |
| QUADSPI | memory-mapped 진입 상태, prescaler=1 → **SCK = HCLK/2**, 0xEB quad I/O,<br>24bit 주소, dummy 4, AlternateBytes 0x20, **SIOO=INST_ONLY_FIRST_CMD**,<br>TimeOut ENABLE/0x20 |
| USB 커널 | `RCC_USBCLKSOURCE_HSI48` |
| RTC | `RCC_RTCCLKSOURCE_LSE` |
| D2SRAM1/2/3 | 클럭 **ON** |
| GPIO 클럭 | A, B, C, D, E, H **ON** |
| 캐시 | I-Cache, D-Cache **ON** |
| MPU | 활성. **R15 = QSPI 0x90000000 8MB, write-through cacheable, 실행 허용** |
| NVIC | 점프 직전 전부 disable + clear, SysTick 정지, 캐시 clean/invalidate |

## 앱이 절대 하면 안 되는 것

### 1. QSPI 커널 클럭을 PLL 에 물리지 말 것

앱의 `SystemInit()` 은 `main()` 보다 먼저 돌면서 RCC 를 리셋한다.

```c
RCC->CFGR = 0x00000000;        // SYSCLK -> HSI
RCC->CR  &= 0xEAF6ED7FU;       // HSE/HSI48/PLL1/PLL2/PLL3 전부 OFF
```

**PLL 계열을 QSPI 커널로 쓰면 앱이 자기 명령어 클럭을 스스로 끊는다.**
`D1HCLK` 만 SYSCLK 을 따라 64MHz 로 강등될 뿐 살아남는다. 03 문서 참고.

`PeriphClockSelection` 에서 `RCC_PERIPHCLK_QSPI` 를 **빼는 것이 가장 안전하다.**
부트로더가 잡아둔 D1HCLK 이 그대로 유지된다.

### 2. `RCC_PERIPHCLK_RTC` 를 넣지 말 것

`HAL_RCCEx_PeriphCLKConfig()` 는 RTC 소스가 **다르면 백업 도메인을 리셋**한다.

```c
if ((RCC->BDCR & RCC_BDCR_RTCSEL) != (PeriphClkInit->RTCClockSelection & RCC_BDCR_RTCSEL))
{
  __HAL_RCC_BACKUPRESET_FORCE();
  __HAL_RCC_BACKUPRESET_RELEASE();
}
```

백업 도메인 리셋 = **RTC 백업 레지스터 전멸**이다. 부트 모드 플래그(DR3), 리셋
카운트(DR5), boot_try(DR6), fault 카운트(DR7)가 날아가 `resetToBoot()` 이 조용히
동작하지 않고 리셋 더블탭도 깨진다.

넣어야 한다면 반드시 `RCC_RTCCLKSOURCE_LSE` (부트로더와 동일)여야 한다.

### 3. QUADSPI 를 다시 초기화하지 말 것

원천적으로 불가능하다. 자기가 그 QSPI 에서 실행 중이다.

## 앱이 해야 하는 것

### PLL1 을 설정한다

`SystemInit()` 이 SYSCLK 을 HSI 로 되돌리므로, `HAL_RCC_OscConfig()` 의 가드

```c
if (__HAL_RCC_GET_SYSCLK_SOURCE() != RCC_CFGR_SWS_PLL1)   // rcc.c:760
```

가 **통과한다.** 즉 앱은 PLL1 을 정상적으로 설정할 수 있고, **해야 한다.**
안 하면 HSI 64MHz 로 돈다 (전속의 1/6).

> 실기 확인 : PLL1 을 복구하지 않은 아두이노 Blink 에서
> `SystemCoreClock = 0x03D09000 = 64,000,000` 이었다.

### `firm_ver_t` 를 `.version` 섹션에 내보낸다

이것만 있으면 부트로더가 **VER 단계로 인식하고 CRC 를 계산해 TAG 로 자동 승격**한다.
빌드 후처리로 태그를 붙일 필요가 없다 (07 문서).

```c
extern uint32_t _fw_flash_begin, _fw_flash_size;
__attribute__((section(".version"), used))
const firm_ver_t firm_ver = {
  .magic_number = VERSION_MAGIC_NUMBER,          // "VER "
  .version_str  = _DEF_FIRMWATRE_VERSION,
  .name_str     = _DEF_BOARD_NAME,
  .firm_addr    = (uint32_t)&_fw_flash_begin,
  .firm_size    = (uint32_t)&_fw_flash_size,
};
```

> **함정** : `(uint32_t)&_fw_flash_end - (uint32_t)&_fw_flash_begin` 을 정적 초기화식에
> 쓰면 **링커가 조용히 0 으로 접는다.** 심볼 두 개의 차이는 단일 relocation 으로
> 인코딩되지 않는데 경고도 없다.
> 링커스크립트에서 `_fw_flash_size = _fw_flash_end - _fw_flash_begin;` 으로 절대 심볼을
> 만들고 C 에서는 그 **주소**를 읽어야 한다.

CRC 범위는 `_fw_flash_begin` 부터 `firm_size` 바이트다. 즉 벡터 + `.version` +
`.text` + `.rodata` + `.data` 의 LMA 까지. **`.data` 의 LMA 가 FLASH 에 마지막으로
놓이는 것**이어야 한다.

### VTOR 을 옮긴다

부트로더는 VTOR/MSP 를 건드리지 않는다.
- MSP : 앱의 `Reset_Handler` 가 `ldr sp, =_estack`
- VTOR : 앱의 `SystemInit()` 이 `SCB->VTOR = &_fw_flash_begin`

**아두이노 코어는 다르다.** `system_stm32h7xx.c:291` 이
`SCB->VTOR = VECT_TAB_BASE_ADDRESS | VECT_TAB_OFFSET;` 로 **OR** 연산이고
`VECT_TAB_BASE_ADDRESS` 기본값이 `FLASH_BANK1_BASE`(0x08000000) 다.
`0x08000000 | 0x90000000 = 0x98000000` 이므로 `build.flash_offset` 으로는 안 된다.
`-DVECT_TAB_BASE_ADDRESS=0x90001000` 을 `build.st_extra_flags` 로 넘기고
`build.flash_offset` 은 `0x0` 으로 둔다.

### (선택) MPU 를 다시 잡는다

부트로더가 QSPI 를 write-through cacheable + 실행 허용으로 넘기므로 그대로 써도 된다.
성능을 더 원하면 write-back write-allocate 로 바꿀 수 있다.

아두이노 코어에는 **MPU 코드가 아예 없다** (`cores/`, `libraries/`, `variants/`
어디에도 `HAL_MPU` 호출이 없다). 그래서 부트로더가 제대로 넘겨주는 것이 중요하다.

## 실기 검증 결과 (아두이노 Blink)

```
빌드 : Arduino_Core_STM32 기반, 28,284 B
       .isr_vector 0x90001000 / .version 0x90001400 / .text 0x90001800
       firm_ver: magic "VER ", name WEACT-H750-MINI, version ARDUINO,
                 firm_addr 0x90001000, firm_size 28284 (= 파일 크기)

CDC 로 쓰기      0.09s (295 KB/s)
FW_END 전 -> VER,  FW_END 후 -> TAG,  crc 0x4D01
플래시 전수 대조 -> 111블록 전부 일치
점프 후 PC       -> HAL_GetTick / delay / getCurrentMillis (Blink 의 delay 루프)
LED (PE3)        -> 점멸 확인
폴트 카운터      -> 0 유지
SystemCoreClock  -> 64MHz (PLL1 미복구 상태)
```
