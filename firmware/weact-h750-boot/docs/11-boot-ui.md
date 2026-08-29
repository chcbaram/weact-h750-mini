# 11. 부트로더 LCD UI

## 목적

USB 나 시리얼 없이도 지금 무슨 일이 벌어지는지 화면으로 알 수 있게 한다.
특히 **다운로드 진행률**.

## 대상 파일

- `src/ap/modules/ui/{ui.c, ui.h}`

## 그리기는 메인 루프에서만

**USB 콜백 안에서 그리면 안 된다.** `tud_msc_write10_cb()` 나 cmd 처리 중에 `lcdUpdateDraw()`
를 부르면 SPI DMA 를 기다리는 동안 `tud_task()` 가 못 돌아 USB 스택이 멈춘다.
호스트는 이걸 전송 실패로 본다.

그래서 UI 모듈은 `MODULE_DEF` 로 등록되어 `moduleUpdate()` 에서만 그린다.
진행률은 상태만 읽어온다.

## 두 경로의 진행률을 합친다

업데이트 경로가 둘이라 진행률 소스도 둘이다.

| 경로 | 방식 |
|---|---|
| UF2 (MSC) | `ui.c` 가 `uf2IsBusy()` / `uf2GetPercent()` 를 **폴링** |
| cmd (HID/CDC) | `cmd_boot.c` 가 `FW_WRITE` 마다 `uiSetProgress()` 를 **푸시** |

```c
bool    busy = uf2IsBusy() || cmd_busy;
uint8_t pct  = uf2IsBusy() ? uf2GetPercent() : cmd_pct;
```

cmd 쪽은 `FW_BEGIN` 에서 `uiSetProgress(0)`, `FW_END` 에서 `uiEndProgress()` 를 부른다.

## 다시 그리는 조건

160×80 프레임버퍼 전체를 SPI DMA 로 밀면 25,600 바이트다. 매 루프 그리면 USB 와
플래시 쓰기가 쓸 시간을 다 먹는다.

1. **10Hz 로 제한** (`millis() - pre_time < 100` 이면 반환)
2. `lcdDrawAvailable()` 로 이전 DMA 가 끝났는지 확인
3. **진행률 값이 바뀔 때만** 다시 그린다

## 화면

**대기 화면** — 진입 사유를 보여준다. 왜 부트로더에 있는지가 제일 궁금한 정보다.

```
H750 BOOT            (11x18, green)
V260829R1            (7x10, white)
DOUBLE RESET         (7x10, gray)   진입 사유
USB CDC+HID+MSC      (7x10, yellow) MSC 가 열렸는지
FW: TAG              (7x10, green)  이미지 식별 단계. NONE 이면 red
────────────────     (파란 바)
```

**진행률 화면**

```
UPDATING       85%   (둘 다 11x18)
┌──────────────────┐
│██████████        │  (라운드 사각형 테두리 + 채움)
└──────────────────┘
```

라벨과 퍼센트를 같은 크기(11x18)로 한 줄에 놓는다.
`"UPDATING"` 8글자 × 11 = 88px (x=4~92), `"%3d%%"` 4글자 × 11 = 44px 를 오른쪽 정렬로
x=112 에 두면 겹치지 않는다. `%3d` 로 폭을 고정해 숫자가 흔들리지 않게 한다.

**점프 화면** — 앱으로 넘어가기 직전에 그린다.

```
RUN APP              (11x18, green)
WEACT-H750-MINI      (7x10, white)   firm_ver_t.name_str
ARDUINO              (7x10, gray)    firm_ver_t.version_str
FW:TAG 0x90009ED5    (7x10, green)   식별 단계 + 실제 진입 주소
────────────────     (초록 바)
```

정상 부팅에서는 **`uiInit()` 이 아예 불리지 않는다.** `bootUp()` 이 `moduleInit()`
앞에서 점프해 버리기 때문이다. 그래서 이전까지는 `lcdInit()` 이 만든 **검은 화면이
켜진 채로** 앱에 넘어갔다 (백라이트는 `lcdInit()` 이 이미 100 으로 켠다).

이름과 버전을 함께 띄우는 게 이 화면의 실질적인 값이다. 이미지를 여러 개 번갈아 구울 때
"지금 뭐가 돌고 있나" 를 눈으로 바로 확인할 수 있다.

**에러 화면** — 점프에 실패했을 때.

```
JUMP FAIL            (11x18, red)
BAD IMAGE            (7x10, red)     실패 사유
FW: TAG              (7x10, gray)    이미지 식별 단계
DROP .UF2 TO FIX     (7x10, yellow)  지금 뭘 하면 되는지
────────────────     (빨간 바)
```

마지막 줄이 중요하다. 사유만 띄우면 사용자가 다음에 뭘 해야 할지 모른다. MSC 가
열렸으면 `DROP .UF2 TO FIX`, 아니면 `WAITING UPDATE` 를 띄운다.

## 점프 화면은 왜 boot.c 에서, 에러 화면은 왜 ui.c 에서 그리나

비대칭이라 헷갈리기 쉽다. 이유가 있다.

**점프 화면** (`uiShowJump()`) 은 `bootJumpFirm()` 이 **직접, 동기로** 그린다.
점프하면 돌아오지 않으므로 메인 루프에 맡길 수가 없다.

두 가지 제약이 있다.

1. **`bspDeInit()` 앞이어야 한다.** `bspDeInit()` 은 NVIC 를 전부 마스크한다. 그 뒤에는
   SPI DMA 완료 인터럽트가 오지 않아 전송이 끝나지 않는다. 화면이 절반만 갱신된 채로
   점프하게 된다.
2. **`lcdRequestDraw()` 가 아니라 `lcdUpdateDraw()` 를 쓴다.** 전자는 DMA 를 걸어두기만
   한다. 완료를 기다려야 한다.

또 `bootGetVer()` 가 indirect 읽기를 하는데, `bootJumpFirm()` 이 이미
`qspiSetXipMode(false)` 를 해둔 뒤라서 성립한다. XiP 를 켠 뒤였다면 못 읽는다.

**에러 화면** 은 반대로 `uiSetError()` 로 **상태만 기록**하고 `uiDrawIdle()` 이 그린다.
점프에 실패하면 부트로더에 머무르고, 그 직후 `moduleInit()` → `uiInit()` →
`uiDrawIdle()` 이 화면을 덮어쓰기 때문이다. 실패 시점에 직접 그리면 지워진다.

## 백라이트 순서

`uiInit()` 에서 **그린 뒤에** 켠다.

```c
uiDrawIdle();
lcdUpdateDraw();
lcdSetBackLight(100);
```

먼저 켜면 초기화 안 된 프레임버퍼의 쓰레기가 한 번 번쩍인다.

## 검증 방법

HID 또는 CDC 로 큰 이미지를 쓰면서 화면을 본다.

```bash
# 256KB 를 천천히 써서 진행률 바가 차는 것을 본다
python3 tools/download/... (cmdproto 사용)
```

## 화면을 눈으로 안 보고 검증하는 법

프레임버퍼가 D2 SRAM(`0x30000000`, 25,600 B)에 그대로 있으므로 SWD 로 떠서 이미지로
바꾸면 된다. 앱(아두이노 Blink)은 LCD 를 안 건드리므로 **점프한 뒤에도 부트로더가
남긴 화면이 그대로 남아 있다.**

```bash
openocd -f tools/openocd/weact-h750.cfg \
        -c init -c halt -c "dump_image fb.bin 0x30000000 25600" -c resume -c shutdown
# RGB565 little-endian 160x80 -> PPM/PNG 로 변환
```

이 방법으로 두 화면 모두 확인했다. 카메라도, 눈으로 보는 것도 필요 없다.

에러 화면은 `-DBOOT_NO_JUMP` 로 임시 빌드하면 재현된다. `bootJumpFirm()` 이
`ERR_BOOT_INVALID_FW` 로 조기 리턴하므로 실패 경로가 그대로 탄다.

## 실측 결과

256KB 를 CDC/HID 로 쓰면서 진행률 바가 0→100% 로 차는 것을 확인했다.
전송이 끝나면 대기 화면으로 자동 복귀한다.

점프/에러 화면도 프레임버퍼 덤프로 확인했다 (2026-08-30).

```
RUN APP  /  WEACT-H750-MINI  /  ARDUINO  /  FW:TAG 0x90009ED5
JUMP FAIL  /  BAD IMAGE  /  FW: TAG  /  DROP .UF2 TO FIX
```

`0x90009ED5` 는 `mdw 0x90001004` 값과 일치한다 - 화면이 실제 진입 주소를 보여준다.

빌드 크기 : 95,712 B → 97,080 B → **97,712 B** (126KB 중 75.7%).
UI 모듈 약 1.4KB, 점프/에러 화면 추가로 +632 B.

모듈 등록도 확인 (`ap` + `cli` + `ui` = 3):

```
[  ] moduleInit()
       count : 3
[  ] moduleBegin()
       cli OK
       ui OK
```
