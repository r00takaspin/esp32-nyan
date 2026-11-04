# Agent Buddy — DSH plugin for Waveshare ESP32-S3-Touch-LCD-1.47

Turns the board into a desk-side "buddy" for DeepSeek Harness:

- **live status** — the NyanCat pet runs while the agent thinks and rests when idle;
- **physical approve/deny** — a pending tool approval appears on the 172×320 touch
  screen with `YES` / `NO`; tapping returns the decision to the Harness;
- **agent drawing** — the `buddy_show` tool lets the agent put a message on the device;
- **keyboard** — the board is a **BLE HID keyboard** (verified advertising as
  `AgentBuddy`); a **USB HID keyboard** build is available for wired use.

## Files

| File | Role |
|---|---|
| `index.js` | Host: spawns the bridge, answers `approval/request`, registers `buddy_show`, serves `127.0.0.1:8788/status` |
| `client.js` | Client: status pill in `conversation.composer.dock` (polls `/status`) |
| `bridge.py` | USB-CDC bridge: JSON lines on stdio ⇄ pipe protocol on the board |
| `cordis.patch.yml` | Bundle patch that inserts the `agent-buddy` row |
| `locale/en.json`, `icon.svg` | Display metadata for Plugin Manager / Settings |

Firmware: `Arduino/nyancat/` (`nyancat.ino`, `nyancat_frames.h`,
`esp_lcd_touch_axs5106l.*`, `ble_kb.*`).

## Flash the firmware (BLE build, default)

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi" \
  --build-path .arduino15/build Arduino/nyancat
arduino-cli upload -p /dev/cu.usbmodem1101 \
  --fqbn "esp32:esp32:esp32s3:UploadSpeed=921600,USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi" \
  --input-dir .arduino15/build
```

## USB HID keyboard build (wired)

USB HID needs TinyUSB, which replaces the USB-Serial/JTAG CDC:

```bash
arduino-cli compile \
  --fqbn "esp32:esp32:esp32s3:UploadSpeed=921600,USBMode=default,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi" \
  --build-path .arduino15/build-usb \
  --build-property "compiler.cpp.extra_flags=-DUSB_HID_KB" Arduino/nyancat
```

To flash it, put the board in download mode (**hold BOOT, tap RESET, release
BOOT**) and upload to the serial port the ROM exposes, then flash the BLE build
the same way to recover the CDC port.

## Install the bundle

In the GUI: **Settings → Plugins → Install bundle from directory** →
`plugins/dsh-agent-buddy`. Requires `python3` with `pyserial` on PATH.

## Config (`cordis.patch.yml`)

| Field | Default | Meaning |
|---|---|---|
| `port` | `/dev/cu.usbmodem1101` | USB CDC device of the board |
| `python` | `python3` | Interpreter that runs `bridge.py`; must have `pyserial` |
| `approvals` | `on` | `on` answers approvals on the device; `off` mirrors and delegates to the GUI |
| `timeoutMs` | `120000` | How long the device may hold an approval before resolving `unavailable` |

## Protocol (board ↔ bridge)

```
Host → device:  S|idle  S|think  S|tool|<text>  S|approve|<id>|<text>  S|error|<text>
Device → host:  E|hello  E|approve|<id>|<1|0>  A|<state>  T|<x>|<y>
```

The board also emits `T|x|y` touch debug coordinates; the HID keyboard sends
`y` / `n` on YES / NO via `kbRaw()`.

## Verification notes (what was actually tested)

- Firmware boots; touch initialises (`read: 8161`); BLE stack starts (`BLE keyboard ready`).
- BLE advertisement confirmed by scanning from macOS: device `AgentBuddy` found.
- Serial protocol verified: `S|think → A|think`, `S|tool|… → A|tool`,
  `S|approve|call_test|… → A|approve`, `S|error|… → A|error`, `S|idle → A|idle`.
- Physical approve verified live: a tap at `(37,143)` produced
  `E|approve|call_test|1` and the state moved to `think`.
- `bridge.py` verified end-to-end (sends states, prints acks).
- Host plugin runtime-tested with a mock DSH context: `buddy_show` registers,
  `127.0.0.1:8788/status` returns `{"state":"idle","connected":true}`, and
  `buddy_show` reports `shown on Agent Buddy: …`.
- Approval answerer tested with a fake bridge: `approval/request` resolved
  `allowed-once` when the device answered YES.
- Host/Client JS and package JSON pass syntax checks; the USB HID variant compiles.

**Not verified here:** plugin installation/activation inside a live profile
(requires the `plugin_manager` tool or the GUI), approval waterfall ordering
against the GUI's own answerer, and actual BLE pairing/keystroke delivery to a
host. Re-check the `approval/request` and `turn/end` event names with
`cordis_inspect_query` in your profile.

## Notes

- `audio`/HID: the BLE build uses Bluedroid; the USB HID build uses TinyUSB.
- `kbRaw()` in `nyancat.ino` is the single keyboard seam for both backends.
