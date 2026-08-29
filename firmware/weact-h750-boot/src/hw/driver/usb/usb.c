#include "usb.h"
#include "cli.h"
#include "cdc.h"

#ifdef _USE_HW_USB
#include "tusb.h"


extern void usbDescInit(bool with_msc);

static bool      is_init  = false;
static UsbMode_t usb_mode = USB_NON_MODE;

#if CLI_USE(HW_USB)
static void cliUsb(cli_args_t *args);
#endif




/*
 * STM32H750 의 OTG_FS 를 device 로 올린다.
 *
 * 이 보드는 **VBUS 가 MCU 에 연결돼 있지 않다** (회로도 02-DC-DC 확인).
 * 그래서 VBUS 감지를 끄고, "B-peripheral 세션 유효" 를 강제로 참으로 만든다.
 * 안 그러면 컨트롤러가 케이블이 안 꽂혔다고 보고 절대 열거되지 않는다.
 *
 * 클래식 H7 은 GOTGCTL 의 BVALOEN/BVALOVAL 을 쓴다. H7RS/U5 는 GCCFG 쪽에
 * VBVALEXTOEN/VBVALOVAL 이 있는데 레지스터가 다르므로 그대로 베끼면 안 된다.
 */
static void usbInitPhy(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  //-- USB 48MHz. bsp.c 의 PeriphCommonClock_Config 에서 이미 HSI48 로 잡았지만
  //   여기서 한 번 더 명시해 둔다.
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USB;
  PeriphClkInitStruct.UsbClockSelection    = RCC_USBCLKSOURCE_HSI48;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_PWREx_EnableUSBVoltageDetector();

  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* PA11 -> USB_OTG_FS_DM, PA12 -> USB_OTG_FS_DP */
  GPIO_InitStruct.Pin       = GPIO_PIN_11 | GPIO_PIN_12;
  GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull      = GPIO_NOPULL;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF10_OTG2_HS;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  __HAL_RCC_USB_OTG_FS_CLK_ENABLE();

  //-- VBUS 감지 끄기
  USB_OTG_FS->GCCFG &= ~USB_OTG_GCCFG_VBDEN;

  //-- B-peripheral 세션 유효 강제
  USB_OTG_FS->GOTGCTL |= USB_OTG_GOTGCTL_BVALOEN;
  USB_OTG_FS->GOTGCTL |= USB_OTG_GOTGCTL_BVALOVAL;

  //-- TinyUSB 의 dcd_init() 도 IRQ 를 켜지만 우선순위가 0(최고)이 된다.
  //   명시적으로 낮춰 둔다.
  HAL_NVIC_SetPriority(OTG_FS_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}

bool usbInit(UsbMode_t mode)
{
  tusb_rhport_init_t dev_init =
  {
    .role  = TUSB_ROLE_DEVICE,
    .speed = TUSB_SPEED_AUTO,
  };

  if (is_init == true) return true;

  usb_mode = mode;

  //-- 디스크립터를 먼저 고른 뒤에 스택을 올려야 한다.
  usbDescInit(mode == USB_MSC_MODE);

  usbInitPhy();

  if (tusb_init(BOARD_TUD_RHPORT, &dev_init) != true)
  {
    logPrintf("[E_] usbInit()\n");
    return false;
  }

  is_init = true;

  logPrintf("[OK] usbInit()\n");
  logPrintf("     mode : %s\n", mode == USB_MSC_MODE ? "CDC+HID+MSC" : "CDC+HID");

#if CLI_USE(HW_USB)
  cliAdd("usb", cliUsb);
#endif
  return true;
}

void usbDeInit(void)
{
  if (is_init != true) return;

  HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
  __HAL_RCC_USB_OTG_FS_CLK_DISABLE();

  is_init  = false;
  usb_mode = USB_NON_MODE;
}

bool usbIsInit(void)
{
  return is_init;
}

bool usbUpdate(void)
{
  if (is_init != true) return false;

  tud_task();
  return true;
}

//-- 케이블이 꽂혀 있고 열거까지 끝났는가
bool usbIsConnect(void)
{
  if (is_init != true) return false;

  return tud_connected() && !tud_suspended();
}

//-- 호스트가 CDC 를 열었는가 (DTR)
bool usbIsOpen(void)
{
  if (is_init != true) return false;

  return tud_cdc_n_connected(0);
}

UsbMode_t usbGetMode(void)
{
  return usb_mode;
}

UsbType_t usbGetType(void)
{
  return (UsbType_t)cdcGetType();
}

/*
 * USB 시리얼 번호를 MCU 의 96비트 UID 로 만든다.
 *
 * UID 는 워드 단위로만 읽힌다. 바이트 접근하면 엉뚱한 값이 나온다.
 */
size_t usbGetSerial(uint16_t desc_str1[], size_t max_chars)
{
  const char *hex = "0123456789ABCDEF";
  uint32_t uid[3];
  size_t   n = 0;

  uid[0] = HAL_GetUIDw0();
  uid[1] = HAL_GetUIDw1();
  uid[2] = HAL_GetUIDw2();

  for (int w=0; w<3 && n+8 <= max_chars; w++)
  {
    for (int i=7; i>=0; i--)
    {
      desc_str1[n++] = hex[(uid[w] >> (i*4)) & 0xF];
    }
  }
  return n;
}

void OTG_FS_IRQHandler(void)
{
  tusb_int_handler(BOARD_TUD_RHPORT, true);
}


#if CLI_USE(HW_USB)
void cliUsb(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("init    : %d\n", usbIsInit());
    cliPrintf("mode    : %s\n", usb_mode == USB_MSC_MODE ? "CDC+HID+MSC" : "CDC+HID");
    cliPrintf("connect : %d\n", usbIsConnect());
    cliPrintf("open    : %d\n", usbIsOpen());
    cliPrintf("baud    : %d\n", (int)cdcGetBaud());
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("usb info\n");
  }
}
#endif

#endif
