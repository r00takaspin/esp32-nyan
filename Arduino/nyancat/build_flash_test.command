#!/bin/bash
# Double-click in Finder: build firmware v2, flash it, run the self-test.
# Full log: tools/build_flash.log (Claude reads it).
cd "$(dirname "$0")"
LOG="tools/build_flash.log"
exec > >(tee "$LOG") 2>&1
export PATH="/opt/homebrew/bin:/usr/local/bin:$HOME/Documents/deepseek-harness/default-workspace/arduino-cli:$PATH"
echo "=== $(date) build/flash/test ==="
FQBN="esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB"
arduino-cli version
if ! arduino-cli compile --fqbn "$FQBN" --library ./GFX_Library_for_Arduino --build-path ./build . ; then
  echo "BUILD_FAILED"; echo "Окно можно закрыть: Claude прочитает лог."; exit 1
fi
echo "BUILD_OK"

ESPTOOL=~/Library/Arduino15/packages/esp32/tools/esptool_py/4.5.1/esptool
BOOTAPP0=~/Library/Arduino15/packages/esp32/hardware/esp32/2.0.14/tools/partitions/boot_app0.bin
LOCK=/tmp/nyan-buddy-bridge.lock
PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)
if [ -z "$PORT" ]; then echo "FLASH_FAILED no board port (plug USB-C)"; exit 1; fi
echo "port: $PORT"
echo "$$" > "$LOCK"                         # park DeepSeek bridge / Claude daemon
for PID in $(lsof -t "$PORT" 2>/dev/null); do kill "$PID"; done
sleep 1
if ! "$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 921600 \
  --before default_reset --after hard_reset write_flash -z \
  --flash_mode qio --flash_freq 80m --flash_size 16MB \
  0x0 build/nyancat.ino.bootloader.bin \
  0x8000 build/nyancat.ino.partitions.bin \
  0xe000 "$BOOTAPP0" \
  0x10000 build/nyancat.ino.bin ; then
  echo "FLASH_FAILED"; rm -f "$LOCK"; exit 1
fi
echo "FLASH_OK"
sleep 2
python3 tools/selftest.py
rm -f "$LOCK"
echo "=== done; the window can be closed ==="
