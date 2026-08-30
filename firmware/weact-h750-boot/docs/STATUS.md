# 진행 상황

최종 갱신 : 2026-08-30 (V260830R3)

## 요약

| 단계 | 내용 | 상태 |
|---|---|---|
| 0 | 스캐폴딩 (CMake, HAL 이식, 링커스크립트) | **완료** |
| 1 | bsp (클럭/캐시/MPU) + 기본 hw 드라이버 | **완료 · 실기** |
| 2 | QSPI 드라이버 + XiP | **완료 · 실기** |
| 3 | LCD (SPI4 + DMA, ST7735S) | **완료 · 실기** |
| 4 | boot 모듈 + 이미지 식별 3단계 + 폴트 루프 차단 | **완료 · 실기** |
| 5 | TinyUSB (CDC + HID, 런타임 MSC) | **완료 · 실기** |
| 6 | cmd 프로토콜 (HID/CDC) | **완료 · 실기** |
| 7 | UF2 + MSC (FAT16) | **완료 · 실기** (드래그앤드롭 확인). 상한 8119 KB |
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
버전  : V260830R3
FLASH : 97,888 B  75.87% / 126 KB
RAM   : 37,952 B   7.24% / 512 KB (AXI)
D2RAM : 28,832 B   9.78% / 288 KB (LCD 프레임버퍼 25,600 B)
산출물: .elf / .bin / .hex / .map
```

## 다음 세션 시작점 — 9단계 `weact-h750-fw`

**부트로더(0~8단계)와 툴링(10단계)은 끝났다. 남은 것은 앱 프로젝트 하나다.**

`firmware/weact-h750-fw/` 는 아직 **부트로더 복사본**이다. 이름과 USB PID 만 바꿔둔
상태이고 링커스크립트가 `STM32H750VBTx_BOOT.ld` 라 내부 플래시로 링크된다.
빌드는 시도하지 않았다.

할 일은 `docs/16-roadmap.md` 9단계에 8항목으로 정리해 두었다. 요점만:

1. `src/bsp/ldscript/STM32H750VBTx_QSPI.ld` 새로 작성
   (`VECTOR 0x90001000 / VER 0x90001400 / FLASH 0x90001800`)
2. **`_fw_flash_size` 를 절대 심볼로** 만들 것 — 링커 접기 함정 (12 문서)
3. `system_stm32h7xx.c` 에 `SCB->VTOR = (uint32_t)&_fw_flash_begin;`
4. `hw_def.h` : `HW_DEV_MODE = HW_DEV_MODE_APP` (지금 BOOT 다)
5. `SystemClock_Config` 에 **`RCC_PERIPHCLK_QSPI` / `_RTC` 를 넣지 말 것** (12 문서)
6. post-build 에 `uf2conv.py --base 0x0 --family 0xFFFF0004`
7. 앱이 `resetConfirmBoot()` 을 부르면 `HW_BOOT_TRY_MAX` 를 켤 수 있다

앱이 생기면 **자체 제작 이미지로 3단계 식별(TAG/VER/RAW) 전 경로를 처음 끝까지**
검증할 수 있다. 지금은 아두이노 빌드로만 확인했다.

`.vscode` 태스크/런처는 앱 프로젝트에 이미 들어가 있다 (빌드가 되면 바로 쓸 수 있다).

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
cd firmware/weact-h750-boot
openocd -f tools/openocd/weact-h750.cfg \
  -c "init" -c "halt" -c "mww 0x5800405C 0x00000003" -c "reset run" -c "shutdown"
```

VSCode 태스크 **`Device - 부트 모드로 강제 진입`** 이 같은 일을 한다.

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
| `0x58004068` | DR6 | boot_try (`HW_BOOT_TRY_MAX = 0` 이라 현재 미사용) |
| `0x5800406C` | DR7 | fault_count (magic `0xA55A0000`) |

값은 전부 `0xA55A0000 | count` 형태다. 매직이 안 맞으면 0 으로 친다
(VBAT 없는 보드에서 백업 도메인이 쓰레기일 수 있다).

**폴트 루프로 막혔을 때** 는 DR7 을 지우면 된다.

```bash
openocd -f tools/openocd/weact-h750.cfg \
  -c "init" -c "halt" -c "mww 0x5800406C 0xA55A0000" -c "reset run" -c "shutdown"
```

반대로 **폴트 루프를 재현**하려면 카운터와 `.noinit` 매직을 함께 세운다
(`faultIsFaultBoot()` 가 true 여야 차단된다).

```bash
-c "mww 0x20000010 0x5555AAAA"    # fault_log.magic_number. 주소는 nm 으로 확인
-c "mww 0x5800406C 0xA55A0003"
```

### 폴트 진단 (`.noinit`, 주소는 빌드마다 변함 — `arm-none-eabi-nm` 으로 확인)

`fault_bfar`, `fault_mmfar`, `fault_hfsr`, `fault_cfsr`, `fault_log`.

SCB 레지스터를 SWD 로 직접 읽을 때의 순서 (**`0xE000ED30` 은 MMFAR 가 아니라 DFSR
이다** — 한 번 헷갈렸다):

| 주소 | 레지스터 |
|---|---|
| `0xE000ED28` | **CFSR** — bit0 IACCVIOL, bit1 DACCVIOL, bit7 MMARVALID, bit16 UNDEFINSTR |
| `0xE000ED2C` | **HFSR** — bit30 FORCED(에스컬레이션), bit1 VECTTBL |
| `0xE000ED30` | DFSR — 디버그용. halt 하면 값이 선다 |
| `0xE000ED34` | **MMFAR** — CFSR bit7(MMARVALID)이 설 때만 유효. IACCVIOL 은 채우지 않는다 |
| `0xE000ED38` | **BFAR** — CFSR bit15(BFARVALID)가 설 때만 유효 |
| `0xE000ED3C` | AFSR — 구현 정의. H7 에서는 쓰이지 않는다 |
| `0xE000ED24` | SHCSR — bit0 MEMFAULTACT, bit16 MEMFAULTENA |
| `0xE000ED04` | ICSR — VECTACTIVE[8:0] 로 지금 어느 예외 안인지 |

```bash
openocd -f tools/openocd/weact-h750.cfg -c init -c halt \
  -c "mdw 0xE000ED28 6" -c "mdw 0xE000ED24" -c "mdw 0xE000ED04" -c resume -c shutdown
#   -> CFSR HFSR DFSR MMFAR BFAR AFSR
```

### 예외 스택 프레임은 32바이트가 아니라 **104바이트** 다

이 타깃은 FPU 가 켜져 있고 lazy stacking 이 동작하므로 `EXC_RETURN` 이
**`0xFFFFFFE9`**(Thread/MSP/**FP 확장 프레임**)로 나온다. 기본 프레임 32바이트가
아니라 **104바이트**다. 스택 깊이를 계산할 때 이걸 빼야 폴트 직전 SP 가 나온다.

프레임 배치는 앞 32바이트가 동일하다.

```
MSP+0x00 R0   +0x04 R1   +0x08 R2   +0x0C R3
MSP+0x10 R12  +0x14 LR   +0x18 PC   +0x1C xPSR      <- 이후 S0-S15, FPSCR
```

`+0x14`/`+0x18` 은 **하드웨어가 푸시한 값이라 신뢰할 수 있다.** 반면 스택을 훑어
리턴 주소를 추정해 재구성한 콜체인은 죽은 프레임의 잔해를 집기 쉽다 — 이것 때문에
아두이노 세션이 폴트 위치를 `loop()` 으로 잘못 짚고 한참 돌아갔다.

`Default_Handler` 는 `b .` 라 레지스터를 건드리지 않는다. 그래서 폴트로 거기 갇혀
있으면 **R4~R11 도 폴트 당시 값 그대로 남아 있다.** 스택에 안 쌓이는 레지스터를
얻는 유일한 방법이다.

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
14. **openocd `connect_assert_srst` 는 더블탭이 된다** — SRST 를 걸면 `PINRSTF` 가
    서고, 짧은 간격 2회면 부트로더가 잔류한다. 우리 cfg 는 `reset_config none` 이라
    해당 없다 (13 문서)
15. **HAL `SD_PowerON()` 의 CMD8 판정 버그** — 카드 무응답을 못 걸러낸다 (16 문서)
16. **`tr`/`sed` 에 `LC_ALL=C`** — 로그에 non-UTF8 바이트가 섞이면 macOS 의 tr 이
    "Illegal byte sequence" 로 죽어 **그 지점부터 로그가 통째로 잘린다**.
    폴트 로그의 초기화 안 된 `.noinit` 필드에서 실제로 겪었다 (`swdlog.sh`)

## 남은 확인 (물리 조작 필요)

1. **NRST 버튼(SW3) 물리 더블탭** — 판정 로직은 확인했으나 버튼 입력으로는 미확정.
   디버거 SRST 와 구분이 안 된다 (위 참고)
2. **BOOT0(SW1) + NRST 로 ROM DFU 진입** — 스키매틱 근거만 있음
3. **PA9/PA10 UART 실출력** — 어댑터가 없어서 아무도 확인한 적이 없다.
   지금까지 로그는 전부 SWD 로 읽었다 (`swdlog.sh`)

### 2026-08-30 에 해소된 것

- **K1(PC13) 극성** — 회로도대로 **풀다운 + 누르면 HIGH** (아두이노 세션 실기)
- **더블탭 판정 로직** — `RESET_BIT_PIN` 만 뜨고 `reset_count : 2`,
  `stay : double reset (msc on)` 까지 확인했다. `RESET_BIT_SOFT` 가 없으므로
  SYSRESETREQ 로 인한 것은 아니다. H5 주석의 "SOFT+PIN 동시 세트" 는 이 실리콘에서
  재현되지 않았다.

  **다만 물리 버튼이었는지는 확정하지 못했다.** 디버거가 SRST 를 걸어도 `PINRSTF` 가
  서므로 구분되지 않는다. 실제로 openocd 를 `connect_assert_srst` 로 붙이면 짧은
  간격의 PIN 리셋 2회가 되어 **더블탭으로 오인된다**(아두이노 세션이 이것 때문에
  한참 헤맸다). 관측 시점에 그 세션이 SWD 를 붙이고 있었을 가능성이 있다.
  SW3 버튼 자체는 아래 "남은 확인" 에 그대로 둔다
- **실제 `.uf2` 드래그앤드롭** — 동작. `INFO_UF2.TXT` 확인 (아두이노 세션)
- **CDC+HID 전용 모드(PID 0xB750)** — 1200bps 터치가 `MODE_BIT_BOOT` 만 쓰므로
  MSC 없이 잔류한다. 의도대로 동작
- **1200bps 터치 → 부트로더 진입 → 기록 → 자동 실행** — 308.8 KB/s, TAG 검증 통과
- **앱 열거** — `1209:B752`
