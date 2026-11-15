# Agent Buddy — плагин Claude Code

Тот же «котик на столе», что и `dsh-agent-buddy` для DeepSeek Harness, но для Claude Code:

| Возможность | DSH-плагин | Этот плагин |
|---|---|---|
| Живой статус (idle / think / tool) | `agent/status` + опрос | хуки `UserPromptSubmit`, `PreToolUse`, `PostToolUse`, `Stop`, `SessionEnd` |
| Физическое YES / NO | `approval/request` | хук `PermissionRequest` → `allow` / `deny` |
| Агент рисует на плате | tool `buddy_show` | MCP-tool `buddy_show` |
| Индикатор в UI | пилюля в composer | `buddy.py statusline` (опционально) |
| Батарея, пэйринг, настройки | — | `buddy_status`, `buddy_settings`, `buddy_pair`, `/agent-buddy:*` |

Нужна прошивка **v2** (`Arduino/nyancat`, `FW_VERSION "2.0"`): плагин проверяет живость платы
пингом `S|ping` → `I|…`; старая прошивка на пинг не отвечает и будет показана как offline.

## Установка

```text
/plugin marketplace add ~/Documents/deepseek-harness/default-workspace/plugins
/plugin install agent-buddy@nyan-buddy
```

Перезапустить Claude Code. Python — системный `python3` (3.8+), **pyserial не нужен**
(используется, если установлен; иначе работа с портом через `termios`).

## Как это устроено

```
Claude Code ──hooks──► buddy.py ─┐
           ──MCP────► mcp_server.py ─┤ HTTP 127.0.0.1:8789
                                 └─► buddyd.py ──USB CDC──► плата
                                          └──buddy-ble (Swift, CoreBluetooth)──BLE──► плата
```

* **Без кабеля — по Bluetooth.** Прошивка держит рядом с HID-клавиатурой сервис
  в стиле Nordic UART (`6E400001-…`) с тем же текстовым протоколом. Когда USB-порта нет, демон
  запускает `buddy-ble` — мост stdin/stdout ↔ BLE на CoreBluetooth; он подключается к уже
  открытому macOS соединению с клавиатурой. Исходник `ble/buddy-ble.swift` собирается `swiftc`
  при первом запуске в `~/.config/agent-buddy/bin/` (имя с хэшем исходника — обновление
  плагина пересобирает). При первом запуске macOS спрашивает доступ к Bluetooth (для Python /
  buddy-ble) — разрешить. Воткнули кабель — демон сам переходит на USB. `ble: off` в конфиге
  отключает Bluetooth-связь.
* `buddyd.py` — фоновый демон, единственный владелец USB-порта. Стартует сам при первом хуке,
  переоткрывает порт после переподключения, пингует плату каждые 2.5 с, завершается через 3 ч простоя.
* «Плата на связи» = ответила на пинг за последние 7 с. Флага, который может «залипнуть»
  (как `deviceAlive` в DSH), нет: выдернули кабель — через ≤7 с подтверждения идут в терминал.
* Подтверждения ставятся в очередь (несколько сессий → по одному на экране). Если за
  `approve_timeout` (45 с) никто не нажал — на плату уходит `S|cancel`, а Claude Code показывает
  свой обычный диалог. `AskUserQuestion` и `ExitPlanMode` хук не трогает.
* Порт делится с DSH-мостом и рецептом прошивки через `/tmp/nyan-buddy-bridge.lock`:
  если порт занят DeepSeek или идёт прошивка, демон ждёт и не мешает.

## Команды

* `/agent-buddy:status` — связь, заряд/зарядка, Bluetooth, настройки.
* `/agent-buddy:pair` — пошаговый пэйринг (открывает мастер на плате); `/agent-buddy:pair repair` — перепривязка.
* `/agent-buddy:settings brightness 3` / `sleep 30` / `keys off` / `approvals terminal` / `approve_timeout 60`.

CLI для отладки: `python3 scripts/buddy.py status|show "текст"|set KEY VALUE|unpair|wizard|stop`.
Лог демона: `~/.config/agent-buddy/daemon.log`, конфиг: `~/.config/agent-buddy/config.json`.

### Статус-строка (по желанию)

В `~/.claude/settings.json`:

```json
{ "statusLine": { "type": "command",
  "command": "python3 ~/Documents/deepseek-harness/default-workspace/plugins/claude-agent-buddy/scripts/buddy.py statusline" } }
```

Выводит, например: `buddy: think | bat 77%+ | BT ok`.

## Настройки

| Ключ | Где хранится | Значения | Смысл |
|---|---|---|---|
| `brightness` | плата (NVS) | 0–4 | подсветка; 2 = прежние 280 |
| `sleep` | плата | 0/5/10/30/60 | гашение экрана на спящем котике, 0 = никогда |
| `keys` | плата | off / **auto** / always | печатать ли `y`/`n` по Bluetooth при нажатии YES/NO; auto — только когда нет связи с агентом (ни по USB, ни по Bluetooth) |
| `approvals` | хост | **device** / terminal | отвечать на запросы с платы или только показывать их |
| `approve_timeout` | хост | 5–280 с | после этого вопрос уходит в терминал |
| `port` | хост | auto / путь | `auto` ищет `/dev/cu.usbmodem*` и выбирает ту плату, что отвечает на пинг |

## Проверено

Сквозной прогон против эмулятора платы на pty: автозапуск демона, think/tool/idle,
транслитерация, YES → allow, NO → deny, таймаут → `S|cancel` + диалог терминала,
параллельные запросы в очереди, «выдернутый кабель» → мгновенный фолбэк, занятый lock
DeepSeek → ожидание, MCP initialize / tools/list / tools/call. **Не проверено на живой
плате и в живом Claude Code** — первый запуск см. чек-лист в `Arduino/nyancat/AGENTS.md`.
