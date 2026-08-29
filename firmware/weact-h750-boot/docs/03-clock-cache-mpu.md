# 03. 클럭 / 캐시 / MPU

## 목적

XiP 가 성립하려면 QSPI 영역이 **실행 가능하고 캐시 가능한** 메모리로 보여야 한다.
그리고 D-Cache 를 켠 채 DMA 를 쓰려면 DMA 버퍼는 **non-cacheable** 이어야 한다.
이 둘을 MPU 로 만든다.

## 대상 파일

- `src/bsp/bsp.c` — `SystemClock_Config()`, `PeriphCommonClock_Config()`, `bspMpuInit()`

## 클럭 트리

```
HSE 25MHz (X1)
  └ PLL1  M=5 (5MHz)  N=160 (VCO 800MHz)  P=2
      └ SYSCLK 400MHz
          D1CPRE=1  -> CPU   400MHz
          HPRE=2    -> HCLK/AXI 200MHz
          APB1~4=2  -> 100MHz
HSI48 -> USB 48MHz
LSE 32.768kHz -> RTC (백업 레지스터)
```

- `PLLRGE = RCC_PLL1VCIRANGE_2` (입력 4~8MHz 구간, 우리는 5MHz)
- `PLLVCOSEL = RCC_PLL1VCOWIDE` (192~836MHz, 우리는 800MHz)
- `PWR_REGULATOR_VOLTAGE_SCALE1` + `FLASH_LATENCY_2`

### 왜 480MHz 가 아니라 400MHz 인가

480MHz(VOS0)는 다이 리비전 V 이상에서만 되고 오버드라이브가 필요하다.

**이 보드는 rev V 로 확인됐다** (SWD 로 `DBGMCU_IDCODE` = `0x20036450`, REV_ID `0x2003`;
코어도 Cortex-M7 r1p1). 즉 480MHz 가 가능하다.

그래도 브링업은 400MHz 로 한다. 클럭을 올리면 `FLASH_LATENCY` 와 QSPI 분주비가 같이
움직이는데, 기본 동작이 검증되기 전에 변수를 늘릴 이유가 없다. QSPI/LCD/USB 가 다 돌고
나면 올린다.

480MHz 로 올릴 때 바꿀 것:
- `PWR_REGULATOR_VOLTAGE_SCALE1` → `SCALE0`
- `PLLN` 160 → 192 (VCO 960MHz, P=2 → 480MHz)
- HCLK 이 240MHz 가 되므로 `FLASH_LATENCY_2` → `FLASH_LATENCY_4`
- QSPI 커널 클럭도 240MHz 가 되므로 `ClockPrescaler` 재계산 (4 → 48MHz)

### FLASH_LATENCY 를 바꿔야 하는 조건

`FLASH_LATENCY_2` 는 **VOS1 에서 140 < HCLK ≤ 210MHz** 구간의 값이다. HCLK 가 200MHz 라
2 WS 다. **HPRE 나 SYSCLK 를 바꾸면 이 값도 반드시 같이 바꿔야 한다.** 너무 낮으면
조용히 오동작한다.

### 주변장치 클럭

| | 소스 | 비고 |
|---|---|---|
| QSPI | **`RCC_QSPICLKSOURCE_PLL2`** (PLL2R = 100MHz) | 프리스케일러 1 → SCK 50MHz |
| USB | `RCC_USBCLKSOURCE_HSI48` | |
| RTC | `RCC_RTCCLKSOURCE_LSE` | 백업 레지스터에 필요 |
| USART1 | `RCC_USART16CLKSOURCE_D2PCLK2` | MspInit 에서 설정 |

`HAL_PWR_EnableBkUpAccess()` 를 `SystemClock_Config()` 안에서 부른다. 이게 없으면
RTC 백업 레지스터 쓰기가 조용히 무시되고, 더블탭 카운터가 영영 0 이 된다.

### QSPI 클럭은 반드시 D1HCLK — 실기에서 물린 것

부트로더가 memory-mapped 를 켜고 앱으로 점프하면, **앱은 QUADSPI 를 다시 초기화하지
않는다. 못 한다** — 자기가 그 QSPI 에서 실행 중이기 때문이다.
즉 부트로더가 잡은 QSPI 설정이 그대로 앱의 XiP 설정이 된다.

처음에는 "앱이 PLL1(SYSCLK)을 바꿔도 흔들리지 않게" PLL2 에서 뽑았다. **틀렸다.**

앱의 `SystemInit()` 은 `main()` 보다 먼저 돌면서 RCC 를 리셋한다.

```c
RCC->CR  |= RCC_CR_HSION;      // HSI 켜고
RCC->CFGR = 0x00000000;        // SYSCLK -> HSI
RCC->CR  &= 0xEAF6ED7FU;       // HSE/HSI48/PLL1/PLL2/PLL3 전부 OFF
```

마지막 줄의 마스크를 비트로 풀면 **PLL2ON(bit26)이 꺼진다.**
앱은 QSPI 에서 XiP 로 실행 중이므로 **자기 명령어 클럭을 스스로 끊는다.**

> 실기 증상 : 점프는 성공하는데 PC 가 `0x900055C0` 에 고정된다. 그 주소는
> `SystemInit` 안의 RCC 쓰기 시퀀스 바로 그 자리였다. 5번 샘플링 전부 동일.

**`RCC->CFGR = 0` 이 `RCC->CR &=` 보다 먼저**라는 점이 핵심이다. SYSCLK 이 HSI 로
넘어간 뒤에 PLL 이 꺼지므로, **D1HCLK 은 64MHz 로 강등될 뿐 끊기지 않는다.**

XiP 에서 쓸 수 있는 클럭 소스는 이것뿐이다.

| 소스 | `SystemInit()` 후 | 판정 |
|---|---|---|
| **D1HCLK** | SYSCLK 따라 64MHz 로 강등, **살아있음** | **이것만 가능** |
| PLL1Q | PLL1ON 꺼짐 → 정지 | 불가 |
| PLL2R | PLL2ON 꺼짐 → 정지 | 불가 |
| CKPER(HSI) | HSI 유지 → 64MHz 고정 | 가능하나 SCK 32MHz 고정 |

`D1CCIPR.QSPISEL` 의 리셋값도 `00` = D1HCLK 이라, 앱이 그 레지스터를 리셋해도
같은 소스가 유지된다. 이 점도 D1HCLK 을 고를 이유다.

`github.com/chcbaram/stm32h750` (실제로 QSPI XiP 점프하는 프로젝트)도
`QspiClockSelection = RCC_QSPICLKSOURCE_D1HCLK` 을 쓴다.

### 프리스케일러를 1 로 고정하는 이유

`SCK = HCLK / 2` 가 항상 성립하게 만든다. 앱이 클럭을 어떻게 바꾸든
**H7 의 최대 HCLK 가 240MHz 이므로 SCK 는 120MHz 를 넘을 수 없고**,
W25Q64JV 규격(133MHz) 안에 자동으로 들어온다.

### 실측 클럭 (구간별)

| 구간 | SYSCLK | HCLK | QSPI SCK |
|---|---|---|---|
| 부트로더 | 400MHz (PLL1) | 200MHz | **100MHz** |
| 앱 — `SystemInit()` 직후 | 64MHz (HSI) | 64MHz | **32MHz** |
| 앱 — PLL1 복구 시 | 480MHz | 240MHz | **120MHz** |

실기에서 앱 실행 중 읽은 레지스터:

```
RCC_CR     = 0x3000C025   PLL1 OFF, PLL2 OFF, PLL3 ON(앱이 켬), HSI ON
RCC_CFGR   = 0x00000000   SYSCLK = HSI
RCC_D1CFGR = 0x00000000   D1CPRE=1, HPRE=1 -> HCLK 64MHz
RCC_D1CCIPR= 0x00000000   QSPISEL=00 = D1HCLK
QUADSPI_CR = 0x01500319   PRESCALER=1 -> SCK = HCLK/2 = 32MHz, EN=1
```

## 캐시

`bspInit()` 에서 **`HAL_Init()` 보다 먼저** 켠다.

```c
SCB_EnableICache();
SCB_EnableDCache();
HAL_Init();
```

`HAL_Init()` 안에서 이미 메모리를 만지기 시작하므로, 중간에 캐시를 켜면 캐시/메모리
불일치가 생길 수 있다.

## MPU 리전

TEX/C/B 조합:

| TEX | C | B | 의미 |
|---|---|---|---|
| 0 | 0 | 0 | Strongly Ordered |
| 0 | 1 | 0 | Normal, Write-through |
| 1 | 0 | 0 | Normal, Non-cacheable |
| 1 | 1 | 1 | Normal, Write-back + write/read allocate |

| # | 주소 | 크기 | 설정 |
|---|---|---|---|
| 0 | `0x00000000` | 4GB | No access, XN, `SubRegionDisable=0x87` |
| 1 | `0x24000000` | 512KB | AXI SRAM. Write-through, XN |
| 2 | `0x30000000` | 512KB | D2 SRAM. **Non-cacheable**, XN |
| 15 | `0x90000000` | 16MB | QSPI. Cacheable, **실행 허용** |

### R0 의 SubRegionDisable = 0x87

4GB 를 512MB 씩 8등분한 서브리전 중 **0, 1, 2, 7 을 비활성화**한다는 뜻이다
(비활성화 = 이 리전이 관여하지 않고 기본 메모리맵을 따름).

- 서브리전 0 (`0x00000000~0x1FFFFFFF`) — 내부 플래시. 기본맵 그대로
- 서브리전 1 (`0x20000000~0x3FFFFFFF`) — SRAM. 아래 R1/R2 가 덮는다
- 서브리전 2 (`0x40000000~0x5FFFFFFF`) — 주변장치
- 서브리전 7 (`0xE0000000~0xFFFFFFFF`) — PPB (SCB, NVIC)

나머지 `0x60000000~0xDFFFFFFF` 는 차단된다. QSPI(`0x90000000`)도 여기 걸리지만
**R15 가 덮어쓴다 — MPU 는 번호가 클수록 우선한다.**

### R15 가 이 프로젝트의 핵심

```c
MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;   // 실행 허용
```

이름이 헷갈리는데, `DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE` 이 **실행을 허용**한다는
뜻이다. 이게 없으면 앱으로 점프하는 순간 MemManage 폴트가 난다.

## 함정

- **`BaseAddress` 는 `Size` 에 정렬돼 있어야 한다.** 안 맞으면 조용히 엉뚱한 영역이 잡힌다.
  D2 SRAM 은 실제로 288KB(SRAM1+2+3)지만 MPU 크기가 2의 거듭제곱이어야 해서 512KB 로 잡았다.
  `0x30000000` 은 512KB 정렬이라 문제없다.
- **`bspDeInit()` 에서 MPU 를 끄지 않는다.** 앱이 자기 `bspInit()` 에서 다시 설정하고,
  그 전까지는 QSPI 영역이 실행 가능해야 한다. 대신 캐시는 반드시 비운다.

  ```c
  SCB_CleanInvalidateDCache();
  SCB_InvalidateICache();
  ```

  QSPI 를 새로 구웠다면 캐시에 옛 내용이 남아 있다. 이걸 빼먹으면 방금 구운 펌웨어가
  아니라 이전 것이 실행되거나 중간에 폭발한다.

## 검증 방법

1. 부팅 배너의 `Booting..Clock` / `Booting..HCLK` 가 400 / 200 인지
2. `micros()` 로 1ms 루프를 재서 실제 클럭 검증
3. QSPI XiP 후 앱 점프가 MemManage 없이 되는지 (4단계)

## 실측 결과

부팅 배너에서 확인 (SWD 로 RAM 로그 덤프):

```
Booting..Clock : 400 Mhz
Booting..HCLK  : 200 Mhz
```

QSPI 는 PLL2R(100MHz) / 프리스케일러 1 = SCK 50MHz 로 JEDEC ID 를 정상 읽는다
(`EF 40 17`). 캐시/MPU 를 켠 상태에서 D2 SRAM 의 `.non_cache` 접근도 정상이다
(단, D2 SRAM 클럭 인에이블이 필요하다 — STATUS.md 의 함정 항목 참고).
