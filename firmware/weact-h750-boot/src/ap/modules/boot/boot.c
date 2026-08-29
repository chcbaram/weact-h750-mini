#include "boot/boot.h"
#include "cli.h"


static BootImgType_t img_type = BOOT_IMG_NONE;
static firm_tag_t    firm_tag;
static firm_ver_t    firm_ver_app;

#if CLI_USE(HW_BOOT)
static void cliBoot(cli_args_t *args);
#endif

static bool     bootIsValidVector(void);
static uint16_t bootCalcCrc(uint32_t addr, uint32_t length, uint16_t *p_crc);




#ifdef BOOT_SELF_TEST
/*
 * 이미지 식별 3단계 자체 시험 (브링업 전용)
 *
 * QSPI 에 가짜 이미지를 만들어 TAG / VER / RAW / NONE 판정과 태그 승격,
 * 그리고 stale 태그 감지를 확인한다.
 *
 * 시험이 끝나면 반드시 지운다. 가짜 벡터를 남기면 다음 부팅에 거기로 점프해서
 * 하드폴트가 난다.
 */
static void bootTestWriteImage(uint32_t fw_size, bool with_ver)
{
  uint8_t    buf[FLASH_SIZE_VEC + FLASH_SIZE_VER];
  firm_ver_t ver;

  memset(buf, 0xFF, sizeof(buf));

  //-- 벡터 테이블 : SP 는 AXI SRAM 끝, PC 는 앱 영역 안 + Thumb 비트
  ((uint32_t *)buf)[0] = 0x24080000;
  ((uint32_t *)buf)[1] = FLASH_ADDR_FIRM_VEC + 0x801;

  if (with_ver)
  {
    memset(&ver, 0, sizeof(ver));
    ver.magic_number = VERSION_MAGIC_NUMBER;
    snprintf(ver.name_str,    sizeof(ver.name_str),    "TEST-APP");
    snprintf(ver.version_str, sizeof(ver.version_str), "V000000T1");
    ver.firm_addr = FLASH_ADDR_FIRM_VEC;
    ver.firm_size = fw_size;
    memcpy(&buf[FLASH_SIZE_VEC], &ver, sizeof(ver));
  }

  flashErase(FLASH_ADDR_FIRM_VEC, FLASH_SIZE_VEC + FLASH_SIZE_VER);
  flashWrite(FLASH_ADDR_FIRM_VEC, buf, sizeof(buf));
}

static void bootTestClear(void)
{
  flashErase(FLASH_ADDR_FIRM, FLASH_SIZE_TAG);
  flashErase(FLASH_ADDR_FIRM_VEC, FLASH_SIZE_VEC + FLASH_SIZE_VER);
}

void bootSelfTest(void)
{
  const char *name[] = {"NONE", "RAW", "VER", "TAG"};
  const uint32_t fw_size = FLASH_SIZE_VEC + FLASH_SIZE_VER;   // 2KB

  logPrintf("[  ] bootSelfTest()\n");

  //-- 1) 아무것도 없음 -> NONE
  bootTestClear();
  logPrintf("     empty        -> %s\n", name[bootVerifyFirm()]);

  //-- 2) 벡터만 있음 -> RAW
  bootTestWriteImage(fw_size, false);
  logPrintf("     vector only  -> %s\n", name[bootVerifyFirm()]);

  //-- 3) firm_ver_t 까지 -> VER
  bootTestWriteImage(fw_size, true);
  logPrintf("     with version -> %s\n", name[bootVerifyFirm()]);

  //-- 4) 태그 승격 -> TAG
  bootPromoteTag();
  logPrintf("     promoted     -> %s\n", name[bootVerifyFirm()]);

  //-- 5) 다른 크기로 앱을 덮어쓴 상황. 태그는 옛것이 남는다 -> stale 감지 -> VER
  bootTestWriteImage(fw_size / 2, true);
  logPrintf("     stale tag    -> %s\n", name[bootVerifyFirm()]);

  //-- 6) 정리. 가짜 벡터를 남기면 다음 부팅에 점프해서 죽는다.
  bootTestClear();
  logPrintf("     cleaned      -> %s\n", name[bootVerifyFirm()]);
}
#endif


bool bootInit(void)
{
  img_type = bootVerifyFirm();

#if CLI_USE(HW_BOOT)
  cliAdd("boot", cliBoot);
#endif
  return true;
}

BootImgType_t bootGetImgType(void)
{
  return img_type;
}

/*
 * QSPI 를 XiP 로 올려두고 읽는다.
 *
 * indirect 로 읽어도 되지만 XiP 로 읽는 편이 훨씬 빠르고, 무엇보다 **앱이 실제로
 * 명령어를 인출할 바로 그 경로**를 여기서 함께 검증하게 된다. indirect 읽기만
 * 성공하고 XiP 가 깨져 있으면 점프 직후 죽는다.
 */
/*
 * 검증용 읽기는 **indirect 모드**로 한다.
 *
 * 부트로더가 하는 일은 QSPI 를 굽고 검증하는 것이다. 굽는 경로가 indirect 이므로
 * 검증도 같은 경로로 해야 자기모순이 없다.
 *
 * memory-mapped(XiP)로 검증하려다 오래 물렸다. 소거/쓰기 직후 XiP 로 올라오면
 * 읽은 값이 실제 내용과 달랐다. 진입 전후 Abort, 플래시 리셋, 타임아웃 카운터,
 * 더미 읽기, 캐시 무효화 범위 조정, SCK 100->50MHz, MPU non-cacheable 까지
 * 시험했지만 전부 재현됐다. 캐시를 꺼도 재현되므로 CPU 캐시 문제가 아니다.
 *
 * XiP 는 앱으로 점프하기 직전에만 켠다 (bootJumpFirm).
 */
static bool bootBeginRead(void)
{
  qspiSetXipMode(false);
  return true;
}

bool bootGetTag(firm_tag_t *p_tag)
{
  if (bootBeginRead() != true) return false;

  if (qspiRead(FLASH_ADDR_FIRM - HW_QSPI_ADDR, (uint8_t *)p_tag, sizeof(firm_tag_t)) != true)
  {
    return false;
  }
  return p_tag->magic_number == TAG_MAGIC_NUMBER;
}

bool bootGetVer(firm_ver_t *p_ver)
{
  if (bootBeginRead() != true) return false;

  if (qspiRead(FLASH_SIZE_TAG + FLASH_SIZE_VEC, (uint8_t *)p_ver, sizeof(firm_ver_t)) != true)
  {
    return false;
  }
  return p_ver->magic_number == VERSION_MAGIC_NUMBER;
}

/*
 * 벡터 테이블 sanity check.
 *
 *   word0 = 초기 MSP  -> 유효한 RAM 범위 안인가
 *   word1 = Reset_Handler -> QSPI 앱 영역 안이고 Thumb 비트가 서 있는가
 *
 * 이것만으로 이미지가 온전하다고 볼 수는 없지만, "완전히 빈 플래시로 점프해서
 * 하드폴트" 는 확실히 막는다.
 */
bool bootIsValidVector(void)
{
  uint32_t sp;
  uint32_t pc;

  if (bootBeginRead() != true) return false;

  {
    uint32_t vec[2];

    if (qspiRead(FLASH_SIZE_TAG, (uint8_t *)vec, sizeof(vec)) != true) return false;
    sp = vec[0];
    pc = vec[1];
  }

  //-- 초기 스택 포인터가 놓일 수 있는 RAM
  bool sp_ok = false;
  if (sp >= 0x20000000 && sp <= 0x20020000) sp_ok = true;   // DTCM  128K
  if (sp >= 0x24000000 && sp <= 0x24080000) sp_ok = true;   // AXI   512K
  if (sp >= 0x30000000 && sp <= 0x30048000) sp_ok = true;   // D2    288K
  if (sp >= 0x38000000 && sp <= 0x38010000) sp_ok = true;   // D3     64K

  if (sp_ok != true) return false;

  if (pc < FLASH_ADDR_FIRM_VEC) return false;
  if (pc >= (FLASH_ADDR_FIRM + FLASH_SIZE_FIRM)) return false;
  if ((pc & 1) == 0) return false;      // Thumb 비트

  return true;
}

uint16_t bootCalcCrc(uint32_t addr, uint32_t length, uint16_t *p_crc)
{
  uint16_t crc = 0;
  uint8_t  buf[256];

  qspiSetXipMode(false);

  for (uint32_t i=0; i<length; i+=sizeof(buf))
  {
    uint32_t len = length - i;

    if (len > sizeof(buf)) len = sizeof(buf);

    if (qspiRead(addr - HW_QSPI_ADDR + i, buf, len) != true) return ERR_BOOT_FLASH_READ;
    crc = utilCalcCRC(crc, buf, len);
  }

  *p_crc = crc;
  return OK;
}

/*
 * 이미지 식별 3단계.
 *
 * 판정 순서가 중요하다. 태그가 있어도 firm_ver_t 가 신고한 크기와 다르면
 * **stale 태그**로 본다. SWD 로 새 이미지를 굽고 옛 태그가 남으면 CRC 가 실패해
 * 부팅이 막히는데, 그러면 개발이 불가능해진다.
 */
BootImgType_t bootVerifyFirm(void)
{
  bool has_ver;
  bool has_tag;

  has_ver = bootGetVer(&firm_ver_app);
  has_tag = bootGetTag(&firm_tag);

  if (has_tag)
  {
    bool stale = false;

    if (firm_tag.fw_addr != FLASH_SIZE_TAG)                        stale = true;
    if (firm_tag.fw_size == 0)                                     stale = true;
    if (firm_tag.fw_size > (FLASH_SIZE_FIRM - FLASH_SIZE_TAG))     stale = true;

    //-- 앱이 크기를 신고했는데 태그와 다르면 태그가 옛것이다.
    if (has_ver && firm_ver_app.firm_size > 0 &&
        firm_ver_app.firm_size != firm_tag.fw_size)
    {
      logPrintf("[!!] stale tag (tag %d != ver %d)\n",
                (int)firm_tag.fw_size, (int)firm_ver_app.firm_size);
      stale = true;
    }

    if (stale != true)
    {
      uint16_t crc = 0;

      bootCalcCrc(FLASH_ADDR_FIRM + FLASH_SIZE_TAG, firm_tag.fw_size, &crc);

      if (crc == (uint16_t)firm_tag.fw_crc)
      {
        img_type = BOOT_IMG_TAG;
        return img_type;
      }
      logPrintf("[!!] fw crc 0x%04X != 0x%04X\n", crc, (uint16_t)firm_tag.fw_crc);
    }
  }

  if (has_ver && firm_ver_app.firm_size > 0 && bootIsValidVector())
  {
    img_type = BOOT_IMG_VER;
    return img_type;
  }

  if (bootIsValidVector())
  {
    img_type = BOOT_IMG_RAW;
    return img_type;
  }

  img_type = BOOT_IMG_NONE;
  return img_type;
}

/*
 * 2단계(VER) -> 1단계(TAG) 승격.
 *
 * 안전한 이유는 TAG 가 **독립된 4KB 섹터**이기 때문이다. 태그를 쓰다 전원이 끊겨도
 * 앱 본체는 멀쩡하고 다음 부팅에 다시 VER 로 떨어질 뿐이다.
 */
bool bootPromoteTag(void)
{
  firm_tag_t tag;
  uint16_t   crc = 0;
  uint32_t   fw_size;

  if (img_type != BOOT_IMG_VER) return false;

  fw_size = firm_ver_app.firm_size;
  if (fw_size == 0 || fw_size > (FLASH_SIZE_FIRM - FLASH_SIZE_TAG)) return false;

  logPrintf("[  ] bootPromoteTag() size %d\n", (int)fw_size);

  if (bootBeginRead() != true) return false;
  bootCalcCrc(FLASH_ADDR_FIRM + FLASH_SIZE_TAG, fw_size, &crc);

  memset(&tag, 0, sizeof(tag));
  tag.magic_number = TAG_MAGIC_NUMBER;
  tag.fw_addr      = FLASH_SIZE_TAG;
  tag.fw_size      = fw_size;
  tag.fw_crc       = crc;
  tag.tag_crc      = utilCalcCRC(0, (uint8_t *)&tag, sizeof(tag) - 4);

  //-- 태그 섹터만 지운다. 앱 본체(0x90001000~)는 건드리지 않는다.
  if (flashErase(FLASH_ADDR_FIRM, FLASH_SIZE_TAG) != true)
  {
    logPrintf("     [E_] tag erase fail\n");
    return false;
  }
  if (flashWrite(FLASH_ADDR_FIRM, (uint8_t *)&tag, sizeof(tag)) != true)
  {
    logPrintf("     [E_] tag write fail\n");
    return false;
  }

  img_type = bootVerifyFirm();
  logPrintf("     -> %s\n", img_type == BOOT_IMG_TAG ? "OK" : "FAIL");

  return img_type == BOOT_IMG_TAG;
}

/*
 * 앱으로 점프.
 *
 * VTOR 과 MSP 는 건드리지 않는다.
 *   MSP  : 앱의 Reset_Handler 가 ldr sp, =_estack 을 한다
 *   VTOR : 앱의 SystemInit() 이 SCB->VTOR = &_fw_flash_begin 을 한다
 *          (아두이노 코어는 -DVECT_TAB_BASE_ADDRESS 로 처리한다)
 */
uint16_t bootJumpFirm(void)
{
  void (*app_entry)(void);
  uint32_t reset_handler = 0;

  if (img_type == BOOT_IMG_NONE)
  {
    return ERR_BOOT_INVALID_FW;
  }

  /*
   * 점프 주소는 **indirect 로 읽는다.**
   *
   * 여기서 오래 물렸다. 원래는 이렇게 되어 있었다.
   *
   *   jump_func = (void (**)(void))(FLASH_ADDR_FIRM_VEC + 4);
   *   (*jump_func)();
   *
   * 이러면 호출하는 그 순간에 memory-mapped 로 다시 읽는다. 그런데 XiP 재진입
   * 직후의 첫 읽기는 신뢰할 수 없어서(04/07 문서) **0xFFFFFFFF 가 돌아왔다.**
   * BLX 0xFFFFFFFF -> PC = 0xFFFFFFFE -> 실행 불가 영역 -> IACCVIOL.
   *
   * 실기 증거 : CFSR = 0x00000001 (IACCVIOL), 폴트 PC = 0xFFFFFFFE,
   *            LR = bootJumpFirm. 초당 9회 부팅 루프.
   *
   * 그래서 값을 indirect 로 미리 읽어 **레지스터에 담아두고** 그 값으로 분기한다.
   * 포인터 역참조를 남겨두면 컴파일러가 호출 시점에 다시 읽는다.
   */
  qspiSetXipMode(false);
  if (qspiRead(FLASH_SIZE_TAG + 4, (uint8_t *)&reset_handler, 4) != true)
  {
    return ERR_BOOT_FLASH_READ;
  }

  if (reset_handler < FLASH_ADDR_FIRM_VEC ||
      reset_handler >= (FLASH_ADDR_FIRM + FLASH_SIZE_FIRM) ||
      (reset_handler & 1) == 0)
  {
    logPrintf("[E_] bad reset handler 0x%X\n", (unsigned int)reset_handler);
    return ERR_BOOT_INVALID_FW;
  }

  logPrintf("[  ] bootJumpFirm()\n");
  logPrintf("     addr : 0x%X\n", (unsigned int)reset_handler);

  app_entry = (void (*)(void))reset_handler;

#ifdef BOOT_NO_JUMP
  logPrintf("     [dbg] BOOT_NO_JUMP - 점프하지 않는다\n");
  return ERR_BOOT_INVALID_FW;
#endif

  resetSetBootMode(0);

  /*
   * 순서가 중요하다. **캐시 정리를 먼저, XiP 진입을 나중에** 한다.
   *
   * 반대로 하면(XiP 켜고 나서 bspDeInit) 앱의 첫 명령어 인출이 쓰레기를 가져와
   * UNDEFINSTR 로 죽었다. 점프 직전까지 memory-mapped 읽기는 멀쩡했으므로
   * (indirect 와 바이트까지 일치 확인) 데이터 경로가 아니라 캐시 정리가
   * QUADSPI 를 건드린 것이다.
   *
   * bspDeInit() 은 QSPI 가 indirect 인 동안 캐시를 비우고, 그 뒤에 XiP 를 켠다.
   * XiP 진입 후에는 아무것도 만지지 않고 바로 분기한다.
   */
  bspDeInit();

  if (qspiSetXipMode(true) != true)
  {
    return ERR_BOOT_INVALID_FW;
  }

  app_entry();

  return OK;    // 여기 오면 안 된다
}


#if CLI_USE(HW_BOOT)
void cliBoot(cli_args_t *args)
{
  bool ret = false;
  const char *type_str[] = {"NONE", "RAW", "VER", "TAG"};


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    firm_ver_t ver;
    firm_tag_t tag;

    cliPrintf("boot addr  : 0x%08X (%d KB)\n", FLASH_ADDR_BOOT, FLASH_SIZE_BOOT/1024);
    cliPrintf("firm addr  : 0x%08X (%d MB, QSPI XiP)\n", FLASH_ADDR_FIRM, FLASH_SIZE_FIRM/(1024*1024));
    cliPrintf("  tag      : 0x%08X (%d KB)\n", FLASH_ADDR_FIRM, FLASH_SIZE_TAG/1024);
    cliPrintf("  vector   : 0x%08X\n", FLASH_ADDR_FIRM_VEC);
    cliPrintf("img type   : %s\n", type_str[bootVerifyFirm()]);

    if (bootGetTag(&tag))
    {
      cliPrintf("tag size   : %d\n", (int)tag.fw_size);
      cliPrintf("tag crc    : 0x%04X\n", (uint16_t)tag.fw_crc);
    }
    if (bootGetVer(&ver))
    {
      cliPrintf("ver name   : %s\n", ver.name_str);
      cliPrintf("ver str    : %s\n", ver.version_str);
      cliPrintf("ver size   : %d\n", (int)ver.firm_size);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "tag"))
  {
    cliPrintf("promote : %s\n", bootPromoteTag() ? "OK":"FAIL");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "jump"))
  {
    cliPrintf("jump...\n");
    delay(100);
    bootJumpFirm();
    cliPrintf("fail\n");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("boot info\n");
    cliPrintf("boot tag\n");
    cliPrintf("boot jump\n");
  }
}
#endif
