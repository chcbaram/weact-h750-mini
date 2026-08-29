#ifndef UF2_DEF_H_
#define UF2_DEF_H_


#include "ap_def.h"


#define UF2_PRODUCT_NAME          _DEF_BOARD_NAME
#define UF2_BOARD_ID              "WEACT-H750"
#define UF2_VOLUME_LABEL          "H750BOOT"


/*
 * 두 상수를 반드시 분리한다.
 *
 *   UF2_MAX_FW_SIZE    : 펌웨어 크기 상한. writtenMask 와 경계 검사의 기준.
 *   UF2_DISK_BLOCK_NUM : FAT16 지오메트리 기준. 손으로 만든 부트섹터가 총섹터
 *                        0x8000, FAT당 128섹터로 하드코딩되어 있으므로 이 값을
 *                        펌웨어 크기에 연동시키면 호스트가 디스크를 못 읽는다.
 *
 * FAT12 가 아니라 **FAT16** 인 이유 : FAT12 는 클러스터 4085개가 한계라
 * 512B 섹터로 약 2MB 까지밖에 안 된다. UF2 파일은 512B 블록당 페이로드가 256B 라
 * 바이너리의 약 2배가 되므로, 앱이 커지면 호스트가 "용량 부족"으로 복사를 거부한다.
 */
#define UF2_DISK_BLOCK_NUM        32768               // 16MB
#define UF2_DISK_BLOCK_SIZE       512
#define UF2_PAYLOAD_SIZE          256

//-- 데이터 영역이 시작하는 LBA. uf2_disk.c 의 부트섹터와 반드시 일치해야 한다.
//     부트섹터 1 + FAT x2 (128x2=256) + 루트디렉터리 32 = 289
#define UF2_DISK_DATA_LBA         289

/*
 * UF2 로 올릴 수 있는 펌웨어 상한.
 *
 * 두 가지 한계 중 **작은 쪽**이다.
 *
 *   (a) QSPI 앱 영역     : FLASH_SIZE_FIRM - FLASH_SIZE_TAG = 8MB - 4KB = 8,384,512 B
 *   (b) FAT16 디스크 용량: .uf2 는 512B 블록마다 페이로드가 256B 라 **바이너리의 2배**다.
 *                          (32768 - 289 - 1[README]) x 256 = 8,314,368 B
 *
 * 지금은 (b)가 작으므로 (b)로 정한다. 8MB 를 꽉 채우려면 부트섹터 지오메트리를
 * 다시 만들어야 한다(총섹터/FAT크기/LBA 오프셋). 남는 70KB 를 위해 손으로 만든
 * FAT 를 건드릴 이유가 없다. CDC/HID/SWD 경로는 이 제한을 받지 않는다.
 *
 * 이전에는 2MB 로 박아두었다. 하드웨어 제약이 아니라 참조 프로젝트에서 그대로
 * 가져온 값이었다. 상한을 올리는 비용은 아래 두 비트맵의 RAM 뿐인데,
 * 이 보드는 AXI SRAM 512KB 중 6.6% 만 쓴다.
 *   writtenMask : MAX_BLOCKS/8+1        1,037 B -> 4,073 B
 *   erase_map   : ERASE_SECTOR_MAX/8       64 B ->   254 B
 */
#define UF2_MAX_FW_SIZE           ((UF2_DISK_BLOCK_NUM - UF2_DISK_DATA_LBA - 1) * UF2_PAYLOAD_SIZE)

#define MAX_BLOCKS                (UF2_MAX_FW_SIZE / UF2_PAYLOAD_SIZE + 100)

//-- QSPI 소거 단위는 4KB(subsector) 다. 04 문서 참고.
//   MAX_FW_SIZE 가 4KB 배수가 아니므로 **올림**해야 한다. 내림하면 마지막 섹터를
//   덮는 비트가 erase_map 밖으로 나간다.
#define UF2_ERASE_SECTOR_SIZE     4096
#define UF2_ERASE_SECTOR_MAX      ((UF2_MAX_FW_SIZE + UF2_ERASE_SECTOR_SIZE - 1) / UF2_ERASE_SECTOR_SIZE)


//-- 상태머신 대기 시간
#define UF2_COMPLETE_WAIT_MS      1000     // 호스트의 FAT/디렉터리 기록 + SYNC 대기
#define UF2_JUMP_WAIT_MS          300


#endif
