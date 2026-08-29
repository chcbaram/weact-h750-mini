# weact-h750-boot 문서

WeAct STM32H750 Mini 용 부트로더. 앱은 외부 QSPI 플래시에서 XiP 로 실행되고,
이 부트로더만이 그 QSPI 를 프로그래밍할 수 있다.

각 문서는 **목적 → 대상 파일 → 설계 결정과 근거 → 함정/주의사항 → 검증 방법 → 실측 결과**
순서를 따른다. 실기에서 확인하지 못한 값은 **[미확인]** 으로 표시한다.

| 문서 | 내용 |
|---|---|
| [STATUS.md](STATUS.md) | **현재 진행 상황 · 개발 환경 · 실기에서 잡은 함정** |
| [00-memory-map.md](00-memory-map.md) | 내부 플래시/QSPI 배치, 태그·버전 포맷, RAM 도메인 선택 근거 |
| [01-project-skeleton.md](01-project-skeleton.md) | 디렉토리, CMake, 툴체인, 링커스크립트, HAL 이식 |
| [02-hw-layer.md](02-hw-layer.md) | hw_def.h, 이식한 드라이버와 핀맵 |
| [03-clock-cache-mpu.md](03-clock-cache-mpu.md) | 클럭 트리, I/D-Cache, MPU 리전 |
| [04-qspi-xip.md](04-qspi-xip.md) | QUADSPI 드라이버, memory-mapped, **XiP 쓰기 불가 제약** |
| [05-lcd-st7735.md](05-lcd-st7735.md) | SPI4+DMA, 프레임버퍼 배치, 패널 오프셋 |
| [06-reset-doubleclick.md](06-reset-doubleclick.md) | H7 리셋 플래그 실측, RTC 백업, 300ms 판정 |
| [07-boot-jump.md](07-boot-jump.md) | 이미지 식별 3단계(TAG/VER/RAW), VTOR·MSP 책임 분담 |
| [08-usb-composite.md](08-usb-composite.md) | TinyUSB, OTG_FS, 런타임 MSC 디스크립터 전환 |
| [09-cmd-protocol.md](09-cmd-protocol.md) | cmd_driver_t, HID 프레이밍, BOOT_CMD_* |
| [10-uf2.md](10-uf2.md) | UF2 파싱, FAT 가상 디스크, family ID |
| [11-boot-ui.md](11-boot-ui.md) | LCD 진행률 UI |
| [12-app-xip.md](12-app-xip.md) | **앱(XiP) 쪽 규약.** 부트로더가 넘기는 상태, 앱 금지사항 |
| 13-tooling.md | openocd + stmqspi, uf2conv, VSCode 태스크 |
| 14-test.md | 호스트/타깃 테스트 |
| 15-arduino-core.md | 아두이노 variant, VTOR 함정, 업로드 툴 |
| [16-roadmap.md](16-roadmap.md) | 남은 설계와 결정 근거 |

## 빌드

```bash
cd firmware/weact-h750-boot
cmake -S . -B build
cmake --build build -j8
```

`-T` 링커 옵션이 `../src/bsp/ldscript/...` 상대경로라 **`build/` 는 반드시 프로젝트
루트 바로 아래**에 있어야 한다.

## 플래시

이 맥(macOS 12.7.6)에서는 `STM32_Programmer_CLI` 가 실행되지 않는다 (Qt 가 macOS 13+ 요구).
**openocd** 를 쓴다.

```bash
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
        -c "program build/weact-h750-boot.elf verify reset exit"
```
