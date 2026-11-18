#!/bin/bash
# install_plugins.command — ставит плагины Agent Buddy во все найденные харнесы одной командой.
# Запуск:  bash install_plugins.command   (или двойной клик в Finder)

cd "$(dirname "$0")" || exit 1
ROOT="$PWD"
ok=0; fail=0

step() { printf '\n\033[1m== %s ==\033[0m\n' "$1"; }
good() { printf '\033[32m  OK\033[0m %s\n' "$1"; ok=$((ok+1)); }
bad()  { printf '\033[31m  FAIL\033[0m %s\n' "$1"; fail=$((fail+1)); }
skip() { printf '\033[33m  —\033[0m %s\n' "$1"; }

# ── Claude Code ──────────────────────────────────────────────────────────────
step "Claude Code"
if command -v claude >/dev/null 2>&1; then
  if claude plugin marketplace add "$ROOT/plugins" \
     && claude plugin install agent-buddy@nyan-buddy -y; then
    good "плагин agent-buddy@nyan-buddy установлен (проверка: /agent-buddy:status)"
  else
    bad "claude plugin install не прошёл — см. вывод выше"
  fi
else
  skip "claude CLI не найден — пропускаю"
fi

# ── DeepSeek Harness ─────────────────────────────────────────────────────────
step "DeepSeek Harness"
DSH="$(command -v dsh 2>/dev/null || true)"
[ -z "$DSH" ] && DSH="/Applications/DeepSeek Harness.app/Contents/Resources/runtime/cli/bin/dsh"
if [ -x "$DSH" ]; then
  python3 -m pip install --quiet pyserial \
    && "$DSH" plugin --profile desktop add "$ROOT/plugins/dsh-agent-buddy" \
    && good "плагин добавлен (в окне DeepSeek: Плагины → Agent Buddy → включить)" \
    || bad "установка не прошла — см. вывод выше"
else
  skip "DeepSeek Harness не найден — пропускаю"
fi

# ── Kimi Work ────────────────────────────────────────────────────────────────
step "Kimi Work"
KREG="$HOME/Library/Application Support/kimi-desktop/daimon-share/daimon/skills/plugin-builder/scripts/register_personal.sh"
if [ -f "$KREG" ]; then
  if bash "$KREG" --confirm-rights "$ROOT/plugins/kimi-agent-buddy"; then
    good "зарегистрирован в personal market (уже установленный плагин обновится; новый — Personal → +)"
  else
    bad "регистрация не прошла — см. вывод выше"
  fi
else
  skip "Kimi Work не найден — пропускаю"
fi

printf '\n\033[1mГотово: %d успешно, %d с ошибкой.\033[0m\n' "$ok" "$fail"
[ "$fail" -eq 0 ]
