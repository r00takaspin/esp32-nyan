#!/usr/bin/env python3
"""Agent Buddy CLI and Claude Code hook entry point.

  buddy.py hook                 read a hook payload on stdin (hooks/hooks.json)
  buddy.py status [--json]      print device / daemon status
  buddy.py show TEXT [MOOD]     put a message on the board
  buddy.py set KEY VALUE        brightness 0-4 | sleep SECONDS | keys off|auto|always
                                approvals device|terminal | approve_timeout SECONDS | port PATH
  buddy.py unpair | wizard | reboot
  buddy.py statusline           one short line for Claude Code's statusLine
  buddy.py stop                 stop the background daemon

Hooks must never break Claude Code: every failure path exits 0 silently, and
for PermissionRequest "no output" means "show the normal terminal dialog".
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import buddylib as bl  # noqa: E402


# ------------------------------------------------------------------ hooks
def hook():
    try:
        payload = json.load(sys.stdin)
    except ValueError:
        return
    ev = payload.get("hook_event_name", "")
    sid = payload.get("session_id") or "default"
    tool = payload.get("tool_name") or ""

    if ev == "PermissionRequest":
        permission(payload, sid, tool)
        return

    body = {"session": sid, "event": ev}
    if ev in ("SessionStart", "Stop"):
        body["state"] = "idle"
    elif ev in ("UserPromptSubmit", "PostToolUse", "PostToolUseFailure", "SubagentStop"):
        body["state"] = "think"
    elif ev == "PreToolUse":
        body["state"] = "tool"
        body["text"] = bl.summarize_tool(tool, payload.get("tool_input"))
    elif ev == "Notification":
        ntype = payload.get("notification_type") or payload.get("type") or ""
        if ntype != "idle_prompt":
            return
        body["state"] = "idle"
    elif ev == "SessionEnd":
        body["event"] = "end"
    else:
        return
    bl.call("/event", body, timeout=1.5)


def permission(payload, sid, tool):
    if tool in bl.INTERACTIVE_TOOLS:
        # A question for the human, not a yes/no: point at the terminal.
        bl.call("/show", {"text": "Claude asks you something in the terminal", "mood": "tool", "hold": 15})
        return
    text = bl.summarize_tool(tool, payload.get("tool_input"))
    cfg = bl.load_config()
    # Block until the board answers (or times out -> terminal dialog).
    res = bl.call("/approve", {"session": sid, "text": text},
                  timeout=float(cfg.get("approve_timeout", 45)) + 15)
    decision = (res or {}).get("decision")
    if decision == "allow":
        out = {"behavior": "allow"}
    elif decision == "deny":
        out = {"behavior": "deny",
               "message": "The user rejected this action on the Agent Buddy desk device."}
    else:
        return   # offline / timeout / disabled: Claude Code shows its own dialog
    print(json.dumps({"hookSpecificOutput": {"hookEventName": "PermissionRequest", "decision": out}}))


# ------------------------------------------------------------------ CLI
def statusline():
    try:
        st = bl.request("/status", timeout=0.3)
    except Exception:
        print("buddy: off")
        return
    if not st.get("connected"):
        print("buddy: offline")
        return
    d = st.get("device", {})
    parts = ["buddy: " + (st.get("display", {}).get("state") or "?")]
    pct = d.get("pct")
    if pct and pct != "-1":
        parts.append("bat %s%%%s" % (pct, "+" if d.get("pwr") in ("charging", "full") else ""))
    parts.append("BT ok" if d.get("ble") == "1" else ("BT paired" if d.get("bond") not in (None, "0") else "BT -"))
    print(" | ".join(parts))


def main(argv):
    cmd = argv[1] if len(argv) > 1 else "status"
    if cmd == "hook":
        try:
            hook()
        except Exception:
            pass
        return 0
    if cmd == "statusline":
        statusline()
        return 0
    if cmd == "status":
        st = bl.ensure_daemon()
        if st:
            import time
            time.sleep(0.1 if st.get("connected") else 3.0)   # give a fresh daemon time to find the board
            try:
                st = bl.request("/status")
            except Exception:
                pass
        print(json.dumps(st, indent=2) if "--json" in argv else bl.describe_status(st))
        return 0
    if cmd == "show" and len(argv) >= 3:
        print(bl.call("/show", {"text": argv[2], "mood": argv[3] if len(argv) > 3 else "tool"}))
        return 0
    if cmd == "set" and len(argv) >= 4:
        print(bl.call("/config", {"key": argv[2], "value": argv[3]}))
        return 0
    if cmd in ("unpair", "wizard", "reboot"):
        print(bl.call("/device", {"cmd": cmd}))
        return 0
    if cmd == "stop":
        try:
            print(bl.request("/shutdown", {}))
        except Exception:
            print("daemon not running")
        return 0
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
