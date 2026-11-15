---
description: Walk through pairing the Agent Buddy (Bluetooth keyboard + USB agent link), or re-pair it
argument-hint: "[repair]"
---
Help me pair my Agent Buddy desk device.

1. Call `buddy_status` to see what is already done.
2. If the arguments say "repair" ($ARGUMENTS), first confirm with me, then call `buddy_pair` with action `repair`.
   Otherwise call `buddy_pair` with action `wizard` so the setup screens open on the board.
3. Call `buddy_pair` with action `instructions` and walk me through only the steps that are still missing,
   one at a time, waiting for me to confirm each step. Re-check with `buddy_status` after each step.
