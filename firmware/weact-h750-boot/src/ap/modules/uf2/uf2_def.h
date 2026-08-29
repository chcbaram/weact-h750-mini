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
#define UF2_MAX_FW_SIZE           (2 * 1024 * 1024)   // 앱 상한 2MB (QSPI 는 8MB)

#define UF2_DISK_BLOCK_NUM        32768               // 16MB
#define UF2_DISK_BLOCK_SIZE       512

#define UF2_PAYLOAD_SIZE          256
#define MAX_BLOCKS                (UF2_MAX_FW_SIZE / UF2_PAYLOAD_SIZE + 100)

//-- QSPI 소거 단위는 4KB(subsector) 다. 04 문서 참고.
#define UF2_ERASE_SECTOR_SIZE     4096
#define UF2_ERASE_SECTOR_MAX      (UF2_MAX_FW_SIZE / UF2_ERASE_SECTOR_SIZE)


//-- 상태머신 대기 시간
#define UF2_COMPLETE_WAIT_MS      1000     // 호스트의 FAT/디렉터리 기록 + SYNC 대기
#define UF2_JUMP_WAIT_MS          300


#endif
