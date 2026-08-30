# 16. 남은 작업

## 9단계 — 앱 프로젝트 `weact-h750-fw`  **[코드 완료 · 실기 미검증]**

빌드까지 끝났다. 상세는 **12 문서** 아래쪽 "앱 프로젝트 구현" 절.

했던 것 (요약):

1. `STM32H750VBTx_QSPI.ld` — `VECTOR 0x90001000 / VER 0x90001400 / FLASH 0x90001800`
2. `_fw_flash_size` 를 SECTIONS **바깥**에 두어 절대 심볼로. `nm` 타입이 `A` 인지 확인
3. `SCB->VTOR = (uint32_t)&_fw_flash_begin` (OR 가 아니라 대입)
4. `hw_def.h` : `HW_DEV_MODE_APP`, LED 500ms, 한글 on, `_USE_HW_QSPI` **미정의**
5. `PeriphCommonClock_Config` 에서 QSPI/RTC 를 빼고 USB 만 남김
6. `bspMpuInit()` 호출 제거 — 부트로더 설정을 물려받는다
7. post-build `.uf2` (`--base 0x0 --family 0xFFFF0004`)
8. `resetConfirmBoot()` 추가 (3초 생존 시)

**중간에 발견한 것**: 스캐폴딩 복사본이 부트로더의 그동안 수정을 못 따라오고
있었다 — QSPI 커널이 아직 `PLL2` 였다. 그대로 빌드했으면 `SystemInit()` 이 PLL2 를
끄는 순간 자기 명령어 클럭이 끊겼을 것이다. 공유 파일 네 개를 재동기화했고,
`boot.c` / `cmd_boot.c` 는 **한 파일이 두 모드를 처리**하게 고쳐서 앞으로
복사본이 어긋나지 않게 했다.

**남은 것: 실기 검증.** STATUS 의 "다음 할 일" 참고.

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

## SD 를 붙일 때 미리 알아둘 것 — HAL `SD_PowerON()` 버그

부트로더는 SD 를 쓰지 않지만 앱이 쓰게 되면 바로 밟는다. 벤더링한
STM32Cube_FW_H7_V1.12.1 에 있다.

```c
/* stm32h7xx_hal_sd.c : SD_PowerON() */
errorstate = SDMMC_CmdOperCond(hsd->Instance);      /* CMD8 */
if (errorstate == SDMMC_ERROR_TIMEOUT)             /* 0x80000000 */
  hsd->SdCard.CardVersion = CARD_V1_X;
else
  hsd->SdCard.CardVersion = CARD_V2_X;
```

`SDMMC_GetCmdResp7()` 이 돌려주는 값은 두 가지다.

| 상황 | 반환값 |
|---|---|
| 소프트웨어 폴링 카운터 소진 (플래그가 아예 안 섬) | `SDMMC_ERROR_TIMEOUT` **0x80000000** |
| **하드웨어 CTIMEOUT — 카드가 CMD8 에 응답 없음** | `SDMMC_ERROR_CMD_RSP_TIMEOUT` **0x00000004** |

**정상적인 "카드 무응답" 은 0x00000004 인데 0x80000000 과 비교한다.** 절대 일치하지
않으므로 카드가 아예 없어도 `CardVersion = CARD_V2_X` 가 기록되고, 그 뒤 CMD55 가
실패하며 `UNSUPPORTED_FEATURE` 로 끝난다.

핸들만 보면 **"V2 카드는 인식했는데 CMD55 만 실패"** 로 읽혀서 엉뚱한 곳을 뒤지게
된다. 실제로는 CMD8 부터 아무 응답이 없었던 것이다. V1 카드도 V2 로 오인된다.

아두이노 세션이 raw CPSM 으로 확정했다.

```
CMD0   STA=0x00000080 CMDSENT      명령은 선에 나간다
CMD8   STA=0x00000004 CTIMEOUT
CMD55  STA=0x00000004 CTIMEOUT
```

## 검토했으나 미구현

- **부트 로그(플래시)** — 내부 플래시가 단일 128KB 섹터라 불가. 두 번째 SPI 플래시(U8, SPI1)로 옮길 수 있다
- **A/B 슬롯 / 롤백** — QSPI 8MB 라 공간은 충분. 단일 영역으로 시작했다
- **소프트웨어 ROM DFU 점프** (`0x1FF09800`) — 아두이노 세션이 요청했다. 부트로더 자신을 덮어쓰는 경로라 확인 프롬프트 필요
- **~~부팅 확인/폴트 자동 복구~~** — **구현했다** (07 문서). `HW_BOOT_FAULT_MAX` 활성,
  `HW_BOOT_TRY_MAX` 는 앱이 `resetConfirmBoot()` 을 불러야 해서 0(비활성)
- **~~LCD 점프 실패 표시~~** — **구현했다** (11 문서). 점프 성공 화면도 함께
- **UF2 상한 8MB 완전 사용** — 지금 8119 KB. 나머지 70KB 를 쓰려면 손으로 만든
  FAT16 부트섹터 지오메트리를 다시 만들어야 한다. 이득이 작다 (10 문서)
