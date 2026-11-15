# AGENTS.md — nyan-buddy (прошивка платы Agent Buddy)

Заметки и рабочие команды для агентов. Проект целиком — в корне репозитория
(`README.md` на один уровень выше), плагин DSH — `plugins/dsh-agent-buddy/`.

## Структура

- Прошивка: `Arduino/nyancat/` — `nyancat.ino`, `nyan_anims.h`, `nyancat_frames.h`,
  `esp_lcd_touch_axs5106l.*`, `ble_kb.*`.
- Плата: Waveshare **ESP32-S3-Touch-LCD-1.47** — 172×320 IPS, тач AXS5106L,
  чип дисплея JD9853, USB-Serial-JTAG (нативный USB).

## Сборка прошивки

Ядро ESP32 **2.0.14** (в `nyancat.ino` есть shim для `ledc`, чтобы собиралось и под 2.x, и под 3.x).

Библиотека GFX v1.5.9 лежит рядом со скетчем: `Arduino/nyancat/GFX_Library_for_Arduino/` (в git не входит,
26 МБ). Если её нет: `arduino-cli lib download "GFX Library for Arduino@1.5.9"` и распаковать сюда
под этим именем. Глобальная 1.6.9 из `Arduino/libraries/` не годится — нужна именно 1.5.9.

```bash
cd Arduino/nyancat
arduino-cli compile \
  --fqbn "esp32:esp32:esp32s3:CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB" \
  --library ./GFX_Library_for_Arduino \
  --build-path ./build .
```

Схема разделов по умолчанию (4 МБ, 1.2 МБ APP) **не влезает** — скетч ~1.4 МБ,
нужен `PartitionScheme=app3M_fat9M_16MB` (3 МБ APP).

## Прошивка (flash)

USB-порт держит `bridge.py` плагина buddy. Пока он открыт, esptool не может сбросить
плату в режим прошивки (мост «крадёт» ответ бутлоадера). Поэтому мост блокируют
через его lock-файл:

```bash
cd Arduino/nyancat
ESPTOOL=~/Library/Arduino15/packages/esp32/tools/esptool_py/4.5.1/esptool
BOOTAPP0=~/Library/Arduino15/packages/esp32/hardware/esp32/2.0.14/tools/partitions/boot_app0.bin
LOCK=/tmp/nyan-buddy-bridge.lock

echo "$$" > "$LOCK"                       # блокируем bridge (он уходит и не трогает порт)
for PID in $(lsof -t /dev/cu.usbmodem1101); do kill "$PID"; done
sleep 1

"$ESPTOOL" --chip esp32s3 --port /dev/cu.usbmodem1101 --baud 921600 \
  --before default_reset --after hard_reset write_flash -z \
  --flash_mode qio --flash_freq 80m --flash_size 16MB \
  0x0 build/nyancat.ino.bootloader.bin \
  0x8000 build/nyancat.ino.partitions.bin \
  0xe000 "$BOOTAPP0" \
  0x10000 build/nyancat.ino.bin

rm -f "$LOCK"                              # возвращаем порт buddy
```

Нюансы:

- `--before default_reset` (автосброс) работает **только** при заблокированном мосте.
- Если автосброс не сработал — вручную: **держать BOOT → коротко RESET → отпустить RESET →
  отпустить BOOT**, затем прошивать с `--before no_reset`.
- Доступ к USB-устройству в песочнице DSH требует `danger-full-access` (подтверждение).

## Ключевые параметры прошивки (`nyancat.ino`)

| Параметр | Значение | Смысл |
|---|---|---|
| `BL_LEVEL` | 280 | яркость подсветки (~27 %), снижена с 460 (меньше просадок на слабой батарее) |
| LEDC shim (`BL_CH`, `BL_BEGIN`) | — | совместимость `ledc` ядра 2.x/3.x; не трогать без нужды |

## Железо / батарея

- Аккумулятор **602535, 3.7 В 600 мА·ч**, 2-пиновый JST: **красный = «+», чёрный = «−»**.
  Проверять полярность мультиметром (на дешёвых батареях провода бывают перепутаны).
- Подключение: «+» → цепь VBAT (пин 2 микросхемы питания P1), «−» → GND.
- Красный огонёк на плате — индикатор зарядки, а не признак работы ESP32.
- После подключения батареи один раз подать питание по USB, чтобы «активировать» плату.
- Напряжение батареи меряется на **GPIO12 (ADC2, канал 1)**, делитель 1:3
  (напряжение = показание АЦП × 3) — так в демо Waveshare (`bsp_battery`).

## ⚠️ Известная ловушка: правки батареи/сдвига ломали экран APPROVE

В одну из сессий были добавлены индикатор заряда (`readBattery()` с
`analogReadMilliVolts(12)`) и сдвиг картинки (`SCREEN_SHIFT_X`, канвас со сдвигом +
`flushScreen()` с `gfx->fillRect`). После этого плата перестала показывать экран
подтверждения: оставалась в idle («спящий котик»), команда `S|approve` не обрабатывалась.

**Эти правки были откачены** (прошивка возвращена к исходному состоянию + LEDC shim +
`BL_LEVEL=280`). Если будешь возвращать заряд/сдвиг — добавляй **по одному** и после
каждой правки проверяй, что экран APPROVE появляется (иначе непонятно, какая именно ломает).

## ⚠️ Известная ловушка: подтверждение прав застревает на плате

Плагин buddy (`index.js`) держит флаг `deviceAlive`, который ставится в `true` по любой
строке от платы и **никогда не сбрасывается**. Если плата «отвалилась» (а флаг остался
`true`), обработчик подтверждений ждёт тап на плате и не отдаёт вопрос в GUI — запрос
расширения прав (`danger-full-access`) висит и отменяется по таймауту, **в окне Harness
подтверждение не появляется**.

**Исправлено (DSH-плагин build v5):** живость = ответ на пинг за последние 7 с, флага больше нет.
Для старых версий: **перезапустить DeepSeek Harness** (сбросит `deviceAlive`), при
необходимости переподключить плагин и переподключить/сбросить плату. После этого
подтверждение снова уходит на плату (если она рабочая) или в GUI.

## Прошивка v2 (FW_VERSION 2.0): мастер настройки, статус, настройки

Новые файлы скетча: `buddy_ui.*` (экраны, кнопки, хит-тест — одна таблица `uiButtons`),
`battery.*` (заряд/зарядка), `ble_kb.*` (добавлены bonding, забыть пару, батарея HID, печать строки).

Навигация на плате:
- первое включение (`setup=false` в NVS) → мастер: HELLO → 1/3 BLUETOOTH → 2/3 AGENT LINK → 3/3 TEST;
- тап по котику → STATUS (батарея/зарядка, Bluetooth, связь с агентом) → SETTINGS
  (яркость, сон экрана, y/n-клавиши, RE-PAIR, SETUP); сами закрываются через 30 с;
- APPROVE всегда перекрывает экраны; мастер после ответа возвращается на ту же страницу;
- первый тап по погасшему экрану только будит его.

Настройки в NVS (`Preferences`, namespace `buddy`): `bri` 0–4 (`BRI_LEVELS`, 2 = прежние 280),
`sleep` (индекс `SLEEP_OPTIONS` 5/10/30/60/never), `kb` (0 off / 1 auto / 2 always), `setup`.
**Изменение поведения:** раньше каждое нажатие YES/NO печатало `y`/`n` по BLE. Теперь по умолчанию
`kb=auto` — печатать только когда нет USB-связи с агентом (иначе `y` попадал бы в терминал Claude Code).

Протокол (обратно совместим с DSH `bridge.py`):

```
Host → dev:  S|ping  S|info            → I|fw=2.0|name=..|ble=0/1|bond=N|peer=..|bat=mV|pct=..|pwr=battery/charging/full|usb=0/1|bri|sleep|kb|setup|ui|st
             S|host|<name>             имя агента на экранах (claude / deepseek)
             S|cfg|bri|0..4  S|cfg|sleep|<сек>  S|cfg|kb|off/auto/always  S|cfg|setup|0
             S|unpair  S|wizard  S|reboot  S|cancel|<id>
```
«Связь с агентом» на плате = любая `S|…` строка за последние 15 с (DSH шлёт статус раз в 2 с).
think/idle/tool поверх APPROVE снимают подтверждение (так DSH уходит в GUI) — не менять.

Зарядка: STAT зарядника ETA6098 идёт только на красный LED, VBUS не измеряется. Поэтому
`usb` = на USB-Serial-JTAG приходят SOF-кадры (`USB_SERIAL_JTAG_FRAM_NUM_REG`), а
charging/full — по напряжению (≥4.15 В = full). Зарядник «без данных» (блок питания) будет виден как
`battery`. АЦП опрашивается раз в 2 с и **никогда во время APPROVE**; `-DFEATURE_BATTERY=0` отключает АЦП целиком.

BLE: имя теперь `AgentBuddy-XXXX` (из MAC, показывается в мастере), bonding «Just Works»
(`ESP_LE_AUTH_REQ_SC_BOND`, `ESP_IO_CAP_NONE`). Код security написан под BLE-API ядра **2.0.14**;
на 3.x выдаётся `#warning` и bonding не настраивается.

### Хост-симуляция (без платы)

`tools/hostsim/run.sh` компилирует скетч с заглушками Arduino/GFX/BLE, прогоняет сценарий
(мастер → подтверждение посреди мастера → статус → настройки → перепривязка → низкий заряд → сон)
с ассертами и сохраняет кадры экранов в `tools/hostsim/out/`. Это не заменяет сборку под ESP32:
заглушки повторяют API, но не проверяют настоящие заголовки ядра.

### Первая прошивка v2 — чек-лист (из-за ловушки с APPROVE выше)

1. Собрать как обычно (команда выше; новые `.cpp` подхватятся сами). Если ругается на BLE security —
   прислать ошибку; временно можно собрать с `-DFEATURE_BATTERY=0`, чтобы исключить АЦП.
2. После прошивки: мастер HELLO на экране. `S|approve|t1|test` из `read_serial`/моста →
   экран APPROVE появляется, тап YES → `E|approve|t1|1`. **Это главный регресс-тест.**
3. `S|ping` → строка `I|fw=2.0|…`, `bat` ≈ 3300–4200 при подключённой батарее.
4. Пэйринг в macOS → «Connected!»; после перезагрузки платы Mac переподключается сам (bonding).

### Откат на v1

Бинарники v1 лежат локально в `fw_backup_v1/` (не в git). Если их нет — исходники v1 в коммите `8214eee`:
`git worktree add /tmp/nyan-v1 8214eee`, собрать оттуда и прошить как обычно.

### Плагин Claude Code

Маркетплейс `plugins/.claude-plugin/marketplace.json`, плагин `plugins/claude-agent-buddy/`. Установка (в Claude Code):

```
/plugin marketplace add ~/Documents/deepseek-harness/default-workspace/plugins
/plugin install agent-buddy@nyan-buddy
/agent-buddy:status
```

Плагин делит lock `/tmp/nyan-buddy-bridge.lock` с мостом DeepSeek: пока DSH держит порт, плагин платы не видит.

### Связь с агентом по Bluetooth и питание (после v2)

- BLE-сервис связи `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` (RX `…0002` write, TX `…0003` notify) — тот же
  текстовый протокол, что по USB; `sendLine()` шлёт в оба канала. На Mac его открывает `buddy-ble`
  из плагина (см. README плагина). macOS кэширует GATT-таблицу сопряжённого устройства, поэтому мост
  ищет плату по стандартным сервисам (180F/180A/1812) и имени, а не только по UUID сервиса связи.
- Характеристики HID должны создаваться **до** `startServices()` (иначе `notify()` падает).
- Экран гаснет → панель в SLPIN, кадры не рисуются, CPU 80 МГц (на экране 160 МГц).
- Standby: на батарее, без связи с агентом (`hostLinked()`), idle и экран тёмный 5 мин → BLE off, light
  sleep, пробуждение по тапу (INT GPIO48 + опрос тача раз в 0,5 с) или по USB (проверка раз в 60 с),
  затем `ESP.restart()` без заставки. **Light sleep с подключённым USB-хостом не возвращается** (ядро 2.0.14) —
  поэтому только на батарее; тест: `S|standby|25`, за 25 с выдернуть кабель.
- Журнал разряда: кольцо в RTC-памяти (переживает рестарт, не отключение питания), раз в 5 мин;
  `S|log` → строки `L|…` (младшие 13 бит — мВ, флаги 0x2000 usb, 0x4000 экран, 0x8000 standby), `S|logclr`.
- `I|` дополнительно: `tmp` (°C чипа), `fms` (мс на кадр), `scr`, `mhz`, `log`.

### Дамп сбоя (coredump)

Ядро 2.0.14 пишет coredump во флеш (`CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH`), раздел `coredump` @ `0xFF0000`
(64 КБ) в схеме `app3M_fat9M_16MB`. После перезагрузки по сбою (USB-лог панику не показывает):

```bash
"$ESPTOOL" --chip esp32s3 --port /dev/cu.usbmodem1101 --before default_reset --after hard_reset \
  read_flash 0xFF0000 0x10000 coredump.bin            # мост заблокировать, как при прошивке
```

Файл: 20 байт заголовка + ELF. В заметке `EXTRA_INFO` — причина (`EXCCAUSE`, 28 = LoadProhibited) и
`EXCVADDR`; в первой `PRSTATUS` — PC упавшей задачи и `a0` (адрес возврата: старшие 2 бита заменить на `0x4`).
Расшифровка адресов:

```bash
A2L=$(find ~/Library/Arduino15/packages/esp32/tools -name xtensa-esp32s3-elf-addr2line | head -1)
$A2L -pfiaC -e build/nyancat.ino.elf 0x<pc> 0x<a0>
```

Так нашлась ошибка «характеристика HID создана после `startServices()`»: PC в `BLEService::getServer()`,
вызов из `BLECharacteristic::notify()`.
