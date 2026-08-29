# 02. hw 계층

## 목적

회로도에서 뽑은 핀맵을 `hw_def.h` 하나에 모으고, 필요한 드라이버를 기존 프로젝트에서
이식한다.

## 대상 파일

- `src/hw/hw_def.h` — 이 보드의 유일한 설정 파일
- `src/hw/hw.c` / `hw.h`
- `src/hw/driver/{led,gpio,uart,rtc,reset,flash,log,fault,assert}.c`

## 핀맵 (회로도 `hardware/STM32H7xx SchDoc V12.pdf`, WeAct STM32H7XX Board V1.2)

| 기능 | 핀 | 비고 |
|---|---|---|
| LED | **PE3** | R6 1.5K → VT2(PDTC114ET NPN) → 파란 LED. **active high** |
| 사용자 키 K1 | **PC13** | SW2, R8 330R |
| UART1 | **PA9**(TX) / **PA10**(RX) | AF7. P1/P2 헤더로 나와 있음 |
| USB | **PA11**(DM) / **PA12**(DP) | OTG_FS. VBUS 는 MCU 에 **연결 안 됨** |
| QSPI | CLK=**PB2** NCS=**PB6** IO0=**PD11** IO1=**PD12** IO2=**PE2** IO3=**PD13** | W25Q64 8MB |
| LCD | SCL=**PE12** SDA=**PE14** DC=**PE13** CS=**PE11** BL=**PE10** | SPI4, ST7735S 160×80 |
| microSD | D0~D3=PC8~PC11, CK=PC12, CMD=PD2 | SDMMC1. 이번 범위 밖 |
| 2번째 SPI 플래시 | SCK=PB3 MISO=PB4 MOSI=PD7 CS=PD6 | U8, 대개 미실장 |
| SWD | PA13 / PA14 | |

**LCD_RESET 은 GPIO 가 아니다.** 보드의 `SYS_RESET` 네트에 물려 있어 패널이 MCU 와 함께
리셋된다. 따라서 `st7735Reset()` 은 딜레이만 주는 no-op 이어야 한다.

**LCD 는 SPI4 다.** PE12=SPI4_SCK, PE14=SPI4_MOSI 가 AF 로 맞아떨어진다. PE13(DC)/PE11(CS)은
일반 GPIO, PE10 은 TIM1_CH2N 이라 백라이트 PWM 이 가능하다.

## 회로도 읽기

PDF 에 벡터 텍스트가 살아 있어서 좌표까지 뽑아 재구성했다. 핀 이름과 네트 라벨이 서로
다른 위치에 그려져 있어 단순 텍스트 추출로는 매칭이 안 되고, `Tm`/`Td` 행렬을 추적해
같은 y 좌표끼리 묶어야 한다.

```python
# streams -> zlib.decompress -> Tm/Td 로 좌표 추적 -> y 로 클러스터링
```

QSPI 블록이 이렇게 나온다 (y 내림차순):

```
550.9  QSPI
544.9  PD13          <- QSPI_BK1_IO3
538.9  PE2           <- QSPI_BK1_IO2
532.9  PD12          <- QSPI_BK1_IO1
526.9  PD11          <- QSPI_BK1_IO0
520.9  PB2           <- QSPI_CLK
514.9  PB6           <- QSPI_BK1_NCS
```

## 이식한 드라이버

| 드라이버 | 출처 | 수정 |
|---|---|---|
| `log.c` `assert.c` | `stm32h5-boot` | 없음 (MCU 비의존) |
| `fault.c` | `stm32h5-boot` | PC 유효 범위를 H7 기준으로 |
| `led.c` | `stm32h5-boot` | 핀 테이블 (PE3, active high) |
| `gpio.c` | `stm32h5-boot` | 핀 테이블 |
| `uart.c` | `stm32h5-boot` | **GPDMA → H7 DMA** (아래 참고) |
| `rtc.c` | `convex-boot` (H743) | 없음 |
| `reset.c` | `stm32h5-boot` | H7 플래그 이름, ECC 제거, `resetToBoot(with_msc)` |
| `flash.c` | 새로 작성 | 내부 플래시는 읽기 전용 |

### uart.c — H5 GPDMA → H7 DMA

`stm32h5-boot` 쪽을 기반으로 삼은 이유는 **가상 채널(`uartSetDriver`)** 이 있기 때문이다.
`convex-boot` 에는 없는데, 6단계에서 CMD CLI 채널에 필요하다.

바꾼 것:

- `DMA_NodeTypeDef`/`DMA_QListTypeDef`(GPDMA 링크드리스트) → `DMA_HandleTypeDef hdma_usart1_rx`
- `__HAL_RCC_GPDMA1_CLK_ENABLE()` → `__HAL_RCC_DMA1_CLK_ENABLE()`
- MspInit 을 `DMA1_Stream0` + `DMA_REQUEST_USART1_RX` + PA9/PA10 AF7 로
- `UART_ADVFEATURE_SWAP_ENABLE` 제거 (이 보드는 TX/RX 를 바꿔 달지 않았다)
- **수신 잔량 읽기** — 아래 함정 참고

수신은 순환 DMA 로 링버퍼에 계속 받아두고, `uartAvailable()` 에서 DMA 잔량으로 쓰기 위치를
역산한다. 인터럽트는 쓰지 않는다.

### flash.c — 주소로 대상 판별

이 보드에는 플래시가 둘이다. `boot.c`/`uf2.c` 가 같은 `flashXxx()` API 만 쓰도록 주소로
대상을 가른다.

- `0x08000000` 대 = 내부 플래시 → **읽기만**. 섹터가 1개라 지우면 부트로더가 날아간다.
- `0x90000000` 대 = QSPI → 2단계에서 `qspi.c` 로 연결한다.

`flashIsProtected()` 가 마지막 방어선이다.

### reset.c — H7 플래그 이름

| H5 | H7 |
|---|---|
| `RCC_FLAG_IWDGRST` | `RCC_FLAG_IWDG1RST` |
| `RCC_FLAG_WWDGRST` | `RCC_FLAG_WWDG1RST` |
| — | `RCC_FLAG_PORRST` 추가 |

ECC 관련(`resetGetEccAddr`/`resetClearEccAddr`)은 통째로 제거했다. H5 는 플래시 2비트 ECC
오류를 NMI 로 올리지만 H7 은 그렇지 않다.

`MODE_BIT_UPDATE` 를 **`MODE_BIT_MSC`** 로 바꿨다. 앱이 부트로더를 부를 때 USB 구성을
고를 수 있어야 하기 때문이다.

## 함정

- **H7 의 `DMA_HandleTypeDef.Instance` 는 `void *` 다.** DMA_Stream 과 BDMA_Channel 을 겸해서
  그렇다. H5 코드의 `->Instance->CBR1` 을 `->Instance->NDTR` 로만 바꾸면
  `request for member 'NDTR' in something not a structure or union` 이 난다.
  **`__HAL_DMA_GET_COUNTER(handle)`** 매크로를 쓸 것.
- **`hw_def.h` 에 `HW_USE_CDC 1` 이 있어도 `_USE_HW_CDC` 가 꺼져 있을 수 있다.**
  `uart.c` 의 가드를 `#if HW_USE_CDC == 1` 에서
  `#if defined(_USE_HW_CDC) && (HW_USE_CDC == 1)` 로 고쳤다.
- **`rtcInit()` 이 `resetInit()` 보다 먼저**여야 한다. `resetInit()` 이 RTC 백업 레지스터로
  리셋 카운트와 부트 모드를 읽는다.
- **`usbInit()` 은 `hwInit()` 에 없다.** `bootUp()` 이 "앱으로 점프하지 않는다"고 판단한 뒤
  `apInit()` 에서 연다. 그렇지 않으면 정상 부팅마다 호스트에 USB 장치가 나타났다 사라진다.

## 검증 방법

1. 빌드 후 `-print-memory-usage`
2. 보드에 올려 LED 500ms 점멸
3. PA9/PA10 에 USB-시리얼을 물려 115200 으로 부팅 배너 확인
4. `cli` 프롬프트에서 `reset info`, `gpio info`, `flash info`

## 실측 결과

**[미확인]** — 아직 실기에서 돌리지 않았다. 빌드만 통과.
