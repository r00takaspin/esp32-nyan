#!/usr/bin/env python3
"""Serial bridge between the DSH Agent Buddy plugin and the ESP32 board.

stdin  : one JSON object per line (commands from the plugin)
           {"cmd":"state","state":"idle|think|tool|approve|error","text":"...","id":"..."}
stdout : one JSON object per line (events from the board)
           {"evt":"hello"}
           {"evt":"ack","state":"idle"}
           {"evt":"approve","id":"call_1","ok":1}
           {"evt":"touch","x":12,"y":140}

The board protocol is the small pipe format documented in nyancat.ino.
"""
import argparse
import atexit
import json
import os
import sys
import threading
import time

LOCK_PATH = "/tmp/nyan-buddy-bridge.lock"


def acquire_lock():
    """Only one bridge may read the serial port; extras exit immediately."""
    try:
        if os.path.exists(LOCK_PATH):
            try:
                pid = int(open(LOCK_PATH).read().strip() or 0)
            except Exception:
                pid = 0
            if pid and pid != os.getpid():
                try:
                    os.kill(pid, 0)   # alive?
                    return False
                except OSError:
                    pass              # stale lock file
        open(LOCK_PATH, "w").write(str(os.getpid()))
        return True
    except Exception:
        return True


def release_lock():
    try:
        if os.path.exists(LOCK_PATH):
            pid = int(open(LOCK_PATH).read().strip() or 0)
            if pid == os.getpid():
                os.remove(LOCK_PATH)
    except Exception:
        pass

try:
    import serial
except ImportError:
    print(json.dumps({"evt": "error", "msg": "pyserial is not installed"}), flush=True)
    sys.exit(1)


def parse_device(line: str):
    p = line.split("|")
    if not p:
        return None
    if p[0] == "E" and len(p) >= 2:
        if p[1] == "hello":
            return {"evt": "hello"}
        if p[1] == "approve" and len(p) >= 4:
            try:
                ok = int(p[3] or 0)
            except ValueError:
                ok = 0
            return {"evt": "approve", "id": p[2], "ok": ok}
    if p[0] == "A" and len(p) >= 2:
        return {"evt": "ack", "state": p[1]}
    if p[0] == "T" and len(p) >= 3:
        try:
            return {"evt": "touch", "x": int(p[1]), "y": int(p[2])}
        except ValueError:
            return None
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/cu.usbmodem1101")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--reconnect", action="store_true")
    args = ap.parse_args()

    if not acquire_lock():
        print(json.dumps({"evt": "error", "msg": "another bridge already owns the port"}), flush=True)
        sys.exit(2)
    atexit.register(release_lock)

    ser = None
    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.2)
    except Exception as e:
        print(json.dumps({"evt": "error", "msg": "open failed: %s" % e}), flush=True)
        sys.exit(1)

    stop = threading.Event()

    def reader():
        buf = b""
        while not stop.is_set():
            try:
                data = ser.read(256)
            except Exception as e:
                print(json.dumps({"evt": "error", "msg": "read: %s" % e}), flush=True)
                # The USB device re-enumerated (cable replug). Exit so the plugin
                # spawns a fresh bridge with a valid handle.
                os._exit(3)
            if not data:
                continue
            buf += data
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                s = raw.decode("utf-8", "replace").strip()
                if not s:
                    continue
                evt = parse_device(s)
                if evt:
                    print(json.dumps(evt), flush=True)

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    def send_state(cmd):
        st = cmd.get("state")
        if not st:
            return
        text = cmd.get("text", "") or ""
        if st == "approve":
            line = "S|approve|%s|%s\n" % (cmd.get("id", "call"), text)
        elif st in ("tool", "error"):
            line = "S|%s|%s\n" % (st, text)
        else:
            line = "S|%s\n" % st
        try:
            ser.write(line.encode("utf-8"))
        except Exception as e:
            print(json.dumps({"evt": "error", "msg": "write: %s" % e}), flush=True)
            os._exit(3)   # stale handle after replug: let the plugin respawn

    for raw in sys.stdin:
        raw = raw.strip()
        if not raw:
            continue
        try:
            cmd = json.loads(raw)
        except json.JSONDecodeError:
            continue
        if cmd.get("cmd") == "state" or "state" in cmd:
            send_state(cmd)

    stop.set()
    time.sleep(0.1)
    try:
        ser.close()
    except Exception:
        pass


if __name__ == "__main__":
    main()
