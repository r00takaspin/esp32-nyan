#!/usr/bin/env python3
"""Bridge between the DSH Agent Buddy plugin and the ESP32 board.

Transport: the USB CDC port when the cable is in (pyserial), otherwise
Bluetooth through buddy-ble, the CoreBluetooth helper shared with the Claude
Code plugin (../claude-agent-buddy/ble). While on Bluetooth the bridge exits
as soon as a USB port shows up, and the plugin respawns it on USB.
The board is pinged every 2.5 s so the plugin can tell a live link from a
dead one ({"evt":"info"} on every reply).

stdin  : one JSON object per line (commands from the plugin)
           {"cmd":"state","state":"idle|think|tool|approve|error","text":"...","id":"..."}
stdout : one JSON object per line (events from the board)
           {"evt":"hello"}
           {"evt":"ack","state":"idle"}
           {"evt":"approve","id":"call_1","ok":1}
           {"evt":"touch","x":12,"y":140}
           {"evt":"info","fw":"2.0",...}     (ping reply)
           {"evt":"link","via":"usb|bluetooth","state":"connected|disconnected"}

The board protocol is the small pipe format documented in nyancat.ino.
"""
import argparse
import atexit
import glob
import json
import os
import select
import subprocess
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
    serial = None   # USB needs pyserial; Bluetooth does not

PING_EVERY = 2.5


def emit(obj):
    print(json.dumps(obj), flush=True)


def usb_ports():
    return sorted(glob.glob("/dev/cu.usbmodem*")) + sorted(glob.glob("/dev/ttyACM*"))


class UsbLink:
    via = "usb"

    def __init__(self, path, baud):
        self.ser = serial.Serial(path, baud, timeout=0.2)

    def read(self):
        return self.ser.read(256)

    def write(self, data):
        self.ser.write(data)

    def close(self):
        self.ser.close()


class BleLink:
    """buddy-ble subprocess: board lines on stdout, "#..." status lines."""
    via = "bluetooth"

    def __init__(self, exe):
        self.proc = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, bufsize=0)

    def read(self):
        r, _, _ = select.select([self.proc.stdout], [], [], 0.2)
        if not r:
            return b""
        data = os.read(self.proc.stdout.fileno(), 4096)
        if not data:
            raise OSError("buddy-ble exited")
        return data

    def write(self, data):
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def close(self):
        try:
            self.proc.terminate()
        except Exception:
            pass


def ble_helper():
    """Build / locate buddy-ble via the Claude Code plugin's helper code."""
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.join(here, "..", "claude-agent-buddy", "scripts"))
    try:
        import buddylib  # noqa: E402
    except ImportError:
        return None
    return buddylib.ble_helper()


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
    if p[0] == "I":
        info = {"evt": "info"}
        for kv in p[1:]:
            if "=" in kv:
                k, v = kv.split("=", 1)
                info[k] = v
        return info
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

    # The configured port first, then whatever usbmodem is there (macOS
    # renames it, e.g. usbmodem1101 -> usbmodem101), then Bluetooth.
    ports = [args.port] if os.path.exists(args.port) else usb_ports()
    ser = None
    if ports and serial is not None:
        try:
            ser = UsbLink(ports[0], args.baud)
        except Exception as e:
            emit({"evt": "error", "msg": "open %s failed: %s" % (ports[0], e)})
    if ser is None:
        exe = ble_helper()
        if not exe:
            emit({"evt": "error", "msg": "no USB port and no Bluetooth helper (swiftc missing?)"})
            sys.exit(1)
        ser = BleLink(exe)
    emit({"evt": "link", "via": ser.via, "state": "open"})

    stop = threading.Event()
    wlock = threading.Lock()

    def write_line(line):
        with wlock:
            ser.write(line.encode("utf-8"))

    def reader():
        buf = b""
        while not stop.is_set():
            try:
                data = ser.read()
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
                if s.startswith("#"):          # buddy-ble status line
                    st = s[1:].split(" ", 1)[0]
                    if st in ("connected", "disconnected"):
                        emit({"evt": "link", "via": "bluetooth", "state": st})
                    if st == "connected":
                        try:
                            write_line("S|host|deepseek\nS|ping\n")
                        except Exception:
                            pass
                    continue
                evt = parse_device(s)
                if evt:
                    print(json.dumps(evt), flush=True)

    t = threading.Thread(target=reader, daemon=True)
    t.start()

    def pinger():
        while not stop.is_set():
            try:
                write_line("S|ping\n")
            except Exception as e:
                emit({"evt": "error", "msg": "write: %s" % e})
                os._exit(3)
            # On Bluetooth, hand over to USB as soon as the cable is plugged in.
            if ser.via == "bluetooth" and serial is not None and usb_ports():
                emit({"evt": "link", "via": "bluetooth", "state": "leaving for usb"})
                ser.close()
                os._exit(3)
            stop.wait(PING_EVERY)

    try:
        write_line("S|host|deepseek\n")
    except Exception:
        pass
    threading.Thread(target=pinger, daemon=True).start()

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
            write_line(line)
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
