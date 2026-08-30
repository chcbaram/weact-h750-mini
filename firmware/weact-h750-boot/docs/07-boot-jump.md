# 07. 이미지 식별과 점프

## 목적

QSPI 에 있는 앱이 실행해도 되는 것인지 판정하고, 맞으면 넘긴다.
**태그가 없는 이미지(아두이노 코어 빌드 등)도 받아들여야 한다.**

## 대상 파일

- `src/ap/modules/boot/boot.{c,h}`
- `src/ap/ap.c` — `bootUp()`
- `src/common/def.h` — `firm_ver_t` (`firm_size` 추가), `firm_tag_t`

## 이미지 식별 3단계

CRC 는 자기 이미지 안에 넣을 수 없지만(닭-달걀) **크기는 링커가 링크 시점에 안다**
(`_fw_flash_end - _fw_flash_begin`). 이 비대칭을 이용한다.

| 단계 | 조건 | 동작 |
|---|---|---|
| **TAG** | `0x90000000` 에 `"TAG "` 매직 | `fw_size`/`fw_crc` 로 전체 CRC 검증 |
| **VER** | TAG 없음. `0x90001400` 에 `"VER "` + `firm_size` | 부트로더가 CRC 를 계산해 **TAG 를 만들어 넣고 승격** |
| **RAW** | 둘 다 없음 | 벡터 테이블만 검사하고 경고와 함께 점프 |
| **NONE** | 벡터 테이블도 아님 | 점프하지 않는다 |

RAW 판정 기준:
- `word0`(초기 MSP)이 유효한 RAM 범위(DTCM / AXI / D2 / D3) 안
- `word1`(Reset_Handler)이 앱 영역 안이고 **Thumb 비트**가 서 있음

### stale 태그 — 개발 중 반드시 필요하다

SWD 로 새 이미지를 굽고 옛 태그가 남으면 CRC 가 실패해 부팅이 막힌다. 그러면 개발이
불가능해진다. 그래서 태그의 `fw_size` 와 `firm_ver_t.firm_size` 가 **다르면 태그를 버리고**
VER 로 내려가 재계산한다.

### 태그 자동 승격이 안전한 이유

TAG 가 **독립된 4KB 섹터**이기 때문이다(00 문서). 태그를 쓰다 전원이 끊겨도 앱 본체는
멀쩡하고 다음 부팅에 다시 VER 로 떨어질 뿐이다. `HW_BOOT_AUTO_TAG` 로 끌 수 있다.

## bootUp() 판정

| 상황 | 동작 |
|---|---|
| 정상 리셋 + 유효 이미지 | **USB 를 열지 않고** 바로 점프 |
| 리셋 더블클릭 | 부트로더 잔류 + **MSC(UF2) 켬** |
| 앱이 요청 (`MODE_BIT_BOOT`) | 잔류. MSC 는 `MODE_BIT_MSC` 로 앱이 선택 |
| 유효 이미지 없음 | 잔류 + MSC 켬 (복구 수단이 필요하다) |

`usbInit()` 은 `bootUp()` 이 리턴한 뒤에만 부른다. 그렇지 않으면 정상 부팅마다 호스트에
USB 장치가 나타났다 사라진다.

## 점프 주소는 indirect 로 읽어 레지스터에 담는다

원래 코드는 이랬다.

```c
jump_func = (void (**)(void))(FLASH_ADDR_FIRM_VEC + 4);
(*jump_func)();
```

**호출하는 그 순간에 memory-mapped 로 다시 읽는다.** 위 SIOO 문제가 있던 동안
그 읽기가 `0xFFFFFFFF` 를 돌려줬고, `BLX 0xFFFFFFFF` → PC = `0xFFFFFFFE` →
실행 불가 영역 → **IACCVIOL** (CFSR = 0x00000001) 로 죽었다. 초당 9회 부팅 루프.

SIOO 를 고친 뒤에도 이 구조는 위험하다. 값을 **indirect 로 미리 읽어 레지스터에
담아두고** 그 값으로 분기한다.

```c
qspiSetXipMode(false);
qspiRead(FLASH_SIZE_TAG + 4, (uint8_t *)&reset_handler, 4);
// 범위 + Thumb 비트 검사
app_entry = (void (*)(void))reset_handler;
...
bspDeInit();               // 캐시 정리를 먼저
qspiSetXipMode(true);      // XiP 진입은 나중에
app_entry();               // 포인터 역참조가 남아 있으면 안 된다
```

**순서도 중요하다.** 캐시 정리(`bspDeInit`)를 XiP 진입보다 **먼저** 한다.
XiP 를 켠 뒤에는 아무것도 만지지 않고 바로 분기한다.

## VTOR / MSP 책임 분담

부트로더는 앱의 `Reset_Handler` 로 바로 점프하고 **둘 다 건드리지 않는다.**

- MSP : 앱의 `Reset_Handler` 가 `ldr sp, =_estack`
- VTOR : 앱의 `SystemInit()` 이 `SCB->VTOR = &_fw_flash_begin`
  (아두이노 코어는 `-DVECT_TAB_BASE_ADDRESS` 로 처리 — 15 문서)

점프 직전 `bspDeInit()` 이 모든 NVIC IRQ 를 막고 SysTick 을 끈다. 앱이 VTOR 을 옮기기
전에 인터럽트가 뜨면 부트로더의 핸들러 주소로 뛰어버린다.

## 함정 — QSPI 검증은 indirect 로 해야 한다

**여기서 가장 오래 물렸다.**

증상: 태그를 쓴 직후 읽으면 실제 내용과 다른 값이 나온다. 손대지도 않은 영역의 CRC 가
`0xE848`(정답 `0x8205`)로 나오고, 태그는 전부 `0xFF` 로 읽혔다. 그런데 **한 번 더 읽으면
정상**이었다.

시도한 것과 결과:

| 시도 | 결과 |
|---|---|
| memory-mapped 진입 전 `HAL_QSPI_Abort()` | 변화 없음 |
| 이탈을 `qspiReset()` → `qspiAbort()` 로 | **더 나빠짐** (이후 모든 읽기 실패) |
| 이탈에 Abort + 플래시 리셋 둘 다 | 원래대로 (여전히 재현) |
| `TimeOutActivation` 활성화 (`0x20`) | 실패 지점만 이동 |
| 진입 후 더미 읽기 + 무효화 | **더 나빠짐** |
| `SCB_CleanInvalidateDCache` → 범위 한정 `InvalidateDCache_by_Addr` | 변화 없음 |
| SCK 100MHz → 50MHz | **동일** → 신호 무결성 문제 아님 |
| MPU 를 non-cacheable 로 | **동일** → CPU 캐시 문제 아님 |

결론: **부트로더는 QSPI 를 memory-mapped 로 읽지 않는다.** 굽는 경로가 indirect 이므로
읽기도 indirect 로 해야 자기모순이 없다. XiP 는 **앱으로 점프하기 직전에만** 켠다.

이건 임시방편이 아니라 **이 저장소들의 기존 방식과 같다.**

- `convex-boot` 은 `qspiSetXipMode()` 를 **한 번도 부르지 않는다.** `flash.c` 의
  `flashRead()` 가 QSPI 주소면 항상 `qspiRead()`(indirect)로 간다.
- `stm32h7-wifi-fsbl` 은 `qspiSetXipMode(true)` 를 `ap.c:11`, **점프 직전 딱 한 번**만
  부르고 **검증을 전혀 하지 않는다** (CRC도 태그도 없이 점프 주소 범위만 확인).

즉 두 프로젝트 모두 "QSPI 에 쓴 뒤 memory-mapped 로 읽어보는" 경로를 밟지 않는다.
그래서 이전에는 문제가 드러나지 않았던 것이다.

## 함께 잡은 것 : 소거 단위가 64KB 였다

`qspiErase()` 가 4KB 가 아니라 **64KB** 를 지우고 있었다. BSP 함수 이름이 직관과 뒤집혀
있는 것이 원인이다.

```
BSP_QSPI_Erase_Block()  -> SUBSECTOR_ERASE_CMD (0x20)  =  4KB
BSP_QSPI_Erase_Sector() -> SECTOR_ERASE_CMD    (0xD8)  = 64KB
```

원본은 `block_size` 를 `SECTOR_SIZE`(64KB)로 잡고 `qspiEraseSector()`(=64KB)를 불렀다.
그러면 `flashErase(0x90000000, 4096)` 이 태그 4KB 가 아니라 **64KB** 를 지워
**앱의 벡터 테이블과 코드 앞부분까지 날아간다.** 태그를 독립 4KB 섹터에 둔 설계가
통째로 무너지고, 모든 펌웨어 업데이트가 조용히 앱을 망가뜨렸을 것이다.

## MPU

QSPI 리전을 실제 플래시 크기(**8MB**)에 정확히 맞췄다. 16MB 로 잡으면
`0x90800000~0x90FFFFFF` 에 플래시가 없는데 Normal 메모리로 보이고, Cortex-M7 은 Normal
메모리를 **투기적으로 프리페치**하므로 없는 영역을 읽으려 든다.

부트로더에서는 non-cacheable 로 둔다. 부트로더는 QSPI 에서 실행하지 않으므로 캐시가
필요 없다. 앱은 자기 `bspInit()` 에서 cacheable 로 다시 잡아 XiP 성능을 얻는다.

## 검증 방법

`CMakeLists.txt` 의 `-DBOOT_SELF_TEST` 를 켜면 `bootSelfTest()` 가 QSPI 에 가짜 이미지를
만들어 각 단계를 확인한다. 가짜 벡터를 남기면 다음 부팅에 거기로 점프해 죽으므로
**시험이 끝나면 반드시 지운다.**

```bash
tools/swd/swdlog.sh list
```

## 실측 결과

```
[  ] bootSelfTest()
     empty        -> NONE
     vector only  -> RAW
     with version -> VER
[  ] bootPromoteTag() size 2048
     -> OK
     promoted     -> TAG
[!!] stale tag (tag 2048 != ver 1024)
     stale tag    -> VER
     cleaned      -> NONE
```

3단계 판정, 태그 자동 승격, stale 태그 감지 모두 정상.

정상 부팅(펌웨어 없음):

```
[  ] bootUp()
     image : NONE
     stay  : no firmware (msc on)
```

빌드 크기 : **70,112 B / 126 KB (54.3%)**

## 부팅 루프 차단

나쁜 이미지로 점프하면 무한히 부팅 루프에 빠질 수 있다. **실제로 겪었다** — 개발 중
가짜 테스트 이미지를 QSPI 에 남겨둔 채 부트로더를 다시 굽고 나서 IACCVIOL 로 초당
9회 리셋했다. SWD 로 백업 레지스터를 직접 써서 빠져나왔다.

### 왜 앱의 협조가 필요 없나

부트로더는 앱의 `Reset_Handler` 로 점프할 뿐 **`SCB->VTOR` 를 건드리지 않는다.**
VTOR 은 앱의 `SystemInit()` 이 바꾼다. 그러니까 점프 직후 ~ VTOR 변경 전 구간의
폴트는 **부트로더 자신의 폴트 핸들러**가 잡는다.

깨진 이미지는 바로 그 구간에서 죽는다. 그래서 아두이노 스케치를 포함해 어떤 앱에도
수정 없이 동작한다.

```
faultReset()  ->  resetIncFaultCount()  ->  NVIC_SystemReset()
                  RTC 백업 DR7 에 누적. .noinit 과 달리 전원이 끊겨도 남는다
```

### 연속 폴트만 센다

누적만 하면 기기 수명 동안 폴트 3번이 쌓이는 순간 영원히 점프하지 않게 된다.

`faultInit()` 이 `.noinit` 의 매직(`0x5555AAAA`)을 보고 "이번 부팅이 폴트 직후인가"
를 판정한다(`faultIsFaultBoot()`). 폴트가 아닌 이유로 부팅했으면 **이전 실행이 폴트로
끝나지 않았다**는 뜻이므로 카운터를 접는다.

```c
if (faultIsFaultBoot() != true && fault_cnt > 0)
{
  resetSetFaultCount(0);
  fault_cnt = 0;
}
if (fault_cnt >= HW_BOOT_FAULT_MAX) { stay = true; with_msc = true; }
```

부수 효과로 **전원을 껐다 켜거나 NRST 를 누르면 한 번 더 시도한다.** 나쁜 이미지면
1초 안에 다시 막히므로 손해가 없고, 일시적인 문제였다면 스스로 복구된다.

### 새 이미지를 커밋하면 접는다

이게 없으면 "폴트 루프로 막힘 → 고친 `.uf2` 를 떨어뜨림 → 카운터가 그대로라 다음
부팅에서 또 막힘" 이 된다. 태그를 쓴 시점이 곧 "사용자가 고쳤다" 는 신호다.

```
uf2.c           uf2Flush()  태그 기록 성공 후   -> resetSetFaultCount(0)
cmd_boot.c      FW_END      태그 기록 성공 후   -> resetSetFaultCount(0)
```

### `HW_BOOT_TRY_MAX` 는 왜 꺼져 있나 — **끄기로 결정했다**

점프 직전에 올리고 앱이 `resetConfirmBoot()` 으로 되돌리는 방식이다.
`weact-h750-fw` 에 확인 호출을 넣었으므로 **기술적으로는 켤 수 있다.**
그럼에도 `0`(비활성)으로 둔다. 이유가 셋이고 첫 번째가 결정적이다.

**1. 부트로더가 아두이노 이미지와 공유된다 (결정적)**

`hw_def.h` 는 부트로더 빌드당 하나뿐이라 켜면 **모든 이미지에 적용**된다.
아두이노 스케치는 `resetConfirmBoot()` 을 부르지 않으므로 **멀쩡한 스케치가
3번 부팅 후 부트로더에 갇힌다.** 이 프로젝트의 목표 중 하나가 "이 부트로더를
아두이노 코어에서도 쓴다" 인데 그걸 정면으로 깨뜨린다.

부트로더가 "이 앱이 확인 신호를 보내는 앱인가" 를 미리 알 방법이 없다.

**2. 잡는 범위가 좁다. 특히 행(hang)은 못 잡는다**

- **앱이 멈추면 리셋이 안 난다.** 부트로더가 다시 안 도니 카운터도 안 올라간다
- **폴트로 죽으면** `HW_BOOT_FAULT_MAX` 가 이미 잡는다 (실제로 겪은 유일한 실패 모드)

남는 것은 "폴트 없이 스스로 리셋을 반복하는 앱" 하나뿐이다. 워치독은 켜지
않았고, `NVIC_SystemReset()` 루프는 만난 적이 없다.

**3. 현실적인 오진이 있다**

```
NRST 1회            -> 더블탭 아님 -> 점프, DR6 = 1
NRST 2회 (1초 뒤)   -> 점프, DR6 = 2
NRST 3회 (1초 뒤)   -> DR6 = 3 -> 갇힘
```

`HW_RESET_DBLCLK_MS`(300ms)와 `HW_BOOT_CONFIRM_MS`(3000ms) 사이 간격으로 리셋을
세 번 누르면 **멀쩡한 앱인데도 갇힌다.** 더블탭으로도 안 잡히는 구간이고,
보드를 만지작거리며 리셋을 툭툭 누르는 것이 정확히 그 패턴이다.

#### 켤 가치가 생기는 조건

- **자체 앱 전용 부트로더 빌드**를 따로 낸다면 — 1번이 사라지므로 켜도 된다
- `HW_BOOT_CONFIRM_MS` 를 훨씬 짧게(예: 500ms) 하고 `HW_BOOT_TRY_MAX` 를 5 정도로
  올리면 3번이 완화된다. 다만 **1번은 그대로다**

그때까지는 `HW_BOOT_FAULT_MAX` 만으로 실용적으로 충분하다. 코드는
`#if HW_BOOT_TRY_MAX > 0` 로 들어가 있고 앱의 `resetConfirmBoot()` 도 살아 있으므로,
상수 하나만 바꾸면 켜진다.

### 검증 (2026-08-30)

SWD 로 상태를 만들어 세 경로를 모두 확인했다.

| 만든 상태 | 결과 |
|---|---|
| `DR7=3`, 폴트 매직 **없음** | 카운터가 `0` 으로 접히고 **앱으로 점프** (PC `0x90001a76`) |
| `DR7=3`, 폴트 매직 **있음** | **잔류.** PC `0x080085cc`, `stay : fault loop (msc on)` |
| 잔류 상태에서 펌웨어 업로드 | `fw end` 에서 카운터가 접히고 점프 |

```
[  ] bootUp()
     image : TAG          <- 이미지는 유효하다. 그래도 막는다
     fault : 3/3
     stay  : fault loop (msc on)
```

LCD 도 `FAULT LOOP` 를 빨간색으로 띄운다.

세 번째 경로는 시험을 준비하던 중 **아두이노 세션이 실제로 업로드를 보내면서
우연히 확인됐다.** 의도한 복구 경로가 실사용에서 그대로 동작한 셈이다.

## 점프 직전 처리 — ST 레퍼런스에 맞췄다 (V260830R8)

사용자가 "제조사 라이브러리에 H750 부트로더 예제가 있을 것" 이라고 짚어서 찾았다.
**`STM32H750B-DK/Templates/ExtMem_Boot`** 가 우리와 정확히 같은 용도다 — 내부
플래시 128KB 부트로더가 외부 QSPI 앱으로 점프하는 ST 공식 템플릿.

```c
/* ST ExtMem_Boot/Src/main.c */
CPU_CACHE_Enable();                              // 맨 처음 켠다 (우리와 같음)
HAL_Init();
SystemClock_Config();
Memory_Startup();                                // QSPI memory-mapped 진입
MPU_Config();

CPU_CACHE_Disable();                             // <- flush 가 아니라 **끈다**
SysTick->CTRL = 0;
JumpToApplication = (pFunction)(*(uint32_t*)(APPLICATION_ADDRESS + 4));
__set_MSP(*(uint32_t*) APPLICATION_ADDRESS);     // <- **MSP 를 설정한다**
JumpToApplication();
```

두 가지를 가져왔다.

### 1. 캐시를 비우지 말고 끈다

```c
SCB_DisableDCache();      // CMSIS 가 내부적으로 clean-invalidate 후 끈다
SCB_InvalidateICache();
SCB_DisableICache();
```

`SCB_DisableDCache()` 는 예전 코드(`SCB_CleanInvalidateDCache()`)의 **상위집합**
이라 안전이 줄지 않는다. **끄는 것이 나은 진짜 이유는 앱 쪽에 있다.**

CMSIS `SCB_EnableDCache()` 에는 가드가 있다.

```c
if (SCB->CCR & SCB_CCR_DC_Msk) return;   /* 이미 켜져 있으면 no-op */
```

**켠 채로 넘기면 앱의 `SCB_EnableDCache()` 가 아무것도 안 한다.** 앱은 자기가
캐시를 켰다고 생각하지만 전체 invalidate 를 한 번도 못 하고, 부트로더가 남긴
캐시 상태를 그대로 물려받는다. 아두이노 코어도 `premain()` 에서 이 함수를
부르므로 똑같이 해당된다(그쪽 세션이 `cores/arduino/main.cpp:25` 로 확인).

### 2. `__set_MSP()` 로 앱의 초기 MSP 를 넣는다

벡터 word[0] 을 indirect 로 읽어 분기 직전에 넣는다. 앱 `Reset_Handler` 의
`ldr sp, =_estack` 이 어차피 덮어쓰므로 베어메탈에서는 없어도 동작하지만,
**`ldr sp` 가 실행되기 전 구간을 없앤다.** 그 구간에서 예외가 뜨면 부트로더의
낡은 MSP 위에 프레임이 쌓인다. RTOS 를 켜서 `CONTROL.SPSEL=1` 인 경우에도
MSP 가 올바르게 남는다(위 조건부 항목 참고).

`bootIsValidVector()` 가 검증한 값이지만 여기서 한 번 더 RAM 범위를 확인하고,
이상하면 설정하지 않고 앱에 맡긴다. **반드시 분기 직전이어야 한다** — SP 를
바꾼 뒤 지역변수를 건드리면 깨진다.

### 실측 — 간헐 폴트가 사라졌다

사용자가 "리셋을 반복하면 가끔 앱이 안 뜬다" 고 보고한 건이다. 아두이노 세션의
`LcdHangul` 스케치 두 판본으로 재현했다. 판정은 **폴트 레지스터 + `uwTick` 증가**
(PC 샘플링은 위양성 10% 라 버렸다).

| 부트로더 | 앱 | 결과 |
|---|---|---|
| SPI4/DMA1 리셋만 | 수정 전 | 정상 37 / **폴트 3** (7.5%) |
| SPI4/DMA1 리셋만 | 수정 후(`93a99a7`) | 정상 40 / 폴트 0 |
| **+ ST 방식(캐시 Disable, set_MSP)** | **수정 전** | **정상 80 / 폴트 0** |

**앱을 안 고쳐도 0/80 이다.** 귀무가설(7.5%) 하에서 0/80 이 나올 확률은 0.196% 다.

> **주의: 두 변경을 한 번에 넣어서 어느 쪽이 실효인지 분리하지 못했다.**
> 둘 다 ST 레퍼런스 근거가 독립적으로 있어 그대로 두지만, 기여도는 미확인이다.

폴트 서명은 `CFSR 0x400`(IMPRECISERR)과 `CFSR 0x001`(IACCVIOL) 두 가지였다.
후자는 **MPU 가 AXI SRAM 을 XN 으로 잡고 있어서** RAM 함수 포인터로 `bx` 하면
나는 것이다. 둘 다 같은 문 — 앱이 준비되기 전에 들어오는 스퓨리어스 SPI4
인터럽트 — 에서 나온다.
