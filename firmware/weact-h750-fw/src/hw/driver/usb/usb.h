#ifndef USB_H_
#define USB_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"


#ifdef _USE_HW_USB

typedef enum
{
  USB_NON_MODE,
  USB_CDC_MODE,       // CDC + HID
  USB_MSC_MODE,       // CDC + HID + MSC (UF2)
} UsbMode_t;

//-- 호스트가 CDC 를 연 보율로 주인을 가른다. cdcGetType() 참고.
typedef enum
{
  USB_CON_CDC = 0,
  USB_CON_CLI = 1,
} UsbType_t;


bool      usbInit(UsbMode_t mode);
void      usbDeInit(void);
bool      usbIsInit(void);
bool      usbUpdate(void);
bool      usbIsOpen(void);
bool      usbIsConnect(void);
UsbMode_t usbGetMode(void);
UsbType_t usbGetType(void);
size_t    usbGetSerial(uint16_t desc_str1[], size_t max_chars);

#endif

#ifdef __cplusplus
}
#endif

#endif
