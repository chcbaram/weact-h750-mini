#!/bin/sh
#
# UART 없이 SWD 로 부팅 로그를 읽는다.
#
# log.c 는 logPrintf() 출력을 UART 로 보내는 동시에 RAM 링버퍼에도 쌓는다.
#   log_buf_boot : 부팅 로그 (logBoot(false) 전까지)
#   log_buf_list : 전체 로그 (항상)
# 이 스크립트는 ELF 심볼에서 버퍼 주소와 길이를 읽어 그대로 덤프한다.
#
# 사용법:  tools/swd/swdlog.sh [boot|list]   (기본 boot)
#
set -e
cd "$(dirname "$0")/../.."

ELF=build/weact-h750-boot.elf
WHICH=${1:-boot}
OUT=$(mktemp -t swdlog)

[ -f "$ELF" ] || { echo "$ELF 가 없다. 먼저 빌드할 것."; exit 1; }

# log_buf_t { u16 line_index; u16 buf_length; u16 buf_length_max; u16 buf_index; u8 *buf; }
HDR=$(arm-none-eabi-nm "$ELF" | awk -v n="log_buf_$WHICH" '$3==n{print "0x"$1}')
[ -n "$HDR" ] || { echo "심볼 log_buf_$WHICH 를 못 찾았다."; exit 1; }

openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
  -c "init" -c "halt" \
  -c "dump_image $OUT.hdr $HDR 12" \
  -c "resume" -c "shutdown" >/dev/null 2>&1

# 헤더에서 buf_length 와 buf 포인터를 꺼낸다 (little endian)
LEN=$(od -An -tu2 -j2 -N2 "$OUT.hdr" | tr -d ' ')
PTR=$(od -An -tx4 -j8 -N4 "$OUT.hdr" | tr -d ' ')

if [ "$LEN" -eq 0 ]; then
  echo "(로그 없음 - buf_length = 0)"
  rm -f "$OUT.hdr"; exit 0
fi

openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
  -c "init" -c "halt" \
  -c "dump_image $OUT.buf 0x$PTR $LEN" \
  -c "resume" -c "shutdown" >/dev/null 2>&1

# LC_ALL=C 가 없으면 macOS 의 tr 이 non-UTF8 바이트에서 "Illegal byte sequence" 로
# 죽는다. 폴트 로그의 초기화 안 된 .noinit 필드처럼 쓰레기 바이트가 섞이면 그
# 지점부터 로그가 통째로 잘린다. 실제로 겪었다.
LC_ALL=C tr -d '\0' < "$OUT.buf"
rm -f "$OUT.hdr" "$OUT.buf"
