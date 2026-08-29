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

## 실측 결과

256KB 를 CDC/HID 로 쓰면서 진행률 바가 0→100% 로 차는 것을 확인했다.
전송이 끝나면 대기 화면으로 자동 복귀한다.

빌드 크기 : 95,712 B → **97,080 B** (126KB 중 75.2%). UI 모듈이 약 1.4KB.

모듈 등록도 확인 (`ap` + `cli` + `ui` = 3):

```
[  ] moduleInit()
       count : 3
[  ] moduleBegin()
       cli OK
       ui OK
```
