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
