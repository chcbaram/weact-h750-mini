# 04. QSPI / XiP

## 목적

앱이 실행될 외부 QSPI 플래시(W25Q64, 8MB)를 다루고, memory-mapped(XiP) 모드를 켜고 끈다.
**이 프로젝트 전체 구조를 결정하는 제약이 여기서 나온다.**

## 대상 파일

- `src/hw/driver/qspi.c` (`convex-boot` 에서 이식 — H743 은 H750 과 같은 QUADSPI IP)
- `src/common/hw/include/qspi/w25q64jv.h`
- `src/hw/driver/flash.c` — 주소로 대상을 가르는 디스패처

## XiP 는 읽기 전용이다 — 가장 중요한 제약

`HAL_QSPI_MemoryMapped()` 로 memory-mapped 모드에 들어가면 QUADSPI 는 **설정된 read 명령
하나만 발행한다.** 소거나 프로그래밍을 하려면 `HAL_QSPI_Abort()` 로 memory-mapped 를
빠져나와 indirect 모드로 돌아가야 한다.

그런데 그 순간 **QSPI 주소 공간에서의 명령어 인출이 끊긴다.** 즉:

> **QSPI 에서 실행 중인 코드는 자기가 있는 QSPI 를 절대 지울 수 없다.**

여기서 이 프로젝트의 구조가 전부 따라 나온다.

- 부트로더는 **내부 플래시**에 있어야 한다. QSPI 를 프로그래밍할 수 있는 유일한 주체다.
- 앱(XiP 실행)은 스스로 업데이트할 수 없다. **요청 + 리셋** 경로만 존재한다
  (`resetToBoot(with_msc)` → `NVIC_SystemReset()`).
- 그래서 HID/UF2/CDC 어느 경로로 오든 실제 쓰기는 항상 부트로더가 한다.

드라이버도 이 제약을 강제한다. `qspiRead/Write/Erase` 는 모두
`assert(qspiGetXipMode() == false)` 를 건다.

```c
bool qspiGetXipMode(void) { return HAL_QSPI_GetState(&hqspi) == HAL_QSPI_STATE_BUSY_MEM_MAPPED; }

bool qspiSetXipMode(bool enable)
{
  if (enable) { if (!qspiGetXipMode()) ret = qspiEnableMemoryMappedMode(); }
  else        { if ( qspiGetXipMode()) ret = qspiReset(); }
  return ret;
}
```

## 캐시 일관성

memory-mapped 를 빠져나와 다시 굽고 재진입하면 `0x90000000` 에 대한 **stale 캐시 라인**이
남는다. CRC 검증이나 앱 점프 직전에 반드시 비운다.

```c
SCB_CleanInvalidateDCache();
SCB_InvalidateICache();
```

`bspDeInit()` 에 들어 있다. 빼먹으면 방금 구운 펌웨어가 아니라 이전 것이 실행되거나
중간에 폭발한다.

## 핀 / 클럭

`convex-boot` 은 BANK2 를 쓰지만 **이 보드는 BANK1** 이다 (`QSPI_FLASH_ID_1`).

| 신호 | 핀 | AF |
|---|---|---|
| CLK | PB2 | AF9 |
| BK1_NCS | PB6 | **AF10** |
| BK1_IO0 | PD11 | AF9 |
| BK1_IO1 | PD12 | AF9 |
| BK1_IO2 | PE2 | AF9 |
| BK1_IO3 | PD13 | AF9 |

> **NCS 만 AF10 이다.** 나머지는 전부 AF9. 여기를 틀리면 CS 가 안 떨어져서 ID 읽기부터 실패한다.

커널 클럭은 **`RCC_QSPICLKSOURCE_PLL2`** (PLL2R = 200MHz), `ClockPrescaler = 1`
→ **SCK 100MHz**. 실기에서 XiP 무결성까지 확인한 값이다.

프리스케일러를 0(분주 없음)으로 두고 커널을 100MHz 로 맞추는 방법도 있지만, 그렇게 하면
SSHIFT(half-cycle 샘플 시프트)를 함께 쓰기 어렵다. 커널을 200MHz 로 올리고 프리스케일러
1 을 유지하는 쪽이 안전하다.

D1HCLK 이나 PLL1Q 가 아니라 **PLL2** 에서 뽑는 것이 중요하다 — 03 문서 참고.
부트로더가 잡은 QSPI 설정이 그대로 앱의 XiP 설정이 되기 때문이다.

W25Q64JV 의 Fast Read Quad I/O(0xEB)는 규격상 133MHz 까지지만, 이 보드는 라인마다
**33R 직렬 저항(R30~R35)** 이 붙어 있어 엣지가 둔하다. 100MHz 가 실측으로 확인된 상한
근처이므로, 더 올리려면 아래 자체 시험으로 반드시 재확인할 것.

## memory-mapped 설정 — SIOO 와 연속 읽기 모드의 짝

```c
s_command.Instruction        = QUAD_INOUT_FAST_READ_CMD;   // 0xEB
s_command.AddressMode        = QSPI_ADDRESS_4_LINES;
s_command.AddressSize        = QSPI_ADDRESS_24_BITS;       // 8MB 라 24비트로 충분
s_command.AlternateBytesSize = QSPI_ALTERNATE_BYTES_8_BITS;
s_command.AlternateBytes     = (1 << 5);                   // M5:M4 = 10b
s_command.DataMode           = QSPI_DATA_4_LINES;
s_command.DummyCycles        = W25Q64JV_DUMMY_CYCLES_READ_QUAD;   // 4
s_command.SIOOMode           = QSPI_SIOO_INST_ONLY_FIRST_CMD;     // <- 이것과 짝
s_mem_mapped_cfg.TimeOutActivation = QSPI_TIMEOUT_COUNTER_ENABLE;
s_mem_mapped_cfg.TimeOutPeriod     = 0x20;
```

**`AlternateBytes = 0x20` 은 M5:M4 = 10b, 즉 플래시를 "연속 읽기 모드" 에 넣는다.**
(끄는 것이 아니다 — 처음에 반대로 알고 있었다)

그 모드에서는 다음 읽기에 `0xEB` 오피코드를 보내면 **안 된다.** 따라서 `SIOOMode` 를
반드시 **`INST_ONLY_FIRST_CMD`** 로 맞춰야 한다.

`INST_EVERY_CMD` 로 두면 QUADSPI 가 매번 `0xEB` 를 보내고, 플래시는 연속 읽기 모드라
그것을 **주소 비트로 해석**해 엉뚱한 데이터를 돌려준다.

### 증상이 왜 그렇게 찾기 어려웠나

**순차 버스트는 명령 하나로 끝나므로 멀쩡히 읽힌다.**

- 자체 시험의 256바이트 패턴 왕복 → 통과
- 4KB 순차 훑기 (`sum 0xFFFFFC00`) → 통과
- 호스트에서 111블록(28KB) 전수 대조 → **전부 일치**

**랜덤 액세스만 깨진다.** 그래서 데이터 경로는 100% 정상인데 앱으로 점프한 뒤
명령어 인출에서만 터졌다.

```
CFSR = 0x00010000  (UNDEFINSTR)
PC   = 0x900057A4  <- ExitRun0Mode 의 첫 명령어. 정상 명령어인데 디코드 실패
LR   = 0x90006C11  <- Reset_Handler 에서 bl 로 호출
```

점프 직전에 indirect 와 memory-mapped 로 같은 주소를 읽어 비교했더니 **바이트까지
일치**했다. 그래서 "데이터는 맞는데 인출만 틀리다" 가 확정됐고, SIOO 로 좁혀졌다.

SCK 를 100MHz → 50MHz 로 낮춰도 **동일하게 재현**됐으므로 신호 무결성 문제가 아니다.

`stm32h7-wifi` 의 FSBL(실제로 XiP 점프에 성공하는 레퍼런스)도 같은 조합이다.

## 부품 판별

부품 번호를 그대로 비교하지 않고 JEDEC ID 의 **용량 바이트를 해석**한다.

```
JEDEC ID = [제조사][타입][용량]
  0xEF = Winbond
  용량 바이트가 log2(바이트수).  0x17 = 2^23 = 8MB,  0x18 = 2^24 = 16MB
```

기대(8MB)와 다르면 경고를 찍는다. 실제 용량이 다르면 XiP 영역 계산이 전부 어긋나기 때문에
조용히 넘어가면 안 된다.

## flash.c 디스패처

이 보드에는 플래시가 둘이라, `boot.c`/`uf2.c` 가 같은 API 만 쓰도록 주소로 가른다.

| 주소 | 대상 | 지원 |
|---|---|---|
| `0x08000000`~ | 내부 플래시 (부트로더) | **읽기만** |
| `0x90000000`~ | QSPI (앱) | 읽기 / 쓰기 / 소거 |

`flashRead()` 는 XiP 중이면 `memcpy` 로 바로 읽는다 (훨씬 빠르다). indirect 일 때만
`qspiRead()` 로 간다.

`flashErase()`/`flashWrite()` 는 **`qspiSetXipMode(false)` 를 먼저 부른다.** 다시 XiP 로
되돌리는 것은 호출자(4단계 `boot.c`, 7단계 `uf2.c`) 책임이다 — 여러 번 쓰는 동안 매번
모드를 오가면 느리기 때문이다.

## 함정

- **`assert()` 가 켜져 있어야 XiP 위반이 잡힌다.** `USE_FULL_ASSERT` 없이 빌드하면
  memory-mapped 중에 `qspiWrite()` 를 불러도 조용히 실패한다.
- **소거 단위는 4KB(subsector)** 다. `FLASH_SIZE_TAG` 를 4KB 로 잡은 이유가 이것이다(00 문서).
- **`qspiSetXipMode(false)` 후 캐시를 비우지 않고 읽으면 옛 데이터가 나온다.**

## 검증 방법 — 자체 시험

**indirect 읽기만 성공해도 XiP 는 깨질 수 있다.** 앱이 실제로 명령어를 인출하는 경로는
memory-mapped 이므로, 반드시 그 경로로 검증해야 한다.

`ap.c` 의 `qspiSelfTest()` 가 이걸 한다 (CMakeLists 에서 `-DQSPI_SELF_TEST` 를 켤 때만 빌드됨).

1. indirect 로 마지막 섹터(`0x907FF000`)를 소거하고 256바이트 패턴을 쓴다
2. `qspiSetXipMode(true)` 로 memory-mapped 전환
3. `SCB_CleanInvalidateDCache()` + `SCB_InvalidateICache()`
4. CPU 가 `0x907FF000` 을 메모리처럼 읽어 패턴과 비교
5. `0x90000000` 부터 4KB 를 워드로 훑어 캐시 라인 경계와 버스트 읽기까지 건드린다

매 부팅마다 섹터를 지우므로 평소에는 꺼둔다. SCK 를 올리거나 배선을 건드린 뒤에만 켠다.

로그는 `hwInit()` 이후라 부팅 버퍼가 아닌 전체 버퍼에 있다.

```bash
tools/swd/swdlog.sh list
```

## 실측 결과

**SCK 100MHz 에서 XiP 무결성 확인.**

```
[OK] qspiInit()
     JEDEC ID : EF 40 17
     Winbond  : 8 MB
[  ] qspiSelfTest() @0x907FF000
     XiP read : OK (err 0/256)
     XiP sweep: 4KB read ok (sum 0xFFFFFC00)
```

- `JEDEC ID EF 40 17` → Winbond, 용량 바이트 `0x17` = 2^23 = 8MB. 회로도의
  "Default Flash: W25Q64" 와 일치.
- `XiP read` — 소거 → 쓰기 → memory-mapped 읽기 왕복 256바이트 전부 일치.
- `XiP sweep` 의 합 `0xFFFFFC00` 이 산술적으로 맞다. 1024 워드 × `0xFFFFFFFF`
  = `-1024 mod 2^32` = `0xFFFFFC00`. 즉 memory-mapped 로 4KB 를 읽어 전부 소거
  상태임이 확인된다. 우연히 나올 수 있는 값이 아니다.

빌드 크기: 42048 B (1단계) → **47536 B** (126KB 중 36.84%). QSPI 드라이버가 약 5.5KB.

## 타임아웃 카운터를 켜면 안 된다 — ST 에라타 (V260830R9)

**이 프로젝트에서 가장 오래 숨어 있던 결함이다. 내가 직접 심었다.**

```c
/* 있었던 것 — 지웠다 */
s_mem_mapped_cfg.TimeOutActivation = QSPI_TIMEOUT_COUNTER_ENABLE;
s_mem_mapped_cfg.TimeOutPeriod     = 0x20;
```

**ES0392** (STM32H742xI/G, H743xI/G, **H750xB**, H753xI device errata):

> 타임아웃 플래그 `TOF` 가 **새 memory-mapped 읽기 요청과 같은 클럭 엣지에
> 세트되면 그 읽기가 실패한다.**
> 워크어라운드 : 타임아웃 카운터를 끈다. (nCS 를 올리려면 읽기마다 abort —
> XiP 에서는 불가능하다.)

> **[미확인]** 문서 번호와 대상 부품은 ST 공식 URL 로 확인했지만, PDF 본문을
> 직접 읽지는 못했다(60초 타임아웃). 섹션 번호는 확인하지 못했다.
> **다만 아래 측정이 결정적이라 번호 없이도 수정은 정당하다.**

### 왜 간헐적인가

카운터는 **QSPI 클럭 사이클**을 센다. `0x20` = 32 SCK 사이클.

```
SCK 120MHz  ->  32 사이클 =  267 ns
SCK  60MHz  ->  32 사이클 =  533 ns
```

D1 AXI 패브릭 경합으로 CPU 의 인출이 그 시간만큼 멈추면 `TOF` 가 서고, 하필
새 요청과 **같은 엣지**면 깨진다. "같은 엣지" 라는 조건이 간헐성의 정체다.

### 증상

**메모리에는 멀쩡한 명령어가 있는데 CPU 가 다른 것을 인출한다.**

```
CFSR 0x00010000  UNDEFINSTR   정상적인 bl 명령어 경계에서 난다
CFSR 0x00000400  IMPRECISERR  섞여 나온다
```

칩에서 읽은 바이트가 ELF 와 같은데도 `UNDEFINSTR` 이 난다 — 이것이 결정적
증거였다(아두이노 세션이 스택 프레임과 레지스터로 확인).

### 왜 SD 에서만 났나

```
카메라  DCMI -> DMA1(D2 마스터) -> D2 SRAM     D1 을 안 지난다
LCD     SPI4 -> DMA1(D2)        -> D2 SRAM     D1 을 안 지난다
SD      SDMMC1 내부 IDMA(D1 마스터) -> **AXI SRAM = D1 슬레이브**
```

**SD 만 D1 마스터가 D1 슬레이브에 쓴다.** QSPI 명령어 인출도 D1 패브릭을
지나므로 거기서 만난다. 카메라/LCD 는 목적지가 D2 라 경합이 약하다.

### 실측 (단일 변수, SCK 120MHz 고정)

| 조건 | `SdAlign` | `SdBench` |
|---|---|---|
| 타임아웃 **ON** | 폴트 **20/20** | 폴트 **10/10** |
| 타임아웃 **OFF** | 폴트 **0/20** | 폴트 **0/10** |

참고로 방향을 가른 실험들:

| 조건 | 결과 | 의미 |
|---|---|---|
| SCK 60MHz (타임아웃 ON) | 폴트 9/20 | 창이 넓어질 뿐 **안 없어진다** |
| SD 버퍼를 D2 로 (타임아웃 ON) | 폴트 0/20 | D1 경합을 없애면 안 난다 |

**"SCK 를 내렸는데 왜 줄지"** 가 오래 설명 안 됐다. 경합 확률로는 반대여야 한다.
카운터가 **SCK 사이클을 센다**는 것으로 뒤집힌다 — 신호 마진이 아니라 시간
창이 넓어진 것이었다.

### 내가 심었다

XiP 브링업 때 memory-mapped 읽기가 불안정해서 이것저것 시도하다 넣었다. 그때는
증상이 줄어드는 것처럼 보였다. **원인을 없앤 것이 아니라 심은 것이었다.**

`boot.c` 의 `bootBeginRead()` 주석 — *"소거/쓰기 직후 XiP 로 올라오면 읽은 값이
실제 내용과 달랐다. 캐시를 꺼도 재현되므로 CPU 캐시 문제가 아니다"* — 도 같은
원인일 가능성이 높다. 그래서 검증 경로를 indirect 로 옮겨 피해간 것이다.
**그 우회가 이제 필요 없을 수 있지만, 한 번에 바꾸면 변수가 섞이므로 따로 잰다.**

STATUS 진단 절차 규칙에 정확히 걸린 사례다 — **"증상이 사라졌다고 원인을 잡은
것이 아니다."**

### 트레이드오프

nCS 가 계속 낮게 유지된다(플래시가 항상 선택된 상태). 소비전력이 조금 는다.
XiP 에서는 정상적인 선택이고, indirect 로 내려갈 때는 `qspiAbort()` 가 nCS 를
올린다. **실측으로 CDC/UF2 경로 모두 정상**이다.

### 회귀 확인 (V260830R9)

```
CDC 업로드     소거 0.57s / 기록 0.33s (292 KB/s) / 검증 TAG / 앱 부팅
UF2 드롭       3초, 자동 점프, VTOR 0x90001000
반복 부팅      20 / 0
```
