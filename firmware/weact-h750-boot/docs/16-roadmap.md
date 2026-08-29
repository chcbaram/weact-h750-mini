# 16. 남은 작업

## 9단계 — 앱 프로젝트 `weact-h750-fw`

`firmware/weact-h750-fw/` 에 부트로더 골격을 복사해 둔 상태다 (`src`, `tools`,
`CMakeLists.txt`). **아직 XiP 용으로 고치지 않았다.**

해야 할 것:

1. **링커스크립트** `src/bsp/ldscript/STM32H750VBTx_QSPI.ld`
   ```
   VECTOR (rx) : 0x90001000, 1K
   VER    (rx) : 0x90001400, 1K
   FLASH  (rx) : 0x90001800, 8M - 6K
   ```
   `_fw_flash_begin` / `_fw_flash_end` 심볼과 `.version` 섹션 필수.
   **`_fw_flash_size = _fw_flash_end - _fw_flash_begin;`** 절대 심볼도 만들 것
   (12 문서의 링커 접기 함정).
   `.data` 의 LMA 가 FLASH 에 마지막으로 놓여야 한다.

2. **`system_stm32h7xx.c`** 에 `SCB->VTOR = (uint32_t)&_fw_flash_begin;`

3. **`hw.c`** 가 `.version` 에 `firm_ver_t` 를 내보내되 `firm_size` 를 채울 것
   (부트로더의 것은 `firm_size = 0` 으로 두어도 된다)

4. **`hw_def.h`** : `HW_DEV_MODE = HW_DEV_MODE_APP`, `HW_LED_TOGGLE_MS = 500`,
   `HW_LCD_HANGUL 1`(앱은 한글 써도 된다), USB PID 를 앱용으로

5. **USB** : CDC + HID 만 (MSC 제외). `uf2` / `ui` 모듈 제거는 이미 되어 있음

6. **`cmd_boot.c`** 는 `HW_DEV_MODE_APP` 로 컴파일되어 `FW_UPDATE`/`FW_JUMP` 가
   `resetToBoot()` 이 된다 (이미 그렇게 작성돼 있음)

7. **post-build** 에 `.bin` + `.uf2` 생성
   `uf2conv.py --base 0x0 --family 0xFFFF0004` (**`--base 0x0` 필수**)

8. **`SystemClock_Config`** — PLL1 을 설정해야 한다 (12 문서).
   `RCC_PERIPHCLK_QSPI` 와 `RCC_PERIPHCLK_RTC` 는 넣지 말 것.

9. **`resetConfirmBoot()` 호출** — 앱이 일정 시간 정상 동작한 뒤 부른다.
   그래야 부트로더의 `HW_BOOT_TRY_MAX` 를 켤 수 있다. 지금은 0(비활성)인데,
   아두이노 코어가 이 함수를 부르지 않아서 켜면 정상 스케치가 막히기 때문이다.
   자체 앱에는 넣을 수 있다 (07 문서).

> 현재 상태: `PRJ_NAME` 과 USB PID 만 앱용으로 바꿔뒀다. 링커스크립트가 아직
> `STM32H750VBTx_BOOT.ld` 라 **빌드하면 내부 플래시로 링크된다.** 빌드 시도조차
> 하지 않은 상태다. `.vscode` 태스크/런처는 이미 들어가 있다.

## 10단계 — 툴링  **[완료]**

`.vscode/tasks.json` / `launch.json` 과 `tools/openocd/*.cfg` 를 두 프로젝트 모두에
넣었다. 실기 검증까지 마쳤다. 상세는 **13 문서**.

남은 것:
- 호스트 업로드 툴 : **Go + CDC** 권장 (CGO 불필요, 295 KB/s. HID 는 39.6 KB/s).
  아두이노 세션이 `baramdl` 로 진행 중

## 11단계 — 아두이노 코어 (별도 저장소, 다른 세션이 진행)

`/Users/hancheol/Documents/Arduino/hardware/baram-stm32-arduino`

이미 전달한 사양은 12 문서에 정리되어 있다. 세션 이름 `baram-stm32-arduino-d5`.

남은 것:
- `SystemClock_Config` 에서 **PLL1 복구** (현재 빠져 있어 64MHz 로 돎)
- 부트로더 `.bin` 교체 (클럭/SIOO/점프 경로가 바뀌어 어제 빌드분은 XiP 점프 실패)
- 호스트 업로드 툴
- `Burn Bootloader` (DFU / ST-LINK)

## 검토했으나 미구현

- **부트 로그(플래시)** — 내부 플래시가 단일 128KB 섹터라 불가. 두 번째 SPI 플래시(U8, SPI1)로 옮길 수 있다
- **A/B 슬롯 / 롤백** — QSPI 8MB 라 공간은 충분. 단일 영역으로 시작했다
- **소프트웨어 ROM DFU 점프** (`0x1FF09800`) — 아두이노 세션이 요청했다. 부트로더 자신을 덮어쓰는 경로라 확인 프롬프트 필요
- **~~부팅 확인/폴트 자동 복구~~** — **구현했다** (07 문서). `HW_BOOT_FAULT_MAX` 활성,
  `HW_BOOT_TRY_MAX` 는 앱이 `resetConfirmBoot()` 을 불러야 해서 0(비활성)
- **~~LCD 점프 실패 표시~~** — **구현했다** (11 문서). 점프 성공 화면도 함께
- **UF2 상한 8MB 완전 사용** — 지금 8119 KB. 나머지 70KB 를 쓰려면 손으로 만든
  FAT16 부트섹터 지오메트리를 다시 만들어야 한다. 이득이 작다 (10 문서)
