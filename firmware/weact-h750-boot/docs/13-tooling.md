# 13. 툴링 — openocd, VSCode 태스크/런처

## 목적

이 보드를 굽고, 리셋하고, 로그를 뽑고, 죽었을 때 되살리는 경로를 전부 VSCode 태스크
하나로 만든다. 손으로 openocd 명령을 외우지 않게 한다.

## 대상 파일

```
firmware/weact-h750-boot/
  tools/openocd/weact-h750.cfg        기본 (SWD, 내부 플래시)
  tools/openocd/weact-h750-qspi.cfg   stmqspi (QSPI 직접 굽기)
  .vscode/tasks.json
  .vscode/launch.json
  .vscode/c_cpp_properties.json
  .vscode/extensions.json
firmware/weact-h750-fw/               같은 구성 (앱 기준으로 방향만 뒤집었다)
```

## 왜 openocd 하나로 통일했나

이 머신에서 쓸 수 있는 SWD 툴이 openocd 뿐이다.

| 툴 | 상태 |
|---|---|
| **openocd 0.12.0** | **동작.** `stm32h7x` 타겟과 `stmqspi` 드라이버 모두 지원 |
| `STM32_Programmer_CLI` | 실행 불가. CubeProgrammer 의 Qt 가 macOS 13+ 를 요구하는데 이 머신은 12.7.6 |
| `pyocd` | 실행 불가. 바이너리는 있으나 파이썬 모듈이 설치돼 있지 않다 |
| `ST-LINK_gdbserver` | 동작하나 **CubeCLT 1.21.0** 으로 고정해야 한다 (1.22 는 실행 불가) |

참조 프로젝트(`stm32h5-w6300`)는 pyocd + CubeProgrammer 기준이라 태스크를 그대로 가져올
수 없었다. 전부 openocd 로 다시 썼다.

## 함정 0 — `transport select hla_swd` 를 박지 않는다

```
Error: Debug adapter doesn't support 'hla_swd' transport
```

`interface/stlink.cfg` 는 어느 openocd 를 쓰느냐에 따라 다른 드라이버를 고른다.

| | `adapter driver` | 쓰는 transport |
|---|---|---|
| homebrew openocd 0.12 | `hla` | `hla_swd` |
| ST 가 배포하는 openocd 포크 | `st-link` (DAP) | `dapdirect_swd` |

`transport select hla_swd` 를 박아두면 후자에서 위 에러로 죽는다. **아예 지정하지
않으면** openocd 가 어댑터가 지원하는 첫 번째 것을 자동으로 고른다.

```
Info : auto-selecting first available session transport "hla_swd".
```

아두이노 세션이 이 에러를 만나서 알게 됐다. 지금은 두 프로젝트 모두 `transport select`
줄이 없다.

## 함정 1 — SRST 를 쓰면 halt 가 안 된다

```
reset_config srst_only          ->  Error: timed out while waiting for target halted
reset_config none separate      ->  정상
```

보드의 NRST 를 **MAX809TEUR 수퍼바이저가 구동**한다. ST-LINK 가 SRST 를 끌어당겨도
수퍼바이저가 리셋 펄스를 자기 타이밍으로 다시 만들어 버려서 openocd 가 halt 시점을
잡지 못한다. `reset_config none` 으로 두면 openocd 가 **SYSRESETREQ(소프트웨어 리셋)** 를
쓴다.

부수 효과가 오히려 유리하다. SYSRESETREQ 는 **백업 도메인을 리셋하지 않으므로**
RTC 백업 레지스터에 부트 모드 플래그를 써넣고 리셋하는 복구 경로가 그대로 성립한다.

## 함정 2 — stmqspi 의 소거 단위는 64KB 다

우리 드라이버(`qspi.c`)는 W25Q64JV 의 **4KB 서브섹터**(0x20)를 쓴다. openocd 의
`stmqspi` 는 내장 플래시 테이블을 보고 **64KB 섹터**(0xD8)로만 지운다.

```
> flash erase_address 0x907FF000 0x1000
Error: address range 0x907ff000 .. 0x907fffff is not sector-aligned
```

그래서 "태그 섹터(4KB)만 지운다" 를 openocd 로는 할 수 없다. `0x90000000` 을 지우면
태그 4KB + **앱 앞부분 60KB** 가 함께 날아간다. 태그만 무효화하고 싶으면 부트로더의
`qspi` CLI 를 쓴다.

## 함정 3 — 앱 디버깅은 하드웨어 브레이크포인트만 된다

앱은 QSPI 에서 XiP 로 돈다. QUADSPI memory-mapped 모드는 **읽기 전용**이라 gdb 가
소프트 브레이크포인트(코드에 `BKPT` 명령어를 써넣는 방식)를 심을 수 없다.
Cortex-M7 의 하드웨어 브레이크포인트는 **8개**뿐이다. 그 이상 걸면 조용히 실패한다.

## 함정 4 — `loadFiles` 에 `.elf` 를 주면 안 된다

`Debug app (SWD 로 굽고 시작)` 구성은 `.bin` 을 준다. `.elf` 를 주면 openocd 가 섹션의
LMA 를 보고 내부 플래시 뱅크로 보내려 한다.

## openocd stmqspi 설정 — 실기 검증 결과

`qspi_init` 은 QUADSPI 를 **1-line Read(0x03), 3바이트 주소, memory-mapped** 로만 세운다.
부트로더/앱이 쓰는 Quad I/O(0xEB) + 연속읽기와는 별개다. openocd 는 halt 상태에서만
접근하므로 충돌하지 않는다.

`reset init` 직후는 SYSCLK=HSI 64MHz 이므로 `PRESCALER=3` → **SCK 16MHz**.
Read(0x03) 의 50MHz 상한 안에 넉넉히 들어간다.

검증한 것 (2026-08-30):

```
flash1 'win w25q64fv/jv' id = 0x1740ef size = 8192 KiB     JEDEC 정상
mdw 0x90000000  ->  54414720 00001000 00007188 00005a42    TAG 매직 + size + CRC
mdw 0x90001000  ->  24080000 90006edd ...                  앱 MSP / Reset_Handler
erase 0x907F0000 (64KB)  ->  0.258s (247 KiB/s)
fillw / verify / re-erase ->  전부 통과
program build/weact-h750-boot.elf verify reset exit  ->  ** Verified OK **
```

`program` 은 앱과 태그를 보존한다 (내부 플래시 뱅크만 건드린다).

## 주요 태스크

부트로더 프로젝트 기준. 앱 프로젝트는 방향만 뒤집은 같은 구성이다.

| 태스크 | 하는 일 |
|---|---|
| `Build - build` | 기본 빌드 (Cmd+Shift+B) |
| `Flash - boot (openocd)` | 내부 플래시에 굽는다. QSPI 는 보존 |
| `Device - reset` | SYSRESETREQ. 백업 도메인 보존 |
| **`Device - 부트 모드로 강제 진입`** | **비상구.** RTC 백업 DR3 에 직접 써넣고 리셋 |
| `Device - 폴트 레지스터 덤프` | `.noinit` 의 CFSR/HFSR/MMFAR/BFAR |
| `Device - 클럭 확인` | RCC/QUADSPI 레지스터로 실제 클럭 확인 (앱 프로젝트) |
| `Log - 부팅 로그 (SWD)` | UART 없이 RAM 링버퍼 덤프 |
| `QSPI - 정보` | JEDEC ID + 태그 + 앱 벡터 테이블 |
| `App - flash via UF2 / CDC / HID / SWD` | 네 가지 업로드 경로 |

### 비상구 태스크

나쁜 이미지를 구워 부팅 루프에 빠지면 리셋 더블탭조차 안 먹는다 (부트로더가 점프해
버리고 앱이 곧바로 죽으므로). 이때 쓴다.

```
mww 0x5800405C 0x00000003    RTC_BKP3R = MODE_BIT_BOOT|MODE_BIT_MSC
reset run
```

`0x5800405C` = RTC 베이스 `0x58004000` + BKP0R 오프셋 `0x50` + DR3(`3*4`).
값 `3` = `MODE_BIT_BOOT`(bit0) | `MODE_BIT_MSC`(bit1).

**이 경로는 실제로 필요했다.** 개발 중 가짜 테스트 이미지를 QSPI 에 남겨둔 채
부트로더를 다시 구웠다가 부팅 루프에 빠졌고, 이 명령으로 빠져나왔다.

## 빌드 산출물

```
build/weact-h750-boot.elf    디버깅 (심볼)
build/weact-h750-boot.bin    download.py / UF2 변환 / openocd program
build/weact-h750-boot.hex    주소를 인자로 못 받는 툴용
build/weact-h750-boot.map    링크 맵
```

`.hex` 를 따로 만드는 이유: `.bin` 은 **주소가 없는 날바이트**라 굽는 쪽에
"0x08000000 에 넣어라" 를 따로 알려줘야 한다. `.hex`(Intel HEX)는 레코드마다 주소를
갖고 있어서 툴이 주소를 몰라도 된다.

```
:020000040800F2              확장 선형 주소 = 0x0800xxxx
:1000000000000824FD440008... 데이터
:04000005080044FDAE          시작 주소 = 0x080044FD (Reset_Handler, Thumb 비트)
:00000001FF                  EOF
```

ST-LINK Utility, CubeProgrammer GUI, 아두이노 코어의 `Burn Bootloader` 레시피처럼
주소 인자를 받지 않는 경로에서 필요하다. openocd 도 그대로 받는다:

```sh
openocd -f tools/openocd/weact-h750.cfg -c 'program build/weact-h750-boot.hex verify reset exit'
```

**앱의 `.hex` 는 대부분의 툴로 못 굽는다.** 주소가 `0x90001000`(QSPI)이라 내부 플래시만
아는 툴은 거부한다. `openocd` + `stmqspi` 설정으로만 가능하다. 앱은 `.uf2` 나 `.bin` 을
쓰는 게 맞다.

## 업로드 경로 네 가지

| 경로 | 속도 | 쓰는 때 |
|---|---|---|
| **UF2** (드래그앤드롭) | — | 정상 경로. 태그까지 부트로더가 써준다 |
| **CDC** (`download.py`) | 295 KB/s | 개발 중 가장 빠르다 |
| **HID** (`download.py --hid`) | 39.6 KB/s | 시리얼 포트 이름에 의존하지 않는다 |
| **SWD** (`openocd stmqspi`) | 247 KB/s (소거) | 복구. 부트로더가 깨져도 된다 |

SWD 경로는 **태그를 쓰지 않는다.** 부트로더가 2단계(VER)로 판정해 첫 부팅에서 CRC 를
계산해 태그로 승격한다. 즉 SWD 로 구운 뒤 첫 부팅은 조금 느리고, 두 번째부터 TAG 다.

## 검증 방법

```sh
# 붙어 있는지
openocd -f tools/openocd/weact-h750.cfg -c 'init' -c 'halt' -c 'reg pc' -c 'resume' -c 'shutdown'
#   앱이 돌고 있으면 pc 가 0x900xxxxx, 부트로더면 0x080xxxxx

# QSPI 가 보이는지
openocd -f tools/openocd/weact-h750-qspi.cfg -c 'init' -c 'reset init' -c 'flash info 1' -c 'shutdown'
#   'win w25q64fv/jv' id = 0x1740ef size = 8192 KiB
```
