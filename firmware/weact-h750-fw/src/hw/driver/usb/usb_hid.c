#include "hw_def.h"

#ifdef _USE_HW_USB
#include "tusb.h"


#ifdef _USE_HW_CMD
extern void drvHidRxReport(uint8_t const *buffer, uint16_t bufsize);
#endif


uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqlen;

  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
  (void)instance;
  (void)report_id;
  (void)report_type;

#ifdef _USE_HW_CMD
  //-- USB 콜백 안이다. 링버퍼에 넣기만 하고 처리는 메인 루프에서 한다.
  drvHidRxReport(buffer, bufsize);
#else
  (void)buffer;
  (void)bufsize;
#endif
}

#endif
