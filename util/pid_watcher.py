#!/usr/bin/env python3
# pid_watch_service.py
import os
import sys
import time
import stat
import json
import errno
import signal
import select
import threading
from typing import Dict, Set

try:
    import requests
except ImportError:
    print("Install requests: pip install requests", file=sys.stderr)
    sys.exit(1)

FIFO_PATH = os.environ.get("PID_WATCH_FIFO", "/run/pidwatch/pid_watch.fifo")
SLACK_WEBHOOK_URL = os.environ.get("SLACK_WEBHOOK_URL")
CHECK_INTERVAL_SEC = int(os.environ.get("PID_WATCH_INTERVAL_SEC", "600"))
PID_STALE_SEC = int(os.environ.get("PID_WATCH_STALE_SEC", "1200"))

last_seen: Dict[int, float] = {}
dead_notified: Set[int] = set()


def ensure_fifo(path: str):
    try:
        st = os.stat(path)
        if not stat.S_ISFIFO(st.st_mode):
            raise RuntimeError(f"{path} exists and is not a FIFO.")
    except FileNotFoundError:
        os.umask(0)
        os.mkfifo(path, 0o660)
        print(f'make new fifo path:{path}', file=sys.stderr)


def open_fifo_nb(path: str):
    # 논블로킹 read FD + EOF 방지용 더미 write FD
    rfd = os.open(path, os.O_RDONLY | os.O_NONBLOCK)
    wfd = os.open(path, os.O_WRONLY | os.O_NONBLOCK)
    return rfd, wfd


def send_slack(text: str):
    if not SLACK_WEBHOOK_URL:
        print(f"[WARN] SLACK_WEBHOOK_URL not set. Message: {text}", file=sys.stderr)
        return
    try:
        resp = requests.post(SLACK_WEBHOOK_URL, json={"text": text}, timeout=5)
        if resp.status_code >= 300:
            print(f"[ERROR] Slack webhook failed: {resp.status_code} {resp.text}", file=sys.stderr)
    except Exception as e:
        print(f"[ERROR] Slack webhook exception: {e}", file=sys.stderr)


def pid_alive(pid: int) -> bool:
    try:
        print(f'pid check pid:{pid}', file=sys.stderr)
        os.kill(pid, 0)
        return True
    except ProcessLookupError:
        print(f'process lookup error :{pid}')
        return False
    except PermissionError:
        print(f'permission denied :{pid}')
        # 프로세스는 존재하나 권한 부족 → 살아있다고 간주
        return True


def parse_lines(buf: bytearray):
    """\n 기준으로 pid 라인을 파싱해서 last_seen 갱신"""
    while True:
        nl = buf.find(b'\n')
        if nl == -1:
            break
        line = bytes(buf[:nl]).strip()
        del buf[:nl + 1]
        if not line:
            continue
        try:
            pid = int(line)
            if pid > 0:
                last_seen[pid] = time.time()
                # 새로 들어온 PID면 사망 알림 중복 방지 목록에서도 제거
                if pid in dead_notified:
                    dead_notified.discard(pid)
                print(f"[INFO] seen pid {pid}", file=sys.stderr)
        except ValueError:
            print(f"[WARN] invalid line: {line!r}", file=sys.stderr)


def periodic_check(stop_event: threading.Event):
    """10분마다 프로세스 생존 확인 & 알림"""
    while not stop_event.is_set():
        now = time.time()
        to_check = list(last_seen.items())
        for pid, ts in to_check:
            # 너무 오래 갱신이 없으면 목록에서 제거(옵션)
            if now - ts > PID_STALE_SEC and pid not in dead_notified:
                # 먼저 살아있는지 확인
                if not pid_alive(pid):
                    dead_notified.add(pid)
                    send_slack(f":rotating_light: PID {pid} is *dead* (last seen {int(now - ts)}s ago).")
                else:
                    # 살아있지만 오래 갱신 없음: 필요 시 별도 알림 가능
                    pass
        stop_event.wait(CHECK_INTERVAL_SEC)


def main():
    if not hasattr(select, "epoll"):
        print("epoll not supported on this platform", file=sys.stderr)
        sys.exit(1)

    ensure_fifo(FIFO_PATH)
    rfd, wfd = open_fifo_nb(FIFO_PATH)

    ep = select.epoll()
    # EPOLLHUP: writer 전부 닫힘 등 → rfd 재오픈 필요
    ep.register(rfd, select.EPOLLIN | select.EPOLLHUP | select.EPOLLERR)

    buf = bytearray()
    stop_evt = threading.Event()
    t = threading.Thread(target=periodic_check, args=(stop_evt,), daemon=True)
    t.start()

    print(f"[INFO] pid-watch(epoll) start FIFO={FIFO_PATH}", file=sys.stderr)

    try:
        while True:
            for fd, ev in ep.poll(5.0):  # timeout=5s
                if fd != rfd:
                    continue
                if ev & (select.EPOLLERR | select.EPOLLHUP):
                    # 안전 재오픈: 등록 해제 → 닫기 → 다시 열고 등록
                    try:
                        ep.unregister(rfd)
                    except Exception:
                        pass
                    try:
                        os.close(rfd)
                    except Exception:
                        pass
                    # dummy writer가 있어도 HUP이 올 수 있으니 방어적으로 재오픈
                    rfd, _w2 = open_fifo_nb(FIFO_PATH)
                    # 기존 wfd는 유지(EOF 방지). _w2는 무시(읽기 재오픈만 하면 됨).
                    ep.register(rfd, select.EPOLLIN | select.EPOLLHUP | select.EPOLLERR)
                    continue

                if ev & select.EPOLLIN:
                    while True:
                        try:
                            chunk = os.read(rfd, 4096)
                            if not chunk:
                                # EOF → 다음 poll에서 HUP 처리될 것
                                break
                            buf.extend(chunk)
                            parse_lines(buf)
                        except OSError as e:
                            if e.errno in (errno.EAGAIN, errno.EWOULDBLOCK, errno.EINTR):
                                break
                            raise
    except KeyboardInterrupt:
        print("[INFO] stopping...", file=sys.stderr)
    finally:
        stop_evt.set()
        t.join(timeout=2.0)
        try:
            ep.unregister(rfd)
        except Exception:
            pass
        try:
            ep.close()
        except Exception:
            pass
        try:
            os.close(rfd)
        except Exception:
            pass
        try:
            os.close(wfd)
        except Exception:
            pass


if __name__ == "__main__":
    main()
