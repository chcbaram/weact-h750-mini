#include "qspi.h"



#ifdef _USE_HW_QSPI
#include "qspi/w25q64jv.h"
#include "cli.h"




/* QSPI Error codes */
#define QSPI_OK            ((uint8_t)0x00)
#define QSPI_ERROR         ((uint8_t)0x01)
#define QSPI_BUSY          ((uint8_t)0x02)
#define QSPI_NOT_SUPPORTED ((uint8_t)0x04)
#define QSPI_SUSPENDED     ((uint8_t)0x08)



/* QSPI Base Address */
#define QSPI_BASE_ADDRESS          0x90000000



/* QSPI Info */
typedef struct {
  uint32_t FlashSize;          /*!< Size of the flash */
  uint32_t EraseSectorSize;    /*!< Size of sectors for the erase operation */
  uint32_t EraseSectorsNumber; /*!< Number of sectors for the erase operation */
  uint32_t ProgPageSize;       /*!< Size of pages for the program operation */
  uint32_t ProgPagesNumber;    /*!< Number of pages for the program operation */

  uint8_t  device_id[20];
} QSPI_Info;


static bool is_init = false;
static QSPI_HandleTypeDef hqspi;


uint8_t BSP_QSPI_Init(void);
uint8_t BSP_QSPI_DeInit(void);
uint8_t BSP_QSPI_Read(uint8_t* p_data, uint32_t addr, uint32_t length);
uint8_t BSP_QSPI_Write(uint8_t* p_data, uint32_t addr, uint32_t length);
uint8_t BSP_QSPI_Erase_Block(uint32_t block_addr);
uint8_t BSP_QSPI_Erase_Sector(uint32_t SectorAddress);
uint8_t BSP_QSPI_Erase_Chip (void);
uint8_t BSP_QSPI_GetStatus(void);
uint8_t BSP_QSPI_GetInfo(QSPI_Info* p_info);
uint8_t BSP_QSPI_EnableMemoryMappedMode(void);
uint8_t BSP_QSPI_GetID(QSPI_Info* p_info);
uint8_t BSP_QSPI_Config(void);
uint8_t BSP_QSPI_Reset(void);
uint8_t BSP_QSPI_Abort(void);

#if CLI_USE(HW_QSPI)
static void cliCmd(cli_args_t *args);
#endif






bool qspiInit(void)
{
  bool ret = true;
  QSPI_Info info;


  if (BSP_QSPI_Init() == QSPI_OK)
  {
    ret = true;
  }
  else
  {
    ret = false;
  }


  if (BSP_QSPI_GetID(&info) == QSPI_OK)
  {
    /*
     * JEDEC ID = [제조사][메모리타입][용량]
     *   0xEF = Winbond
     *   3번째 바이트가 log2(바이트수) 다. 0x17 = 2^23 = 8MB (W25Q64)
     *                                     0x18 = 2^24 = 16MB (W25Q128)
     *
     * 부품 번호를 그대로 비교하지 않고 용량을 해석하는 이유 : 이 보드는 QSPI 가
     * 소켓이 아니라 실장이지만, 회로도상 W25Qxx 계열이면 무엇이든 올라갈 수 있다.
     * 실제 용량이 기대와 다르면 XiP 영역 계산이 전부 어긋나므로 여기서 잡는다.
     */
    uint8_t  mfr  = info.device_id[0];
    uint8_t  type = info.device_id[1];
    uint8_t  cap  = info.device_id[2];
    uint32_t size = (cap >= 0x10 && cap <= 0x1B) ? (1UL << cap) : 0;

    logPrintf("[%s] qspiInit()\n", (mfr == 0xEF && size > 0) ? "OK" : "E_");
    logPrintf("     JEDEC ID : %02X %02X %02X\n", mfr, type, cap);

    if (mfr == 0xEF && size > 0)
    {
      logPrintf("     Winbond  : %d MB\n", (int)(size / (1024*1024)));

      if (size != W25Q64JV_FLASH_SIZE)
      {
        //-- 빌드가 가정한 용량과 실제가 다르다. XiP 영역 계산이 어긋난다.
        logPrintf("     [!!] expect %d MB (W25Q64JV)\n",
                  (int)(W25Q64JV_FLASH_SIZE / (1024*1024)));
      }
      ret = true;
    }
    else
    {
      logPrintf("     [E_] unknown flash\n");
      ret = false;
    }
  }
  else
  {
    logPrintf("[E_] qspiInit() - GetID fail\n");
    ret = false;
  }

  is_init = ret;

#if CLI_USE(HW_QSPI)
  cliAdd("qspi", cliCmd);
#endif
  return ret;
}

#ifdef QSPI_SELF_TEST
/*
 * QSPI 자체 시험 (브링업 전용)
 *
 * indirect 모드로 패턴을 쓰고, memory-mapped(XiP)로 전환해 CPU 가 직접 읽어
 * 비교한다. XiP 로 읽는 것이 핵심이다 - 앱이 실제로 명령어를 인출하는 경로가
 * 바로 이 경로이기 때문이다. indirect 읽기만 성공해도 XiP 는 깨질 수 있다.
 *
 * 시험 영역은 QSPI 마지막 섹터. SCK 를 올리거나 배선을 건드린 뒤에만 켠다.
 */
#define QSPI_TEST_ADDR   (HW_QSPI_SIZE - 4096)

void qspiSelfTest(void)
{
  static uint8_t buf[256];
  uint32_t err_cnt = 0;

  logPrintf("[  ] qspiSelfTest() @0x%X\n", HW_QSPI_ADDR + QSPI_TEST_ADDR);

  qspiSetXipMode(false);

  if (qspiErase(QSPI_TEST_ADDR, 4096) != true)
  {
    logPrintf("     [E_] erase fail\n");
    return;
  }

  for (int i=0; i<256; i++)
  {
    buf[i] = (uint8_t)(i * 7 + 0x5A);
  }
  if (qspiWrite(QSPI_TEST_ADDR, buf, 256) != true)
  {
    logPrintf("     [E_] write fail\n");
    return;
  }

  if (qspiSetXipMode(true) != true)
  {
    logPrintf("     [E_] memory-mapped enable fail\n");
    return;
  }

  //-- 방금 구웠으므로 캐시에 옛 내용이 남아 있을 수 있다.
  SCB_CleanInvalidateDCache();
  SCB_InvalidateICache();

  {
    volatile uint8_t *p = (volatile uint8_t *)(HW_QSPI_ADDR + QSPI_TEST_ADDR);

    for (int i=0; i<256; i++)
    {
      if (p[i] != (uint8_t)(i * 7 + 0x5A))
      {
        if (err_cnt < 4)
          logPrintf("     [E_] [%d] %02X != %02X\n", i, p[i], (uint8_t)(i*7+0x5A));
        err_cnt++;
      }
    }
  }

  logPrintf("     XiP read : %s (err %d/256)\n", err_cnt == 0 ? "OK":"FAIL", (int)err_cnt);

  //-- 넓은 범위도 훑어본다. 캐시 라인 경계와 버스트 읽기를 건드리기 위함.
  {
    volatile uint32_t *p = (volatile uint32_t *)(HW_QSPI_ADDR);
    uint32_t sum = 0;
    for (int i=0; i<1024; i++) sum += p[i];
    logPrintf("     XiP sweep: 4KB read ok (sum 0x%08X)\n", (unsigned int)sum);
  }
}
#endif


bool qspiReset(void)
{
  bool ret = false;

  if (is_init == true)
  {
    if (BSP_QSPI_Reset() == QSPI_OK)
    {
      ret = true;
    }
  }

  return ret;
}

bool qspiAbort(void)
{
  bool ret = false;

  if (is_init == true)
  {
    if (BSP_QSPI_Abort() == QSPI_OK)
    {
      ret = true;
    }
  }

  return ret;
}

bool qspiIsInit(void)
{
  return is_init;
}

bool qspiRead(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  uint8_t ret;

  assert(qspiGetXipMode() == false);

  if (addr >= qspiGetLength())
  {
    return false;
  }  

  ret = BSP_QSPI_Read(p_data, addr, length);

  if (ret == QSPI_OK)
  {
    return true;
  }
  else
  {
    return false;
  }
}

bool qspiWrite(uint32_t addr, uint8_t *p_data, uint32_t length)
{
  uint8_t ret;


  assert(qspiGetXipMode() == false);

  if (addr >= qspiGetLength())
  {
    return false;
  }

  ret = BSP_QSPI_Write(p_data, addr, length);

  if (ret == QSPI_OK)
  {
    return true;
  }
  else
  {
    return false;
  }
}

bool qspiEraseBlock(uint32_t block_addr)
{
  uint8_t ret;


  assert(qspiGetXipMode() == false);

  ret = BSP_QSPI_Erase_Block(block_addr);

  if (ret == QSPI_OK)
  {
    return true;
  }
  else
  {
    return false;
  }
}

bool qspiEraseSector(uint32_t sector_addr)
{
  uint8_t ret;


  assert(qspiGetXipMode() == false);

  ret = BSP_QSPI_Erase_Sector(sector_addr);

  if (ret == QSPI_OK)
  {
    return true;
  }
  else
  {
    return false;
  }  
}
bool qspiErase(uint32_t addr, uint32_t length)
{
  bool ret = true;
  uint32_t flash_length;


  assert(qspiGetXipMode() == false);


  /*
   * 소거 단위는 4KB(subsector) 다.
   *
   * [함정] BSP 함수 이름이 직관과 뒤집혀 있다. 원본 그대로 두면 안 된다.
   *
   *   BSP_QSPI_Erase_Block()  -> SUBSECTOR_ERASE_CMD (0x20)  =  4KB
   *   BSP_QSPI_Erase_Sector() -> SECTOR_ERASE_CMD    (0xD8)  = 64KB
   *
   * 원본은 SECTOR_SIZE(64KB) 단위로만 지웠다. 그러면 flashErase(0x90000000, 4096)
   * 이 태그 4KB 가 아니라 64KB 를 지워버려 앱의 벡터 테이블과 코드 앞부분까지
   * 날아간다. 태그를 독립 4KB 섹터에 둔 설계(00 문서)가 통째로 무너진다.
   *
   * 그렇다고 4KB 로만 지우면 큰 이미지에서 느리다. 아래에서 둘을 섞는다.
   */
  flash_length = W25Q64JV_FLASH_SIZE;

  if ((addr > flash_length) || ((addr+length) > flash_length))
  {
    return false;
  }
  if (length == 0)
  {
    return false;
  }

  /*
   * 4KB 섹터와 64KB 블록을 섞어 쓴다.
   *
   * 지우기가 업로드 시간의 대부분이다. 실측(아두이노 세션, 242KB 이미지):
   *
   *   총 4.92s = 전송 0.9s + **지우기 4.02s**   (60섹터 x 67ms)
   *
   * 4KB 섹터는 typ 45ms, 64KB 블록은 typ 150ms 다. 같은 64KB 를 지우는 데
   * 섹터 16개면 720ms, 블록 하나면 150ms — **4.8배** 차이다.
   *
   * 그래서 **요청 범위 안에 온전히 들어가는 64KB 블록만** 블록으로 지우고,
   * 앞뒤 자투리는 4KB 섹터로 지운다.
   *
   * **범위 밖은 절대 지우지 않는다.** 이 함수는 태그 섹터 하나(4KB)만 지우는
   * 데도 쓰인다(FW_BEGIN). 꼬리를 64KB 로 올려버리면 그 호출이 앱 본체를
   * 통째로 날린다. 범위를 넓히고 싶으면 **호출자가** 그렇게 요청해야 한다.
   */
  {
    const uint32_t sec = W25Q64JV_SUBSECTOR_SIZE;   // 0x1000  4KB
    const uint32_t blk = W25Q64JV_SECTOR_SIZE;      // 0x10000 64KB
    uint32_t addr_s = addr & ~(sec - 1);                        // 4KB 로 내림
    uint32_t addr_e = (addr + length + sec - 1) & ~(sec - 1);   // 4KB 로 올림
    uint32_t cur    = addr_s;

    ret = true;
    while (cur < addr_e && ret == true)
    {
      if (((cur & (blk - 1)) == 0) && ((cur + blk) <= addr_e))
      {
        ret = qspiEraseSector(cur);   // 0xD8 = 64KB (이름에 속지 말 것)
        cur += blk;
      }
      else
      {
        ret = qspiEraseBlock(cur);    // 0x20 = 4KB
        cur += sec;
      }
    }
  }

  return ret;
}

bool qspiEraseChip(void)
{
  uint8_t ret;

  assert(qspiGetXipMode() == false);

  ret = BSP_QSPI_Erase_Chip();

  if (ret == QSPI_OK)
  {
    return true;
  }
  else
  {
    return false;
  }
}

bool qspiGetStatus(void)
{
  uint8_t ret;

  ret = BSP_QSPI_GetStatus();

  if (ret == QSPI_OK)
  {
    return true;
  }
  else
  {
    return false;
  }
}

bool qspiGetInfo(qspi_info_t* p_info)
{
  uint8_t ret;

  ret = BSP_QSPI_GetInfo((QSPI_Info *)p_info);

  if (ret == QSPI_OK)
  {
    return true;
  }
  else
  {
    return false;
  }
}

bool qspiEnableMemoryMappedMode(void)
{
  uint8_t ret;

  ret = BSP_QSPI_EnableMemoryMappedMode();

  if (ret == QSPI_OK)
  {
    return true;
  }
  else
  {
    return false;
  }
}

bool qspiSetXipMode(bool enable)
{
  uint8_t ret = true;

  if (enable)
  {
    if (qspiGetXipMode() == false)
    {
      /*
       * memory-mapped 진입 **전에도** Abort 가 필요하다.
       *
       * QUADSPI 는 자체 프리페치 버퍼를 갖는다. 소거/쓰기로 플래시 내용이 바뀐 뒤
       * 그대로 재진입하면 **첫 읽기가 옛 내용으로 나온다.** CPU D-Cache 를
       * 무효화해도 소용없다 - 페리페럴 안쪽이라 캐시보다 하위다.
       *
       * 그리고 Abort 만으로는 부족하다. 플래시도 리셋해서 이전 읽기 시퀀스에서
       * 확실히 빼내야 한다. Abort 는 페리페럴만 정리하기 때문이다.
       *
       * 실기에서 재현됨 : 태그를 쓴 직후 읽으면 전부 0xFF 로 나오고
       * (crc E848 != 정답 8205), 명시적으로 XiP 를 나갔다 다시 들어오면 정상이었다.
       * SCK 를 50MHz 로 낮춰도 동일했으므로 신호 무결성 문제가 아니다.
       */
      qspiAbort();
      qspiReset();
      ret = qspiEnableMemoryMappedMode();

    }
  }
  else
  {
    if (qspiGetXipMode() == true)
    {
      /*
       * memory-mapped 이탈은 **HAL_QSPI_Abort()** 로 해야 한다.
       *
       * 원본은 여기서 qspiReset()(= 플래시 칩에 0x66/0x99 소프트 리셋)을 불렀다.
       * 그러면 QUADSPI 페리페럴의 프리페치 버퍼가 비워지지 않아, 다시 XiP 로
       * 들어간 뒤 **첫 읽기가 stale 값(0xFF)으로 나온다.** 실기에서 재현됨:
       * 태그를 쓴 직후 bootGetTag() 가 한 번 실패하고 재시도하면 성공했다.
       *
       * 다만 Abort 만으로도 안 된다. 실기에서 확인 : Abort 만 하면 이후 모든
       * 읽기가 실패한다. 플래시 쪽도 리셋(0x66/0x99)해서 이전 읽기 시퀀스에서
       * 확실히 빠져나오게 해야 한다.
       *
       *   Abort  : QUADSPI 페리페럴 정리 + 프리페치 버퍼 flush
       *   Reset  : 플래시 칩을 알려진 상태로
       *
       * 둘 다 필요하다.
       */
      ret  = qspiAbort();
      ret &= qspiReset();
    }
  }

  return ret;
}

bool qspiGetXipMode(void)
{
  bool ret = false;

  if (HAL_QSPI_GetState(&hqspi) == HAL_QSPI_STATE_BUSY_MEM_MAPPED)
  {
    ret = true;
  }

  return ret;
}

uint32_t qspiGetAddr(void)
{
  return QSPI_BASE_ADDRESS;
}

uint32_t qspiGetLength(void)
{
  return W25Q64JV_FLASH_SIZE;
}






static uint8_t QSPI_ResetMemory(QSPI_HandleTypeDef *hqspi);
static uint8_t QSPI_WriteEnable(QSPI_HandleTypeDef *hqspi);
static uint8_t QSPI_AutoPollingMemReady(QSPI_HandleTypeDef *hqspi, uint32_t Timeout);
static uint8_t QSPI_ReadStatus(QSPI_HandleTypeDef *hqspi, uint8_t cmd, uint8_t *p_data);
static uint8_t QSPI_WriteStatus(QSPI_HandleTypeDef *hqspi, uint8_t cmd, uint8_t data);


uint8_t BSP_QSPI_Init(void)
{
  hqspi.Instance = QUADSPI;

  /* Call the DeInit function to reset the driver */
  if (HAL_QSPI_DeInit(&hqspi) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* QSPI initialization */
  /*
   * QSPI 커널 클럭은 D1HCLK = 200MHz (bsp.c 의 PeriphCommonClock_Config).
   * ClockPrescaler = 1 -> 200 / (1+1) = 100MHz.
   *
   * W25Q64JV 의 Fast Read Quad I/O(0xEB) 는 규격상 133MHz 까지지만, 이 보드는
   * QSPI 라인마다 33R 직렬 저항(R30~R35)이 붙어 있어 엣지가 둔하다.
   * 100MHz 는 실기에서 memory-mapped 읽기로 검증한 값이다.
   *
   * PLL2 가 아니라 D1HCLK 을 쓰는 이유는 bsp.c 주석 참고.
   * 앱의 SystemInit() 이 PLL2 를 꺼버리기 때문이다.
   *
   * W25Q64JV 는 quad 읽기에서 133MHz 까지 되지만, 이 보드는 QSPI 라인마다
   * 33R 직렬 저항(R30~R35)이 붙어 있어 파형이 둔하다. 50MHz 로 시작해서
   * 실기에서 확인한 뒤 올린다.
   */
  hqspi.Init.ClockPrescaler      = 1;
  hqspi.Init.FifoThreshold       = 4;
  hqspi.Init.SampleShifting      = QSPI_SAMPLE_SHIFTING_HALFCYCLE;
  hqspi.Init.FlashSize           = POSITION_VAL(W25Q64JV_FLASH_SIZE);
  hqspi.Init.ChipSelectHighTime  = QSPI_CS_HIGH_TIME_5_CYCLE;  
  hqspi.Init.ClockMode           = QSPI_CLOCK_MODE_0;
  hqspi.Init.FlashID             = QSPI_FLASH_ID_1;
  hqspi.Init.DualFlash           = QSPI_DUALFLASH_DISABLE;

  if (HAL_QSPI_Init(&hqspi) != HAL_OK)
  {
    logPrintf("HAL_QSPI_Init() fail\n");
    return QSPI_ERROR;
  }

  /* QSPI memory reset */
  if (QSPI_ResetMemory(&hqspi) != QSPI_OK)
  {
    logPrintf("QSPI_ResetMemory() fail\n");
    return QSPI_NOT_SUPPORTED;
  }

  if (BSP_QSPI_Config() != QSPI_OK)
  {
    logPrintf("QSPI_Config() fail\n");
    return QSPI_NOT_SUPPORTED;
  }

  return QSPI_OK;
}

uint8_t BSP_QSPI_Reset(void)
{
  if (QSPI_ResetMemory(&hqspi) != QSPI_OK)
  {
    return QSPI_NOT_SUPPORTED;
  }

  return QSPI_OK;
}

uint8_t BSP_QSPI_Abort(void)
{
  if (HAL_QSPI_Abort(&hqspi) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t BSP_QSPI_Config(void)
{
  uint8_t reg = 0;


  if (QSPI_ReadStatus(&hqspi, READ_STATUS_REG2_CMD, &reg) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  // QUAD MODE Enable
  if ((reg & (1<<1)) == 0x00)
  {
    reg |= (1<<1);
    if (QSPI_WriteStatus(&hqspi, WRITE_STATUS_REG2_CMD, reg) != QSPI_OK)
    {
      return QSPI_ERROR;
    }
  }


  return QSPI_OK;
}

uint8_t BSP_QSPI_DeInit(void)
{
  hqspi.Instance = QUADSPI;

  /* Call the DeInit function to reset the driver */
  if (HAL_QSPI_DeInit(&hqspi) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t BSP_QSPI_Read(uint8_t* p_data, uint32_t addr, uint32_t length)
{
  QSPI_CommandTypeDef s_command = {0};


  /* Initialize the read command */

  s_command.InstructionMode    = QSPI_INSTRUCTION_1_LINE;
  s_command.Instruction        = QUAD_INOUT_FAST_READ_CMD;
  s_command.AddressMode        = QSPI_ADDRESS_4_LINES;
  s_command.AddressSize        = QSPI_ADDRESS_24_BITS;
  s_command.AlternateByteMode  = QSPI_ALTERNATE_BYTES_4_LINES;
  s_command.AlternateBytesSize = QSPI_ALTERNATE_BYTES_8_BITS;
  s_command.AlternateBytes     = 0;

  s_command.DataMode         = QSPI_DATA_4_LINES;
  s_command.DummyCycles      = W25Q64JV_DUMMY_CYCLES_READ_QUAD;
  s_command.DdrMode          = QSPI_DDR_MODE_DISABLE;
  s_command.DdrHoldHalfCycle = QSPI_DDR_HHC_ANALOG_DELAY;
  s_command.SIOOMode         = QSPI_SIOO_INST_EVERY_CMD;

  s_command.Address = addr;
  s_command.NbData  = length;


  /* Send the command */
  if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return false;
  }

  /* Reception of the data */
  if (HAL_QSPI_Receive(&hqspi, p_data, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return false;
  }

  return QSPI_OK;
}

uint8_t BSP_QSPI_Write(uint8_t* p_data, uint32_t addr, uint32_t length)
{
  QSPI_CommandTypeDef s_command = {0};
  uint32_t end_addr, current_size, current_addr;

  /* Calculation of the size between the write address and the end of the page */
  current_size = W25Q64JV_PAGE_SIZE - (addr % W25Q64JV_PAGE_SIZE);

  /* Check if the size of the data is less than the remaining place in the page */
  if (current_size > length)
  {
    current_size = length;
  }

  /* Initialize the adress variables */
  current_addr = addr;
  end_addr = addr + length;

  /* Initialize the program command */
  s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
  s_command.Instruction       = QUAD_IN_FAST_PROG_CMD;
  s_command.AddressMode       = QSPI_ADDRESS_1_LINE;
  s_command.AddressSize       = QSPI_ADDRESS_24_BITS;
  
  s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;

  s_command.DataMode          = QSPI_DATA_4_LINES;
  s_command.DummyCycles       = 0;
  s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
  s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
  s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;


  /* Perform the write page by page */
  do
  {
    s_command.Address = current_addr;
    s_command.NbData  = current_size;

    /* Enable write operations */
    if (QSPI_WriteEnable(&hqspi) != QSPI_OK)
    {
      return QSPI_ERROR;
    }

    /* Configure the command */
    if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
      return QSPI_ERROR;
    }

    /* Transmission of the data */
    if (HAL_QSPI_Transmit(&hqspi, p_data, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
    {
      return QSPI_ERROR;
    }

    /* Configure automatic polling mode to wait for end of program */
    if (QSPI_AutoPollingMemReady(&hqspi, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != QSPI_OK)
    {
      return QSPI_ERROR;
    }

    /* Update the address and size variables for next page programming */
    current_addr += current_size;
    p_data += current_size;
    current_size = ((current_addr + W25Q64JV_PAGE_SIZE) > end_addr) ? (end_addr - current_addr) : W25Q64JV_PAGE_SIZE;
  } while (current_addr < end_addr);

  return QSPI_OK;
}

uint8_t BSP_QSPI_Erase_Block(uint32_t BlockAddress)
{
  QSPI_CommandTypeDef s_command = {0};

  /* Initialize the erase command */
  s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
  s_command.Instruction       = SUBSECTOR_ERASE_CMD;
  s_command.AddressMode       = QSPI_ADDRESS_1_LINE;
  s_command.AddressSize       = QSPI_ADDRESS_24_BITS;
  s_command.Address           = BlockAddress;
  s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
  s_command.DataMode          = QSPI_DATA_NONE;
  s_command.DummyCycles       = 0;
  s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
  s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
  s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;


  /* Enable write operations */
  if (QSPI_WriteEnable(&hqspi) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  /* Send the command */
  if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Configure automatic polling mode to wait for end of erase */
  if (QSPI_AutoPollingMemReady(&hqspi, W25Q64JV_SUBSECTOR_ERASE_MAX_TIME) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t BSP_QSPI_Erase_Sector(uint32_t SectorAddress)
{
  QSPI_CommandTypeDef s_command = {0};

  /* Initialize the erase command */
  s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
  s_command.Instruction       = SECTOR_ERASE_CMD;   // 0xD8, 64KB block erase
  s_command.AddressMode       = QSPI_ADDRESS_1_LINE;
  s_command.AddressSize       = QSPI_ADDRESS_24_BITS;
  s_command.Address           = SectorAddress;
  s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
  s_command.DataMode          = QSPI_DATA_NONE;
  s_command.DummyCycles       = 0;
  s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
  s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
  s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;


  /* Enable write operations */
  if (QSPI_WriteEnable(&hqspi) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  /* Send the command */
  if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Configure automatic polling mode to wait for end of erase */
  /* 64KB block erase 는 subsector(4KB) 보다 오래 걸린다. 4KB 기준 타임아웃을 쓰면
     지연 시 erase 를 실패로 처리해 기록이 깨진다. */
  if (QSPI_AutoPollingMemReady(&hqspi, W25Q64JV_SECTOR_ERASE_MAX_TIME) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t BSP_QSPI_Erase_Chip(void)
{
  QSPI_CommandTypeDef s_command = {0};

  /* Initialize the erase command */
  s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
  s_command.Instruction       = BULK_ERASE_CMD;
  s_command.AddressMode       = QSPI_ADDRESS_NONE;
  s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
  s_command.DataMode          = QSPI_DATA_NONE;
  s_command.DummyCycles       = 0;
  s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
  s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
  s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

  /* Enable write operations */
  if (QSPI_WriteEnable(&hqspi) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  /* Send the command */
  if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Configure automatic polling mode to wait for end of erase */
  if (QSPI_AutoPollingMemReady(&hqspi, W25Q64JV_BULK_ERASE_MAX_TIME) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t BSP_QSPI_GetStatus(void)
{
  QSPI_CommandTypeDef s_command = {0};
  uint8_t reg;

  /* Initialize the read flag status register command */
  s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
  s_command.Instruction       = READ_FLAG_STATUS_REG_CMD;
  s_command.AddressMode       = QSPI_ADDRESS_NONE;
  s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
  s_command.DataMode          = QSPI_DATA_1_LINE;
  s_command.DummyCycles       = 0;
  s_command.NbData            = 1;
  s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
  s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
  s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

  /* Configure the command */
  if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Reception of the data */
  if (HAL_QSPI_Receive(&hqspi, &reg, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Check the value of the register */
  if ((reg & (W25Q64JV_FSR_PRERR | W25Q64JV_FSR_VPPERR | W25Q64JV_FSR_PGERR | W25Q64JV_FSR_ERERR)) != 0)
  {
    return QSPI_ERROR;
  }
  else if ((reg & (W25Q64JV_FSR_PGSUS | W25Q64JV_FSR_ERSUS)) != 0)
  {
    return QSPI_SUSPENDED;
  }
  else if ((reg & W25Q64JV_FSR_READY) != 0)
  {
    return QSPI_OK;
  }
  else
  {
    return QSPI_BUSY;
  }
}

uint8_t BSP_QSPI_GetID(QSPI_Info* p_info)
{
  QSPI_CommandTypeDef s_command = {0};


  /* Initialize the read flag status register command */
  s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
  s_command.Instruction       = READ_ID_CMD;
  s_command.AddressMode       = QSPI_ADDRESS_NONE;
  s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
  s_command.DataMode          = QSPI_DATA_1_LINE;
  s_command.DummyCycles       = 0;
  s_command.NbData            = 20;
  s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
  s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
  s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;
  
  /* Configure the command */
  if (HAL_QSPI_Command(&hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Reception of the data */
  if (HAL_QSPI_Receive(&hqspi, p_info->device_id, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

uint8_t BSP_QSPI_GetInfo(QSPI_Info* p_info)
{
  /* Configure the structure with the memory configuration */
  p_info->FlashSize          = W25Q64JV_FLASH_SIZE;
  p_info->EraseSectorSize    = W25Q64JV_SUBSECTOR_SIZE;
  p_info->EraseSectorsNumber = (W25Q64JV_FLASH_SIZE/W25Q64JV_SUBSECTOR_SIZE);
  p_info->ProgPageSize       = W25Q64JV_PAGE_SIZE;
  p_info->ProgPagesNumber    = (W25Q64JV_FLASH_SIZE/W25Q64JV_PAGE_SIZE);

  return QSPI_OK;
}

uint8_t BSP_QSPI_EnableMemoryMappedMode(void)
{
  QSPI_CommandTypeDef      s_command        = {0};
  QSPI_MemoryMappedTypeDef s_mem_mapped_cfg = {0};

  /* Configure the command for the read instruction */
  s_command.InstructionMode    = QSPI_INSTRUCTION_1_LINE;
  s_command.Instruction        = QUAD_INOUT_FAST_READ_CMD;
  s_command.AddressMode        = QSPI_ADDRESS_4_LINES;
  s_command.AddressSize        = QSPI_ADDRESS_24_BITS;
  s_command.AlternateByteMode  = QSPI_ALTERNATE_BYTES_4_LINES;
  s_command.AlternateBytesSize = QSPI_ALTERNATE_BYTES_8_BITS;
  /*
   * AlternateBytes = 0x20 은 M5:M4 = 10b, 즉 플래시를 **연속 읽기 모드**에 넣는다.
   * 그 모드에서는 다음 읽기에 0xEB 오피코드를 보내면 안 된다.
   *
   * 따라서 SIOOMode 를 **INST_ONLY_FIRST_CMD** 로 해야 짝이 맞는다.
   * INST_EVERY_CMD 로 두면 QUADSPI 가 매번 0xEB 를 보내고 플래시는 그것을 주소
   * 비트로 해석해 쓰레기를 돌려준다.
   *
   * 증상이 고약하다. 순차 버스트는 명령 하나로 끝나므로 멀쩡히 읽히고
   * (자체 시험의 4KB 훑기도 통과했다), **랜덤 액세스만 깨진다.**
   * 그래서 앱으로 점프한 뒤 명령어 인출에서만 터졌다
   * (CFSR=0x00010000 UNDEFINSTR, PC=ExitRun0Mode 의 정상 명령어 위치).
   *
   * stm32h7-wifi 의 FSBL(실제로 XiP 점프에 성공하는 레퍼런스)도 이 조합이다.
   */
  s_command.AlternateBytes     = (1 << 5);

  s_command.DataMode          = QSPI_DATA_4_LINES;
  s_command.DummyCycles       = W25Q64JV_DUMMY_CYCLES_READ_QUAD;
  s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
  s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
  s_command.SIOOMode          = QSPI_SIOO_INST_ONLY_FIRST_CMD;

  s_mem_mapped_cfg.TimeOutActivation = QSPI_TIMEOUT_COUNTER_ENABLE;
  s_mem_mapped_cfg.TimeOutPeriod     = 0x20;

  if (HAL_QSPI_MemoryMapped(&hqspi, &s_command, &s_mem_mapped_cfg) != HAL_OK)
  {
    return QSPI_ERROR;
  }
  return QSPI_OK;
}

static uint8_t QSPI_ResetMemory(QSPI_HandleTypeDef *p_hqspi)
{
  QSPI_CommandTypeDef s_command = {0};


  if (HAL_QSPI_GetState(&hqspi) != HAL_QSPI_STATE_READY)
  {
    HAL_QSPI_Abort(p_hqspi);
  }

  /*
   * **연속 읽기 모드를 먼저 명시적으로 푼다.**
   *
   * XiP 설정이 `AlternateBytes = 0x20`(M5:M4 = 10b) 이라 앱이 도는 동안 플래시는
   * **연속 읽기 모드**에 있다. 이 상태의 플래시는 다음 트랜잭션에서 **명령
   * 바이트를 아예 기대하지 않는다** — 곧바로 주소 24비트 + 모드 8비트를
   * 기다린다.
   *
   * NRST 나 SYSRESETREQ 는 **MCU 만 리셋한다. 플래시는 리셋하지 않는다.**
   * (이 보드는 W25Q64 의 /RESET 이 배선돼 있지 않다.) 그래서 워엄 리셋 직후의
   * 플래시는 연속 읽기 모드 그대로다. 아래 `0x66` 을 보내면 플래시는 그것을
   * **주소로 읽는다.** 리셋이 안 걸린다.
   *
   * 그동안 대부분 동작한 것은 **우연이었다.** 1-line 으로 보내는 `0x66` 이
   * 8클럭을 주는데, 연속 읽기 모드의 플래시는 그 8클럭을 IO0~3 에서 4비트씩
   * 32비트(주소 24 + 모드 8)로 받는다. 그때 **IO1~3 은 아무도 몰지 않는다.**
   * 뜬 값이 모드 비트 자리에서 `10b` 가 아니면 연속 읽기가 풀리고 뒤의 `0x99`
   * 부터 정상 명령으로 먹힌다. 하필 `10b` 로 읽히면 안 풀린다.
   *
   * **뜬 핀 값에 정확성을 기대는 것은 결함이다.** 명령 없이 4-line 주소 +
   * `AlternateBytes = 0x00`(M5:M4 = 00) 을 보내 확정적으로 푼다. 플래시가 이미
   * 정상 상태면 이 트랜잭션은 유효하지 않은 명령으로 무시된다 — 무해하다.
   */
  s_command.InstructionMode   = QSPI_INSTRUCTION_NONE;
  s_command.AddressMode       = QSPI_ADDRESS_4_LINES;
  s_command.AddressSize       = QSPI_ADDRESS_24_BITS;
  s_command.Address           = 0;
  s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_4_LINES;
  s_command.AlternateBytesSize= QSPI_ALTERNATE_BYTES_8_BITS;
  s_command.AlternateBytes    = 0x00;          // M5:M4 = 00 -> 연속 읽기 해제
  s_command.DataMode          = QSPI_DATA_NONE;
  s_command.DummyCycles       = 0;
  s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
  s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
  s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

  //-- 실패해도 무시한다. 이미 정상 상태면 실패하는 것이 정상이다.
  HAL_QSPI_Command(p_hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE);

  if (HAL_QSPI_GetState(&hqspi) != HAL_QSPI_STATE_READY)
  {
    HAL_QSPI_Abort(p_hqspi);
  }

  /* Initialize the reset enable command */
  s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
  s_command.Instruction       = RESET_ENABLE_CMD;
  s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
  s_command.AddressMode       = QSPI_ADDRESS_NONE;
  s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
  s_command.DataMode          = QSPI_DATA_NONE;
  s_command.DummyCycles       = 0;
  s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
  s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
  s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

  /* Send the command */
  if (HAL_QSPI_Command(p_hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Send the reset memory command */
  s_command.Instruction = RESET_MEMORY_CMD;
  if (HAL_QSPI_Command(p_hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  s_command.InstructionMode   = QSPI_INSTRUCTION_4_LINES;
  s_command.Instruction       = RESET_ENABLE_CMD;
  /* Send the command */
  if (HAL_QSPI_Command(p_hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }


  /* Send the reset memory command */
  s_command.Instruction = RESET_MEMORY_CMD;
  if (HAL_QSPI_Command(p_hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /*
   * 소프트웨어 리셋(0x99) 후 칩이 복구될 시간을 준다.
   *
   * W25Q64JV 의 tRST 는 약 30us 다. 이 시간 안에는 어떤 명령도 받지 않는다.
   * 원본은 바로 AutoPollingMemReady 로 넘어가는데, 그 폴링이 리셋 중인 칩에서
   * 쓰레기를 읽고 "준비됨" 으로 통과해 버린다.
   *
   * 그 결과 memory-mapped 재진입 직후 **첫 읽기가 어긋난다.** 실기에서
   * 앱으로 점프한 직후 명령어 인출이 쓰레기를 가져와 UNDEFINSTR 로 죽었다
   * (CFSR=0x00010000, PC=ExitRun0Mode 의 정상 명령어 위치).
   *
   * HAL_Delay 를 쓰지 않는 이유 : 이 경로가 SysTick 이 없는 상황에서도 불릴 수
   * 있다. 넉넉하게 바쁜 대기로 돈다.
   */
  for (volatile uint32_t i = 0; i < 20000; i++)
  {
    __NOP();
  }

  /* Configure automatic polling mode to wait the memory is ready */
  if (QSPI_AutoPollingMemReady(p_hqspi, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

static uint8_t QSPI_WriteEnable(QSPI_HandleTypeDef *p_hqspi)
{
  QSPI_CommandTypeDef     s_command;
  QSPI_AutoPollingTypeDef s_config;

  /* Enable write operations */
  s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
  s_command.Instruction       = WRITE_ENABLE_CMD;
  s_command.AddressMode       = QSPI_ADDRESS_NONE;
  s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
  s_command.DataMode          = QSPI_DATA_NONE;
  s_command.DummyCycles       = 0;
  s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
  s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
  s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

  if (HAL_QSPI_Command(p_hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Configure automatic polling mode to wait for write enabling */
  s_config.Match           = W25Q64JV_SR_WREN;
  s_config.Mask            = W25Q64JV_SR_WREN;
  s_config.MatchMode       = QSPI_MATCH_MODE_AND;
  s_config.StatusBytesSize = 1;
  s_config.Interval        = 0x10;
  s_config.AutomaticStop   = QSPI_AUTOMATIC_STOP_ENABLE;

  s_command.Instruction    = READ_STATUS_REG_CMD;
  s_command.DataMode       = QSPI_DATA_1_LINE;

  if (HAL_QSPI_AutoPolling(p_hqspi, &s_command, &s_config, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

static uint8_t QSPI_AutoPollingMemReady(QSPI_HandleTypeDef *p_hqspi, uint32_t Timeout)
{
  QSPI_CommandTypeDef     s_command;
  QSPI_AutoPollingTypeDef s_config;

  /* Configure automatic polling mode to wait for memory ready */
  s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
  s_command.Instruction       = READ_STATUS_REG_CMD;
  s_command.AddressMode       = QSPI_ADDRESS_NONE;
  s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
  s_command.DataMode          = QSPI_DATA_1_LINE;
  s_command.DummyCycles       = 0;
  s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
  s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
  s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

  s_config.Match           = 0;
  s_config.Mask            = W25Q64JV_SR_WIP;
  s_config.MatchMode       = QSPI_MATCH_MODE_AND;
  s_config.StatusBytesSize = 1;
  s_config.Interval        = 0x10;
  s_config.AutomaticStop   = QSPI_AUTOMATIC_STOP_ENABLE;

  if (HAL_QSPI_AutoPolling(p_hqspi, &s_command, &s_config, Timeout) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  return QSPI_OK;
}

static uint8_t QSPI_ReadStatus(QSPI_HandleTypeDef *p_hqspi, uint8_t cmd, uint8_t *p_data)
{
  QSPI_CommandTypeDef s_command = {0};
  uint8_t reg;

  /* Initialize the read flag status register command */
  s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
  s_command.Instruction       = cmd;
  s_command.AddressMode       = QSPI_ADDRESS_NONE;
  s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
  s_command.DataMode          = QSPI_DATA_1_LINE;
  s_command.DummyCycles       = 0;
  s_command.NbData            = 1;
  s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
  s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
  s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;

  /* Configure the command */
  if (HAL_QSPI_Command(p_hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Reception of the data */
  if (HAL_QSPI_Receive(p_hqspi, &reg, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  *p_data = reg;

  return QSPI_OK;
}

static uint8_t QSPI_WriteStatus(QSPI_HandleTypeDef *p_hqspi, uint8_t cmd, uint8_t data)
{
  QSPI_CommandTypeDef s_command = {0};

  /* Initialize the program command */
  s_command.InstructionMode   = QSPI_INSTRUCTION_1_LINE;
  s_command.Instruction       = cmd;
  s_command.AddressMode       = QSPI_ADDRESS_NONE;
  s_command.AlternateByteMode = QSPI_ALTERNATE_BYTES_NONE;
  s_command.DataMode          = QSPI_DATA_1_LINE;
  s_command.DummyCycles       = 0;
  s_command.DdrMode           = QSPI_DDR_MODE_DISABLE;
  s_command.DdrHoldHalfCycle  = QSPI_DDR_HHC_ANALOG_DELAY;
  s_command.SIOOMode          = QSPI_SIOO_INST_EVERY_CMD;
  s_command.NbData            = 1;


  /* Enable write operations */
  if (QSPI_WriteEnable(p_hqspi) != QSPI_OK)
  {
    return QSPI_ERROR;
  }

  /* Configure the command */
  if (HAL_QSPI_Command(p_hqspi, &s_command, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Transmission of the data */
  if (HAL_QSPI_Transmit(p_hqspi, &data, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return QSPI_ERROR;
  }

  /* Configure automatic polling mode to wait for end of program */
  if (QSPI_AutoPollingMemReady(p_hqspi, HAL_QPSI_TIMEOUT_DEFAULT_VALUE) != QSPI_OK)
  {
    return QSPI_ERROR;
  }


  return QSPI_OK;
}

void HAL_QSPI_MspInit(QSPI_HandleTypeDef* qspiHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if (qspiHandle->Instance == QUADSPI)
  {
    __HAL_RCC_QSPI_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();

    /*
     * WeAct STM32H7XX Board V1.2 (회로도 05-QSPI.SchDoc)
     *
     *   PB2  ------> QUADSPI_CLK       (AF9)
     *   PB6  ------> QUADSPI_BK1_NCS   (AF10)
     *   PD11 ------> QUADSPI_BK1_IO0   (AF9)
     *   PD12 ------> QUADSPI_BK1_IO1   (AF9)
     *   PE2  ------> QUADSPI_BK1_IO2   (AF9)
     *   PD13 ------> QUADSPI_BK1_IO3   (AF9)
     *
     * NCS 만 AF10 이라는 점에 주의. 나머지는 전부 AF9 다.
     */
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;

    GPIO_InitStruct.Pin       = GPIO_PIN_2;
    GPIO_InitStruct.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin       = GPIO_PIN_6;
    GPIO_InitStruct.Alternate = GPIO_AF10_QUADSPI;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin       = GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13;
    GPIO_InitStruct.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

    GPIO_InitStruct.Pin       = GPIO_PIN_2;
    GPIO_InitStruct.Alternate = GPIO_AF9_QUADSPI;
    HAL_GPIO_Init(GPIOE, &GPIO_InitStruct);
  }
}

void HAL_QSPI_MspDeInit(QSPI_HandleTypeDef* qspiHandle)
{
  if (qspiHandle->Instance == QUADSPI)
  {
    __HAL_RCC_QSPI_CLK_DISABLE();

    /*
     * 반드시 MspInit 과 같은 핀 목록이어야 한다.
     *
     * 여기를 안 고쳐서 실기에서 물렸다. 원본(convex)은 BK2(PE7~PE10)를 쓰는데,
     * 이 보드는 BK1 이고 PE10 은 LCD 백라이트다. BSP_QSPI_Init() 이 맨 앞에서
     * HAL_QSPI_DeInit() 을 부르므로, gpioInit() 이 출력으로 잡아둔 PE10 이
     * 여기서 다시 아날로그로 리셋되어 백라이트가 죽었다.
     */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_2 | GPIO_PIN_6);
    HAL_GPIO_DeInit(GPIOD, GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13);
    HAL_GPIO_DeInit(GPIOE, GPIO_PIN_2);
  }
}



#if CLI_USE(HW_QSPI)
void cliCmd(cli_args_t *args)
{
  bool ret = false;
  uint32_t i;
  uint32_t addr;
  uint32_t length;
  uint8_t  data;
  uint32_t pre_time;
  bool flash_ret;



  if(args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("qspi flash addr  : 0x%X\n", 0);
    cliPrintf("qspi xip   addr  : 0x%X\n", qspiGetAddr());
    cliPrintf("qspi xip   mode  : %s\n", qspiGetXipMode() ? "True":"False");
    cliPrintf("qspi state       : ");

    switch(HAL_QSPI_GetState(&hqspi))
    {
      case HAL_QSPI_STATE_RESET:
        cliPrintf("RESET\n");
        break;
      case HAL_QSPI_STATE_READY:
        cliPrintf("READY\n");
        break;                                              
      case HAL_QSPI_STATE_BUSY:
        cliPrintf("BUSY\n");
        break;       
      case HAL_QSPI_STATE_BUSY_INDIRECT_TX:
        cliPrintf("BUSY_TX_IND\n");
        break;    
      case HAL_QSPI_STATE_BUSY_INDIRECT_RX:
        cliPrintf("BUSY_RX_IND\n");
        break;            
      case HAL_QSPI_STATE_BUSY_AUTO_POLLING:
        cliPrintf("BUSY_AUTO_POLLING\n");
        break;          
      case HAL_QSPI_STATE_BUSY_MEM_MAPPED:
        cliPrintf("BUSY_MEM_MAPPED\n");
        break;         
      case HAL_QSPI_STATE_ABORT:
        cliPrintf("ABORT\n");
        break;        
      case HAL_QSPI_STATE_ERROR:
        cliPrintf("ERROR\n");
        break;                                                                                        
      default:
        cliPrintf("UNKWNON\n");
        break;
    }
    ret = true;
  }
  
  if(args->argc == 1 && args->isStr(0, "test"))
  {
    uint8_t rx_buf[256];

    for (int i=0; i<100; i++)
    {
      if (qspiRead(0x1000*i, rx_buf, 256))
      {
        cliPrintf("%d : OK\n", i);
      }
      else
      {
        cliPrintf("%d : FAIL\n", i);
        break;
      }
    }
    ret = true;
  }    

  if (args->argc == 2 && args->isStr(0, "xip"))
  {
    bool xip_enable;

    xip_enable = args->isStr(1, "on") ? true:false;

    if (qspiSetXipMode(xip_enable))
      cliPrintf("qspiSetXipMode() : OK\n");
    else
      cliPrintf("qspiSetXipMode() : Fail\n");
    
    cliPrintf("qspi xip mode  : %s\n", qspiGetXipMode() ? "True":"False");

    ret = true;
  } 

  if (args->argc == 3 && args->isStr(0, "read"))
  {
    addr   = (uint32_t)args->getData(1);
    length = (uint32_t)args->getData(2);

    for (i=0; i<length; i++)
    {
      flash_ret = qspiRead(addr+i, &data, 1);

      if (flash_ret == true)
      {
        cliPrintf( "addr : 0x%X\t 0x%02X\n", addr+i, data);
      }
      else
      {
        cliPrintf( "addr : 0x%X\t Fail\n", addr+i);
      }
    }
    ret = true;
  }
  
  if(args->argc == 3 && args->isStr(0, "erase") == true)
  {
    addr   = (uint32_t)args->getData(1);
    length = (uint32_t)args->getData(2);

    pre_time = millis();
    flash_ret = qspiErase(addr, length);

    cliPrintf( "addr : 0x%X\t len : %d %d ms\n", addr, length, (millis()-pre_time));
    if (flash_ret)
    {
      cliPrintf("OK\n");
    }
    else
    {
      cliPrintf("FAIL\n");
    }
    ret = true;
  }

  if(args->argc == 3 && args->isStr(0, "write") == true)
  {
    uint32_t flash_data;

    addr = (uint32_t)args->getData(1);
    flash_data = (uint32_t )args->getData(2);

    pre_time = millis();
    flash_ret = qspiWrite(addr, (uint8_t *)&flash_data, 4);

    cliPrintf( "addr : 0x%X\t 0x%X %dms\n", addr, flash_data, millis()-pre_time);
    if (flash_ret)
    {
      cliPrintf("OK\n");
    }
    else
    {
      cliPrintf("FAIL\n");
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "speed-test") == true)
  {
    uint32_t buf[512/4];
    uint32_t cnt;
    uint32_t pre_time;
    uint32_t exe_time;
    uint32_t xip_addr;

    xip_addr = qspiGetAddr();
    cnt = 1024*1024 / 512;
    pre_time = millis();
    for (int i=0; i<cnt; i++)
    {
      if (qspiGetXipMode())
      {
        memcpy(buf, (void *)(xip_addr + i*512), 512);
      }
      else
      {
        if (qspiRead(i*512, (uint8_t *)buf, 512) == false)
        {
          cliPrintf("qspiRead() Fail:%d\n", i);
          break;
        }
      }
    }
    exe_time = millis()-pre_time;
    if (exe_time > 0)
    {
      cliPrintf("%d KB/sec\n", 1024 * 1000 / exe_time);
    }
    ret = true;
  }


  if (ret == false)
  {
    cliPrintf("qspi info\n");
    cliPrintf("qspi xip on:off\n");
    cliPrintf("qspi test\n");
    cliPrintf("qspi speed-test\n");
    cliPrintf("qspi read  [addr] [length]\n");
    cliPrintf("qspi erase [addr] [length]\n");
    cliPrintf("qspi write [addr] [data]\n");
  }
}
#endif

#endif