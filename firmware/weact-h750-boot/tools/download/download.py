#!/usr/bin/env python3
"""부트로더 cmd 패킷 프로토콜로 펌웨어를 다운로드한다.

  python3 download.py [fw.bin] [--port PORT] [--hid] [--no-jump]

기본값은 build/stm32h5-w6300-fw.bin 이고 CDC(시리얼) 채널을 쓴다.
--hid 를 주면 WebHID 와 같은 HID 채널로 보낸다(hidapi 필요).

UF2 드래그&드롭과 같은 슬롯에 같은 태그 포맷으로 기록되므로, 슬롯 핑퐁과
롤백이 동일하게 동작한다.
"""
import argparse, glob, os, struct, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cmdproto import (DEV_MODE_BOOT, IMG_TYPE, parse_info, BOOT_CMD_INFO, BOOT_CMD_FW_JUMP,
                      CmdChannel, SerialTransport, HidTransport, TcpTransport,
                      BOOT_CMD_FW_BEGIN, BOOT_CMD_FW_ERASE, BOOT_CMD_FW_WRITE,
                      BOOT_CMD_FW_END, BOOT_CMD_FW_VERIFY, BOOT_CMD_FW_UPDATE,
                      BOOT_CMD_VERSION, parse_version)

CHUNK = 512      # cmd 최대 데이터 1024B. offset 4B 를 빼고도 여유가 있다.
CMD_BAUD = 921600   # 115200 이 아니면 무엇이든 된다. USB CDC 라 실제 속도와 무관하다.


def find_default_bin():
    here = os.path.dirname(os.path.abspath(__file__))
    cand = glob.glob(os.path.join(here, "..", "..", "build", "*.bin"))
    return os.path.normpath(cand[0]) if cand else None


def open_channel(args):
    if args.tcp:
        print(f"TCP {args.tcp}:{TcpTransport.PORT}")
        return CmdChannel(TcpTransport(args.tcp))

    if args.hid:
        return CmdChannel(HidTransport())

    import serial
    port = args.port
    if port is None:
        ports = sorted(glob.glob("/dev/cu.usbmodem*"))
        if not ports:
            sys.exit("시리얼 포트를 찾지 못했다. --port 로 지정한다.")
        port = ports[-1]
        print(f"포트 자동 선택: {port}")
    # 115200 은 "터미널" 로 예약되어 있다. 그 보율로 열면 펌웨어가 CDC 를 CLI 에
    # 넘겨버려 cmd 패킷을 아무도 읽지 않는다. 다른 보율로 열어 cmd 가 독점하게 한다.
    # (firmware cdcGetType() / cmd_task.c cmdChIsEnabled())
    ser = serial.Serial(port, CMD_BAUD, timeout=0.3)
    time.sleep(0.4)
    return CmdChannel(SerialTransport(ser))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary", nargs="?", default=None)
    ap.add_argument("--port", default=None, help="CDC 시리얼 포트")
    ap.add_argument("--hid", action="store_true", help="HID 채널 사용")
    ap.add_argument("--tcp", metavar="IP", default=None,
                    help="이더넷 OTA. 보드 IP 를 준다 (discover.py 로 찾는다)")
    ap.add_argument("--no-jump", action="store_true", help="점프 없이 기록·검증까지만")
    args = ap.parse_args()

    path = args.binary or find_default_bin()
    if not path or not os.path.exists(path):
        sys.exit("펌웨어 .bin 을 찾지 못했다. 먼저 빌드한다.")

    fw = open(path, "rb").read()
    #-- **패딩하지 않는다.** 16바이트 정렬은 STM32H5 내부 플래시(쿼드워드 단위
    #   프로그래밍) 요구였다. 여기 대상은 QSPI 이고 페이지 프로그램은 바이트
    #   단위다. 패딩하면 그 길이가 태그의 fw_size 로 들어가 .version 의
    #   firm_size 와 어긋나고, 부트로더가 stale tag 로 판정해 VER 로 강등한다.
    #   (같은 실수를 UF2 경로에서도 했다. 10 문서)

    ch = open_channel(args)

    #-- 먼저 INFO 로 지금 상대가 부트로더인지 앱인지 가른다.
    #   앱이면 FW_* 명령이 ERR_BOOT_WRONG_CMD 로 거부된다(앱에서 실행되면
    #   QUADSPI 를 indirect 로 내려 자기 명령어 인출을 끊기 때문이다).
    #   그때는 FW_UPDATE 로 부트로더 진입을 요청하고 끝낸다.
    r = ch.request(BOOT_CMD_INFO)
    info = parse_info(r["data"])
    print(f"연결됨    : {info['name']} {info['version']}  [{info['mode_str']}]")

    if info["mode"] != DEV_MODE_BOOT:
        print("  앱이다. 부트로더 진입을 요청한다.")
        try:
            ch.request(BOOT_CMD_FW_UPDATE, timeout=3.0)
        except TimeoutError:
            pass                      # 응답 직후 리셋한다. 정상이다.
        print("  리셋했다. 몇 초 뒤 다시 실행하면 부트로더로 붙는다.")
        return

    r = ch.request(BOOT_CMD_VERSION)
    v = parse_version(r["data"])
    if v["img_type"]:
        print(f"현재 FIRM : {v['name']} {v['version']}  "
              f"[{v['img_str']}] {v['fw_size']} bytes  crc 0x{v['fw_crc']:04X}")
    else:
        print("현재 FIRM : 없음")

    r = ch.request(BOOT_CMD_FW_BEGIN, struct.pack("<I", len(fw)))
    if r["err"]:
        sys.exit(f"FW_BEGIN 실패 err=0x{r['err']:04X}")
    print(f"{os.path.basename(path)}  {len(fw)/1024:.1f} KB")

    t0 = time.time()
    r = ch.request(BOOT_CMD_FW_ERASE, timeout=30.0)
    if r["err"]:
        sys.exit(f"FW_ERASE 실패 err=0x{r['err']:04X}")
    print(f"  소거   {time.time()-t0:.2f}s")

    t0 = time.time()
    for off in range(0, len(fw), CHUNK):
        r = ch.request(BOOT_CMD_FW_WRITE, struct.pack("<I", off) + fw[off:off+CHUNK])
        if r["err"]:
            sys.exit(f"\nFW_WRITE off=0x{off:X} 실패 err=0x{r['err']:04X}")
        if (off // CHUNK) % 16 == 0:
            print(f"\r  기록   {off*100//len(fw):3d}%", end="", flush=True)
    dt = time.time() - t0
    print(f"\r  기록   100%  {dt:.2f}s  ({len(fw)/dt/1024:.0f} KB/s)")

    #-- 태그는 FW_END 가 쓴다. 이게 커밋 마커라, 여기까지 와야 부트로더가 점프한다.
    r = ch.request(BOOT_CMD_FW_END, timeout=15.0)
    if r["err"]:
        sys.exit(f"FW_END 실패 err=0x{r['err']:04X}")

    r = ch.request(BOOT_CMD_FW_VERIFY, timeout=15.0)
    if r["err"]:
        sys.exit(f"FW_VERIFY 실패 err=0x{r['err']:04X}")
    img = r["data"][0] if r["data"] else 0
    print(f"  검증   {IMG_TYPE[img] if img < len(IMG_TYPE) else '?'}")

    if args.no_jump:
        print("  (--no-jump : 점프하지 않았다. 리셋하면 부팅한다)")
        return

    #-- 부트로더는 응답을 보낸 직후 점프한다. USB 가 사라지므로 호스트가 응답을
    #   못 받을 수 있는데, 명령이 받아들여진 시점에 점프는 이미 확정이다.
    try:
        ch.request(BOOT_CMD_FW_JUMP, timeout=3.0)
    except TimeoutError:
        pass
    print("  점프   요청 완료.")


if __name__ == "__main__":
    main()
