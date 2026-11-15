#!/usr/bin/env python3
"""Minimal stdio MCP server exposing the Agent Buddy to Claude.

Same capability as the DSH plugin's `buddy_show`, plus status, settings and
pairing control. Standard library only (newline-delimited JSON-RPC 2.0).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import buddylib as bl  # noqa: E402

PROTOCOL = "2025-06-18"

TOOLS = [
    {
        "name": "buddy_show",
        "description": ("Show a short message on the user's desk Agent Buddy (a small ESP32 touch "
                        "screen with a Nyan Cat) for a few seconds and pick the cat's mood. Use it for "
                        "milestones the user would like to notice from across the room."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "text": {"type": "string", "description": "Message, ASCII, up to ~170 chars"},
                "mood": {"type": "string", "enum": ["tool", "think", "idle", "error"],
                         "description": "tool = message bar over the running cat (default), "
                                        "error = red bar with the grumpy cat"},
                "seconds": {"type": "integer", "minimum": 2, "maximum": 60,
                            "description": "How long to hold it (default 8)"},
            },
            "required": ["text"],
        },
    },
    {
        "name": "buddy_status",
        "description": ("Read the Agent Buddy status: USB link, battery level and charging, Bluetooth "
                        "keyboard pairing, settings and approval mode."),
        "inputSchema": {"type": "object", "properties": {}},
    },
    {
        "name": "buddy_settings",
        "description": ("Change Agent Buddy settings. Device: brightness 0-4, sleep (screen-off "
                        "seconds on the idle cat, 0 = never), keys (off|auto|always: whether YES/NO "
                        "also type y/n over Bluetooth). Host: approvals (device = answer permission "
                        "prompts on the board, terminal = only show them), approve_timeout seconds, "
                        "port (serial device path or 'auto')."),
        "inputSchema": {
            "type": "object",
            "properties": {
                "brightness": {"type": "integer", "minimum": 0, "maximum": 4},
                "sleep": {"type": "integer", "enum": [0, 5, 10, 30, 60]},
                "keys": {"type": "string", "enum": ["off", "auto", "always"]},
                "approvals": {"type": "string", "enum": ["device", "terminal"]},
                "approve_timeout": {"type": "integer", "minimum": 5, "maximum": 280},
                "port": {"type": "string"},
            },
        },
    },
    {
        "name": "buddy_pair",
        "description": ("Bluetooth pairing control. action=repair forgets the paired computer on the "
                        "board and starts pairing again (the user must also remove the old entry in the "
                        "computer's Bluetooth settings); action=wizard reopens the on-device setup "
                        "wizard; action=instructions returns step-by-step pairing instructions."),
        "inputSchema": {
            "type": "object",
            "properties": {"action": {"type": "string", "enum": ["instructions", "repair", "wizard"]}},
            "required": ["action"],
        },
    },
]

PAIR_HELP = """Pairing the Agent Buddy (two independent links):

A. Bluetooth keyboard (optional - lets YES/NO type y/n into any app)
  1. On the board: tap the cat -> SETTINGS -> SETUP (or it opens by itself on first boot).
  2. On the Mac: System Settings -> Bluetooth. On Windows: Settings -> Bluetooth & devices -> Add device -> Bluetooth.
  3. Find "AgentBuddy-XXXX" (the exact name is shown on the board) and press Connect. No code is needed.
  4. The board turns green: "Connected!". Tap NEXT.
  Re-pairing: board SETTINGS -> RE-PAIR -> FORGET, and on the computer "Forget This Device"
  for the old AgentBuddy entry; then repeat steps 2-4. Skipping the computer side makes it
  fail to reconnect, because the computer still holds the old keys.

B. Agent link over USB (required for live status and approvals in Claude Code)
  1. Plug the board's USB-C into the computer (data cable, not a charge-only one).
  2. Install this plugin (it is what you are using now) and start/restart Claude Code.
  3. The wizard's step 2 shows "Agent: claude linked" within a few seconds.
  If it says the port is busy, quit DeepSeek Harness (its bridge owns the same port) or
  finish flashing - both use /tmp/nyan-buddy-bridge.lock.

C. Test: the wizard's step 3 can type "buddy ok" over Bluetooth into a text field. Then DONE.
"""


def tool_call(name, args):
    if name == "buddy_show":
        r = bl.call("/show", {"text": args.get("text", ""), "mood": args.get("mood", "tool"),
                              "hold": args.get("seconds", 8)})
        if not r:
            return "Agent Buddy daemon is unreachable.", True
        return ("Shown on Agent Buddy: %s" % bl.device_text(args.get("text", "")) if r.get("connected")
                else "Agent Buddy is offline (not plugged in?) - message not shown."), False
    if name == "buddy_status":
        st = bl.ensure_daemon()
        return bl.describe_status(st), st is None
    if name == "buddy_settings":
        done = []
        for key in ("brightness", "sleep", "keys", "approvals", "approve_timeout", "port"):
            if key in args:
                r = bl.call("/config", {"key": key, "value": args[key]}) or {}
                done.append("%s=%s: %s" % (key, args[key], "ok" if r.get("ok") else r.get("error", "failed")))
        if not done:
            return "Nothing to change. " + bl.describe_status(bl.ensure_daemon()), False
        return "\n".join(done), False
    if name == "buddy_pair":
        action = args.get("action", "instructions")
        if action == "instructions":
            return PAIR_HELP, False
        r = bl.call("/device", {"cmd": "unpair" if action == "repair" else "wizard"}) or {}
        if not r.get("ok"):
            return "Could not reach the board: %s" % r.get("error", "daemon unreachable"), True
        if action == "repair":
            return ("The board forgot its Bluetooth pairing and shows the pairing screen. Now on the "
                    "computer remove the old AgentBuddy device (Forget This Device) and connect to it "
                    "again from the Bluetooth settings."), False
        return "The setup wizard is open on the board.", False
    return "Unknown tool %s" % name, True


def handle(msg):
    mid = msg.get("id")
    method = msg.get("method")
    if method == "initialize":
        return {"jsonrpc": "2.0", "id": mid, "result": {
            "protocolVersion": msg.get("params", {}).get("protocolVersion", PROTOCOL),
            "capabilities": {"tools": {}},
            "serverInfo": {"name": "agent-buddy", "version": bl.VERSION},
        }}
    if method == "tools/list":
        return {"jsonrpc": "2.0", "id": mid, "result": {"tools": TOOLS}}
    if method == "tools/call":
        p = msg.get("params", {})
        try:
            text, err = tool_call(p.get("name"), p.get("arguments") or {})
        except Exception as e:
            text, err = "Agent Buddy error: %s" % e, True
        return {"jsonrpc": "2.0", "id": mid,
                "result": {"content": [{"type": "text", "text": text}], "isError": bool(err)}}
    if method == "ping":
        return {"jsonrpc": "2.0", "id": mid, "result": {}}
    if mid is None:
        return None          # notifications (initialized, cancelled, ...)
    return {"jsonrpc": "2.0", "id": mid, "error": {"code": -32601, "message": "method not found"}}


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except ValueError:
            continue
        resp = handle(msg)
        if resp is not None:
            sys.stdout.write(json.dumps(resp) + "\n")
            sys.stdout.flush()


if __name__ == "__main__":
    main()
