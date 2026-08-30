# 09. cmd 프로토콜 (HID / CDC)

## 목적

호스트 툴이 펌웨어를 굽고 상태를 조회하는 커맨드 채널. **CDC 와 HID 가 같은 프로토콜을
공유**하고, 전송계층만 갈아끼운다.

## 대상 파일

- `src/hw/driver/cmd.c` — 패킷 파서 (전송계층 무관)
- `src/ap/modules/cmd/cmd_task.c` — 채널 목록과 폴링
- `src/ap/modules/cmd/driver/{drv_usb.c, drv_hid.c, drv_cli.c}`
- `src/ap/modules/cmd/process/cmd_boot.c` — 커맨드 처리
- `tools/download/{cmdproto.py, download.py}` — 호스트 쪽

## 패킷 포맷

```
STX0(0x02) STX1(0xFD) type cmd_l cmd_h err_l err_h len_l len_h [data...] checksum
checksum = (~sum(header+data)) + 1
type : CMD 0x00 / RESP 0x01 / EVENT 0x02 / LOG 0x03 / PING 0x04
data 최대 : HW_CMD_MAX_DATA_LENGTH (1024)
```

전송계층은 `cmd_driver_t` (open/close/available/flush/read/write) 로 추상화된다.
그래서 CDC 든 HID 든 이더넷이든 드라이버만 붙이면 된다.

## HID 프레이밍

HID 는 스트림이 아니라 **64바이트 고정 리포트** 단위다. `cmd.c` 는 바이트 스트림을
기대하므로 변환이 필요하다.

```
리포트[0]  = 유효 바이트 수 (1~63)
리포트[1:] = 페이로드
```

**길이를 앞에 두는 이유** : HID 는 항상 64바이트를 꽉 채워 보내므로 패딩과 실제 데이터를
구분할 방법이 필요하다. 패딩을 그냥 흘려보내면 패킷 파서가 쓰레기 바이트를 먹는다.

수신은 USB 콜백에서 링버퍼(`qbuffer`)에 풀어 넣기만 하고, 처리는 메인 루프에서 한다.

## HID 장치 식별 — 다른 키보드와 겹치면 안 된다

`TUD_HID_REPORT_DESC_GENERIC_INOUT` 은 usage page **0xFF00** / usage 0x01 을 쓴다.
TinyUSB 기본값이라 온갖 장치가 같이 쓰고, QMK/VIA 는 0xFF60/0x61 이다.
호스트 툴이 "0xFF00 짜리 HID" 를 찾으면 남의 장치까지 걸린다.

그래서 이 부트로더 전용으로 **usage page `0xFF75` / usage `0x01`** 을 쓴다.

**호스트 툴의 매칭 조건 (셋 다 볼 것):**

```
VID 0xCAFE  +  PID 0xB010(CDC+HID) 또는 0xB011(+MSC)  +  usage page 0xFF75
```

보드가 늘어나면 usage page 는 그대로 두고 PID 로 구분한다. 그러면 툴 하나로 모든
부트로더를 찾되 키보드와는 절대 안 겹친다.

vendor-defined 범위(0xFF00~0xFFFF)를 쓰는 것 자체도 필수다. 윈도우는 Generic Desktop 의
키보드/마우스 top-level collection 을 사용자 프로그램이 여는 것을 막는다.

## CDC 소유권

CDC 하나를 CLI 와 cmd 가 나눠 쓸 수 없다. **호스트가 연 보율로 주인을 가른다** (08 문서).

| 보율 | 주인 |
|---|---|
| 115200 | CLI |
| 그 외 (툴은 921600) | cmd |

HID 는 전용 채널이라 항상 돈다. `cmdChIsEnabled()` 가 이 판정을 한다.

## 커맨드 셋

| 코드 | 이름 | 비고 |
|---|---|---|
| 0x0000 | `INFO` | 메모리 맵 + 이름/버전. mode 로 부트/앱 구분 |
| 0x0001 | `VERSION` | 현재 이미지의 img_type / size / crc / 이름 / 버전 |
| 0x0002 | `FW_BEGIN` | 크기 신고. **태그 섹터를 먼저 지운다** |
| 0x0003 | `FW_ERASE` | 앱 영역 소거 |
| 0x0004 | `FW_WRITE` | `[offset:4][data]` |
| 0x0005 | `FW_READ` | `[offset:4][len:4]` → data |
| 0x0006 | `FW_END` | CRC 계산 후 **태그 기록 = 커밋** |
| 0x0007 | `FW_VERIFY` | img_type 반환 |
| 0x0008 | `FW_UPDATE` | 부트: 점프 / 앱: `resetToBoot(true)` |
| 0x0009 | `FW_JUMP` | 부트: 점프 / 앱: `resetToBoot(false)` |
| 0x0010 | `CLI` | cmd 패킷 위로 CLI 터널링 |
| 0x0011 | `CLI_MORE` | 출력 이어받기 |

### FW_BEGIN 이 태그를 먼저 지우는 이유

전송이 중간에 끊겨도 태그가 무효라 부트로더가 **옛 이미지로 점프하지 않는다.**
태그가 독립 4KB 섹터라 앱 본체는 건드리지 않는다(00 문서).

### FW_UPDATE / FW_JUMP 가 부트와 앱에서 다른 이유

앱은 QSPI 에서 XiP 로 실행 중이고, QUADSPI 는 memory-mapped 상태에서 쓰기가 안 된다.
쓰려면 indirect 로 내려가야 하는데 그 순간 자기 명령어 인출이 끊긴다(04 문서).
**앱이 직접 굽는 것은 원천적으로 불가능**하므로 부트로더에 요청하고 리셋하는 길뿐이다.

`HW_DEV_MODE` 로 같은 파일이 양쪽에서 다르게 동작한다.

## 함정

- **`#if CFG_TUD_HID` 앞에 `tusb.h` 를 포함**해야 한다. `cmd_task.h` 만 포함하면
  매크로가 정의되지 않아 `drv_hid.c` 전체가 조용히 사라지고 링크 에러로만 드러난다.
  (`uf2_disk.c` 도 같은 문제를 겪었다 — 10 문서)
- CLI 터널링 중에는 `cliMgrEnable(false)` 로 재진입을 막아야 한다. 안 그러면 같은
  `moduleUpdate` 안에서 `cliMain()` 이 또 불리며 포트를 되돌려 버린다.

## 검증 방법

```bash
# CDC (921600 - 115200 이면 CLI 가 CDC 를 쥔다)
python3 tools/download/download.py ...

# HID
#   VID 0xCAFE + PID 0xB011 + usage_page 0xFF75 로 필터
```

## 실측 결과

### HID 장치 식별

```
$ hid.enumerate(0xCAFE, 0)
PID=0xB011  usage_page=0xFF75  usage=0x01  product=WEACT-H750-BOOT

usage_page 0xFF75 필터 -> 매칭 1개
```

macOS `ioreg` 로도 확인 : `PrimaryUsagePage = 65397` (0xFF75), `PrimaryUsage = 1`.

### CDC 경로

```
BOOT_CMD_INFO  err=0  len=104
   magic      = 0x5555AAAA
   mode       = 0x00000000      (HW_DEV_MODE_BOOT)
   boot_addr  = 0x08000000      boot_size = 0x00020000
   firm_addr  = 0x90000000      firm_vec  = 0x90001000
   firm_size  = 0x00800000      tag_size  = 0x00001000
   max_fw     = 0x00200000      family    = 0xFFFF0004
   name       = WEACT-H750-BOOT
   version    = V260829R1

BOOT_CMD_FW_VERIFY  err=0x0010 (ERR_BOOT_INVALID_FW)  img_type=0 (NONE)
```

### HID 경로 — 펌웨어 쓰기 전 과정

가짜 이미지 2048B(벡터 테이블 + `firm_ver_t`)를 HID 로 굽고 검증했다.

```
FW_BEGIN  -> 0
FW_ERASE  -> 0
FW_WRITE  -> OK (2048 bytes)
FW_END    -> 0
FW_VERIFY -> err=0x0000  img_type=3 (TAG)
VERSION   -> TAG  size=2048  crc=0x9ECE  (기록한 firm_ver_t 의 이름/버전이 그대로 읽힘)
```

**CDC 와 HID 가 같은 프로토콜로 동작하고, 쓰기 전 과정이 TAG 단계까지 통과한다.**

정리 확인 : `FW_BEGIN`(태그 소거) + `FW_ERASE`(벡터 소거) 후 `img_type=0 (NONE)`.
태그만 지우면 `VER` 로 떨어져 다음 부팅에 자동 승격 후 가짜 벡터로 점프하므로,
**시험 이미지는 벡터까지 지워야 한다.**

빌드 크기 : **95,712 B / 126 KB (74.2%)**

## 소거가 업로드 시간의 대부분이다 (2026-08-30)

아두이노 세션이 격리해서 재 왔다. 242KB 이미지, `baramdl`:

```
앱 -> 부트로더 진입 포함   6.02 / 6.07 / 6.04 s
이미 부트로더에 있을 때    4.92 / 4.91 / 4.95 s   <- 터치/재열거가 아니다
  그중 전송                0.9 s (242 KB, 278 KB/s)
  **나머지 4.02 s 가 지우기** (60섹터 x 67ms)
```

W25Q64JV 는 **4KB 섹터 typ 45ms, 64KB 블록 typ 150ms** 다. 같은 64KB 를
섹터 16개로 지우면 720ms, 블록 하나면 150ms — **4.8배** 차이다.

### `FW_ERASE` 는 64KB 정렬 범위를 요청한다

`qspiErase()` 는 **요청 범위 안에 온전히 들어가는 64KB 블록만** 블록으로 지우고
앞뒤 자투리는 4KB 로 지운다. **범위 밖은 절대 안 지운다** — 태그 하나(4KB)만
지우는 `FW_BEGIN` 이 앱을 날리면 안 되기 때문이다.

그래서 `0x90001000` 부터 요청하면 시작이 64KB 정렬이 아니라 앞 15섹터가 4KB 로
떨어져 이득이 절반으로 준다.

```
242KB, 정렬 안 함 : 4KB x 28 + 64KB x 2  ->  약 2.3s (1.7배)
242KB, 정렬함     : 64KB x 4             ->  약 0.9s (4.5배)
```

**범위를 넓히는 판단은 호출자가 한다.** `FW_BEGIN` 이 이미 태그를 지웠으므로
`FLASH_ADDR_FIRM`(0x90000000, 64KB 정렬)부터 요청하고 꼬리도 64KB 로 올린다.

```c
uint32_t len = FLASH_SIZE_TAG + (uint32_t)wr_length;
len = (len + 0xFFFF) & ~0xFFFF;
if (len > FLASH_SIZE_FIRM) len = FLASH_SIZE_FIRM;
flashErase(FLASH_ADDR_FIRM, len);
```

초과 소거는 최대 64KB-1 인데, 전부 **이번 전송이 덮어쓸 자리 뒤의 빈 공간**이다.

| 펌웨어 | 소거 | 블록 | 초과 |
|---|---|---|---|
| 61,496 B | 131,072 B | 2 | 65,480 B |
| 98,808 B | 131,072 B | 2 | 28,168 B |
| 242,092 B | 262,144 B | 4 | 15,956 B |
| 1,048,576 B | 1,114,112 B | 17 | 61,440 B |

## 호스트 툴(`download.py`)이 처음부터 깨져 있었다

2026-08-30 에 소거 시간을 재려다 발견했다.

```
cmdproto.py:183  struct.error: unpack requires a buffer of 84 bytes
```

`parse_info()` 와 `parse_version()` **둘 다** `stm32h5-w6300` 의 **A/B 슬롯 구조**를
그대로 파싱하고 있었다. 이 보드는 슬롯이 없다(QSPI 전체가 단일 펌웨어 영역).

```
              펌웨어(packed)                호스트(가져온 것)
INFO      10 x u32 + 32 + 32 = 104 B     8 x u32 + name/version 오프셋도 다름
VERSION   1+3+4+4+32+32     =  76 B     84 B 슬롯 항목 x 3 + 4
```

`download.py` 본문도 슬롯 시대였다 — `FW_BEGIN` 응답에서 `slot` 을 꺼내고,
`FW_VERIFY` 에 슬롯 번호를 넘기고, `ERR_BOOT_NO_PENDING` 을 처리했다.
우리 펌웨어는 셋 다 없다.

**왜 아무도 안 밟았나** : CDC 업로드를 전부 아두이노 세션의 `baramdl` 로만 했다.
"CDC 업로드가 동작한다" 는 사실이었지만 **그 툴로는 아니었다.**
STATUS 진단 절차 4번이 여기서 나왔다.

고친 뒤 구조는 이렇다.

```
INFO_FMT = "<10I32s32s"      # 104 B
VER_FMT  = "<B3sII32s32s"    #  76 B
```

길이가 안 맞으면 `struct.error` 대신 **"펌웨어의 boot_info_t 와 어긋난 것이다"**
라고 말하게 했다. 다음에 구조체를 바꾸면 바로 알 수 있다.

흐름도 다시 썼다. `INFO` 로 부트로더인지 앱인지 먼저 가르고, **앱이면 `FW_UPDATE`
로 부트로더 진입만 요청하고 끝낸다** — 앱에서 `FW_*` 를 보내면 거부당한다(12 문서).

### `FW_END` 도 `.version` 의 `firm_size` 를 권위로 삼는다

`download.py` 를 고치고 나서 곧바로 드러났다. 업로드는 성공하는데 `FW_VERIFY` 가
**TAG 가 아니라 VER** 을 돌려줬다.

원인은 호스트의 **16바이트 정렬 패딩**이었다.

```python
if len(fw) % 16:
    fw += b"\xFF" * (16 - len(fw) % 16)      # 쿼드워드 정렬
```

`FW_END` 가 `wr_index`(호스트가 실제로 보낸 최대 끝 오프셋)를 태그 크기로 쓰니
98,808 대신 98,816 이 들어가고, `.version` 의 `firm_size` 와 어긋나 다음 검증에서
**stale tag** 로 판정된다.

**UF2 경로에서 이미 같은 실수를 했었다**(10 문서). 거기는 UF2 블록의 256B 패딩,
여기는 호스트의 16B 패딩 — 원인은 다르고 증상은 같다.

두 군데를 고쳤다.

- `download.py` : **패딩하지 않는다.** 16바이트 정렬은 STM32H5 내부 플래시의
  쿼드워드 프로그래밍 요구였다. QSPI 페이지 프로그램은 바이트 단위다
- `FW_END` : `.version` 이 있으면 그 `firm_size` 를 쓴다. 호스트가 무엇을 하든
  태그가 정확해진다

**이미지가 자기 크기를 알고 있으면 그것이 권위다.** 전송 계층이 붙인 것은
이미지의 일부가 아니다.

### 실측 (2026-08-30, 96.5KB, V260830R7)

```
연결됨    : WEACT-H750-BOOT V260830R7  [BOOT]
현재 FIRM : WEACT-H750-FW V260830R5  [TAG] 98808 bytes  crc 0x6B59
  소거   0.56s      (2 x 64KB 블록)
  기록   0.36s      (269 KB/s)
  검증   TAG
```

### 사용자가 체감하는 전체 업로드 (아두이노 세션 측정)

```
22.3s   최초 보고 (호스트 툴이 죽은 포트에서 3초 타임아웃을 까먹던 시절)
 9.07s  포트를 USB ID 로 먼저 판별하도록 고친 뒤
 8.14s  arduino-cli 의 중복 1200bps 터치 제거
 5.54s  **부트로더 64KB 소거** (여기가 우리 몫이다)
```

연속 4회 5.82 / 5.69 / 5.44 / 5.46s, 80초 유휴를 끼워도 5.57 / 5.30 / 5.58s 로
**첫 회 페널티가 없다.**

> 사용자가 "처음 다운로드가 느리다" 고 한 것은 업로드가 아니라 **빌드**였다.
> 27.93 / 27.94 / **2.31**s — 3회차부터 코어 캐시가 먹는다. IDE 가 컴파일과
> 업로드를 한 동작으로 보여주니 다운로드가 느린 것처럼 보인다.
> (2회차까지 캐시를 못 쓰는 이유는 arduino-cli 쪽이라 미확인)
