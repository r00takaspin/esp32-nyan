#!/usr/bin/env python3
"""Agent Buddy daemon: owns the USB serial port and serves a tiny local HTTP API.

Claude Code hooks are short-lived processes, but the board needs one long-lived
owner of /dev/cu.usbmodem*. This daemon is started on demand by the hooks
(buddylib.ensure_daemon) and:

  * keeps the serial link open, re-opens it after a replug, and pings the board
    every 2.5 s - "connected" means "answered a ping recently", never a flag
    that can get stuck (the DSH plugin's deviceAlive problem);
  * merges the state of all running Claude Code sessions into one display state;
  * queues permission prompts: one at a time on the board, each with a timeout,
    cancelled on the board when it expires;
  * shares /tmp/nyan-buddy-bridge.lock with the DeepSeek bridge and the flashing
    recipe, so only one program talks to the board at a time.

Transport: the USB CDC port when the cable is in, otherwise Bluetooth through
the buddy-ble helper (same text protocol over the board's BLE link service).
The cable wins: while on Bluetooth the daemon switches as soon as USB shows up.

HTTP (127.0.0.1:<http_port>, JSON):
  GET  /status
  POST /event    {session, event, state, text}
  POST /approve  {session, text}          -> {decision: allow|deny|timeout|offline|disabled|busy}
  POST /show     {text, mood, hold}
  POST /config   {key, value}
  POST /device   {cmd: unpair|wizard|reboot|info}
  POST /shutdown
"""
import glob
import json
import os
import select
import signal
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import buddylib as bl  # noqa: E402

PING_EVERY = 2.5
ALIVE_WINDOW = 7.0
SESSION_STALE = 30 * 60
IDLE_EXIT = 3 * 60 * 60


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


# ------------------------------------------------------------------ serial
class Port:
    """pyserial when available, otherwise raw termios (macOS / Linux)."""

    def __init__(self, path, baud=115200):
        self.path = path
        self.ser = None
        self.fd = None
        try:
            import serial  # type: ignore
            self.ser = serial.Serial(path, baud, timeout=0.2)
            return
        except ImportError:
            pass
        import termios
        fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        a = termios.tcgetattr(fd)
        a[0] = 0
        a[1] = 0
        a[2] = termios.CS8 | termios.CLOCAL | termios.CREAD
        a[3] = 0
        a[4] = a[5] = termios.B115200
        a[6][termios.VMIN] = 0
        a[6][termios.VTIME] = 1
        termios.tcsetattr(fd, termios.TCSANOW, a)
        self.fd = fd

    def read(self):
        if self.ser:
            return self.ser.read(256)
        import select
        r, _, _ = select.select([self.fd], [], [], 0.2)
        if not r:
            return b""
        data = os.read(self.fd, 512)
        if not data:
            raise OSError("device closed")
        return data

    def write(self, data):
        if self.ser:
            self.ser.write(data)
            return
        view = memoryview(data)
        deadline = time.time() + 1.0
        while view:
            try:
                n = os.write(self.fd, view)
                view = view[n:]
            except BlockingIOError:
                if time.time() > deadline:
                    raise OSError("write timeout")
                time.sleep(0.01)

    def close(self):
        try:
            if self.ser:
                self.ser.close()
            elif self.fd is not None:
                os.close(self.fd)
        except Exception:
            pass


class BlePort:
    """The board over Bluetooth: a buddy-ble subprocess speaking lines on stdio."""

    path = "bluetooth"

    def __init__(self, exe):
        self.proc = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, bufsize=0)
        self.linked = False

    def read(self):
        r, _, _ = select.select([self.proc.stdout], [], [], 0.2)
        if not r:
            return b""
        data = os.read(self.proc.stdout.fileno(), 4096)
        if not data:
            raise OSError("buddy-ble exited (%s)" % self.proc.poll())
        return data

    def write(self, data):
        try:
            self.proc.stdin.write(data)
            self.proc.stdin.flush()
        except (BrokenPipeError, ValueError) as e:
            raise OSError("buddy-ble: %s" % e)

    def close(self):
        try:
            self.proc.terminate()
            self.proc.wait(timeout=2)
        except Exception:
            try:
                self.proc.kill()
            except Exception:
                pass


def candidate_ports(cfg):
    if cfg["port"] and cfg["port"] != "auto":
        return [cfg["port"]]
    pats = ["/dev/cu.usbmodem*", "/dev/ttyACM*"]
    out = []
    for p in pats:
        out.extend(sorted(glob.glob(p)))
    return out


def lock_owner():
    """PID holding the shared lock if it is alive and not us, else None."""
    try:
        pid = int(open(bl.LOCK_PATH).read().strip() or 0)
    except (OSError, ValueError):
        return None
    if not pid or pid == os.getpid():
        return None
    try:
        os.kill(pid, 0)
        return pid
    except OSError:
        return None


def take_lock():
    try:
        with open(bl.LOCK_PATH, "w") as f:
            f.write(str(os.getpid()))
    except OSError:
        pass


def release_lock():
    try:
        if int(open(bl.LOCK_PATH).read().strip() or 0) == os.getpid():
            os.remove(bl.LOCK_PATH)
    except (OSError, ValueError):
        pass


# ------------------------------------------------------------------ daemon
class Buddy:
    def __init__(self):
        self.cfg = bl.load_config()
        self.lock = threading.RLock()
        self.port = None
        self.port_path = None
        self.port_state = "missing"
        self.error = ""
        self.lock_pid = None
        self.last_rx = 0.0
        self.last_info = 0.0
        self.device = {}
        self.sessions = {}              # id -> {state, text, ts}
        self.manual = None              # (state, text, until)
        self.sent = None                # last (state, text) put on the board
        self.approve_gate = threading.Lock()   # one prompt on the board at a time
        self.pending = None             # {"id", "event", "result"}
        self.seq = 0
        self.last_request = time.time()
        self.running = True

    # ---------------- link ----------------
    @property
    def connected(self):
        return self.port is not None and time.time() - self.last_info < ALIVE_WINDOW

    def send(self, line):
        with self.lock:
            if not self.port:
                return False
            try:
                self.port.write((line + "\n").encode("ascii", "replace"))
                return True
            except Exception as e:
                log("write failed:", e)
                self._drop("write: %s" % e)
                return False

    def _drop(self, why):
        if self.port:
            self.port.close()
        self.port = None
        self.sent = None
        self.error = why
        self.port_state = "missing"
        release_lock()

    def _open(self):
        owner = lock_owner()
        if owner:
            self.port_state, self.lock_pid = "busy", owner
            return
        self.lock_pid = None
        ports = candidate_ports(self.cfg)
        if not ports:
            self._open_ble()
            return
        for i, path in enumerate(ports):
            try:
                take_lock()
                p = Port(path)
            except Exception as e:
                release_lock()
                self.error = "open %s: %s" % (path, e)
                self.port_state = "error"
                continue
            # Several usbmodem devices may be plugged in: keep the one that
            # answers a ping like an Agent Buddy (fall back to the last one).
            if len(ports) > 1 and not self._probe(p) and i < len(ports) - 1:
                p.close()
                release_lock()
                continue
            with self.lock:
                self.port, self.port_path = p, path
                self.port_state = "open"
                self.error = ""
                self.sent = None
            log("opened", path)
            self.send("S|host|%s" % bl.device_text(self.cfg.get("host_name", "claude"), 14))
            self.send("S|ping")
            self.refresh(force=True)
            return

    def _open_ble(self):
        if self.cfg.get("ble", "auto") == "off":
            self.port_state = "missing"
            return
        exe = bl.ble_helper(log)
        if not exe:
            self.port_state = "missing"
            return
        try:
            p = BlePort(exe)
        except OSError as e:
            self.error = "buddy-ble: %s" % e
            self.port_state = "missing"
            return
        with self.lock:
            self.port, self.port_path = p, p.path
            self.port_state = "open"
            self.error = ""
            self.sent = None
        log("bluetooth link helper started")
        # Queued by the helper until the board is attached.
        self.send("S|host|%s" % bl.device_text(self.cfg.get("host_name", "claude"), 14))
        self.send("S|ping")
        self.refresh(force=True)

    def _probe(self, p, wait=1.5):
        try:
            p.write(b"S|ping\n")
            buf, end = b"", time.time() + wait
            while time.time() < end:
                buf += p.read()
                if b"I|fw=" in buf or b"E|hello" in buf or b"AgentBuddy" in buf:
                    return True
        except Exception:
            pass
        return False

    def serial_loop(self):
        buf = b""
        while self.running:
            if not self.port:
                self._open()
                if not self.port:
                    time.sleep(2.0)
                    continue
                buf = b""
            try:
                data = self.port.read()
            except Exception as e:
                log("read failed:", e)
                with self.lock:
                    self._drop("read: %s" % e)
                time.sleep(1.0)
                continue
            if not data:
                continue
            buf += data
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.decode("utf-8", "replace").strip()
                if line:
                    self.on_line(line)

    def ping_loop(self):
        while self.running:
            time.sleep(PING_EVERY)
            if isinstance(self.port, BlePort) and candidate_ports(self.cfg) and not lock_owner():
                log("USB cable plugged in, leaving bluetooth")
                with self.lock:
                    self._drop("switching to USB")
                continue
            if self.port:
                self.send("S|ping")
                # Answers to pings prove the link; an open port that never
                # answers is either dead or old firmware.
                if self.last_rx and not self.last_info and time.time() - self.last_rx < ALIVE_WINDOW:
                    self.port_state = "legacy"
            self.expire()
            if time.time() - self.last_request > IDLE_EXIT and not self.sessions:
                log("idle, exiting")
                self.shutdown()

    def on_line(self, line):
        if line.startswith("#"):              # buddy-ble status: "#connected <name>" etc.
            log("ble:", line[1:])
            if line.startswith("#connected"):
                with self.lock:
                    self.sent = None
                self.send("S|host|%s" % bl.device_text(self.cfg.get("host_name", "claude"), 14))
                self.send("S|ping")
                self.refresh(force=True)
            return
        self.last_rx = time.time()
        p = line.split("|")
        if p[0] == "I":
            info = {}
            for kv in p[1:]:
                if "=" in kv:
                    k, v = kv.split("=", 1)
                    info[k] = v
            with self.lock:
                self.device = info
                self.last_info = time.time()
                if self.port_state != "open":
                    self.port_state = "open"
        elif p[0] == "E" and len(p) >= 2 and p[1] == "hello":
            log("board says hello (reset)")
            with self.lock:
                self.sent = None
            self.send("S|host|%s" % bl.device_text(self.cfg.get("host_name", "claude"), 14))
            self.refresh(force=True)
            pend = self.pending
            if pend:   # the board rebooted mid-prompt: show it again
                self.send("S|approve|%s|%s" % (pend["id"], pend["text"]))
        elif p[0] == "E" and len(p) >= 4 and p[1] == "approve":
            pend = self.pending
            if pend and pend["id"] == p[2]:
                pend["result"] = "allow" if p[3].strip() == "1" else "deny"
                pend["event"].set()

    # ---------------- display state ----------------
    def expire(self):
        now = time.time()
        with self.lock:
            for sid in [s for s, v in self.sessions.items() if now - v["ts"] > SESSION_STALE]:
                del self.sessions[sid]
        self.refresh()

    def wanted(self):
        now = time.time()
        if self.pending:
            return None                       # the prompt owns the screen
        if self.manual and now < self.manual[2]:
            return self.manual[0], self.manual[1]
        busy = [v for v in self.sessions.values() if v["state"] in ("think", "tool")]
        if busy:
            v = max(busy, key=lambda x: x["ts"])
            return v["state"], v["text"]
        return "idle", ""

    def refresh(self, force=False):
        with self.lock:
            want = self.wanted()
            if want is None or (want == self.sent and not force):
                return
            state, text = want
            line = "S|%s|%s" % (state, text) if state in ("tool", "error") else "S|%s" % state
            if self.send(line):
                self.sent = want

    def event(self, body):
        sid = str(body.get("session") or "default")
        ev = body.get("event", "")
        with self.lock:
            if ev == "end":
                self.sessions.pop(sid, None)
            else:
                state = body.get("state") or "think"
                text = bl.device_text(body.get("text", ""))
                self.sessions[sid] = {"state": state, "text": text, "ts": time.time()}
        self.refresh()
        return {"ok": True}

    def show(self, body):
        mood = body.get("mood") or "tool"
        if mood not in ("idle", "think", "tool", "error"):
            mood = "tool"
        hold = max(2, min(int(body.get("hold") or 8), 60))
        with self.lock:
            self.manual = (mood, bl.device_text(body.get("text", "")), time.time() + hold)
        self.refresh(force=True)
        threading.Timer(hold + 0.2, self.refresh).start()
        return {"ok": self.connected, "connected": self.connected}

    # ---------------- approvals ----------------
    def approve(self, body):
        mode = self.cfg.get("approvals", "device")
        text = bl.device_text(body.get("text") or "Approval needed")
        if mode != "device":
            self.show({"text": "Answer in terminal: " + text, "mood": "tool", "hold": 20})
            return {"decision": "disabled"}
        if not self.connected:
            return {"decision": "offline"}
        timeout = max(5, min(int(self.cfg.get("approve_timeout", 45)), 280))
        deadline = time.time() + timeout
        # Parallel sessions queue up: one prompt on the board at a time.
        if not self.approve_gate.acquire(timeout=timeout):
            return {"decision": "busy"}
        try:
            with self.lock:
                self.seq += 1
                pend = {"id": "c%d" % self.seq, "text": text, "event": threading.Event(), "result": None}
                self.pending = pend
            if not self.send("S|approve|%s|%s" % (pend["id"], text)):
                return {"decision": "offline"}
            while time.time() < deadline:
                if pend["event"].wait(0.25):
                    log("approval", pend["id"], pend["result"])
                    return {"decision": pend["result"]}
                if not self.connected:          # unplugged mid-prompt
                    return {"decision": "offline"}
            self.send("S|cancel|%s" % pend["id"])
            return {"decision": "timeout"}
        finally:
            with self.lock:
                self.pending = None
                self.sent = None
            self.approve_gate.release()
            # After a tap the board plays its reaction, then returns to "think".
            threading.Timer(4.0, self.refresh, kwargs={"force": True}).start()

    # ---------------- config / device ----------------
    def config(self, body):
        key, value = body.get("key"), body.get("value")
        dev = {"brightness": "bri", "sleep": "sleep", "keys": "kb"}
        if key in dev:
            if not self.connected:
                return {"ok": False, "error": "device offline"}
            self.send("S|cfg|%s|%s" % (dev[key], bl.device_text(value, 12)))
            time.sleep(0.4)
            return {"ok": True, "device": self.device}
        if key == "approvals" and value in ("device", "terminal"):
            pass
        elif key == "approve_timeout":
            value = max(5, min(int(value), 280))
        elif key == "ble" and value in ("auto", "off"):
            pass
        elif key in ("port", "host_name"):
            value = str(value)
        else:
            return {"ok": False, "error": "unknown key %s" % key}
        bl.save_config({key: value})
        self.cfg = bl.load_config()
        if key == "port":
            with self.lock:
                self._drop("port changed")
        if key == "host_name":
            self.send("S|host|%s" % bl.device_text(value, 14))
        return {"ok": True}

    def device_cmd(self, body):
        cmd = body.get("cmd")
        if cmd not in ("unpair", "wizard", "reboot", "info"):
            return {"ok": False, "error": "unknown cmd"}
        if not self.connected:
            return {"ok": False, "error": "device offline"}
        self.send("S|%s" % cmd)
        time.sleep(0.4)
        return {"ok": True, "device": self.device}

    def status(self):
        a = self.cfg
        return {
            "version": bl.VERSION,
            "root": bl.HERE,
            "pid": os.getpid(),
            "port": self.port_path,
            "port_state": self.port_state if self.port_state != "open" or self.connected else "silent",
            "lock_owner": self.lock_pid,
            "connected": self.connected,
            "last_rx_age": round(time.time() - self.last_rx, 1) if self.last_rx else None,
            "device": self.device,
            "display": ({"state": "approve", "text": self.pending["text"]} if self.pending else
                        dict(zip(("state", "text"), self.sent or self.wanted() or ("idle", "")))),
            "sessions": len(self.sessions),
            "approvals": {"mode": a.get("approvals"), "timeout": a.get("approve_timeout"),
                          "pending": bool(self.pending)},
            "error": self.error,
        }

    def shutdown(self):
        self.running = False
        with self.lock:
            if self.port:
                self.port.close()
                self.port = None
        release_lock()
        threading.Thread(target=self.httpd.shutdown, daemon=True).start()


BUDDY = Buddy()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _reply(self, obj, code=200):
        data = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        BUDDY.last_request = time.time()
        if self.path.startswith("/status"):
            self._reply(BUDDY.status())
        else:
            self._reply({"error": "not found"}, 404)

    def do_POST(self):
        BUDDY.last_request = time.time()
        try:
            n = int(self.headers.get("Content-Length") or 0)
            body = json.loads(self.rfile.read(n).decode() or "{}") if n else {}
        except ValueError:
            body = {}
        routes = {
            "/event": BUDDY.event,
            "/approve": BUDDY.approve,
            "/show": BUDDY.show,
            "/config": BUDDY.config,
            "/device": BUDDY.device_cmd,
        }
        if self.path == "/shutdown":
            self._reply({"ok": True})
            BUDDY.shutdown()
            return
        fn = routes.get(self.path)
        if not fn:
            self._reply({"error": "not found"}, 404)
            return
        try:
            self._reply(fn(body))
        except Exception as e:  # never let one bad request kill the daemon
            log("error in", self.path, e)
            self._reply({"error": str(e)}, 500)


def main():
    cfg = BUDDY.cfg
    try:
        httpd = ThreadingHTTPServer(("127.0.0.1", int(cfg["http_port"])), Handler)
    except OSError as e:
        log("http port busy, another daemon is probably running:", e)
        sys.exit(0)
    httpd.daemon_threads = True
    BUDDY.httpd = httpd
    signal.signal(signal.SIGTERM, lambda *_: BUDDY.shutdown())
    log("agent-buddy daemon %s pid %d on 127.0.0.1:%s" % (bl.VERSION, os.getpid(), cfg["http_port"]))
    threading.Thread(target=BUDDY.serial_loop, daemon=True).start()
    threading.Thread(target=BUDDY.ping_loop, daemon=True).start()
    try:
        httpd.serve_forever()
    finally:
        BUDDY.running = False
        release_lock()
        log("bye")


if __name__ == "__main__":
    main()
