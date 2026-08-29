#include "hw_def.h"

#ifdef _USE_HW_USB
#include "tusb.h"
#include "usb.h"


/*
 * 런타임 컴포지트 전환
 *
 * 부트로더는 두 가지 모습으로 열거된다.
 *   CDC + HID        : 평소 (앱이 요청해서 들어온 경우 등)
 *   CDC + HID + MSC  : 리셋 더블클릭. UF2 드래그앤드롭이 필요할 때
 *
 * 디스크립터를 두 벌 미리 만들어 두고 usbDescInit() 이 고른다.
 * **PID 도 함께 바꾼다.** 같은 VID/PID 로 인터페이스 구성만 바뀌면 호스트(특히
 * 윈도우)가 캐시된 드라이버 정보를 재사용해 새 인터페이스를 인식하지 못한다.
 *
 * 이 방식은 Adafruit_nRF52_Bootloader 계열에서 쓰던 것과 같다
 * (nrf52_bot/.../usb_desc.c 의 _cdc_only).
 */
static bool is_with_msc = false;


//--------------------------------------------------------------------
// Device Descriptor
//--------------------------------------------------------------------

//-- CDC 가 IAD 를 쓰므로 디바이스 클래스는 MISC/COMMON/IAD 여야 한다.
static tusb_desc_device_t desc_device =
{
  .bLength            = sizeof(tusb_desc_device_t),
  .bDescriptorType    = TUSB_DESC_DEVICE,
  .bcdUSB             = 0x0200,

  .bDeviceClass       = TUSB_CLASS_MISC,
  .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
  .bDeviceProtocol    = MISC_PROTOCOL_IAD,
  .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,

  .idVendor           = HW_USB_VID,
  .idProduct          = HW_USB_PID,
  .bcdDevice          = 0x0100,

  .iManufacturer      = 0x01,
  .iProduct           = 0x02,
  .iSerialNumber      = 0x03,

  .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void)
{
  return (uint8_t const *)&desc_device;
}


//--------------------------------------------------------------------
// HID Report Descriptor
//--------------------------------------------------------------------

/*
 * 전용 usage page 를 쓴다.
 *
 * 두 가지를 동시에 만족해야 한다.
 *
 * 1) OS 가 키보드/마우스로 오인하면 안 된다.
 *    윈도우는 Generic Desktop 의 키보드/마우스 top-level collection 을 사용자
 *    프로그램이 여는 것을 막는다. vendor-defined 범위(0xFF00~0xFFFF)를 써야 한다.
 *
 * 2) **다른 HID 장치와 헷갈리면 안 된다.**
 *    TinyUSB 의 TUD_HID_REPORT_DESC_GENERIC_INOUT 은 usage page 0xFF00 / usage 0x01
 *    을 쓰는데, 이건 기본값이라 온갖 장치가 같이 쓴다. QMK/VIA 는 0xFF60/0x61 이다.
 *    호스트 툴이 "0xFF00 짜리 HID" 를 찾으면 키보드까지 걸린다.
 *
 *    그래서 이 부트로더 전용으로 **0xFF75 / usage 0x01** 을 쓴다.
 *
 * 호스트 툴의 매칭 조건 (셋 다 봐야 한다):
 *    VID 0x1209 + PID 0xB752 + usage page 0xFF75
 * 보드가 여러 종류로 늘어나면 usage page 는 그대로 두고 PID 로 구분한다.
 * 그러면 툴 하나로 모든 baram 부트로더를 찾되 키보드와는 절대 안 겹친다.
 */
#define HID_USAGE_PAGE_BOOT   0xFF75
#define HID_USAGE_BOOT        0x01

uint8_t const desc_hid_report[] =
{
  HID_USAGE_PAGE_N ( HID_USAGE_PAGE_BOOT, 2                ),
  HID_USAGE        ( HID_USAGE_BOOT                        ),
  HID_COLLECTION   ( HID_COLLECTION_APPLICATION            ),
    //-- 호스트 -> 장치 (OUT)
    HID_USAGE       ( 0x02                                  ),
    HID_LOGICAL_MIN ( 0x00                                  ),
    HID_LOGICAL_MAX_N ( 0x00ff, 2                           ),
    HID_REPORT_SIZE ( 8                                     ),
    HID_REPORT_COUNT( CFG_TUD_HID_EP_BUFSIZE                ),
    HID_OUTPUT      ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ),

    //-- 장치 -> 호스트 (IN)
    HID_USAGE       ( 0x03                                  ),
    HID_LOGICAL_MIN ( 0x00                                  ),
    HID_LOGICAL_MAX_N ( 0x00ff, 2                           ),
    HID_REPORT_SIZE ( 8                                     ),
    HID_REPORT_COUNT( CFG_TUD_HID_EP_BUFSIZE                ),
    HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ),
  HID_COLLECTION_END
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
  (void)instance;
  return desc_hid_report;
}


//--------------------------------------------------------------------
// Configuration Descriptor
//--------------------------------------------------------------------

enum
{
  ITF_NUM_CDC = 0,
  ITF_NUM_CDC_DATA,
  ITF_NUM_HID,
  ITF_NUM_MSC,
  ITF_NUM_TOTAL_MSC,
};
#define ITF_NUM_TOTAL_NO_MSC    (ITF_NUM_MSC)

#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82
#define EPNUM_HID_OUT     0x03
#define EPNUM_HID_IN      0x83
#define EPNUM_MSC_OUT     0x04
#define EPNUM_MSC_IN      0x84

#define CONFIG_LEN_NO_MSC  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN + TUD_HID_INOUT_DESC_LEN)
#define CONFIG_LEN_MSC     (CONFIG_LEN_NO_MSC + TUD_MSC_DESC_LEN)

//-- 문자열 인덱스
#define STR_IDX_CDC   4
#define STR_IDX_HID   5
#define STR_IDX_MSC   6


static uint8_t const desc_cfg_no_msc[] =
{
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL_NO_MSC, 0, CONFIG_LEN_NO_MSC,
                        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STR_IDX_CDC, EPNUM_CDC_NOTIF, 8,
                     EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

  TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID, STR_IDX_HID, HID_ITF_PROTOCOL_NONE,
                           sizeof(desc_hid_report),
                           EPNUM_HID_OUT, EPNUM_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 1),
};

static uint8_t const desc_cfg_msc[] =
{
  TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL_MSC, 0, CONFIG_LEN_MSC,
                        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

  TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, STR_IDX_CDC, EPNUM_CDC_NOTIF, 8,
                     EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),

  TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID, STR_IDX_HID, HID_ITF_PROTOCOL_NONE,
                           sizeof(desc_hid_report),
                           EPNUM_HID_OUT, EPNUM_HID_IN, CFG_TUD_HID_EP_BUFSIZE, 1),

  TUD_MSC_DESCRIPTOR(ITF_NUM_MSC, STR_IDX_MSC, EPNUM_MSC_OUT, EPNUM_MSC_IN, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
  (void)index;
  return is_with_msc ? desc_cfg_msc : desc_cfg_no_msc;
}


//--------------------------------------------------------------------
// String Descriptors
//--------------------------------------------------------------------

static char const *string_desc_arr[] =
{
  (const char[]){0x09, 0x04},   // 0: en-US
  "BARAM",                      // 1: Manufacturer
  _DEF_BOARD_NAME,              // 2: Product
  NULL,                         // 3: Serial (UID 에서 생성)
  "H750 CDC",                   // 4
  "H750 BOOT HID",              // 5
  "H750 MSC",                   // 6
};

static uint16_t _desc_str[32 + 1];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
  size_t chr_count = 0;

  (void)langid;

  switch (index)
  {
    case 0:
      memcpy(&_desc_str[1], string_desc_arr[0], 2);
      chr_count = 1;
      break;

    case 3:
      chr_count = usbGetSerial(&_desc_str[1], 32);
      break;

    default:
      if (index >= (sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) return NULL;
      {
        const char *str = string_desc_arr[index];
        size_t len = strlen(str);

        if (len > 32) len = 32;
        for (size_t i=0; i<len; i++) _desc_str[1+i] = str[i];
        chr_count = len;
      }
      break;
  }

  //-- 첫 워드는 길이(바이트) + 타입
  _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

  return _desc_str;
}


/*
 * 어떤 모습으로 열거할지 정한다. tusb_init() 보다 먼저 불려야 한다.
 */
void usbDescInit(bool with_msc)
{
  is_with_msc = with_msc;

  desc_device.idProduct = with_msc ? HW_USB_PID_MSC : HW_USB_PID;
}

#endif
