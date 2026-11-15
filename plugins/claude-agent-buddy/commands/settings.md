---
description: View or change Agent Buddy settings (brightness, screen sleep, y/n keys, approval mode, timeout)
argument-hint: "[e.g. brightness 3, sleep 30, approvals terminal]"
---
Requested change: $ARGUMENTS

If no change was requested, call `buddy_status` and show the current settings with what each one does:
brightness 0-4, sleep (0/5/10/30/60 s, screen-off on the idle cat), keys (off/auto/always: YES/NO also type y/n over
Bluetooth; auto = only when no agent is linked over USB), approvals (device/terminal), approve_timeout (seconds before
the prompt falls back to the terminal).
Otherwise apply it with `buddy_settings` and confirm the new values.
