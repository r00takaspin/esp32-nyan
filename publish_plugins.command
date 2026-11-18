#!/bin/bash
# publish_plugins.command — публикует плагины на GitHub для удалённой установки.
#
# Что делает:
#   1. пушит main;
#   2. обновляет ветки plugin-dsh-agent-buddy и plugin-kimi-agent-buddy
#      (subtree-снапшоты plugins/dsh-agent-buddy и plugins/kimi-agent-buddy),
#      из которых DeepSeek и Kimi ставят плагины одной командой с GitHub.
#
# Запускать после коммита изменений в plugins/*. Незакоммиченное не публикуется.

cd "$(dirname "$0")" || exit 1
set -e

git push origin main

for P in dsh-agent-buddy kimi-agent-buddy; do
  echo "== plugin-$P =="
  git branch -D "plugin-$P" 2>/dev/null || true
  git subtree split --prefix="plugins/$P" -b "plugin-$P" >/dev/null
  git push -f origin "plugin-$P"
done

echo "Опубликовано: main + plugin-dsh-agent-buddy + plugin-kimi-agent-buddy"
