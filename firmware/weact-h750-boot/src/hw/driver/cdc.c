#include "cdc.h"

#ifdef _USE_HW_CDC
#include "usb/usb.h"
#include "tusb.h"


static bool is_init = false;


bool cdcInit(void)
{
  is_init = true;
  return true;
}

bool cdcIsInit(void)
{
  return is_init;
}

bool cdcIsConnect(void)
{
  if (usbIsInit() != true) return false;

  return tud_cdc_n_connected(0);
}

uint32_t cdcAvailable(void)
{
  if (usbIsInit() != true) return 0;

  return tud_cdc_n_available(0);
}

uint8_t cdcRead(void)
{
  if (usbIsInit() != true) return 0;

  return (uint8_t)tud_cdc_n_read_char(0);
}

/*
 * 호스트가 받아가지 않으면 영원히 막힌다. 100ms 로 끊는다.
 * 그동안 usbUpdate() 를 계속 돌려야 FIFO 가 비워진다.
 */
uint32_t cdcWrite(uint8_t *p_data, uint32_t length)
{
  uint32_t sent_len = 0;
  uint32_t pre_time;

  if (cdcIsConnect() != true) return 0;

  pre_time = millis();
  while (sent_len < length)
  {
    uint32_t tx_len;

    usbUpdate();

    tx_len = tud_cdc_n_write(0, &p_data[sent_len], length - sent_len);
    if (tx_len > 0)
    {
      sent_len += tx_len;
    }

    if (cdcIsConnect() != true) break;
    if (millis() - pre_time >= 100) break;
  }
  tud_cdc_n_write_flush(0);

  return sent_len;
}

uint32_t cdcGetBaud(void)
{
  cdc_line_coding_t coding;

  if (usbIsInit() != true) return 0;

  tud_cdc_get_line_coding(&coding);
  return coding.bit_rate;
}

/*
 * CDC 하나를 CLI 와 cmd 패킷이 나눠 쓸 수 없다. 둘 다 cdcRead() 를 부르면
 * 서로의 바이트를 훔친다. 그래서 **호스트가 연 보율로 주인을 가른다.**
 *
 *   115200  -> USB_CON_CLI. 사람이 터미널로 붙은 것이다. CLI 가 CDC 를 쥔다.
 *   그 외    -> USB_CON_CDC. 호스트 툴이다. cmd 가 CDC 를 독점한다.
 *
 * HID 는 전용 채널이라 이 판정과 무관하게 항상 돈다.
 */
uint8_t cdcGetType(void)
{
  if (cdcGetBaud() == 115200) return USB_CON_CLI;

  return USB_CON_CDC;
}

#endif
