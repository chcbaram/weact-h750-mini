#include "cmd_task.h"
#include "cli.h"


/*
 * 부트로더 커맨드 셋.
 *
 * CDC 와 HID 가 이 하나를 공유한다. 채널 드라이버(cmd_driver_t)만 다르다.
 * 앱에도 같은 파일이 들어가는데, HW_DEV_MODE 로 동작이 갈린다 (아래 FW_UPDATE 참고).
 */
#define BOOT_CMD_INFO             0x0000
#define BOOT_CMD_VERSION          0x0001
#define BOOT_CMD_FW_BEGIN         0x0002
#define BOOT_CMD_FW_ERASE         0x0003
#define BOOT_CMD_FW_WRITE         0x0004
#define BOOT_CMD_FW_READ          0x0005
#define BOOT_CMD_FW_END           0x0006
#define BOOT_CMD_FW_VERIFY        0x0007
#define BOOT_CMD_FW_UPDATE        0x0008
#define BOOT_CMD_FW_JUMP          0x0009
#define BOOT_CMD_CLI              0x0010
#define BOOT_CMD_CLI_MORE         0x0011


/*
 * 호스트가 연결 직후 가장 먼저 부르는 커맨드의 응답.
 *
 * 부트로더와 앱이 같은 VID 로 열거되므로 USB 만으로는 어느 쪽인지 알기 어렵다.
 * mode 와 이름/버전을 여기서 알려준다.
 */
typedef struct
{
  uint32_t magic;
  uint32_t mode;              // HW_DEV_MODE_BOOT / HW_DEV_MODE_APP
  uint32_t boot_addr;
  uint32_t boot_size;
  uint32_t firm_addr;         // 태그 시작 (0x90000000)
  uint32_t firm_vec_addr;     // 앱 벡터 시작 (0x90001000)
  uint32_t firm_size;
  uint32_t tag_size;
  uint32_t max_fw_size;
  uint32_t family_id;
  char     name[32];
  char     version[32];
} __attribute__((packed)) boot_info_t;

typedef struct
{
  uint8_t  img_type;          // BootImgType_t
  uint8_t  rsv[3];
  uint32_t fw_size;
  uint32_t fw_crc;
  char     name[32];
  char     version[32];
} __attribute__((packed)) boot_version_t;


//-- FW_BEGIN ~ FW_END 사이의 전송 상태
static int32_t  wr_length = -1;    // 호스트가 신고한 크기. -1 이면 전송 중이 아니다
static uint32_t wr_index  = 0;     // 기록된 최대 끝 오프셋




bool cmdBootProcess(cmd_t *p_cmd)
{
  uint16_t  cmd      = p_cmd->packet.cmd;
  uint16_t  err_code = OK;
  uint8_t  *p_data   = p_cmd->packet.data;
  uint32_t  length   = p_cmd->packet.length;


  switch (cmd)
  {
    case BOOT_CMD_INFO:
    {
      boot_info_t info;

      memset(&info, 0, sizeof(info));
      info.magic         = MAGIC_NUMBER;
      info.mode          = HW_DEV_MODE;
      info.boot_addr     = FLASH_ADDR_BOOT;
      info.boot_size     = FLASH_SIZE_BOOT;
      info.firm_addr     = FLASH_ADDR_FIRM;
      info.firm_vec_addr = FLASH_ADDR_FIRM_VEC;
      info.firm_size     = FLASH_SIZE_FIRM;
      info.tag_size      = FLASH_SIZE_TAG;
#if HW_DEV_MODE == HW_DEV_MODE_BOOT
      info.max_fw_size   = UF2_MAX_FW_SIZE;
#else
      //-- 앱은 UF2 모듈이 없다. 굽는 주체가 아니므로 0 으로 신고한다.
      info.max_fw_size   = 0;
#endif
      info.family_id     = BOARD_UF2_FAMILY_ID;
      snprintf(info.name,    sizeof(info.name),    "%s", _DEF_BOARD_NAME);
      snprintf(info.version, sizeof(info.version), "%s", _DEF_FIRMWATRE_VERSION);

      cmdSendResp(p_cmd, cmd, OK, (uint8_t *)&info, sizeof(info));
      break;
    }

    case BOOT_CMD_VERSION:
    {
      boot_version_t ver;
      firm_ver_t     fw_ver;
      firm_tag_t     tag;

      memset(&ver, 0, sizeof(ver));
      /*
       * 부트로더는 CRC 까지 다시 계산해 판정한다. 앱은 그럴 수 없다 —
       * 전수 읽기가 indirect 를 요구하고, 그 순간 자기 명령어 인출이 끊긴다.
       * 앱이 돌고 있다는 것 자체가 이미 검증을 통과했다는 뜻이므로
       * 태그/버전 매직만 보고 단계를 신고한다 (boot.c 의 앱 분기).
       */
#if HW_DEV_MODE == HW_DEV_MODE_BOOT
      ver.img_type = (uint8_t)bootVerifyFirm();
#else
      ver.img_type = (uint8_t)bootGetImgType();
#endif

      if (bootGetTag(&tag))
      {
        ver.fw_size = tag.fw_size;
        ver.fw_crc  = tag.fw_crc;
      }
      if (bootGetVer(&fw_ver))
      {
        //-- 플래시에서 읽은 문자열은 NUL 종료가 보장되지 않는다.
        memcpy(ver.name,    fw_ver.name_str,    sizeof(ver.name));
        memcpy(ver.version, fw_ver.version_str, sizeof(ver.version));
        ver.name[sizeof(ver.name)-1]       = 0;
        ver.version[sizeof(ver.version)-1] = 0;
      }
      cmdSendResp(p_cmd, cmd, OK, (uint8_t *)&ver, sizeof(ver));
      break;
    }

    /*
     * 여기부터 FW_END 까지는 **부트로더 전용**이다.
     *
     * 앱에서 실행되면 flashErase()/flashWrite() 가 qspiSetXipMode(false) 를 불러
     * memory-mapped 를 빠져나간다. 그 순간 자기 명령어 인출이 끊겨 **그 자리에서
     * 죽는다.** 응답조차 못 보낸다 (04/12 문서).
     *
     * 호스트 툴은 INFO 의 mode 로 부트로더인지 앱인지 먼저 판별하고, 앱이면
     * FW_UPDATE 를 보내 부트로더로 넘어가게 한 뒤 굽는다. 그래도 순서를 틀리는
     * 툴이 있을 수 있으니 여기서 막는다.
     */
#if HW_DEV_MODE == HW_DEV_MODE_APP
    case BOOT_CMD_FW_BEGIN:
    case BOOT_CMD_FW_ERASE:
    case BOOT_CMD_FW_WRITE:
    case BOOT_CMD_FW_END:
    case BOOT_CMD_FW_VERIFY:
      err_code = ERR_BOOT_WRONG_CMD;
      cmdSendResp(p_cmd, cmd, err_code, NULL, 0);
      break;
#else
    case BOOT_CMD_FW_BEGIN:
    {
      uint32_t fw_size = 0;

      if (length < 4)
      {
        err_code = ERR_BOOT_WRONG_CMD;
      }
      else
      {
        memcpy(&fw_size, &p_data[0], 4);

        if (fw_size == 0 || fw_size > UF2_MAX_FW_SIZE)
        {
          err_code = ERR_BOOT_WRONG_RANGE;
        }
        else
        {
          /*
           * 태그 섹터를 **먼저** 지운다.
           *
           * 전송이 중간에 끊겨도 태그가 무효라 부트로더가 옛 이미지로 점프하지
           * 않는다. 태그가 독립 4KB 섹터라 앱 본체는 건드리지 않는다(00 문서).
           */
          if (flashErase(FLASH_ADDR_FIRM, FLASH_SIZE_TAG) != true)
          {
            err_code = ERR_BOOT_FLASH_ERASE;
          }
          else
          {
            wr_length = (int32_t)fw_size;
            wr_index  = 0;
            logPrintf("[  ] fw begin %d bytes\n", (int)fw_size);
#ifdef _USE_HW_LCD
            uiSetProgress(0);
#endif
          }
        }
      }
      cmdSendResp(p_cmd, cmd, err_code, NULL, 0);
      break;
    }

    case BOOT_CMD_FW_ERASE:
    {
      if (wr_length <= 0)
      {
        err_code = ERR_BOOT_WRONG_CMD;
      }
      else if (flashErase(FLASH_ADDR_FIRM_VEC, (uint32_t)wr_length) != true)
      {
        err_code = ERR_BOOT_FLASH_ERASE;
      }
      cmdSendResp(p_cmd, cmd, err_code, NULL, 0);
      break;
    }

    case BOOT_CMD_FW_WRITE:
    {
      uint32_t offset = 0;

      if (length < 4 || wr_length <= 0)
      {
        err_code = ERR_BOOT_WRONG_CMD;
      }
      else
      {
        uint32_t n = length - 4;

        memcpy(&offset, &p_data[0], 4);

        if ((offset + n) > (uint32_t)wr_length)
        {
          err_code = ERR_BOOT_WRONG_RANGE;
        }
        else if (flashWrite(FLASH_ADDR_FIRM_VEC + offset, &p_data[4], n) != true)
        {
          err_code = ERR_BOOT_FLASH_WRITE;
        }
        else if ((offset + n) > wr_index)
        {
          wr_index = offset + n;
#ifdef _USE_HW_LCD
          uiSetProgress((uint8_t)(((uint64_t)wr_index * 100) / (uint32_t)wr_length));
#endif
        }
      }
      cmdSendResp(p_cmd, cmd, err_code, NULL, 0);
      break;
    }

    case BOOT_CMD_FW_READ:
    {
      uint32_t offset = 0;
      uint32_t len    = 0;
      uint8_t  buf[256];

      if (length < 8)
      {
        err_code = ERR_BOOT_WRONG_CMD;
      }
      else
      {
        memcpy(&offset, &p_data[0], 4);
        memcpy(&len,    &p_data[4], 4);

        if (len > sizeof(buf) || (offset + len) > FLASH_SIZE_FIRM)
          err_code = ERR_BOOT_WRONG_RANGE;
        else if (flashRead(FLASH_ADDR_FIRM + offset, buf, len) != true)
          err_code = ERR_BOOT_FLASH_READ;
      }

      if (err_code == OK)
        cmdSendResp(p_cmd, cmd, OK, buf, len);
      else
        cmdSendResp(p_cmd, cmd, err_code, NULL, 0);
      break;
    }

    case BOOT_CMD_FW_END:
    {
      /*
       * 태그를 마지막에 쓴다. 이게 커밋 마커다.
       * 여기까지 와야 부트로더가 이 이미지로 점프한다.
       */
      firm_tag_t tag;
      uint16_t   crc = 0;
      uint8_t    buf[256];

      if (wr_length <= 0 || wr_index == 0)
      {
        err_code = ERR_BOOT_WRONG_CMD;
      }
      else
      {
        for (uint32_t i=0; i<wr_index && err_code == OK; i+=sizeof(buf))
        {
          uint32_t n = wr_index - i;

          if (n > sizeof(buf)) n = sizeof(buf);
          if (flashRead(FLASH_ADDR_FIRM_VEC + i, buf, n) != true)
            err_code = ERR_BOOT_FLASH_READ;
          else
            crc = utilCalcCRC(crc, buf, n);
        }
      }

      if (err_code == OK)
      {
        memset(&tag, 0, sizeof(tag));
        tag.magic_number = TAG_MAGIC_NUMBER;
        tag.fw_addr      = FLASH_SIZE_TAG;
        tag.fw_size      = wr_index;
        tag.fw_crc       = crc;
        tag.tag_crc      = utilCalcCRC(0, (uint8_t *)&tag, sizeof(tag) - 4);

        if (flashErase(FLASH_ADDR_FIRM, FLASH_SIZE_TAG) != true)
          err_code = ERR_BOOT_FLASH_ERASE;
        else if (flashWrite(FLASH_ADDR_FIRM, (uint8_t *)&tag, sizeof(tag)) != true)
          err_code = ERR_BOOT_FLASH_WRITE;
        else
        {
          //-- 커밋 성공. 폴트 카운터를 접는다 (uf2.c 와 같은 이유)
          resetSetFaultCount(0);
          logPrintf("[  ] fw end %d bytes, crc 0x%04X\n", (int)wr_index, crc);
        }
      }

      wr_length = -1;
#ifdef _USE_HW_LCD
      uiEndProgress();
#endif
      cmdSendResp(p_cmd, cmd, err_code, NULL, 0);
      break;
    }

    case BOOT_CMD_FW_VERIFY:
    {
      uint8_t img = (uint8_t)bootVerifyFirm();

      if (img == BOOT_IMG_NONE) err_code = ERR_BOOT_INVALID_FW;

      cmdSendResp(p_cmd, cmd, err_code, &img, 1);
      break;
    }
#endif  // HW_DEV_MODE_APP

    case BOOT_CMD_FW_UPDATE:
    case BOOT_CMD_FW_JUMP:
    {
#if HW_DEV_MODE == HW_DEV_MODE_BOOT
      //-- 부트로더 : 바로 점프한다
      if (bootVerifyFirm() == BOOT_IMG_NONE)
      {
        err_code = ERR_BOOT_INVALID_FW;
        cmdSendResp(p_cmd, cmd, err_code, NULL, 0);
      }
      else
      {
        cmdSendResp(p_cmd, cmd, OK, NULL, 0);
        delay(100);          // 응답이 나갈 시간을 준다
        bootJumpFirm();
      }
#else
      /*
       * 앱 : 직접 굽는 것이 **원천적으로 불가능**하다.
       *
       * 앱은 QSPI 에서 XiP 로 실행 중이고, QUADSPI 는 memory-mapped 상태에서
       * 쓰기가 안 된다. 쓰려면 indirect 로 내려가야 하는데 그 순간 자기 명령어
       * 인출이 끊긴다(04 문서). 그래서 부트로더에 요청하고 리셋하는 길뿐이다.
       */
      cmdSendResp(p_cmd, cmd, OK, NULL, 0);
      delay(100);
      resetToBoot(cmd == BOOT_CMD_FW_UPDATE);   // UPDATE 면 MSC 도 열어준다
#endif
      break;
    }

    case BOOT_CMD_CLI:
    {
      uint8_t prev_port = cliGetPort();

      drvCliPutLine(p_cmd, p_data, length);

      // cli.c 는 '현재 열린 포트' 로 출력한다. 잠시 가상 CLI 채널로 돌려놓아야
      // 출력이 USART1 이 아니라 우리 버퍼로 모인다.
      //
      // cli_mgr 이 같은 moduleUpdate 안에서 cliMain() 을 또 부르면 재진입이 되고,
      // 포트를 원래대로 되돌려 버리기까지 한다. 그동안 통째로 꺼둔다.
      cliMgrEnable(false);
      cliOpen(HW_UART_CH_CMD, 0);

      {
        uint32_t pre_time = millis();

        while (millis() - pre_time < 300)
        {
          cliMain();
          if (uartAvailable(HW_UART_CH_CMD) == 0)
            break;
        }
        cliMain();      // 프롬프트까지 뱉게 한 번 더
      }

      cliOpen(prev_port, 0);
      cliMgrEnable(true);

      drvCliEndOut();
    }
    /* fall through - 첫 조각을 바로 보낸다 */

    case BOOT_CMD_CLI_MORE:
    {
      static uint8_t resp[CLI_CHUNK_SIZE + 1];
      uint8_t  *p_out;
      uint32_t  out_len;

      out_len = drvCliGetOut(&p_out, CLI_CHUNK_SIZE);

      resp[0] = drvCliHasMore() ? 1 : 0;
      memcpy(&resp[1], p_out, out_len);

      cmdSendResp(p_cmd, cmd, CMD_OK, resp, (uint16_t)(out_len + 1));

      if (resp[0] == 0)
        drvCliClearOut();
      break;
    }

    //-- 보드 시각 조회/설정.
    //
    //   op 하나로 읽기와 쓰기를 겸하고, 어느 쪽이든 **현재 값을 되돌려준다.**
    //   쓰기 직후 확인이 한 번에 끝난다(convex 의 CMD_RTC 와 같은 형태).
    //
    //   값은 uint32 epoch 다. 다만 RTC 는 지역시(SNTP 가 KST 로 맞춘다)를 담고
    //   있으므로, "달력 필드를 UTC 로 간주해 만든 epoch" 이라는 약속이다.
    //   호스트는 표시할 때 getUTC*() 를 쓰고, 보낼 때 자기 지역시를 같은 방식으로
    //   인코딩한다. 그러면 화면에 보이는 값이 보드의 벽시계와 정확히 일치한다.
    //
    //   요청 : [0] op, (SET 이면) [1..4] epoch
    //   응답 : [0..3] 현재 epoch (0 = 시각 모름)
    //

    default:
      cmdSendResp(p_cmd, cmd, ERR_BOOT_WRONG_CMD, NULL, 0);
      break;
  }

  return true;
}
