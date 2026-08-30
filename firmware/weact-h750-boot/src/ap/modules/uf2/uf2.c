#include "uf2.h"
#include "cli.h"


#if CLI_USE(HW_BOOT)
static void cliUf2(cli_args_t *args);
#endif

static bool     is_init      = false;
static bool     is_done_req  = false;
static bool     is_jump_req  = false;
static bool     is_tr_active = false;

static uint32_t tr_family    = 0;
static uint32_t flash_len    = 0;     // 기록된 최대 끝 오프셋
static uint8_t  percent      = 0;

static uint8_t  erase_map[(UF2_ERASE_SECTOR_MAX + 7) / 8];   // 올림. 내림하면 마지막 섹터에서 넘친다


static void uf2TransferReset(WriteState *state);
static bool uf2FlashEraseOnce(uint32_t offset, uint32_t len);
static bool uf2FlashWrite(uint32_t target_addr, void const *data, uint32_t len);
static bool uf2FlashFlush(void);



bool uf2Init(void)
{
  is_init      = true;
  is_done_req  = false;
  is_jump_req  = false;
  is_tr_active = false;
  flash_len    = 0;
  percent      = 0;
  memset(erase_map, 0, sizeof(erase_map));

  logPrintf("[OK] uf2Init()\n");
  logPrintf("     familyID : 0x%08X\n", (unsigned int)BOARD_UF2_FAMILY_ID);
  logPrintf("     max fw   : %d KB\n", (int)(UF2_MAX_FW_SIZE/1024));

#if CLI_USE(HW_BOOT)
  cliAdd("uf2", cliUf2);
#endif
  return true;
}

bool uf2IsBusy(void)     { return is_tr_active || is_done_req; }
uint8_t uf2GetPercent(void) { return percent; }
void uf2RequestJump(void)   { is_jump_req = true; }

static inline bool uf2IsBlock(UF2_Block const *bl)
{
  return (bl->magicStart0 == UF2_MAGIC_START0) &&
         (bl->magicStart1 == UF2_MAGIC_START1) &&
         (bl->magicEnd    == UF2_MAGIC_END) &&
         (bl->flags & UF2_FLAG_FAMILYID) &&
         !(bl->flags & UF2_FLAG_NOFLASH);
}

void uf2TransferReset(WriteState *state)
{
  memset(state, 0, sizeof(WriteState));
  memset(erase_map, 0, sizeof(erase_map));
  flash_len = 0;
  percent   = 0;
}

/*
 * 섹터를 전송당 한 번만 지운다.
 *
 * UF2 블록은 순서가 뒤죽박죽으로 올 수 있고 같은 섹터에 여러 블록이 들어간다.
 * 매번 지우면 앞서 쓴 내용이 날아간다.
 */
bool uf2FlashEraseOnce(uint32_t offset, uint32_t len)
{
  uint32_t sector_s = offset / UF2_ERASE_SECTOR_SIZE;
  uint32_t sector_e = (offset + len - 1) / UF2_ERASE_SECTOR_SIZE;

  for (uint32_t i=sector_s; i<=sector_e; i++)
  {
    uint8_t  mask = 1 << (i % 8);
    uint32_t pos  = i / 8;

    if (i >= UF2_ERASE_SECTOR_MAX) return false;
    if (erase_map[pos] & mask) continue;

    if (flashErase(FLASH_ADDR_FIRM + FLASH_SIZE_TAG + i * UF2_ERASE_SECTOR_SIZE,
                   UF2_ERASE_SECTOR_SIZE) != true)
    {
      return false;
    }
    erase_map[pos] |= mask;
  }
  return true;
}

/*
 * targetAddr 은 uf2conv.py --base 0x0 로 만들었으므로 **0 기준 오프셋**이다.
 * 앱 본체가 시작하는 0x90001000 을 더한다.
 */
bool uf2FlashWrite(uint32_t target_addr, void const *data, uint32_t len)
{
  if (target_addr + len > UF2_MAX_FW_SIZE) return false;

  if (uf2FlashEraseOnce(target_addr, len) != true) return false;

  if (flashWrite(FLASH_ADDR_FIRM + FLASH_SIZE_TAG + target_addr,
                 (uint8_t *)data, len) != true)
  {
    return false;
  }

  //-- 블록이 순서대로 오지 않으므로 누적이 아니라 **최대 끝 오프셋**을 기록한다.
  if (target_addr + len > flash_len) flash_len = target_addr + len;

  return true;
}

/*
 * 태그를 마지막에 쓴다. 이게 커밋 마커다.
 *
 * 태그는 독립된 4KB 섹터라 앱 본체를 건드리지 않고 쓸 수 있다(00 문서).
 * 중간에 전원이 끊기면 태그가 무효라 부트로더가 점프를 거부한다.
 */
bool uf2FlashFlush(void)
{
  firm_tag_t tag;
  uint16_t   crc = 0;
  uint8_t    buf[256];

  uint32_t   fw_size;

  if (flash_len == 0) return false;

  /*
   * 태그에 넣을 크기를 정한다. **`flash_len` 을 그대로 쓰면 안 된다.**
   *
   * `flash_len` 은 UF2 블록이 실제로 덮은 최대 끝 오프셋이다. UF2 는 512B 블록마다
   * 256B 페이로드를 나르므로 **마지막 블록이 256B 경계로 패딩**된다. 즉 실제
   * 이미지보다 최대 255B 크다.
   *
   * 실측: 98,744B 이미지 -> 386블록 -> flash_len = 98,816 (72B 초과)
   *
   * 그대로 태그에 쓰면 `.version` 의 `firm_size` 와 어긋나서, 다음 부팅에
   * 부트로더가 **"stale tag" 로 판정하고 태그를 다시 만든다.** 자가 교정되므로
   * 벽돌은 아니지만 매번 헛돌고, CRC 도 패딩을 포함한 값이라 CDC(`FW_END`)로
   * 구운 것과 달라진다. 같은 이미지가 경로에 따라 다른 태그를 갖게 된다.
   *
   * 이미지에 `.version` 이 있으면 그 `firm_size` 가 **권위 있는 값**이다.
   * 없으면(RAW 이미지) `flash_len` 밖에 알 방법이 없으니 그대로 쓴다.
   */
  fw_size = flash_len;
  {
    firm_ver_t ver;

    if (flashRead(FLASH_ADDR_FIRM + FLASH_SIZE_TAG + FLASH_SIZE_VEC,
                  (uint8_t *)&ver, sizeof(ver)) == true &&
        ver.magic_number == VERSION_MAGIC_NUMBER &&
        ver.firm_size > 0 &&
        ver.firm_size <= flash_len)
    {
      fw_size = ver.firm_size;
    }
  }

  //-- CRC 는 굽는 경로와 같은 indirect 로 다시 읽어 계산한다 (07 문서)
  for (uint32_t i=0; i<fw_size; i+=sizeof(buf))
  {
    uint32_t len = fw_size - i;

    if (len > sizeof(buf)) len = sizeof(buf);
    if (flashRead(FLASH_ADDR_FIRM + FLASH_SIZE_TAG + i, buf, len) != true) return false;
    crc = utilCalcCRC(crc, buf, len);
  }

  memset(&tag, 0, sizeof(tag));
  tag.magic_number = TAG_MAGIC_NUMBER;
  tag.fw_addr      = FLASH_SIZE_TAG;
  tag.fw_size      = fw_size;
  tag.fw_crc       = crc;
  tag.tag_crc      = utilCalcCRC(0, (uint8_t *)&tag, sizeof(tag) - 4);

  if (flashErase(FLASH_ADDR_FIRM, FLASH_SIZE_TAG) != true) return false;
  if (flashWrite(FLASH_ADDR_FIRM, (uint8_t *)&tag, sizeof(tag)) != true) return false;

  /*
   * 새 이미지를 커밋했다. 폴트 카운터를 접는다.
   *
   * 이게 없으면 "폴트 루프로 막힘 -> 사용자가 고친 .uf2 를 떨어뜨림 -> 카운터가
   * 그대로라 다음 부팅에서 또 막힘" 이 된다. 태그를 쓴 시점이 곧 "사용자가
   * 고쳤다" 는 신호다.
   */
  resetSetFaultCount(0);

  logPrintf("[  ] uf2 flush : %d bytes (blk %d), crc 0x%04X\n",
            (int)fw_size, (int)flash_len, crc);
  return true;
}

int uf2WriteBlock(uint32_t block_no, uint8_t *data, WriteState *state)
{
  UF2_Block *bl = (void *)data;
  bool is_new_block = true;

  (void)block_no;

  if (!uf2IsBlock(bl)) return UF2_RET_NOT_UF2;

  if (bl->familyID != BOARD_UF2_FAMILY_ID)
  {
    logPrintf("[E_] familyID 0x%X != 0x%X\n",
              (unsigned int)bl->familyID, (unsigned int)BOARD_UF2_FAMILY_ID);
    return UF2_RET_NOT_UF2;
  }

  //-- 새 전송 판정. 총 블록 수가 다르면 다른 파일이다.
  if (is_tr_active == false || bl->familyID != tr_family ||
      (bl->numBlocks > 0 && state->numBlocks > 0 && bl->numBlocks != state->numBlocks))
  {
    uf2TransferReset(state);
    is_tr_active = true;
    tr_family    = bl->familyID;
    logPrintf("[  ] uf2 begin (%d blocks)\n", (int)bl->numBlocks);
  }

  if (bl->numBlocks > 0 && bl->numBlocks < MAX_BLOCKS)
    state->numBlocks = bl->numBlocks;

  /*
   * 호스트가 같은 블록을 다시 보내는 경우가 있다. 다시 기록하면 길이가 두 번
   * 반영되고 소거가 다시 걸려 앞서 쓴 내용이 날아간다.
   */
  if (state->numBlocks > 0 && bl->blockNo < MAX_BLOCKS)
  {
    uint8_t  mask = 1 << (bl->blockNo % 8);
    uint32_t pos  = bl->blockNo / 8;

    if (state->writtenMask[pos] & mask)
    {
      is_new_block = false;
    }
    else
    {
      state->writtenMask[pos] |= mask;
      state->numWritten++;
    }
  }

  if (is_new_block)
  {
    if (uf2FlashWrite(bl->targetAddr, bl->data, bl->payloadSize) != true)
    {
      state->aborted = true;
      is_tr_active   = false;
      return UF2_RET_ERR;
    }
    ledToggle(_DEF_LED1);
  }

  if (state->numBlocks > 0)
    percent = (uint8_t)((state->numWritten * 100) / state->numBlocks);

  return UF2_DISK_BLOCK_SIZE;
}

bool uf2FlashComplete(WriteState *state)
{
  if (state->aborted) return false;

  /*
   * 호스트는 전송이 끝난 뒤에도 FAT/디렉터리 갱신으로 write10 을 더 보낸다.
   * 그때마다 complete_cb 가 다시 불리므로 한 번 마무리한 전송을 재실행하지
   * 않도록 막는다.
   */
  if (is_done_req) return true;

  if (uf2FlashFlush() != true)
  {
    logPrintf("[E_] uf2FlashFlush()\n");
    is_tr_active     = false;
    state->numBlocks = 0;
    return false;
  }

  is_tr_active     = false;
  state->numBlocks = 0;
  is_done_req      = true;
  return true;
}

//-- 완료 후 상태머신. USB 콜백 밖(메인 루프)에서 돈다.
void uf2Update(void)
{
  static uint8_t  state = 0;
  static uint32_t pre_time = 0;

  if (is_init != true) return;

  switch (state)
  {
    case 0:
      if (is_done_req || is_jump_req)
      {
        pre_time = millis();
        state = 1;
      }
      break;

    case 1:
      //-- 호스트가 FAT/디렉터리 기록과 SYNCHRONIZE_CACHE 를 마칠 시간을 준다.
      if (millis() - pre_time >= UF2_COMPLETE_WAIT_MS)
      {
        state = 2;
      }
      break;

    case 2:
      /*
       * **여기서 `delay()` 를 쓰면 안 된다.**
       *
       * 부트로더의 `delay()` 는 `cliLoopIdle()` -> `moduleUpdate()` 를 부른다.
       * 그러면 이 함수가 state 2 인 채로 다시 들어와 또 `delay()` 를 부른다.
       * **무한 재귀다.** 실기에서 "uf2 -> jump" 만 끝없이 찍히고 `bootJumpFirm()`
       * 에는 영영 도달하지 못했다. 파일은 정상적으로 구워졌는데 자동 점프만
       * 안 되는, 원인을 짚기 어려운 형태로 나타난다.
       *
       * `reset.c` 의 더블탭 판정이 같은 이유로 `HAL_Delay()` 를 쓴다.
       *
       * 다만 여기서는 `HAL_Delay()` 도 맞지 않는다. 이 300ms 는 호스트가 마지막
       * 응답을 받아갈 시간이라 그동안 USB 는 계속 돌아야 한다. 그래서
       * `usbUpdate()`(= `tud_task()`) 만 직접 돌린다. `moduleUpdate()` 를 거치지
       * 않으므로 재진입이 없다.
       */
      state = 3;                 // 재진입 방어. 아래 루프가 이 함수를 다시 부르지는
                                 // 않지만, 실패 경로로 돌아올 때까지 잠가둔다.
      if (bootVerifyFirm() != BOOT_IMG_NONE)
      {
        uint32_t t = millis();

        logPrintf("[  ] uf2 -> jump\n");
        while (millis() - t < UF2_JUMP_WAIT_MS)
        {
#ifdef _USE_HW_USB
          usbUpdate();
#endif
        }
        bootJumpFirm();
      }
      //-- 점프에 실패해도 벽돌이 아니다. 다시 복사할 수 있게 되돌린다.
      logPrintf("[E_] uf2 jump failed\n");
      is_done_req = false;
      is_jump_req = false;
      state = 0;
      break;

    case 3:
      //-- 점프 준비 중. 재진입을 무시한다.
      break;
  }
}


#if CLI_USE(HW_BOOT)
void cliUf2(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("familyID  : 0x%08X\n", (unsigned int)BOARD_UF2_FAMILY_ID);
    cliPrintf("max fw    : %d KB\n", (int)(UF2_MAX_FW_SIZE/1024));
    cliPrintf("disk      : %d MB (FAT16)\n",
              (int)((uint32_t)UF2_DISK_BLOCK_NUM * UF2_DISK_BLOCK_SIZE / (1024*1024)));
    cliPrintf("medium    : %s\n", uf2DiskGetMedium() ? "present" : "not present");
    cliPrintf("busy      : %d  (%d%%)\n", uf2IsBusy(), uf2GetPercent());
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("uf2 info\n");
  }
}
#endif
