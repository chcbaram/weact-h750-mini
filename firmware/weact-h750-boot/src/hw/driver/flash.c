#include "flash.h"
#include "cli.h"
#ifdef _USE_HW_QSPI
#include "qspi.h"
#endif


#ifdef _USE_HW_FLASH


/*
 * 이 보드의 플래시는 두 종류다.
 *
 *   내부 플래시 0x08000000, 128KB : 부트로더 자신. 섹터가 1개뿐이라
 *                                   지우면 부트로더가 통째로 날아간다.
 *                                   그래서 여기서는 읽기만 지원한다.
 *   QSPI       0x90000000,   8MB : 앱(XiP). 2단계에서 qspi.c 가 붙으면
 *                                   flashErase/Write 가 이쪽으로 넘어간다.
 *
 * 주소로 대상을 판별해 boot.c / uf2.c 가 같은 flashXxx() API 만 쓰게 한다.
 */

#define FLASH_ADDR_INT        0x08000000
#define FLASH_SIZE_INT        (128*1024)


static bool flashInRange(uint32_t addr, uint32_t length, uint32_t base, uint32_t size);

#if CLI_USE(HW_FLASH)
static void cliFlash(cli_args_t *args);
#endif

static bool is_init = false;




bool flashInit(void)
{
  is_init = true;

  logPrintf("[OK] flashInit()\n");
  logPrintf("     Boot   : 0x%08X 128KB (1 sector)\n", (int)FLASH_ADDR_INT);

#if CLI_USE(HW_FLASH)
  cliAdd("flash", cliFlash);
#endif

  return true;
}

bool flashIsInit(void)
{
  return is_init;
}

bool flashInRange(uint32_t addr, uint32_t length, uint32_t base, uint32_t size)
{
  if (addr < base) return false;
  if (addr + length > base + size) return false;
  return true;
}

/*
 * 부트로더 자신이 있는 영역을 보호한다. 마지막 방어선이라 여기서 한 번 더 막는다.
 */
bool flashIsProtected(uint32_t addr, uint32_t length)
{
  if (addr + length <= FLASH_PROTECT_ADDR) return false;
  if (addr >= FLASH_PROTECT_ADDR + FLASH_PROTECT_SIZE) return false;

  return true;
}

bool flashRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  if (flashInRange(addr, length, FLASH_ADDR_INT, FLASH_SIZE_INT))
  {
    memcpy(p_data, (void *)addr, length);
    return true;
  }

#ifdef _USE_HW_QSPI
  if (flashInRange(addr, length, HW_QSPI_ADDR, HW_QSPI_SIZE))
  {
    /*
     * QSPI 는 **항상 indirect 로 읽는다.** memory-mapped 로 읽지 않는다.
     *
     * 부트로더는 자기가 굽는 플래시를 다시 읽는다. 쓰기 경로가 indirect 이므로
     * 읽기도 같은 경로여야 자기모순이 없다. 소거/쓰기 직후 XiP 로 올라와 읽으면
     * 실제 내용과 다른 값이 나오는 것을 실기에서 확인했다.
     *
     * convex-boot 의 flash.c 도 정확히 같은 방식이고(그 프로젝트는 XiP 를 아예
     * 켜지 않는다), stm32h7-wifi 의 FSBL 은 검증 없이 점프 직전에만 XiP 를 켠다.
     * XiP 는 앱으로 넘길 때만 필요하다.
     */
    return qspiRead(addr - HW_QSPI_ADDR, p_data, length);
  }
#endif

  return false;
}

bool flashErase(uint32_t addr, uint32_t length)
{
  if (flashIsProtected(addr, length))
  {
    logPrintf("[E_] flashErase() protected 0x%08X\n", (int)addr);
    return false;
  }

#ifdef _USE_HW_QSPI
  if (flashInRange(addr, length, HW_QSPI_ADDR, HW_QSPI_SIZE))
  {
    //-- QUADSPI 는 memory-mapped 상태에서 소거/쓰기가 불가능하다.
    //   설정된 read 명령만 발행하기 때문이다. indirect 로 내려간다.
    qspiSetXipMode(false);
    return qspiErase(addr - HW_QSPI_ADDR, length);
  }
#endif

  //-- 내부 플래시는 섹터가 1개(=부트로더 전체)라 소거 수단을 제공하지 않는다.
  return false;
}

bool flashWrite(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  if (flashIsProtected(addr, length))
  {
    logPrintf("[E_] flashWrite() protected 0x%08X\n", (int)addr);
    return false;
  }

#ifdef _USE_HW_QSPI
  if (flashInRange(addr, length, HW_QSPI_ADDR, HW_QSPI_SIZE))
  {
    qspiSetXipMode(false);
    return qspiWrite(addr - HW_QSPI_ADDR, p_data, length);
  }
#endif

  //-- 내부 플래시 쓰기는 지원하지 않는다 (소거를 못 하므로 의미가 없다).
  return false;
}


#if CLI_USE(HW_FLASH)
void cliFlash(cli_args_t *args)
{
  bool ret = false;


  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("BOOT   : 0x%08X  %d KB\n", FLASH_ADDR_BOOT, FLASH_SIZE_BOOT/1024);
    cliPrintf("FIRM   : 0x%08X  %d KB  (QSPI, XiP)\n", FLASH_ADDR_FIRM, FLASH_SIZE_FIRM/1024);
    cliPrintf("  TAG  : 0x%08X  %d KB\n", FLASH_ADDR_FIRM, FLASH_SIZE_TAG/1024);
    cliPrintf("  VEC  : 0x%08X\n", FLASH_ADDR_FIRM_VEC);
    ret = true;
  }

  if (args->argc == 3 && args->isStr(0, "read"))
  {
    uint32_t addr   = (uint32_t)args->getData(1);
    uint32_t length = (uint32_t)args->getData(2);
    uint8_t  data;

    for (uint32_t i=0; i<length; i++)
    {
      if (flashRead(addr+i, &data, 1) == false)
      {
        cliPrintf("read fail 0x%08X\n", (int)(addr+i));
        break;
      }
      if ((i % 16) == 0) cliPrintf("\n0x%08X : ", (int)(addr+i));
      cliPrintf("%02X ", data);
    }
    cliPrintf("\n");
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("flash info\n");
    cliPrintf("flash read  [addr] [length]\n");
  }
}
#endif

#endif
