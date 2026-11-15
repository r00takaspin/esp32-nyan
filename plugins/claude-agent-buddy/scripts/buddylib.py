"""Shared helpers for the Agent Buddy Claude Code plugin.

Only the standard library is used (Python 3.8+), so hooks start fast with the
system python3 and nothing has to be pip-installed.
"""
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

VERSION = "1.0.0"
HERE = os.path.dirname(os.path.abspath(__file__))

CONFIG_DIR = os.path.expanduser(os.environ.get("AGENT_BUDDY_HOME", "~/.config/agent-buddy"))
CONFIG_PATH = os.path.join(CONFIG_DIR, "config.json")
LOG_PATH = os.path.join(CONFIG_DIR, "daemon.log")

# Same lock file as the DeepSeek bridge and the flashing recipe in AGENTS.md:
# whoever holds it owns the USB port.
LOCK_PATH = "/tmp/nyan-buddy-bridge.lock"

DEFAULTS = {
    "port": "auto",              # "auto" or e.g. "/dev/cu.usbmodem1101"
    "http_port": 8789,           # DSH uses 8788
    "approvals": "device",       # "device": answer on the board, "terminal": only show
    "approve_timeout": 45,       # seconds the board may hold a permission prompt
    "host_name": "claude",
}

# Tools whose permission prompt is really a question for the human: the hook
# must not answer them (Claude Code ignores a bare allow for these anyway).
INTERACTIVE_TOOLS = {"AskUserQuestion", "ExitPlanMode"}


def load_config():
    cfg = dict(DEFAULTS)
    try:
        with open(CONFIG_PATH) as f:
            user = json.load(f)
        if isinstance(user, dict):
            cfg.update({k: v for k, v in user.items() if k in DEFAULTS})
    except (OSError, ValueError):
        pass
    for key in DEFAULTS:
        env = os.environ.get("AGENT_BUDDY_" + key.upper())
        if env:
            cfg[key] = int(env) if isinstance(DEFAULTS[key], int) else env
    return cfg


def save_config(updates):
    os.makedirs(CONFIG_DIR, exist_ok=True)
    try:
        with open(CONFIG_PATH) as f:
            cur = json.load(f)
        if not isinstance(cur, dict):
            cur = {}
    except (OSError, ValueError):
        cur = {}
    cur.update(updates)
    tmp = CONFIG_PATH + ".tmp"
    with open(tmp, "w") as f:
        json.dump(cur, f, indent=2)
    os.replace(tmp, CONFIG_PATH)


# --------------------------------------------------------------- device text
RU = {
    "а": "a", "б": "b", "в": "v", "г": "g", "д": "d", "е": "e", "ё": "e", "ж": "zh",
    "з": "z", "и": "i", "й": "y", "к": "k", "л": "l", "м": "m", "н": "n", "о": "o",
    "п": "p", "р": "r", "с": "s", "т": "t", "у": "u", "ф": "f", "х": "h", "ц": "c",
    "ч": "ch", "ш": "sh", "щ": "sch", "ъ": "", "ы": "y", "ь": "", "э": "e", "ю": "yu",
    "я": "ya",
}


def device_text(s, limit=170):
    """The board font is 7-bit ASCII and '|' / newlines break the protocol."""
    out = []
    for ch in str(s or ""):
        low = ch.lower()
        if low in RU:
            t = RU[low]
            out.append(t.capitalize() if ch != low and t else t)
        elif ch in "|\r\n\t":
            out.append(" ")
        elif 32 <= ord(ch) < 127:
            out.append(ch)
        else:
            out.append("?")
    return " ".join("".join(out).split())[:limit]


def summarize_tool(tool_name, tool_input):
    """'Bash: git push origin main' style one-liner for the device."""
    ti = tool_input if isinstance(tool_input, dict) else {}
    name = tool_name or "tool"
    if name.startswith("mcp__"):
        parts = name.split("__")
        name = "mcp:" + (parts[-1] if parts else name)
    detail = ""
    for key in ("command", "file_path", "notebook_path", "url", "pattern", "query",
                "description", "prompt", "path"):
        v = ti.get(key)
        if isinstance(v, str) and v.strip():
            detail = v.strip()
            break
    if detail and ("file_path" in ti or "notebook_path" in ti) and len(detail) > 60:
        detail = "..." + detail[-57:]
    return "%s: %s" % (name, detail) if detail else name


# --------------------------------------------------------------- daemon client
def _url(cfg, path):
    return "http://127.0.0.1:%d%s" % (int(cfg["http_port"]), path)


def request(path, body=None, timeout=2.0, cfg=None):
    cfg = cfg or load_config()
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(_url(cfg, path), data=data,
                                 headers={"Content-Type": "application/json"},
                                 method="GET" if body is None else "POST")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return json.loads(r.read().decode() or "{}")


def _spawn_daemon():
    os.makedirs(CONFIG_DIR, exist_ok=True)
    log = open(LOG_PATH, "a")
    subprocess.Popen([sys.executable, os.path.join(HERE, "buddyd.py")],
                     stdin=subprocess.DEVNULL, stdout=log, stderr=log,
                     start_new_session=True, close_fds=True)


def ensure_daemon(cfg=None, wait=3.0):
    """Return the daemon's /status, starting (or upgrading) it if needed."""
    cfg = cfg or load_config()
    try:
        st = request("/status", timeout=0.6, cfg=cfg)
        # Only the version decides: the hooks run the marketplace source copy and
        # the CLI / MCP server may run the cache copy; comparing paths made two
        # copies of the same version shut each other down every few seconds.
        if st.get("version") == VERSION:
            return st
        # An older plugin version is still running: replace it.
        try:
            request("/shutdown", {}, timeout=0.6, cfg=cfg)
        except Exception:
            pass
        time.sleep(0.4)
    except Exception:
        pass
    _spawn_daemon()
    deadline = time.time() + wait
    while time.time() < deadline:
        time.sleep(0.15)
        try:
            return request("/status", timeout=0.5, cfg=cfg)
        except Exception:
            continue
    return None


def call(path, body=None, timeout=2.0):
    """Request with auto-start. Returns None when the daemon is unreachable."""
    cfg = load_config()
    try:
        return request(path, body, timeout=timeout, cfg=cfg)
    except (urllib.error.URLError, ConnectionError, OSError):
        if ensure_daemon(cfg) is None:
            return None
        try:
            return request(path, body, timeout=timeout, cfg=cfg)
        except Exception:
            return None


def describe_status(st):
    """Human-readable multi-line status (used by the MCP tool and the CLI)."""
    if not st:
        return "Agent Buddy daemon is not running and could not be started (see %s)." % LOG_PATH
    lines = []
    port_state = st.get("port_state")
    if not st.get("connected"):
        hint = {
            "missing": "board not found - plug the USB-C cable into the computer",
            "busy": "USB port is owned by another program (%s)" % (st.get("lock_owner") or "DeepSeek bridge / flashing"),
            "legacy": "board answers but runs old firmware (no ping) - flash firmware v2",
        }.get(port_state, st.get("error") or "no reply from the board")
        lines.append("Device: OFFLINE - " + hint)
    else:
        lines.append("Device: online on %s (fw %s, %s)" % (
            st.get("port"), st.get("device", {}).get("fw", "?"), st.get("device", {}).get("name", "?")))
    d = st.get("device") or {}
    if d:
        pct = d.get("pct")
        bat = "%s%%" % pct if pct not in (None, "-1", -1) else "unknown"
        mv = d.get("bat")
        if mv and str(mv) != "0":
            bat += " (%.2f V)" % (int(mv) / 1000.0)
        lines.append("Battery: %s, power: %s" % (bat, d.get("pwr", "?")))
        ble = d.get("ble") == "1"
        bonds = int(d.get("bond") or 0)
        if ble:
            pair = "paired and connected"
        elif bonds:
            pair = "paired, not connected right now"
        else:
            pair = "NOT paired"
        peer = d.get("peer")
        lines.append("Bluetooth keyboard: %s%s" % (pair, (" with " + peer) if peer else ""))
        lines.append("Setup wizard: %s" % ("done" if d.get("setup") == "1" else "not finished"))
        lines.append("Settings: brightness %s/4, screen sleep %s, y/n keys %s" % (
            d.get("bri", "?"),
            "never" if d.get("sleep") == "0" else "%ss" % d.get("sleep", "?"),
            d.get("kb", "?")))
    a = st.get("approvals", {})
    lines.append("Approvals: %s (timeout %ss)%s" % (
        a.get("mode"), a.get("timeout"), " - one pending now" if a.get("pending") else ""))
    disp = st.get("display", {})
    lines.append("Showing: %s%s; sessions tracked: %s" % (
        disp.get("state"), (" - " + disp["text"]) if disp.get("text") else "", st.get("sessions")))
    return "\n".join(lines)
