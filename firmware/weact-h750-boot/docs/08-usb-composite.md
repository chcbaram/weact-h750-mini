# 08. USB (TinyUSB, 런타임 컴포지트)

## 목적

부트로더를 USB 장치로 올린다. **더블탭 여부에 따라 열거되는 인터페이스가 달라야 한다.**

- 평소 : CDC + HID
- 리셋 더블클릭 / 앱이 요청 : CDC + HID + **MSC(UF2)**

## 대상 파일

- `src/lib/tinyusb/` — TinyUSB **0.20.0** (`stm32h7r-fw` 에서 벤더링)
- `src/hw/driver/usb/{tusb_config.h, usb.c, usb.h, usb_desc.c, usb_hid.c}`
- `src/hw/driver/cdc.c`

## 왜 TinyUSB 인가

ST 스택도 컴포지트를 만들 수 있지만(`USBD_RegisterClassComposite`), 런타임 전환에는
TinyUSB 가 압도적으로 단순하다.

| 항목 | TinyUSB | ST 스택 |
|---|---|---|
| 런타임 전환 | `const` 디스크립터 2벌 + 콜백에서 고르기. 30줄 | 컴포지트 빌더. `classId` 수동 복원 안 하면 **CDC RX 콜백이 조용히 죽음**(ST 샘플 자체 버그), `USBD_free` 가 실제로 해제 안 해서 재초기화 전 `USBD_static_reset` 필요 |
| UF2 모듈 | `tud_msc_*` 콜백 기반. 그대로 재사용 | 재작성 |
| HID cmd 채널 | `tud_hid_report()` 기반. 그대로 재사용 | 재작성 |
| H750 OTG_FS | `dcd_dwc2` + `OPT_MCU_STM32H7`. `convex-boot`(H743)에서 검증됨 | 가능 |

## STM32H750 OTG_FS 초기화

```c
PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
HAL_PWREx_EnableUSBVoltageDetector();

/* PA11 -> DM, PA12 -> DP.  AF10 (OTG2_HS 라는 이름이지만 FS 포트다) */
GPIO_InitStruct.Alternate = GPIO_AF10_OTG2_HS;

__HAL_RCC_USB_OTG_FS_CLK_ENABLE();

//-- 이 보드는 VBUS 가 MCU 에 연결돼 있지 않다 (회로도 02-DC-DC)
USB_OTG_FS->GCCFG   &= ~USB_OTG_GCCFG_VBDEN;      // VBUS 감지 끄기
USB_OTG_FS->GOTGCTL |= USB_OTG_GOTGCTL_BVALOEN;   // B-peripheral 세션 유효 강제
USB_OTG_FS->GOTGCTL |= USB_OTG_GOTGCTL_BVALOVAL;

HAL_NVIC_SetPriority(OTG_FS_IRQn, 5, 0);          // TinyUSB 는 0(최고)로 둔다
```

**VBUS 오버라이드를 빼먹으면 절대 열거되지 않는다.** 컨트롤러가 케이블이 안 꽂혔다고
판단하기 때문이다.

레지스터 이름에 주의. 클래식 H7 은 `GOTGCTL` 의 `BVALOEN`/`BVALOVAL` 이고,
H7RS/U5 는 `GCCFG` 의 `VBVALEXTOEN`/`VBVALOVAL` 이다. 그대로 베끼면 컴파일은 되는데
동작하지 않는다.

`RHPort` 는 0 (H750 은 OTG_FS 하나뿐이다). D-Cache 를 켜고 쓰므로
`CFG_TUD_MEM_DCACHE_ENABLE 1` 로 DWC2 의 캐시 유지보수를 켠다.

## 런타임 컴포지트 전환

디스크립터를 두 벌 미리 만들어 두고 `usbDescInit(bool with_msc)` 가 고른다.

```c
uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
  return is_with_msc ? desc_cfg_msc : desc_cfg_no_msc;
}
```

**PID 도 함께 바꾼다** (`0xB010` ↔ `0xB011`). 같은 VID/PID 로 인터페이스 구성만 바뀌면
호스트(특히 윈도우)가 캐시된 드라이버 정보를 재사용해 새 인터페이스를 인식하지 못한다.

`CFG_TUD_MSC` 는 컴파일 타임에 1 로 두고 **디스크립터만** 바꾼다.

인터페이스/엔드포인트 배치:

```
ITF 0,1  CDC   EP 0x81(notif) 0x02(out) 0x82(in)
ITF 2    HID   EP 0x03(out)   0x83(in)
ITF 3    MSC   EP 0x04(out)   0x84(in)     <- with_msc 일 때만
```

CDC 가 IAD 를 쓰므로 디바이스 클래스는 `MISC`/`COMMON`/`IAD` 여야 한다.

HID 는 **vendor-defined usage page(0xFF00)** 를 쓴다. OS 가 키보드/마우스로 오인하지
않고(윈도우는 그런 top-level collection 을 사용자 프로그램이 여는 것을 막는다),
WebHID 선택창에도 잡힌다.

## CDC 소유권 — 보율로 가른다

CDC 하나를 CLI 와 cmd 패킷이 나눠 쓸 수 없다. 둘 다 `cdcRead()` 를 부르면 서로의 바이트를
훔친다. 그래서 **호스트가 연 보율로 주인을 정한다.**

| 보율 | 주인 |
|---|---|
| 115200 | `USB_CON_CLI` — 사람이 터미널로 붙었다. CLI 가 CDC 를 쥔다 |
| 그 외 | `USB_CON_CDC` — 호스트 툴이다. cmd 가 독점한다 |

HID 는 전용 채널이라 이 판정과 무관하게 항상 돈다.

## 시리얼 번호

MCU 의 96비트 UID 로 만든다. **UID 는 워드 단위로만 읽힌다** — 바이트 접근하면 엉뚱한
값이 나온다.

## 함정

- `usbInit()` 을 `hwInit()` 에서 부르지 않는다. `bootUp()` 이 "점프하지 않는다"고 판단한
  뒤 `apInit()` 에서 연다. 그렇지 않으면 정상 부팅마다 호스트에 장치가 나타났다 사라진다.
- `usbDescInit()` 은 `tusb_init()` **보다 먼저** 불려야 한다.
- `uf2_disk.c` 처럼 `#if CFG_TUD_MSC` 로 감싼 파일은 **`tusb.h` 를 먼저 포함**해야 한다.
  안 그러면 매크로가 정의되지 않아 파일 전체가 조용히 사라지고 링크 에러로만 드러난다.

## 검증 방법

```bash
system_profiler SPUSBDataType | grep -A9 WEACT-H750-BOOT
ls /Volumes/                       # 더블탭/펌웨어 없음일 때 H750BOOT
ls /dev/cu.usbmodem*               # CDC
```

## 실측 결과

**열거 성공.**

```
WEACT-H750-BOOT:
  Product ID: 0xb011          <- MSC 포함 모드 PID. 런타임 전환 동작 확인
  Vendor ID: 0xcafe
  Serial Number: 001E00333233510837333531
  Manufacturer: BARAM
  Speed: Up to 12 Mb/s
```

시리얼이 SWD 로 읽은 UID(`001e0033 32335108 37333531`)와 정확히 일치한다.

**CDC CLI 동작 확인** (`/dev/cu.usbmodem1412301`, 115200):

```
cli# boot info
boot addr  : 0x08000000 (128 KB)
firm addr  : 0x90000000 (8 MB, QSPI XiP)
  tag      : 0x90000000 (4 KB)
  vector   : 0x90001000
img type   : NONE

cli# usb info
init    : 1
mode    : CDC+HID+MSC
connect : 1
open    : 1
baud    : 115200
```

CLI 명령 14개(HELP/MD/LOG/UART/RTC/RESET/GPIO/QSPI/FLASH/LCD/BOOT/UF2/USB/MODULE) 전부 등록.

**[미확인]** CDC+HID 전용 모드(PID `0xB010`)는 아직 실기 확인 못 했다. 현재 QSPI 가 비어
있어 `bootUp()` 이 "no firmware" 로 판단해 MSC 를 강제로 켜기 때문이다. 9단계에서 실제
앱이 올라간 뒤 확인한다.
