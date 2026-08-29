# 진행 상황

최종 갱신 : 2026-08-30

## 요약

| 단계 | 내용 | 상태 |
|---|---|---|
| 0 | 스캐폴딩 (CMake, HAL 이식, 링커스크립트) | **완료** |
| 1 | bsp (클럭/캐시/MPU) + 기본 hw 드라이버 | **완료 · 실기** |
| 2 | QSPI 드라이버 + XiP | **완료 · 실기** |
| 3 | LCD (SPI4 + DMA, ST7735S) | **완료 · 실기** |
| 4 | boot 모듈 + 이미지 식별 3단계 | **완료 · 실기** |
| 5 | TinyUSB (CDC + HID, 런타임 MSC) | **완료 · 실기** |
| 6 | cmd 프로토콜 (HID/CDC) | **완료 · 실기** |
| 7 | UF2 + MSC (FAT16) | **완료** (볼륨/README 확인. 실제 .uf2 드롭 미시험) |
| 8 | LCD 진행률 UI (+점프/에러 화면) | **완료 · 실기** |
| 9 | 앱 프로젝트 `weact-h750-fw` | **스캐폴딩만** (아래 참고) |
| 10 | 툴링 (openocd, tasks.json) | **완료 · 실기** (13 문서) |
| 11 | 아두이노 코어 연동 | **별도 세션에서 진행 중** |

## 최대 성과 — 아두이노 앱이 QSPI XiP 로 실행됨

다른 세션(`baram-stm32-arduino-d5`)이 빌드한 실제 Arduino Blink 를 CDC 로 올려
XiP 실행까지 확인했다.

```
바이너리 : build/arduino-blink-test/blink.bin  (28,284 B)
CDC 쓰기 : 0.09s (295 KB/s)
FW_END 전 -> VER,  후 -> TAG,  crc 0x4D01
플래시 전수 대조 -> 111블록 전부 일치
점프 후 PC -> HAL_GetTick / delay / getCurrentMillis  (Blink 의 delay(500) 루프)
LED(PE3) 점멸 육안 확인.  폴트 카운터 0 유지.
```

## 현재 빌드

```
FLASH : 97224 B 75.35% / 126 KB
D2RAM : 28,832 B (LCD 프레임버퍼 25,600 B)
```

## 클럭 (확정)

| 구간 | SYSCLK | HCLK | QSPI SCK |
|---|---|---|---|
| 부트로더 | 400MHz (PLL1 M=5 N=160 P=2) | 200MHz | **100MHz** |
| 앱 — SystemInit 직후 | 64MHz (HSI) | 64MHz | **32MHz** |
| 앱 — PLL1 복구 시 | **480MHz** | **240MHz** | **120MHz** — 실기 검증됨 |

**QSPI 커널 = `RCC_QSPICLKSOURCE_D1HCLK`, 프리스케일러 1 고정** →
`SCK = HCLK/2` 가 항상 성립. H7 최대 HCLK 240MHz 에서도 SCK 120MHz 로
W25Q64JV 규격(133MHz) 안에 자동으로 들어온다.

### SCK 120MHz XiP 실기 검증 (2026-08-30)

아두이노 세션이 480MHz 로 다시 빌드한 Blink(29,064 B)로 확인했다.

```
SystemCoreClock = 0x1C9C3800 = 480,000,000     ✓
RCC_CR          = 0x3303C025  PLL1 ON, PLL3 ON, PLL2 OFF, HSE ON
RCC_D1CFGR      = 0x00000048  D1CPRE=1, HPRE=2 -> HCLK 240MHz
RCC_D1CCIPR     = 0x00000000  QSPISEL=00 = D1HCLK (앱이 안 건드림)  ✓
QUADSPI_CR      = 0x01500319  PRESCALER=1 -> SCK 120MHz
```

**31초 소킹 결과**

```
PC 샘플 : 0x900070b4 / 0x9000708c / 0x9000533c  (전부 QSPI, 다양)
uwTick  : 30286 -> 61480 = 31,194 ms / 실제 31초  -> 오차 0.6%
폴트    : 0 (a55a0000)
```

SysTick 이 480MHz 기준으로 정확하고, **SCK 120MHz XiP 가 장시간 안정적이다.**
W25Q64JV 정격 133MHz 대비 여유 13MHz.

## 개발 환경

| | |
|---|---|
| 호스트 | macOS 12.7.6, Apple Silicon |
| 컴파일러 | arm-none-eabi-gcc 14.2.1 (homebrew) |
| 빌드 | cmake + **Unix Makefiles** (Ninja 불가) |
| 플래시/디버그 | **openocd 0.12.0** |
| HAL | STM32Cube_FW_H7 **V1.12.1** |
| TinyUSB | **0.20.0** (stm32h7r-fw 에서 벤더링) |

### 쓸 수 없는 것

- **`STM32_Programmer_CLI`** — Qt 가 macOS 13+ 요구. 이 머신은 12.7.6
- **pyocd** — 바이너리는 있으나 파이썬 모듈 없음
- **Ninja 제너레이터** — 툴체인 cmake 가 `CMAKE_MAKE_PROGRAM` 을 make 로 잡아 충돌

### 자주 쓰는 명령

대부분 VSCode 태스크로 있다 (13 문서). 손으로 칠 때는:

```bash
cd firmware/weact-h750-boot
cmake -S . -B build && cmake --build build -j8

# 부트로더 굽기 (QSPI 의 앱과 태그는 보존된다)
openocd -f tools/openocd/weact-h750.cfg \
        -c "program build/weact-h750-boot.elf verify reset exit"

# 지금 뭐가 돌고 있나  (0x900xxxxx = 앱, 0x080xxxxx = 부트로더)
openocd -f tools/openocd/weact-h750.cfg -c init -c halt -c "reg pc" -c resume -c shutdown

# QSPI 상태  (JEDEC + 태그 + 앱 벡터 테이블)
openocd -f tools/openocd/weact-h750-qspi.cfg \
        -c init -c "reset init" -c "flash info 1" -c "mdw 0x90000000 4" -c "mdw 0x90001000 4" -c shutdown

./tools/swd/swdlog.sh boot     # UART 없이 SWD 로 부팅 로그
./tools/swd/swdlog.sh list     # 전체 로그
```

**`reset_config srst_only` 를 쓰면 안 된다.** MAX809 수퍼바이저가 NRST 를 구동해서
halt 타이밍을 못 잡는다. `none separate` → SYSRESETREQ 를 쓴다 (13 문서).

CDC cmd 프로토콜 (**921600** 으로 열 것. 115200 이면 CLI 가 CDC 를 가져감):

```bash
cd tools/download   # cmdproto.py 의 SerialTransport / HidTransport 사용
```

HID 는 `hidapi` 필요. 사용자 환경을 건드리지 않으려면 격리 venv 에 설치.
매칭 조건: **VID 0x1209 + PID 0xB750(CDC+HID)/0xB751(+MSC)/0xB752(앱) + usage page 0xFF75**
`cmdproto.py` 의 `HidTransport` 가 세 PID 를 순서대로 시도한다.
(0xCAFE 는 TinyUSB 예제용이라 폐기. `1209:B010` 은 이미 남이 등록돼 있었다 — 08 문서)

### 화면을 눈으로 안 보고 확인하기

프레임버퍼가 D2 SRAM 에 그대로 남아 있고, 앱은 LCD 를 안 건드린다.

```bash
openocd -f tools/openocd/weact-h750.cfg \
        -c init -c halt -c "dump_image fb.bin 0x30000000 25600" -c resume -c shutdown
# RGB565 LE 160x80 -> PNG (11 문서에 변환 스니펫)
```

## 실기 확인된 값

| 항목 | 값 |
|---|---|
| `DBGMCU_IDCODE` | `0x20036450` → DEV_ID 0x450, **REV_ID 0x2003 = revision V** |
| `FLASH_SIZE` | `0x0080` = **128 KB** |
| 코어 | Cortex-M7 **r1p1** |
| UID | `001E0033 32335108 37333531` |
| QSPI JEDEC | `EF 40 17` = Winbond **W25Q64, 8MB** |
| ST-LINK | V2-1, VID:PID `0483:3752`, FW `V2J45M30` |
| CDC 포트 | `/dev/cu.usbmodem1412301` (위치에 따라 바뀜) |

## 비상 복구 — QSPI 이미지가 나빠 부팅이 막힐 때

부트로더가 유효해 보이는(그러나 실행 불가능한) 이미지로 점프해 죽으면 리셋해도
계속 같은 곳으로 뛴다. SWD 로 RTC 백업 레지스터에 부트 요청을 직접 쓴다.

```bash
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
  -c "init" -c "halt" -c "mww 0x5800405C 0x00000003" -c "reset run" -c "shutdown"
```

`0x5800405C` = RTC_BKP_DR3(`HW_RTC_BOOT_MODE`), `0x3` = `MODE_BIT_BOOT|MODE_BIT_MSC`.
`resetInit()` 이 읽고 바로 지우므로 **한 번만** 적용된다.
로그에 `stay : app request (msc on)` 이 뜨면 성공. 그 뒤 CDC/HID 로
`FW_BEGIN` + `FW_ERASE` 하면 깨끗해진다.

**부트로더를 새로 구워도 QSPI 의 앱과 TAG 는 완전히 보존된다** (실증됨).

### RTC 백업 레지스터 맵

| 주소 | 레지스터 | 용도 |
|---|---|---|
| `0x5800405C` | DR3 | `HW_RTC_BOOT_MODE` — bit0 BOOT, bit1 MSC |
| `0x58004060` | DR4 | reset_bits |
| `0x58004064` | DR5 | reset_count (magic `0xA55A0000`) |
| `0x58004068` | DR6 | boot_try |
| `0x5800406C` | DR7 | fault_count |

### 폴트 진단 (`.noinit`, 주소는 빌드마다 변함 — `arm-none-eabi-nm` 으로 확인)

`fault_bfar`, `fault_mmfar`, `fault_hfsr`, `fault_cfsr`, `fault_log`.
CFSR 해석: bit0 IACCVIOL(명령어 접근 위반), bit1 DACCVIOL, bit16 UNDEFINSTR.

## 실기에서 잡은 함정 (전부 문서에 상세 기록)

1. **D2 SRAM 클럭** — 리셋 직후 꺼져 있다. `.non_cache` 접근 시 HardFault (02 문서)
2. **MspDeInit 핀 목록** — MspInit 만 고치면 남의 핀을 리셋시킨다 (05 문서)
3. **LCD 백라이트 active LOW** — 레퍼런스의 `on_state` 로 극성 추론 금지 (05 문서)
4. **폰트 타입 낭비** — K_font/wWanToJohabTable/wEngFon, 총 20.8KB (05 문서)
5. **QSPI 소거 단위** — BSP 함수 이름이 뒤집혀 있다 (07 문서)
6. **QSPI 검증은 indirect 로** — memory-mapped 로 검증하지 않는다 (07 문서)
7. **SIOO 와 연속 읽기 모드** — 순차는 통과, 랜덤만 깨진다 (04 문서)
8. **`SystemInit()` 이 PLL2 를 끈다** — QSPI 커널은 D1HCLK 만 가능 (03/12 문서)
9. **점프 주소를 memory-mapped 로 재읽기 금지** (07 문서)
10. **`#if CFG_TUD_*` 앞에 `tusb.h`** — 없으면 파일이 조용히 사라진다 (09/10 문서)
11. **RTC 클럭 재선택 = 백업 도메인 리셋** (12 문서)
12. **SWD 의 SRST 가 안 먹는다** — MAX809 수퍼바이저. `reset_config none` (13 문서)
13. **openocd stmqspi 소거 단위는 64KB** — 태그 4KB 만 지우는 건 불가능 (13 문서)

## 남은 확인 (물리 조작 필요)

1. **NRST 버튼(SW3) 더블탭** — 300ms 안에 두 번. `RESET_BIT_PIN` 만 뜨는지
2. **BOOT0(SW1) + NRST 로 ROM DFU 진입** — 스키매틱 근거만 있음
3. **K1(PC13) 극성**
4. **실제 `.uf2` 드래그앤드롭**
5. **CDC+HID 전용 모드(PID 0xB750)** — 부트 모드 진입 경로가 전부 MSC 를 함께 켠다.
   앱이 `resetToBoot(with_msc=false)` 로 요청하는 경로가 생겨야 시험할 수 있다
