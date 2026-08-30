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
| MPU | 활성 (`MPU_CTRL = 0x5`, ENABLE + PRIVDEFENA). 리전 표는 03 문서 |
| MSP | **설정하지 않는다.** 앱 `Reset_Handler` 의 `ldr sp, =_estack` 이 잡는다<br>(**단, 부트로더에 RTOS 를 켜면 조건이 깨진다 — 아래 참고**) |
| VTOR | **건드리지 않는다.** 앱 `SystemInit()` 이 옮긴다 |
| CONTROL | 0. 앱은 처음부터 끝까지 **MSP** 로 돈다 (PSP 미사용) |
| NVIC | 점프 직전 전부 disable + clear, SysTick 정지, 캐시 clean/invalidate |

### 조건부 — 부트로더에 RTOS 를 켜면 MSP/CONTROL 을 넘겨야 한다

지금은 `_USE_HW_RTOS` 가 어디에도 정의돼 있지 않아 `main()` 이 베어메탈 분기를 타고,
`CONTROL = 0` 이므로 앱의 `ldr sp, =_estack` 이 **MSP** 를 잡는다. 그래서 부트로더가
MSP 를 설정하지 않아도 무해하다.

**`main.c` 의 `#ifdef _USE_HW_RTOS` 분기를 켜는 순간 이것이 버그가 된다.**

`bootJumpFirm()` 이 태스크 컨텍스트에서 불리면 `CONTROL.SPSEL = 1` 이다. 그 상태로
넘기면 앱의 `ldr sp, =_estack` 이 MSP 가 아니라 **PSP** 를 잡는다. 결과:

- 앱의 스레드 코드는 PSP(정상 위치)에서 돈다 — 겉보기엔 잘 동작한다
- 그런데 **모든 예외/인터럽트 핸들러는 MSP 를 쓴다.** 그 MSP 는 부트로더가
  남긴 낡은 값이다
- 부트로더와 앱의 `_estack` 이 **둘 다 `0x24080000`** 이라 그 낡은 MSP 는
  앱의 스택 영역 한복판을 가리키고, 인터럽트가 뜰 때마다 그 아래를 밟는다
- 증상이 **"특정 변수 배치에서만 죽는다"** 로 나온다. 원인을 짚기 대단히 어렵다

켤 때 필요한 코드는 이것이다 (`qspiSetXipMode(true)` 다음, `app_entry()` 직전).

```c
__set_CONTROL(0);
__ISB();
__set_MSP(vec0);          // 벡터 word[0] = 앱의 초기 MSP
```

`vec0` 은 `bootIsValidVector()` 가 이미 RAM 범위로 검증하는 값이라 따로 검사할
필요는 없다. **지금은 불필요하고 미검증이므로 코드로 넣지 않았다** — 점프 경로는
이 프로젝트에서 가장 예민한 부분이라, RTOS 를 실제로 켤 때 실기 검증과 함께 넣는다.
`boot.c` 의 `app_entry()` 직전에 같은 내용을 주석으로 남겨 두었다.

다른 세션(`hancheol-5a`)이 지적했다.

### 메모리 속성 실측 (2026-08-30, 앱 실행 중 SWD 덤프)

```
MPU_TYPE 0x00001000   DREGION=16
MPU_CTRL 0x00000005   ENABLE=1, PRIVDEFENA=1
R0  RBAR 0x00000000   RASR 0x1004873f
R1  RBAR 0x24000001   RASR 0x13020025   AXI SRAM 512KB
R2  RBAR 0x30000002   RASR 0x130c0025   D2 SRAM (구 512KB 판)
CCR      0x00070200   DC=1 IC=1 BP=1 STKALIGN=1
```

**R1(AXI SRAM) = `TEX=000, C=1, B=0` → Normal, write-through, no write allocate.**

이것이 앱 쪽 캐시 코드에 직접 영향을 준다. **write-through 영역에는 dirty 라인이
존재할 수 없다.** 모든 스토어가 즉시 메모리까지 나가므로

- `SCB_CleanDCache_by_Addr()` 는 사실상 no-op 이다
- `SCB_InvalidateDCache_by_Addr()` 는 clean 라인만 버리므로 **이웃 데이터를 잃지
  않는다.** 범위를 캐시 라인 경계로 넓혀도 데이터가 파괴되지 않는다

DMA 버퍼를 굳이 AXI 에 둘 이유가 없다면 **RAM_D2(`0x30000000`)** 로 옮기는 쪽이
낫다. 거기는 Normal **non-cacheable** 이라 캐시 유지보수가 아예 필요 없다.

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
**부트로더의 리전 표가 곧 앱의 최종 메모리 속성이다.**

주의 하나: **AXI SRAM(R1)은 XN 이다.** ldscript 가 `*(.RamFunc)` 를 `.data` 에 넣고
`.data` 가 `RAM_D1` 로 가면, `__RAM_FUNC` 코드가 생기는 순간 MemManage 로 죽는다.
MPU 를 풀지 말고 **ITCM 으로 보내면 된다** — R0 의 `SubRegionDisable = 0x87` 이
서브리전 0(`0x00000000~0x1FFFFFFF`)을 비활성화하므로 ITCM 은 기본 메모리맵
(Normal, 실행 가능)을 따른다. 0-wait 라 성능도 낫다.

### 앱의 `SCB_EnableDCache()` 에 가드가 있는지 확인할 것

부트로더가 **I-Cache/D-Cache 를 켠 채로** 넘긴다. 앱이 다시 켜는 것 자체는 흔하다
(아두이노 코어는 `main.cpp` 의 `premain()` 에서 부른다).

문제는 CMSIS 버전이다. 최신 구현에는 가드가 있다.

```c
if (SCB->CCR & SCB_CCR_DC_Msk) return;   /* 이미 켜져 있으면 no-op */
```

**가드가 없는 옛 CMSIS 는 `SCB_InvalidateDCache()`(clean 이 아니라 set/way 전체
무효화)를 먼저 한다.** 그러면 `premain()` 직전까지 startup 이 수행한 `.data` 복사와
`.bss` 클리어가 dirty 상태로 캐시에 있다가 통째로 버려진다.

지금 조합에서는 문제가 없다 — R1 이 write-through 라 애초에 dirty 가 없고, 아두이노가
쓰는 CMSIS 6.2.0(`armv7m_cachel1.h:145`)에도 가드가 있다(다른 세션이 확인).
**다만 코어/툴체인 버전을 옮기면 다시 살아날 수 있는 함정이다.**

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
