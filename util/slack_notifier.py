import os, sys, json
import requests

SLACK_WEBHOOK_URL = os.environ.get("SLACK_WEBHOOK_URL")
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


def parse_headers(line):
    # "ver:3 server:supervisor serial:16 pool:slack_notifier poolserial:1 eventname:PROCESS_STATE_EXITED len:192"
    parts = [kv.split(":", 1) for kv in line.strip().split()]
    return {k: v for k, v in parts if len(k) and len(v)}

def main():
    # supervisor eventlistener 프로토콜
    while True:
        # READY 신호
        sys.stdout.write("READY\n")
        sys.stdout.flush()

        header = sys.stdin.readline()
        if not header:
            break
        hdrs = parse_headers(header)
        length = int(hdrs.get("len", "0"))

        payload = sys.stdin.read(length) if length > 0 else ""
        # payload는 "processname:..., groupname:..., from_state:..., pid:..." 와 data 부분으로 구성
        # header line 다음 줄에 data(=stdout/stderr tail 등)가 붙을 수 있어 split 처리
        # 표준 포맷: <kv pairs>\n<data...>
        if "\n" in payload:
            kvline, _data = payload.split("\n", 1)
        else:
            kvline, _data = payload, ""

        fields = parse_headers(kvline.replace(" ", "\n"))  # 공백→개행으로 바꿔 재사용
        event = hdrs.get("eventname", "UNKNOWN")
        pname = fields.get("processname", "?")
        gname = fields.get("groupname", "?")
        fstate = fields.get("from_state", "?")
        pid = fields.get("pid", "?")
        expected = fields.get("expected", "0")  # EXITED일 때 0/1

        # 관심 이벤트만 슬랙 전송
        if event in ("PROCESS_STATE_EXITED", "PROCESS_STATE_FATAL", "PROCESS_STATE_BACKOFF", "PROCESS_STATE_STOPPED"):
            # 메시지 구성
            emoji = {
                "PROCESS_STATE_EXITED":  "🔁" if expected == "1" else "💥",
                "PROCESS_STATE_FATAL":   "🛑",
                "PROCESS_STATE_BACKOFF": "⚠️",
                "PROCESS_STATE_STOPPED": "⏹️",
            }.get(event, "ℹ️")

            text = (f"{emoji} *{event}*  "
                    f"`{gname}:{pname}` pid={pid} from_state={fstate} expected={expected}")
            send_slack(text)

        # 처리 결과 통보 (필수)
        sys.stdout.write("RESULT 2\nOK")
        sys.stdout.flush()

if __name__ == "__main__":
    main()