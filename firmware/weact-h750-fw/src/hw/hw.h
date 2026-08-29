#ifndef HW_H_
#define HW_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "hw_def.h"

#include "led.h"
#include "uart.h"
#include "cli.h"
#include "log.h"
#include "fault.h"
#include "reset.h"
#include "rtc.h"
#include "gpio.h"
#include "flash.h"
#ifdef _USE_HW_QSPI
#include "qspi.h"
#endif
#ifdef _USE_HW_SPI
#include "spi.h"
#endif
#ifdef _USE_HW_LCD
#include "lcd.h"
#endif
#include "swtimer.h"
#include "qbuffer.h"
#include "util_core.h"

#ifdef _USE_HW_USB
#include "usb.h"
#endif
#ifdef _USE_HW_CDC
#include "cdc.h"
#endif


bool hwInit(void);

#ifdef __cplusplus
}
#endif

#endif
